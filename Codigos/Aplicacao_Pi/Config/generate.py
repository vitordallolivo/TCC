"""
generate.py — Gerador de headers C para firmware SIBA
Lê SIBA.yaml e renderiza os templates Jinja2 para o firmware

Outputs:
  lib/OS/Common_Types.h  — Enums compartilhados
  lib/OS/CMD.h           — Struct de comando (PC -> Board)
  lib/SYS/Log_Manager.h  — Struct de telemetria (Board -> PC) + SENSORS
                           (contém extensões manuais — ver comentário no topo do arquivo)
"""

import yaml
import os
from jinja2 import Environment, FileSystemLoader

TEMPLATE_DIR  = os.path.dirname(os.path.abspath(__file__))
SIBA_LIB_OS   = os.path.abspath(os.path.join(TEMPLATE_DIR, "../../SIBA/lib/OS"))
SIBA_LIB_SYS  = os.path.abspath(os.path.join(TEMPLATE_DIR, "../../SIBA/lib/SYS"))

env = Environment(loader=FileSystemLoader(TEMPLATE_DIR), keep_trailing_newline=True)

def render(template_name, data, output_path):
    t = env.get_template(template_name)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(t.render(data))
    print(f"✔  {output_path}")

def main():
    with open(os.path.join(TEMPLATE_DIR, 'SIBA.yaml'), 'r') as f:
        cfg = yaml.safe_load(f)

    # Enums usados como campo do CMD precisam ter tamanho de 1 byte para o
    # struct bater com o formato de fio compacto (<BBBfb>, 8 bytes). O tamanho
    # de um enum C é impl-definido (4 bytes com gcc host/native_sim; 1 byte só
    # com -fshort-enums) — por isso marcamos ESSES enums com
    # __attribute__((packed)). log_status/status_type (telemetria, 4 bytes) NÃO
    # são marcados. Ver Aplicacao_Pi/CLAUDE.md "CMD de 8 bytes / native_sim".
    enums = cfg.get('enums', {})
    cfg['packed_enums'] = sorted({
        f['type'] for f in cfg.get('structs', {}).get('CMD', [])
        if f.get('type') in enums
    })
    print(f"Enums empacotados (1 byte) por uso no CMD: {cfg['packed_enums']}")

    render('Common_template.h.j2', cfg, os.path.join(SIBA_LIB_OS,  'Common_Types.h'))
    render('CMD_template.h.j2',    cfg, os.path.join(SIBA_LIB_OS,  'CMD.h'))
    render('LOG_template.h.j2',    cfg, os.path.join(SIBA_LIB_SYS, 'Log_Manager.h'))

if __name__ == "__main__":
    main()