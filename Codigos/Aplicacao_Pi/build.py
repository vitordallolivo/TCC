"""build.py — gera o executável do Aplicacao_Pi com PyInstaller.

Uso:
    python build.py

Sempre `--onedir` (nunca `--onefile`): Config/*.yaml precisa ser GRAVÁVEL
entre execuções (perfis de motor, SAME.yaml, preferências) — `--onefile` se
auto-extrai numa pasta temporária que é apagada ao fechar o app, perdendo
tudo. Usa `--contents-directory .` pra manter Config/GUI/native_sim_bin
direto ao lado do executável (em vez do `_internal/` padrão do PyInstaller
6+), que é o que `mainApp.py`/`mainAlbatrozGUI.py`/`manager.py` esperam
quando `sys.frozen` é True (ver CLAUDE.md).

No Linux, embute os 3 `zephyr.exe` (SIBA/SAME/SPMM) já compilados via
`make native_sim` em cada pasta — assim o build gerado pode ser movido pra
qualquer pasta/máquina (Linux, mesma arquitetura/glibc) e o `native_sim`
continua funcionando sem depender do repo git do lado (testado e validado
— ver CLAUDE.md). No Windows/macOS, `native_sim` não roda de qualquer
forma (zephyr.exe é ELF POSIX de Linux), então esses binários nem são
empacotados lá.

Também empacota `autotune_cost.py`/`helice_coeff.py` (Scripts/) como
executáveis PRÓPRIOS em `Scripts/_bin/<nome>/<nome>` — dentro do app
congelado, `sys.executable` é o próprio app, não um Python genérico, então
`EstimationRunner` não consegue rodar esses scripts como `[sys.executable,
script.py]` (ver Communication_Data/estimation.py `_invocation`). Ficam em
pasta própria por script (não achatada com --contents-directory) pra não
misturar as libs internas (numpy/scipy bundled) de um build com o outro.
"""
import os
import platform
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CODIGOS_DIR = os.path.dirname(SCRIPT_DIR)  # Aplicacao_Pi/.. = Codigos/

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
    for board in ("SIBA", "SAME", "SPMM"):
        exe = os.path.join(CODIGOS_DIR, board, "build_native_sim", "zephyr", "zephyr.exe")
        if os.path.exists(exe):
            args += ["--add-binary", f"{exe}{SEP}native_sim_bin/{board}"]
            print(f"[BUILD] native_sim de {board} será incluído ({exe}).")
        else:
            print(f"[BUILD] Aviso: {exe} não encontrado — native_sim de {board} "
                  f"ficará indisponível nesse build. Rode 'make native_sim' na "
                  f"pasta {board}/ antes, se quiser incluir.")
    return args


def build_estimation_helpers(main_dist_dir):
    """Empacota cada script de Scripts/ invocado via EstimationRunner como
    seu próprio executável em <main_dist_dir>/Scripts/_bin/<nome>/<nome>.

    Não usa --contents-directory aqui (layout normal, com _internal/) —
    cada helper fica na SUA PRÓPRIA pasta, então não tem risco de um helper
    sobrescrever .so/.dll do outro (os dois embutem numpy/scipy por conta
    própria). O cwd continua sendo Scripts/ (não a pasta do helper) quando
    o EstimationRunner sobe o processo, então test_coeff_auto.py (carregado
    por caminho relativo) é achado igual — está solto em Scripts/, copiado
    pelo --add-data do build principal.
    """
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
