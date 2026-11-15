"""Ensaio em degraus de throttle na SIBA, gravando toda a telemetria em CSV.

Uso (com a GUI FECHADA — ela segura a porta 5555 e manda o "pronto" sozinha):
    .venv/bin/python Ensaios/ensaio_siba.py [degraus%] [s_por_degrau] [espera_arme_s]
    .venv/bin/python Ensaios/ensaio_siba.py 20,25,30,35,40,30,20 6 15
O CSV sai em Ensaios/ensaio_siba_<data>_<hora>.csv (uma linha por pacote de
telemetria, 10 Hz, + hora do PC, fase do ensaio e throttle comandado).

Sequência: 3 s de linha de base (ESC desarmado) -> "pronto" (pré-carga 5 s +
potência + ESC armado em 0 %) -> espera 8 s -> degraus de throttle, cada um
DWELL s -> volta a 0 % -> desliga (ESC + relés). Respeita o botão de
emergência (sem override): se a emergência acender, aborta.
Ctrl-C / qualquer erro: throttle 0 + desligar, sempre (finally).
"""
import socket, struct, time, sys, csv, os, yaml
APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # gui/
sys.path.insert(0, APP)
from Communication_Data import codecs
STEPS = [float(x) for x in (sys.argv[1] if len(sys.argv) > 1 else "20,25,30,35,40,30,20").split(",")]
DWELL = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
ARM_WAIT = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0   # relés + arranque do ESC
raw = yaml.safe_load(open(os.path.join(APP, 'Config', 'SIBA.yaml'))); en = raw['enums']
lay = codecs.telemetry_layout(codecs.board_protocol_cfg(raw, 'SIBA'))
ix = lambda c, n: en[c].index(n)
BOARD = ('169.254.97.200', 5000)
def c_start(st): return struct.pack('<BBBfb', ix('func_type','FUNC_START'), ix('instruct_type','INSTR_WRITE'), ix('target_type','TARGET_DEFAULT'), 1.0, ix('status_type', st))
def c_esc(p):    return struct.pack('<BBBfb', ix('func_type','FUNC_ESC'), ix('instruct_type','INSTR_WRITE'), ix('target_type','TARGET_DEFAULT'), float(p), 0)
rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
rx.bind(('', 5555)); rx.settimeout(0.2)
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
fn = os.path.join(APP, 'Ensaios', time.strftime('ensaio_siba_%Y%m%d_%H%M%S.csv'))
fh = open(fn, 'w', newline=''); w = csv.writer(fh)
names = [f['name'] for f in lay]
w.writerow(['pc_time_s', 't_s', 'fase', 'throttle_cmd_pct'] + names)
t0 = time.time(); state = {'fase': 'base', 'thr': 0.0}; last_print = [0.0]; n = [0]
def log(msg): print(f"{time.time()-t0:7.1f}s  {msg}", flush=True)
def send(c, what): tx.sendto(c, BOARD); log(f"-> {what}")
def pump(sec):
    end = time.time() + sec
    while time.time() < end:
        try: d, _ = rx.recvfrom(2048)
        except socket.timeout: continue
        r = codecs.decode_telemetry(lay, d)
        if not r: continue
        now = time.time(); n[0] += 1
        w.writerow([f"{now:.3f}", f"{now-t0:.3f}", state['fase'], state['thr']] + [r[k] for k in names])
        ec = int(r['error_code'])
        if state['fase'] not in ('base', 'fim') and (ec >> 19 & 1):
            raise RuntimeError("EMERGENCIA ativa (bit 19) — abortando")
        if now - last_print[0] >= 1.0:
            last_print[0] = now
            log(f"[{state['fase']:>8} {state['thr']:5.1f}%] rpm_est={r['rpm_same']:7.0f} Te={r['torque_eletrico']:7.3f} "
                f"ia={r['i_a']:6.2f} ib={r['i_b']:6.2f} Va={r['V_a']:5.2f} drops={r['batch_drops']:.0f} "
                f"proc={r['t_proc_us']:.0f}us err=0x{ec:08X}")
try:
    log(f"CSV: {fn} | degraus {STEPS} % x {DWELL:.0f} s")
    pump(3.0)
    if n[0] == 0: raise RuntimeError("sem telemetria (GUI aberta segurando a porta 5555? cabo?)")
    state['fase'] = 'armando'; send(c_start('STATUS_OK'), 'PRONTO (pre-carga 5 s -> potencia -> ESC armado)')
    pump(ARM_WAIT)
    for p in STEPS:
        state['fase'] = 'degrau'; state['thr'] = p; send(c_esc(p), f'throttle {p:.1f} %'); pump(DWELL)
    state['fase'] = 'zero'; state['thr'] = 0.0; send(c_esc(0), 'throttle 0 %'); pump(4.0)
except KeyboardInterrupt:
    log("Ctrl-C — abortando")
except Exception as e:
    log(f"ABORTADO: {e}")
finally:
    state['fase'] = 'fim'; state['thr'] = 0.0
    for _ in range(2):
        tx.sendto(c_esc(0), BOARD); tx.sendto(c_start('STATUS_ERROR'), BOARD); time.sleep(0.1)
    log("-> throttle 0 % + DESLIGAR (ESC desarmado, reles abertos)")
    try: pump(3.0)
    except Exception: pass
    fh.close(); log(f"fim — {n[0]} pacotes em {fn}")
