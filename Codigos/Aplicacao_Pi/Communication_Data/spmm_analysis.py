"""Camada 2/3 — pós-processamento dos ensaios da SPMM (tudo no PC, em Python).

Segue as fórmulas de ../SPMM/CLAUDE.md:
- Atrito (regime permanente):  Te = B·ω + τc  → regressão linear (polyfit).
- Inércia (coast-down):        J·(dω/dt) = -(B·ω + τc)  →  J = -(B·ω+τc)/(dω/dt),
  com t = n·Ts (contador de amostra, NUNCA o horário de chegada do pacote).

Funções puras (numpy) — sem I/O, sem rede.
"""
import numpy as np


def fit_atrito(omega, te):
    """Regressão Te = B·ω + τc. Retorna (B, tc, r2).

    B = coef. angular [N·m·s/rad], tc = intercepto [N·m], r2 = qualidade.
    """
    omega = np.asarray(omega, float)
    te = np.asarray(te, float)
    if len(omega) < 2:
        return float("nan"), float("nan"), float("nan")
    B, tc = np.polyfit(omega, te, 1)
    pred = B * omega + tc
    ss_res = float(np.sum((te - pred) ** 2))
    ss_tot = float(np.sum((te - np.mean(te)) ** 2)) or 1e-12
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
        slope, _ = np.polyfit(tw, ww, 1)     # dω/dt na janela
        w_mean = float(np.mean(ww))
        if slope >= 0:                        # não está desacelerando: ignora
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
