"""
generate.py — Gerador de headers C para os firmwares SIBA e SAME.
Lê SIBA.yaml e SAME.yaml e renderiza os templates Jinja2 para CADA firmware,
tudo de uma vez (a fonte de verdade do protocolo é o .yaml de cada placa).

Para cada placa gera:
  <placa>/lib/OS/Common_Types.h  — Enums compartilhados
  <placa>/lib/OS/CMD.h           — Struct de comando (PC -> Board)
  <placa>/lib/SYS/Log_Manager.h  — Struct de telemetria (Board -> PC) + SENSORS
  <placa>/lib/OS/CMD_Core_prm.h  — Defaults de calibração/faixa por sensor
                                    (se o .yaml tiver `sensor_defaults`)
"""

import yaml
import os
from jinja2 import Environment, FileSystemLoader

TEMPLATE_DIR = os.path.dirname(os.path.abspath(__file__))
CODIGOS_DIR  = os.path.abspath(os.path.join(TEMPLATE_DIR, "../.."))   # .../Codigos

# Placas a gerar: (arquivo .yaml, pasta do firmware sob Codigos/).
BOARDS = [
    ("SIBA.yaml", "SIBA"),
    ("SAME.yaml", "SAME"),
]

env = Environment(loader=FileSystemLoader(TEMPLATE_DIR), keep_trailing_newline=True)


def render(template_name, data, output_path):
    t = env.get_template(template_name)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(t.render(data))
    print(f"✔  {output_path}")


def _c_float(v):
    """Formata um valor de sensor_defaults como literal C float. Strings
    passam direto (macros como 'CMD_CORE_NO_LIMIT'/'-CMD_CORE_NO_LIMIT');
    números viram literal float com sufixo 'f'."""
    if isinstance(v, str):
        return v
    return f"{float(v)}f"


def generate_board(yaml_name, board_dir):
    """Gera Common_Types.h / CMD.h / Log_Manager.h de uma placa a partir do seu
    .yaml, na estrutura <Codigos>/<board_dir>/lib/{OS,SYS}/."""
    yaml_path = os.path.join(TEMPLATE_DIR, yaml_name)
    if not os.path.exists(yaml_path):
        print(f"⚠  {yaml_name} não encontrado — pulando {board_dir}.")
        return
    with open(yaml_path, 'r') as f:
        cfg = yaml.safe_load(f)
    cfg['board_yaml'] = yaml_name        # p/ o comentário "edite <placa>.yaml"
    cfg['board'] = board_dir

    # Enums usados como campo do CMD precisam ter 1 byte para o struct bater com
    # o formato de fio compacto (<BBBfb>, 8 bytes). O tamanho de um enum C é
    # impl-definido (4 bytes com gcc host/native_sim; 1 byte só com
    # -fshort-enums) — por isso marcamos ESSES enums com __attribute__((packed)).
    # Enums de telemetria (log_status/status_type) NÃO são marcados.
    enums = cfg.get('enums', {})
    cfg['packed_enums'] = sorted({
        fld['type'] for fld in cfg.get('structs', {}).get('CMD', [])
        if fld.get('type') in enums
    })
    print(f"[{board_dir}] enums empacotados (1 byte) por uso no CMD: {cfg['packed_enums']}")

    lib_os  = os.path.join(CODIGOS_DIR, board_dir, "lib", "OS")
    lib_sys = os.path.join(CODIGOS_DIR, board_dir, "lib", "SYS")
    render('Common_template.h.j2', cfg, os.path.join(lib_os,  'Common_Types.h'))
    render('CMD_template.h.j2',    cfg, os.path.join(lib_os,  'CMD.h'))
    render('LOG_template.h.j2',    cfg, os.path.join(lib_sys, 'Log_Manager.h'))

    # Enum de códigos de erro (<board>_error_bit_t) — fonte única compartilhada
    # com a GUI (errors.py). Só gera se o yaml trouxer a seção `error_bits`.
    if cfg.get('error_bits'):
        cfg['error_bit_type'] = f"{board_dir.lower()}_error_bit_t"
        render('Error_Codes_template.h.j2', cfg, os.path.join(lib_sys, 'Error_Codes.h'))
    else:
        print(f"[{board_dir}] sem `error_bits` no yaml — Error_Codes.h não gerado.")

    # Limiares do Security_Manager (SM_*) — gerados só se o yaml trouxer a seção.
    security_limits = cfg.get('security_limits')
    if security_limits:
        for e in security_limits:
            e['value_c'] = _c_float(e['value'])
        render('Security_prm_template.h.j2', cfg, os.path.join(lib_sys, 'Security_prm.h'))
    else:
        print(f"[{board_dir}] sem `security_limits` no yaml — Security_prm.h não gerado.")

    sensor_defaults = cfg.get('sensor_defaults')
    if sensor_defaults:
        for e in sensor_defaults:
            e['linear_c'] = _c_float(e['linear'])
            e['offset_c'] = _c_float(e['offset'])
            e['min_c'] = _c_float(e['min'])
            e['max_c'] = _c_float(e['max'])
            # `zero` é o ponto da entrada em que a grandeza física vale zero,
            # na MESMA unidade de x (mV no pino). Com zero = 0 a lei afim recai
            # exatamente no comportamento anterior (y = linear*x + offset), o
            # que mantém intactos os sensores já calibrados.
            e['zero_c'] = _c_float(e.get('zero', 0.0))
            # `law` escolhe como x vira y:
            #   affine -> y = linear*(x - zero) + offset      (default)
            #   sqrt   -> y = linear*sqrt(x - zero) + offset
            # A raiz existe por causa do tubo de Pitot: por Bernoulli a
            # velocidade cresce com a RAIZ da pressão diferencial, então
            # nenhuma reta descreve o sensor em faixa alguma.
            law = str(e.get('law', 'affine')).lower()
            if law not in ('affine', 'sqrt'):
                raise ValueError(
                    f"{yaml_name}: sensor {e['target']} tem law='{law}'; "
                    "apenas 'affine' e 'sqrt' são suportados.")
            e['law_c'] = 'SENSOR_LAW_SQRT' if law == 'sqrt' else 'SENSOR_LAW_AFFINE'
        cfg['sensor_defaults_needs_security'] = any('error_bit' in e for e in sensor_defaults)
        cfg['sensor_defaults_needs_log_manager'] = any(
            e['target'].startswith('SENSOR_ID_') for e in sensor_defaults)
        cfg['sensor_defaults_needs_common_types'] = any(
            e['target'].startswith('TARGET_') for e in sensor_defaults)
        render('CMD_Core_prm_template.h.j2', cfg, os.path.join(lib_os, 'CMD_Core_prm.h'))
    else:
        print(f"[{board_dir}] sem `sensor_defaults` no yaml — CMD_Core_prm.h não gerado.")


def main():
    for yaml_name, board_dir in BOARDS:
        generate_board(yaml_name, board_dir)


if __name__ == "__main__":
    main()
