import sys
import os
import tomllib
import importlib.metadata
import subprocess
from pathlib import Path
from packaging.requirements import Requirement

# Configuração de diretório para evitar erros de caminho relativo.
# Se congelado (PyInstaller), __file__ apontaria pro _MEIPASS (pasta temp
# apagada ao fechar o app) — Config/*.yaml precisa ficar ao lado do
# executável de verdade pra perfis/preferências persistirem entre execuções.
if getattr(sys, "frozen", False):
    SCRIPT_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
os.chdir(SCRIPT_DIR)
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

# Debug/log em arquivo (faulthandler p/ segfault + app.log/session.log).
# Feito o MAIS CEDO possível, antes de qualquer import pesado/thread.
try:
    import debug_log
    debug_log.setup(SCRIPT_DIR)
except Exception as _e:
    print(f"[DEBUG] Falha ao iniciar logs: {_e}")

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

    # Limpa processos native_sim (zephyr.exe) órfãos de execuções anteriores
    # que crasharam sem limpar — senão vários streamam UDP na porta 5000 ao
    # mesmo tempo (dados misturados + travamentos).
    try:
        from Communication_Data.native_sim import kill_stray_native_sims
        kill_stray_native_sims()
    except Exception as _e:
        print(f"[NATIVE_SIM] limpeza de órfãos falhou: {_e}")

    # Importação tardia da GUI após as dependências e o path estarem OK
    try:
        from PySide6.QtWidgets import QApplication
        from GUI.mainAlbatrozGUI import AlbatrozGUI
        
        app = QApplication(sys.argv)
        app.setStyle("Fusion")

        # Captura mensagens do Qt (avisos que costumam preceder o crash).
        try:
            import debug_log
            debug_log.install_qt_message_handler()
        except Exception as _e:
            print(f"[DEBUG] Qt handler falhou: {_e}")
        
        window = AlbatrozGUI()
        window.show()
        
        sys.exit(app.exec())
        
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()