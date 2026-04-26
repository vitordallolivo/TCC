import sys
import os
import tomllib
import importlib.metadata
import subprocess
from pathlib import Path
from packaging.requirements import Requirement

# Configuração de diretório para evitar erros de caminho relativo
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(SCRIPT_DIR)
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

def handle_dependencies():
    """Verifica e instala dependências do pyproject.toml."""
    pyproject_path = Path("pyproject.toml")
    if not pyproject_path.exists():
        return True
    
    try:
        with open(pyproject_path, "rb") as f:
            pyproject = tomllib.load(f)
        dependencies = pyproject.get('project', {}).get('dependencies', [])
        
        for dep in dependencies:
            req = Requirement(dep)
            try:
                importlib.metadata.version(req.name)
            except importlib.metadata.PackageNotFoundError:
                print(f"Instalando: {dep}")
                subprocess.check_call([sys.executable, "-m", "pip", "install", dep])
        return True
    except Exception as e:
        print(f"Erro nas dependências: {e}")
        return False

def main():
    if not handle_dependencies():
        sys.exit(1)

    # Importação tardia da GUI após as dependências e o path estarem OK
    try:
        from PySide6.QtWidgets import QApplication
        from GUI.mainAlbatrozGUI import AlbatrozGUI
        
        app = QApplication(sys.argv)
        app.setStyle("Fusion")
        
        window = AlbatrozGUI()
        window.show()
        
        sys.exit(app.exec())
        
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()