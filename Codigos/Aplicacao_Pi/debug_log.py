"""Infra de debug/log em arquivo — para caçar o segfault e ver o que acontece.

Cria uma pasta `logs/` com:
- **crash.log** — o `faulthandler` despeja aqui o traceback de TODAS as threads
  no instante de uma **Falha de segmentação** (SIGSEGV/SIGABRT/...). É isso que
  aponta exatamente onde o C (pyqtgraph/numpy) quebrou e qual thread fazia o quê.
- **app.log** — logging de eventos + exceções não tratadas (main e threads).
- **session.log** — espelho do que sai no TinyTerminal (o stream [LOG]/[NET]/…).

Chamar `setup(base_dir)` UMA vez, bem no início (antes de subir a GUI).
"""
import atexit
import datetime
import faulthandler
import logging
import os
import signal
import sys
import threading
import traceback

_state = {"crash": None, "session_path": None, "log_dir": None}


def setup(base_dir):
    log_dir = os.path.join(base_dir, "logs")
    os.makedirs(log_dir, exist_ok=True)
    _state["log_dir"] = log_dir

    # 1) faulthandler: no segfault, dump do stack de TODAS as threads em crash.log
    crash = open(os.path.join(log_dir, "crash.log"), "a", buffering=1, encoding="utf-8")
    crash.write(f"\n===== sessão {datetime.datetime.now():%Y-%m-%d %H:%M:%S} =====\n")
    crash.flush()
    faulthandler.enable(file=crash, all_threads=True)
    # Também nesses sinais (alguns crashes chegam como SIGABRT/SIGBUS/SIGFPE):
    for sig in ("SIGABRT", "SIGBUS", "SIGFPE", "SIGILL", "SIGSEGV"):
        s = getattr(signal, sig, None)
        if s is not None:
            try:
                faulthandler.register(s, file=crash, all_threads=True, chain=True)
            except Exception:
                pass
    _state["crash"] = crash

    # 2) logging de eventos + erros em app.log
    logging.basicConfig(
        filename=os.path.join(log_dir, "app.log"),
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(threadName)s %(name)s: %(message)s",
    )
    logging.info("=== app iniciada (pid %d) ===", os.getpid())

    # 3) exceções NÃO tratadas — main thread e threads.
    # PySide/PyQt costuma ENGOLIR exceções dentro de slots ("crashes without
    # error when run from the command line"). Este hook as torna VISÍVEIS:
    # imprime no terminal real + grava em crash.log/app.log. (Prática dos fóruns
    # PyQt/StackOverflow que o usuário citou.)
    def excepthook(t, v, tb):
        txt = "".join(traceback.format_exception(t, v, tb))
        logging.error("Exceção não tratada:\n%s", txt)
        crash.write("\n[EXCEPTION main]\n" + txt); crash.flush()
        try:
            sys.__stderr__.write("\n*** EXCEÇÃO NÃO TRATADA (slot/callback) ***\n" + txt + "\n")
            sys.__stderr__.flush()
        except Exception:
            pass
        sys.__excepthook__(t, v, tb)

    sys.excepthook = excepthook

    def thread_excepthook(args):
        txt = "".join(traceback.format_exception(args.exc_type, args.exc_value, args.exc_traceback))
        logging.error("Exceção em thread %s:\n%s", args.thread.name, txt)
        crash.write(f"\n[EXCEPTION thread {args.thread.name}]\n" + txt); crash.flush()

    threading.excepthook = thread_excepthook

    _state["session_path"] = os.path.join(log_dir, "session.log")
    atexit.register(lambda: logging.info("=== app encerrada ==="))
    print(f"[DEBUG] Logs em: {log_dir} (crash.log / app.log / session.log)")
    return log_dir


def install_qt_message_handler():
    """Captura as mensagens do Qt (qWarning/qCritical/qFatal) para o log. Elas
    costumam PRECEDER um crash e explicá-lo (ex.: 'Cannot create children for a
    parent that is in a different thread', acesso a widget de thread errada,
    QPainter reentrante, etc.). Chamar DEPOIS de criar o QApplication."""
    try:
        from PySide6.QtCore import qInstallMessageHandler, QtMsgType
    except Exception:
        return
    lvls = {
        getattr(QtMsgType, "QtDebugMsg", None): "DEBUG",
        getattr(QtMsgType, "QtInfoMsg", None): "INFO",
        getattr(QtMsgType, "QtWarningMsg", None): "WARNING",
        getattr(QtMsgType, "QtCriticalMsg", None): "CRITICAL",
        getattr(QtMsgType, "QtFatalMsg", None): "FATAL",
    }

    def handler(mode, ctx, message):
        lvl = lvls.get(mode, "?")
        loc = f" ({ctx.file}:{ctx.line})" if ctx and getattr(ctx, "file", None) else ""
        logging.log(logging.WARNING if lvl in ("WARNING", "CRITICAL", "FATAL") else logging.INFO,
                    "Qt[%s] %s%s", lvl, message, loc)
        crash = _state.get("crash")
        if crash is not None and lvl in ("WARNING", "CRITICAL", "FATAL"):
            crash.write(f"[Qt-{lvl}] {message}{loc}\n"); crash.flush()

    qInstallMessageHandler(handler)
    logging.info("Qt message handler instalado.")


def session_log_path():
    """Caminho do espelho do terminal (o TinyTerminal escreve aqui também)."""
    return _state.get("session_path")


def log_event(msg, level=logging.INFO):
    logging.log(level, msg)
