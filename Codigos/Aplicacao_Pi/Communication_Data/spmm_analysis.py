"""Camada 2/3 — pós-processamento dos ensaios da SPMM (tudo no PC, em Python).

Segue as fórmulas de ../SPMM/CLAUDE.md:
- Atrito (regime permanente):  Te = B·ω + τc  → regressão linear (forma fechada).
- Inércia (coast-down):        J·(dω/dt) = -(B·ω + τc)  →  J = -(B·ω+τc)/(dω/dt),
  com t = n·Ts (contador de amostra, NUNCA o horário de chegada do pacote).

Funções puras (numpy) — sem I/O, sem rede.
"""
import numpy as np


def _linfit(x, y):
    """Regressão linear y = a·x + b em FORMA FECHADA (mínimos quadrados), SEM
    LAPACK/`np.polyfit`. Robusta: filtra NaN/inf e retorna (nan, nan) se houver
    menos de 2 pontos ou x degenerado (variância ~0). Evita o crash do LAPACK
    (DLASCL illegal value / segfault) com dados ruins vindos de bancada
    desconectada.
    """
    x = np.asarray(x, float)
    y = np.asarray(y, float)
    m = np.isfinite(x) & np.isfinite(y)
    x, y = x[m], y[m]
    n = x.size
    if n < 2:
        return float("nan"), float("nan")
    sx = float(x.sum()); sy = float(y.sum())
    sxx = float((x * x).sum()); sxy = float((x * y).sum())
    denom = n * sxx - sx * sx
    if abs(denom) < 1e-12:          # x praticamente constante -> reta indefinida
        return float("nan"), float("nan")
    a = (n * sxy - sx * sy) / denom
    b = (sy - a * sx) / n
    return float(a), float(b)


def fit_atrito(omega, te):
    """Regressão Te = B·ω + τc. Retorna (B, tc, r2).

    B = coef. angular [N·m·s/rad], tc = intercepto [N·m], r2 = qualidade.
    Usa `_linfit` (forma fechada) — nunca chama LAPACK, então dados
    degenerados/NaN retornam nan em vez de causar segfault.
    """
    omega = np.asarray(omega, float)
    te = np.asarray(te, float)
    B, tc = _linfit(omega, te)
    if not np.isfinite(B):
        return float("nan"), float("nan"), float("nan")
    m = np.isfinite(omega) & np.isfinite(te)
    om, t = omega[m], te[m]
    pred = B * om + tc
    ss_res = float(np.sum((t - pred) ** 2))
    ss_tot = float(np.sum((t - np.mean(t)) ** 2)) or 1e-12
    r2 = 1.0 - ss_res / ss_tot
    return float(B), float(tc), float(r2)


def compute_J(t, omega, B, tc, n_windows=5):
    """Momento de inércia J a partir do coast-down ω(t).

    J = -(B·ω + τc) / (dω/dt). Calcula J em `n_windows` janelas de RPM ao longo
    do decaimento (validação: J deve ficar ~constante). Em cada janela ajusta
    uma reta em ω(t) (slope = dω/dt) e usa o ω médio.

    Retorna {"J_mean", "J_std", "windows": [ {omega, dwdt, J}, ... ]}.
    """
    t = np.asarray(t, float)
    omega = np.asarray(omega, float)
    # Ordena por tempo e mantém só o trecho de decaimento (ω decrescente).
    order = np.argsort(t)
    t, omega = t[order], omega[order]
    ok = np.isfinite(t) & np.isfinite(omega) & (omega > 0)
    t, omega = t[ok], omega[ok]
    if len(t) < 2 * n_windows:
        return {"J_mean": float("nan"), "J_std": float("nan"), "windows": []}

    idx = np.array_split(np.arange(len(t)), n_windows)
    windows = []
    for seg in idx:
        if len(seg) < 3:
            continue
        tw, ww = t[seg], omega[seg]
        slope, _ = _linfit(tw, ww)            # dω/dt na janela (forma fechada)
        w_mean = float(np.mean(ww))
        if not np.isfinite(slope) or slope >= 0:   # indefinido ou não desacelera
            continue
        J = -(B * w_mean + tc) / slope        # slope<0 → J>0
        windows.append({"omega": w_mean, "dwdt": float(slope), "J": float(J)})

    Js = np.array([w["J"] for w in windows], float)
    return {
        "J_mean": float(np.mean(Js)) if len(Js) else float("nan"),
        "J_std": float(np.std(Js)) if len(Js) else float("nan"),
        "windows": windows,
    }


def rpm_to_omega(rpm):
    return np.asarray(rpm, float) * (2.0 * np.pi / 60.0)
