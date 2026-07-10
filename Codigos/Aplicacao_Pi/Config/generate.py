"""
generate.py — Gerador de headers C para os firmwares SIBA e SAME.
Lê SIBA.yaml e SAME.yaml e renderiza os templates Jinja2 para CADA firmware,
tudo de uma vez (a fonte de verdade do protocolo é o .yaml de cada placa).

Para cada placa gera:
  <placa>/lib/OS/Common_Types.h  — Enums compartilhados
  <placa>/lib/OS/CMD.h           — Struct de comando (PC -> Board)
  <placa>/lib/SYS/Log_Manager.h  — Struct de telemetria (Board -> PC) + SENSORS
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


def main():
    for yaml_name, board_dir in BOARDS:
        generate_board(yaml_name, board_dir)


if __name__ == "__main__":
    main()
