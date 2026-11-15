"""Varredura de um parâmetro do observador com o motor num degrau fixo.

Arma a bancada, sobe o throttle até THR e, com a rotação estável, troca o
parâmetro (por CMD, pela rede) a cada DWELL segundos, gravando toda a
telemetria em CSV. O observador reinicia a cada troca (Observer_Apply_Pending),
então os primeiros ~1–2 s de cada valor são re-travamento da PLL.
No fim (ou em erro/Ctrl-C): devolve o parâmetro ao default, throttle 0 e
desliga ESC + relés.

Uso (GUI FECHADA):
    .venv/bin/python Ensaios/varredura_observador.py [alvo] [valores] [thr%] [s_por_valor]
    .venv/bin/python Ensaios/varredura_observador.py TARGET_INDUCTANCE 11.3e-6,5.65e-6,22.6e-6,45.2e-6,11.3e-6 30 8
"""
import socket, struct, time, sys, csv, os, yaml
APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # gui/
sys.path.insert(0, APP)
from Communication_Data import codecs

TARGET = sys.argv[1] if len(sys.argv) > 1 else "TARGET_INDUCTANCE"
VALUES = [float(x) for x in (sys.argv[2] if len(sys.argv) > 2 else
          "11.3e-6,5.65e-6,22.6e-6,45.2e-6,11.3e-6").split(",")]
THR = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
DWELL = float(sys.argv[4]) if len(sys.argv) > 4 else 8.0
ARM_WAIT, SETTLE = 15.0, 8.0          # relés + arranque do ESC; rotação estabilizar

raw = yaml.safe_load(open(os.path.join(APP, 'Config', 'protocolo.yaml'))); en = raw['enums']
lay = codecs.telemetry_layout(codecs.board_protocol_cfg(raw, 'PLACA'))
ix = lambda c, n: en[c].index(n)
BOARD = ('169.254.97.200', 5000)
def c_start(st): return struct.pack('<BBBfb', ix('func_type','FUNC_START'), ix('instruct_type','INSTR_WRITE'), ix('target_type','TARGET_DEFAULT'), 1.0, ix('status_type', st))
def c_esc(p):    return struct.pack('<BBBfb', ix('func_type','FUNC_ESC'), ix('instruct_type','INSTR_WRITE'), ix('target_type','TARGET_DEFAULT'), float(p), 0)
def c_param(v):  return struct.pack('<BBBfb', ix('func_type','FUNC_SENSOR'), ix('instruct_type','INSTR_WRITE'), ix('target_type', TARGET), float(v), ix('default_action','DEFAULT_ACTION_NONE'))
def c_param_default(): return struct.pack('<BBBfb', ix('func_type','FUNC_SENSOR'), ix('instruct_type','INSTR_WRITE'), ix('target_type', TARGET), 0.0, ix('default_action','DEFAULT_ACTION_SENSE_RESET_DEFAULT'))

rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
rx.bind(('', 5555)); rx.settimeout(0.2)
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
fn = os.path.join(APP, 'Ensaios', time.strftime(f'varredura_{TARGET.replace("TARGET_","").lower()}_%Y%m%d_%H%M%S.csv'))
fh = open(fn, 'w', newline=''); w = csv.writer(fh)
names = [f['name'] for f in lay]
w.writerow(['pc_time_s', 't_s', 'fase', 'throttle_cmd_pct', 'param_valor'] + names)
t0 = time.time(); st = {'fase': 'base', 'thr': 0.0, 'val': float('nan')}; last = [0.0]; n = [0]
def log(m): print(f"{time.time()-t0:7.1f}s  {m}", flush=True)
def send(c, what): tx.sendto(c, BOARD); log(f"-> {what}")
def pump(sec):
    end = time.time() + sec
    while time.time() < end:
        try: d, _ = rx.recvfrom(2048)
        except socket.timeout: continue
        r = codecs.decode_telemetry(lay, d)
        if not r: continue
        now = time.time(); n[0] += 1
        w.writerow([f"{now:.3f}", f"{now-t0:.3f}", st['fase'], st['thr'], st['val']] + [r[k] for k in names])
        ec = int(r['error_code'])
        if st['fase'] not in ('base', 'fim') and (ec >> 19 & 1):
            raise RuntimeError("EMERGENCIA ativa (bit 19) — abortando")
        if now - last[0] >= 1.0:
            last[0] = now
            log(f"[{st['fase']:>9} {st['thr']:4.0f}% {TARGET[7:]}={st['val']:.4g}] rpm={r['rpm_same']:6.0f} "
                f"Te={r['torque_eletrico']:6.3f} drops={r['batch_drops']:.0f} err=0x{ec:08X}")
try:
    log(f"CSV: {fn} | {TARGET} = {VALUES} a {THR:.0f}% x {DWELL:.0f} s")
    pump(3.0)
    if n[0] == 0: raise RuntimeError("sem telemetria (GUI aberta? cabo?)")
    st['fase'] = 'armando'; send(c_start('STATUS_OK'), 'PRONTO (relés + ESC)'); pump(ARM_WAIT)
    st['fase'] = 'subindo'; st['thr'] = THR; send(c_esc(THR), f'throttle {THR:.0f} %'); pump(SETTLE)
    for v in VALUES:
        st['fase'] = 'varredura'; st['val'] = v; send(c_param(v), f'{TARGET} = {v:.4g}'); pump(DWELL)
except KeyboardInterrupt:
    log("Ctrl-C — abortando")
except Exception as e:
    log(f"ABORTADO: {e}")
finally:
    st['fase'] = 'fim'; st['thr'] = 0.0
    for _ in range(2):
        tx.sendto(c_param_default(), BOARD); tx.sendto(c_esc(0), BOARD); tx.sendto(c_start('STATUS_ERROR'), BOARD); time.sleep(0.1)
    log(f"-> {TARGET} de volta ao default + throttle 0 % + DESLIGAR")
    try: pump(3.0)
    except Exception: pass
    fh.close(); log(f"fim — {n[0]} pacotes em {fn}")
