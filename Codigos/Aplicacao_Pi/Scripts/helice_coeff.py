"""
identificacao_helice.py — identificação do coeficiente global de carga da
hélice (kQ) por patamar de RPM, a partir do torque real (média) e do torque
estimado pelo SMO (RMS, robusto às quedas de observabilidade).

Lê os ganhos otimizados de smo_gains.json (fallback embutido), roda a simulação
via test_coeff_auto e gera a figura com o nome usado no TCC.
"""
import numpy as np, math, json, os, importlib.util
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.signal import butter, lfilter

# ===================== CONFIG =====================
IMG_DIR    = "."                       # ex.: "Textuais/Imgs/Desenvolvimento"
# SMO_GAINS_JSON escolhe os ganhos e SMO_PLOT_SUFFIX vai no nome da figura
# (ex.: "_15x6E"); a carga da hélice vem de SMO_K_LOAD (test_coeff_auto).
GAINS_JSON = os.environ.get("SMO_GAINS_JSON", "smo_gains.json")
PLOT_SUFFIX = os.environ.get("SMO_PLOT_SUFFIX", "")
KS1, KS2, KT = 2_872_277.0, -842_983.0, 2_641.5   # fallback
ATRASO_PI_S = 0.2                      # descarta o transitório do PI (s)
RPM_MIN     = 1000.0                    # ignora patamares abaixo disso
LP_HZ       = 40.0                     # corte do filtro de tendência (figura)
# =================================================

# Modo CSV: consome o CSV exportado pela GUI e calcula, por ponto de
# operação, J=V/(n*D), CT=T/(rho*n²*D⁴), CQ=Q/(rho*n²*D⁵), CP=2*pi*CQ,
# eta=J*CT/CP. É uma lista de pontos, não um coeficiente único.
# --csv roda este modo e sai; senão cai no modo simulação.
def _read_telemetry_csv(path):
    """Lê o CSV exportado (cabeçalho + colunas por nome). Retorna
    (header, {coluna: np.array})."""
    import csv as _csv
    with open(path, newline="") as f:
        rows = list(_csv.reader(f))
    if len(rows) < 2:
        raise ValueError("CSV sem dados")
    header = [h.strip() for h in rows[0]]
    cols = {h: [] for h in header}
    for r in rows[1:]:
        if len(r) < len(header):
            continue
        for i, h in enumerate(header):
            try:
                cols[h].append(float(r[i]))
            except ValueError:
                cols[h].append(float("nan"))
    return header, {h: np.array(v) for h, v in cols.items()}


def _run_csv(path, thrust_col, torque_col="torque_eletrico", rpm_col="rpm_same",
             v_col="air_speed", D=None, rho=1.225, plot=False, outdir=".",
             v_fixed=None):
    header, cols = _read_telemetry_csv(path)

    # rpm_same (estimado SAME) por padrão; cai em 'rpm' (SIBA) se faltar/vazio.
    if rpm_col not in cols or np.all(~np.isfinite(cols.get(rpm_col, np.array([np.nan])))):
        if "rpm" in cols:
            print(f"[hélice] '{rpm_col}' ausente/vazio — usando 'rpm'.")
            rpm_col = "rpm"
    for rot, col in [("tração", thrust_col), ("torque", torque_col), ("RPM", rpm_col)]:
        if col not in cols:
            print(f"ERRO: coluna '{col}' ({rot}) não está no CSV. Colunas: {header}")
            return 1
    if not D or D <= 0:
        print("ERRO: informe o diâmetro D da hélice (--D).")
        return 1

    rpm = cols[rpm_col]; T = cols[thrust_col]; Q = cols[torque_col]
    # Velocidade do ar: fixa para TODOS os pontos (se v_fixed marcado) ou a do
    # CSV (coluna v_col) por ponto.
    if v_fixed is not None:
        V = np.full(len(rpm), float(v_fixed))
    else:
        V = cols[v_col] if v_col in cols else np.zeros_like(rpm)

    valid = np.isfinite(rpm) & (rpm > 500) & np.isfinite(T) & np.isfinite(Q)
    rpm, T, Q, V = rpm[valid], T[valid], Q[valid], V[valid]
    if len(rpm) == 0:
        print("ERRO: sem pontos válidos (RPM>500 com tração/torque finitos).")
        return 1

    # Agrupa em pontos de operação: RPM ~100 e V ~0.5 (colapsa o stream 10 Hz).
    groups = {}
    for i in range(len(rpm)):
        vi = V[i] if np.isfinite(V[i]) else 0.0
        k = (round(rpm[i] / 100.0) * 100, round(vi * 2.0) / 2.0)
        groups.setdefault(k, []).append(i)

    v_src = f"fixa={v_fixed} m/s" if v_fixed is not None else f"'{v_col}' (CSV)"
    print(f"Modo CSV (hélice) — {len(rpm)} amostras, {len(groups)} pontos de operação")
    print(f"  RPM='{rpm_col}'  tração='{thrust_col}'  torque='{torque_col}'  V={v_src}  D={D} m  ρ={rho}")
    hdr = (f"{'RPM':>7} {'V[m/s]':>7} {'J':>7} {'T[N]':>9} {'Q[N·m]':>9} "
           f"{'CT':>8} {'CQ':>9} {'CP':>8} {'eta':>6}")
    print(hdr); print("-" * len(hdr))

    points = []
    for _, idxs in sorted(groups.items()):
        idxs = np.array(idxs)
        rpm_m = float(np.mean(rpm[idxs])); T_m = float(np.mean(T[idxs]))
        Q_m = float(np.mean(Q[idxs])); V_m = float(np.nanmean(V[idxs])) if np.any(np.isfinite(V[idxs])) else 0.0
        n = rpm_m / 60.0
        if n <= 0:
            continue
        J = V_m / (n * D)
        CT = T_m / (rho * n * n * D**4)
        CQ = Q_m / (rho * n * n * D**5)
        CP = 2.0 * math.pi * CQ
        eta = (J * CT / CP) if (CP > 1e-12 and V_m > 0) else float("nan")
        p = dict(rpm=rpm_m, V=V_m, J=J, thrust=T_m, torque=Q_m, CT=CT, CQ=CQ, CP=CP, eta=eta)
        points.append(p)
        # Linha parseável pela GUI (uma por ponto de operação).
        print(f"POINT rpm={rpm_m:.1f} V={V_m:.2f} J={J:.4f} thrust={T_m:.4f} "
              f"torque={Q_m:.5f} CT={CT:.5f} CQ={CQ:.6f} CP={CP:.5f} eta={eta:.4f}")
        print(f"{rpm_m:>7.0f} {V_m:>7.2f} {J:>7.3f} {T_m:>9.3f} {Q_m:>9.4f} "
              f"{CT:>8.4f} {CQ:>9.5f} {CP:>8.4f} {eta:>6.3f}")
    print("-" * len(hdr))
    ct_mean = float(np.mean([p["CT"] for p in points])) if points else 0.0
    cq_mean = float(np.mean([p["CQ"] for p in points])) if points else 0.0
    print(f"RESULT n_points={len(points)} CT_mean={ct_mean:.5f} CQ_mean={cq_mean:.6f}")

    # Salva a LISTA de coeficientes em CSV (para reuso/registro).
    os.makedirs(outdir, exist_ok=True)
    lista_csv = os.path.join(outdir, "Helice_coeficientes.csv")
    import csv as _csv
    with open(lista_csv, "w", newline="") as f:
        wr = _csv.writer(f)
        wr.writerow(["RPM", "V", "J", "thrust_N", "torque_Nm", "CT", "CQ", "CP", "eta"])
        for p in points:
            wr.writerow([f"{p['rpm']:.1f}", f"{p['V']:.3f}", f"{p['J']:.4f}",
                         f"{p['thrust']:.4f}", f"{p['torque']:.5f}", f"{p['CT']:.5f}",
                         f"{p['CQ']:.6f}", f"{p['CP']:.5f}", f"{p['eta']:.4f}"])
    print("salvo:", lista_csv)

    if plot:
        varia_V = np.ptp([p["V"] for p in points]) > 0.5
        xk = "J" if varia_V else "rpm"
        xs = [p[xk] for p in points]
        fig, (a1, a2) = plt.subplots(1, 2, figsize=(12, 5))
        a1.scatter(xs, [p["CT"] for p in points], c="blue", s=50)
        a1.set_ylabel("CT"); a1.set_title("Coeficiente de tração")
        a2.scatter(xs, [p["CQ"] for p in points], c="red", s=50)
        a2.set_ylabel("CQ"); a2.set_title("Coeficiente de torque")
        for ax in (a1, a2):
            ax.set_xlabel("Razão de avanço J" if xk == "J" else "RPM")
            ax.grid(True, alpha=0.4)
        fig.tight_layout()
        saida = os.path.join(outdir, "Helice_CT_CQ.png")
        fig.savefig(saida, dpi=120, bbox_inches="tight"); plt.close(fig)
        print("salvo:", saida)
    return 0


if __name__ == "__main__":
    import argparse
    _ap = argparse.ArgumentParser(description="Coeficientes da hélice (CT, CQ, J) a partir do CSV de telemetria")
    _ap.add_argument("--csv", default=None, help="CSV completo exportado pela GUI")
    _ap.add_argument("--thrust-col", default=None, help="Coluna da TRAÇÃO (força escolhida, ex.: load_cell_0)")
    _ap.add_argument("--torque-col", default="torque_eletrico", help="Coluna do torque (padrão: estimado pela SAME)")
    _ap.add_argument("--rpm-col", default="rpm_same", help="Coluna de RPM (padrão: estimado pela SAME)")
    _ap.add_argument("--v-col", default="air_speed", help="Coluna da velocidade do ar")
    _ap.add_argument("--v-fixed", type=float, default=None,
                     help="Velocidade do ar FIXA p/ todos os pontos (ignora --v-col)")
    _ap.add_argument("--D", type=float, default=None, help="Diâmetro da hélice [m]")
    _ap.add_argument("--rho", type=float, default=1.225, help="Densidade do ar [kg/m³]")
    _ap.add_argument("--plot", action="store_true", help="Gera as figuras CT/CQ")
    _ap.add_argument("--outdir", default=".", help="Diretório de saída")
    _args, _ = _ap.parse_known_args()
    if _args.csv:
        import sys as _sys
        if not _args.thrust_col:
            print("ERRO: informe a coluna de tração com --thrust-col.")
            _sys.exit(2)
        _sys.exit(_run_csv(_args.csv, _args.thrust_col, _args.torque_col, _args.rpm_col,
                           _args.v_col, _args.D, _args.rho, _args.plot, _args.outdir,
                           v_fixed=_args.v_fixed))
    # sem --csv: segue no modo simulação (código original abaixo)

if os.path.exists(GAINS_JSON):
    _g = json.load(open(GAINS_JSON))["gains"]
    KS1, KS2, KT = _g["Ks1"], _g["Ks2"], _g["Kt"]
    print(f"ganhos de {GAINS_JSON}: Ks1={KS1:,.0f} Ks2={KS2:,.0f} Kt={KT:,.1f}")

# carrega o simulador
spec = importlib.util.spec_from_file_location("smo", "test_coeff_auto.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
t, Ts, N = m.t, m.Ts, m.N
RPM_target_profile, k_load = m.RPM_target_profile, m.k_load

out = m.run_sim_core(N, Ts, RPM_target_profile, m.PHASE_TEST_MODE,
                     m.GBW_OPAMP, m.GAIN_OPAMP, m.PLANT_BEMF, m.SMO_BEMF,
                     KS1, KS2, KT, m.SMO_SWITCH_FUNC)

# ----------------------------------------------------------------------
# Detecção dos patamares (alvo constante e acima de RPM_MIN)
# ----------------------------------------------------------------------
w_m_rad   = out[0] * (math.pi / 30.0)     # vel. real  [rad/s]
w_est_rad = out[1] * (math.pi / 30.0)     # vel. estimada
tq_real   = out[2]                        # torque real
tq_smo    = out[3]                        # torque do SMO (correntes ESTIMADAS)

# Torque para a identificação: correntes MEDIDAS projetadas no ângulo do
# observador.  T = 1,5*Ke*p*(f_a(th)*i_a + f_b(th)*i_b).  O torque interno do
# SMO usa as correntes estimadas, cuja trepidação (chattering) não tem média
# nula e enviesa a média do patamar (+44 % a +115 % com hélices reais, onde o
# torque médio é ~20x menor que a ondulação do acionamento em seis passos).
# Com a corrente medida o viés some (erro < 0,5 %); é o mesmo critério do
# ensaio de bancada (corrente medida projetada no ângulo estimado).
_fs = np.vectorize(lambda th: m.f_shape(th, m.SMO_BEMF))
th_hat = out[5]
f_al = _fs(th_hat)
f_be = (1.0/math.sqrt(3.0)) * (_fs(th_hat - 2*math.pi/3) - _fs(th_hat - 4*math.pi/3))
tq_est = 1.5 * m.Ke * m.p * (f_al*out[19] + f_be*out[20])

target_diff = np.append(np.diff(RPM_target_profile), 0)
mascara = (target_diff == 0) & (RPM_target_profile > RPM_MIN)
edges = np.diff(mascara.astype(int))
starts = np.where(edges == 1)[0] + 1
ends   = np.where(edges == -1)[0] + 1
if mascara[0]:  starts = np.insert(starts, 0, 0)
if mascara[-1]: ends   = np.append(ends, N)

# ----------------------------------------------------------------------
# Identificação de kQ por patamar:  T = kQ * omega^2
#   real -> média do torque ;  SMO -> RMS (robusto às quedas)
# ----------------------------------------------------------------------
atraso = int(ATRASO_PI_S / Ts)
k_real_list, k_smo_list, rpm_patamares = [], [], []

hdr = f"{'Intervalo (s)':<15} | {'Alvo':<7} | {'TqReal_Avg':<10} | {'TqSMO_RMS':<10} | {'k_Real':<9} | {'k_SMO'}"
print("=" * len(hdr)); print(hdr); print("-" * len(hdr))
for s, e in zip(starts, ends):
    if (e - s) <= atraso:
        continue
    idx_s, idx_e = s + atraso, e - 10
    w_v, w_s_v = w_m_rad[idx_s:idx_e], w_est_rad[idx_s:idx_e]
    tr_v, ts_v = tq_real[idx_s:idx_e], tq_est[idx_s:idx_e]

    tq_real_avg = np.mean(tr_v)
    tq_smo_rms  = np.mean(ts_v)
    # desconta as perdas a vazio (B*w + tau_c) antes de atribuir o torque à
    # hélice — zero se o simulador roda sem perdas (SMO_B_LOSS/SMO_TAU_C)
    B_l, tau_c = getattr(m, "B_loss", 0.0), getattr(m, "TAU_C", 0.0)
    k_r = (tq_real_avg - B_l*np.mean(w_v)   - tau_c) / (np.mean(w_v)**2)
    k_s = (tq_smo_rms  - B_l*np.mean(w_s_v) - tau_c) / (np.mean(w_s_v)**2)

    k_real_list.append(k_r); k_smo_list.append(k_s)
    rpm_patamares.append(RPM_target_profile[idx_s])
    print(f"[{s*Ts:>5.2f}-{e*Ts:>5.2f}] | {RPM_target_profile[idx_s]:>7.1f} | "
          f"{tq_real_avg:>10.4f} | {tq_smo_rms:>10.4f} | {k_r:>9.2e} | {k_s:>9.2e}")

media_k_smo  = np.mean(k_smo_list)  if k_smo_list  else 0.0
media_k_real = np.mean(k_real_list) if k_real_list else 0.0
erro_pct = abs(media_k_smo - k_load) / k_load * 100 if k_load else 0.0
print("-" * len(hdr))
print(f"VALOR NOMINAL: {k_load:.2e}  |  MÉDIA K_REAL: {media_k_real:.2e}  |  "
      f"MÉDIA K_SMO: {media_k_smo:.2e}  |  ERRO: {erro_pct:.2f}%")
print("=" * len(hdr))

# ----------------------------------------------------------------------
# Figura
# ----------------------------------------------------------------------
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))

fs = 1.0 / Ts
b, a = butter(1, LP_HZ / (0.5 * fs), btype='low')
ax1.plot(t, lfilter(b, a, tq_real), 'b',   lw=2,   label='Te real (filtrado)')
ax1.plot(t, lfilter(b, a, tq_est),  'r--', lw=1.5, label='Te estimado (filtrado)')
ax1.set_title("Comparativo de Torque: Dinâmica Real vs Estimada")
ax1.set_ylabel("Torque [N.m]"); ax1.legend(loc='upper right'); ax1.grid(True)

ax2.axhline(y=k_load, color='k', ls='--', label='k Nominal (Teórico)')
if rpm_patamares:
    ax2.scatter(rpm_patamares, k_real_list, c='blue', s=100, label='k Real')
    ax2.scatter(rpm_patamares, k_smo_list, c='red', marker='x', s=100, label='k SMO')
    _todos = list(k_real_list) + list(k_smo_list) + [k_load]
    ax2.set_ylim(min(_todos)*0.9, max(_todos)*1.1)
ax2.set_title(f"Identificação do Coeficiente k (Erro Final: {erro_pct:.2f}%)")
ax2.set_ylabel("Coeficiente k"); ax2.set_xlabel("Velocidade [RPM]")
ax2.legend(); ax2.grid(True)

plt.tight_layout()
os.makedirs(IMG_DIR, exist_ok=True)
saida = os.path.join(IMG_DIR, f"Algortimo_Coeficiente_helice{PLOT_SUFFIX}.png")
fig.savefig(saida, dpi=120, bbox_inches="tight"); plt.close(fig)
print("salvo:", saida)