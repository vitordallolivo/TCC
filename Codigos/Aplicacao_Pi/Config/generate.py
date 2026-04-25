import yaml
import os
from jinja2 import Environment, FileSystemLoader

# Configura o diretório onde estão os templates .j2
TEMPLATE_DIR = os.path.dirname(os.path.abspath(__file__))
env = Environment(loader=FileSystemLoader(TEMPLATE_DIR))

def render_and_save(template_name, data, output_path):
    """Carrega o template, renderiza com os dados e salva no destino."""
    template = env.get_template(template_name)
    content = template.render(data)
    
    # Cria os diretórios pai se não existirem
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write(content)
    print(f"✔ Generated: {output_path}")

def main():
    # 1. Carrega o arquivo YAML (Fonte única da verdade)
    yaml_path = os.path.join(TEMPLATE_DIR, 'SIBA.yaml')
    with open(yaml_path, 'r') as f:
        config = yaml.safe_load(f)

    # Caminho base para a biblioteca do firmware (SIBA)
    # Ajustado conforme seu 'tree': ../../SIBA/lib/
    base_lib = os.path.abspath(os.path.join(TEMPLATE_DIR, "../../SIBA/lib/"))

    # 2. Gera o Common_Types.h (Enums compartilhadas)
    # Local sugerido: OS para que ambos acessem
    render_and_save(
        'Common_template.h.j2', 
        config, 
        os.path.join(base_lib, 'OS/Common_Types.h')
    )

    # 3. Gera o CMD.h (Struct de Comando)
    render_and_save(
        'CMD_template.h.j2', 
        config, 
        os.path.join(base_lib, 'OS/CMD.h')
    )

    # 4. Gera o Log_Manager.h ou Telemetry.h (Struct de Telemetria)
    # Usando o caminho que apareceu no seu tree
    render_and_save(
        'LOG_template.h.j2', 
        config, 
        os.path.join(base_lib, 'SYS/Log_Manager.h')
    )

if __name__ == "__main__":
    main()