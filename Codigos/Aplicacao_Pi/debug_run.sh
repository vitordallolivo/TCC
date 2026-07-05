#!/usr/bin/env bash
# ============================================================================
# Roda a aplicação SOB O GDB para capturar o backtrace C/C++ do segfault.
#
# Por que: o faulthandler (logs/crash.log) só mostra o nível PYTHON. Como o
# crash é em C++ (Qt/pyqtgraph/numpy), a main thread aparece só como "app.exec".
# O gdb mostra a função C exata que quebrou, em TODAS as threads.
#
# Uso:  ./debug_run.sh    (ou: bash debug_run.sh)
#       -> reproduza o crash normalmente na GUI
#       -> o backtrace completo vai para  logs/gdb_crash.log
#       -> me mande esse arquivo.
# ============================================================================
cd "$(dirname "$0")" || exit 1
mkdir -p logs

PY=.venv/bin/python
[ -x "$PY" ] || PY="$(command -v python3 || command -v python)"

if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb não encontrado. Instale com:  sudo apt install gdb"
    exit 1
fi

echo "=========================================================="
echo " Rodando a app sob gdb. Reproduza o crash normalmente."
echo " O backtrace C/C++ vai para: logs/gdb_crash.log"
echo "=========================================================="
ulimit -c unlimited 2>/dev/null

gdb -q -batch \
    -ex "set pagination off" \
    -ex "set logging file logs/gdb_crash.log" \
    -ex "set logging overwrite on" \
    -ex "set logging enabled on" \
    -ex "run" \
    -ex "printf \"\n===== SINAL RECEBIDO — backtrace de TODAS as threads =====\n\"" \
    -ex "thread apply all bt" \
    -ex "printf \"\n===== backtrace FULL (thread do crash) =====\n\"" \
    -ex "bt full" \
    -ex "set logging enabled off" \
    -ex "quit" \
    --args "$PY" mainApp.py

echo ""
echo "Encerrado. Veja/mande:  logs/gdb_crash.log"
