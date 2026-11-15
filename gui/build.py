"""build.py — gera o executável do gui com PyInstaller (`python build.py`).

Sempre --onedir: Config/*.yaml precisa ser gravável entre execuções, e
--onefile se auto-extrai numa pasta temporária descartada ao fechar.
--contents-directory . mantém Config/GUI/native_sim_bin ao lado do
executável (sem o _internal/ padrão), como mainApp.py/manager.py esperam.

No Linux embute os zephyr.exe (SIBA/SAME) de `make native_sim`. Os
scripts de Scripts/ viram executáveis próprios (sys.executable no app
congelado não é um Python genérico) em pastas separadas, pra não misturar
numpy/scipy de um build com o outro.
"""
import os
import platform
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CODIGOS_DIR = os.path.dirname(SCRIPT_DIR)  # gui/.. = raiz do repositório

# --add-data/--add-binary usam ':' no Linux/macOS e ';' no Windows.
SEP = ";" if platform.system() == "Windows" else ":"


def ensure_pyinstaller():
    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("[BUILD] Instalando PyInstaller no venv atual...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyinstaller"])


def native_sim_binary_args():
    """--add-binary pros zephyr.exe já compilados (só existe/faz sentido no Linux)."""
    if platform.system() != "Linux":
        return []
    args = []
    for board, folder in (("SIBA", "firmware"),):
        exe = os.path.join(CODIGOS_DIR, folder, "build_native_sim", "zephyr", "zephyr.exe")
        if os.path.exists(exe):
            args += ["--add-binary", f"{exe}{SEP}native_sim_bin/{board}"]
            print(f"[BUILD] native_sim de {board} será incluído ({exe}).")
        else:
            print(f"[BUILD] Aviso: {exe} não encontrado — native_sim de {board} "
                  f"ficará indisponível nesse build. Rode 'make native_sim' na "
                  f"pasta {board}/ antes, se quiser incluir.")
    return args


def build_estimation_helpers(main_dist_dir):
    """Empacota cada script de Scripts/ como executável próprio em
    <main_dist_dir>/Scripts/_bin/<nome>/<nome>, cada um na sua pasta pra não
    misturar numpy/scipy bundled entre eles."""
    scripts_dir = os.path.join(SCRIPT_DIR, "Scripts")
    bin_dist = os.path.join(main_dist_dir, "Scripts", "_bin")
    work_root = os.path.join(SCRIPT_DIR, "build")
    for script in ("autotune_cost.py", "helice_coeff.py"):
        name = os.path.splitext(script)[0]
        cmd = [
            sys.executable, "-m", "PyInstaller",
            "--name", name,
            "--onedir",
            "--noconfirm",
            "--distpath", bin_dist,
            "--workpath", os.path.join(work_root, f"_estim_{name}"),
            "--specpath", work_root,
            os.path.join("Scripts", script),
        ]
        print("[BUILD]", " ".join(cmd))
        subprocess.check_call(cmd, cwd=SCRIPT_DIR)
    print(f"[BUILD] Helpers de estimação prontos em {bin_dist}")


def main():
    ensure_pyinstaller()
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--name", "AlbatrozGUI",
        "--onedir",
        "--noconfirm",
        "--contents-directory", ".",
        "--add-data", f"Config{SEP}Config",
        "--add-data", f"Scripts{SEP}Scripts",
        "--add-data", f"GUI/mainAlbatrozGUI.ui{SEP}GUI",
        *native_sim_binary_args(),
        "mainApp.py",
    ]
    print("[BUILD]", " ".join(cmd))
    subprocess.check_call(cmd, cwd=SCRIPT_DIR)
    main_dist_dir = os.path.join(SCRIPT_DIR, "dist", "AlbatrozGUI")
    build_estimation_helpers(main_dist_dir)
    print(f"[BUILD] Pronto: {main_dist_dir}")


if __name__ == "__main__":
    main()
