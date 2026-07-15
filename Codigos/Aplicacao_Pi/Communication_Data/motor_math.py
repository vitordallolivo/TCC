"""motor_math.py — Clarke/Park pra obter Iq/Id das correntes de fase medidas
(SAME), sem depender da estimativa de torque do motor (Kt é específico de
cada motor). Clarke é puramente geométrica; Park precisa de theta_e.

Uso: coletar os dados brutos primeiro (buffers da GUI) e calcular depois,
pra poder testar fontes de theta_e diferentes offline. Todas as funções
aceitam escalar ou array numpy. Clarke com invariância de amplitude (2/3).
"""
import numpy as np

_SQRT3_2 = np.sqrt(3.0) / 2.0


def clarke(ia, ib, ic):
    """abc -> αβ (invariância de amplitude, 2/3). Independente do motor.

    Retorna (i_alpha, i_beta). Para um sistema equilibrado (ia+ib+ic=0) isto
    coincide com a forma reduzida de 2 correntes."""
    ia = np.asarray(ia, dtype=float)
    ib = np.asarray(ib, dtype=float)
    ic = np.asarray(ic, dtype=float)
    i_alpha = (2.0 / 3.0) * (ia - 0.5 * ib - 0.5 * ic)
    i_beta = (2.0 / 3.0) * (_SQRT3_2 * (ib - ic))
    return i_alpha, i_beta


def park(i_alpha, i_beta, theta_e):
    """αβ -> dq. `theta_e` = ângulo ELÉTRICO do rotor (rad). Retorna (id, iq).

        id =  iα·cos(θ) + iβ·sin(θ)
        iq = -iα·sin(θ) + iβ·cos(θ)
    """
    i_alpha = np.asarray(i_alpha, dtype=float)
    i_beta = np.asarray(i_beta, dtype=float)
    theta_e = np.asarray(theta_e, dtype=float)
    c = np.cos(theta_e)
    s = np.sin(theta_e)
    i_d = i_alpha * c + i_beta * s
    i_q = -i_alpha * s + i_beta * c
    return i_d, i_q


def magnitude(i_alpha, i_beta):
    """|I| = sqrt(iα²+iβ²). Independe de θ_e; ≈ iq quando o controle mantém id≈0."""
    i_alpha = np.asarray(i_alpha, dtype=float)
    i_beta = np.asarray(i_beta, dtype=float)
    return np.hypot(i_alpha, i_beta)


def electrical_angle_from_speed(t, rpm, pole_pairs, theta0=0.0):
    """Reconstrói theta_e integrando a velocidade (theta_e = theta0 + integral de omega_e dt).

    Uma fonte candidata de theta_e (a partir do RPM da SIBA) pra experimentar
    offline, não uma referência absoluta — depende do alinhamento em theta0.

    Args:
        t: tempo em segundos (array)
        rpm: rotação mecânica
        pole_pairs: pares de polos do motor
        theta0: ângulo elétrico inicial (rad)

    Returns:
        Ângulo elétrico em [0, 2*pi).
    """
    t = np.asarray(t, dtype=float)
    rpm = np.asarray(rpm, dtype=float)
    omega_e = 2.0 * np.pi * (rpm / 60.0) * float(pole_pairs)
    if t.size < 2:
        return np.full_like(omega_e, theta0)
    dt = np.diff(t, prepend=t[0])
    theta = theta0 + np.cumsum(omega_e * dt)
    return np.mod(theta, 2.0 * np.pi)


def dq_from_phases(ia, ib, ic, theta_e):
    """Atalho: abc -> (id, iq, i_alpha, i_beta, |I|) para um θ_e dado."""
    i_alpha, i_beta = clarke(ia, ib, ic)
    i_d, i_q = park(i_alpha, i_beta, theta_e)
    return i_d, i_q, i_alpha, i_beta, magnitude(i_alpha, i_beta)


def imbalance(ia, ib, ic):
    """Diagnóstico de desbalanceamento das 3 fases (num sistema equilibrado
    sem neutro, ia+ib+ic ~= 0). Vetorizado: com arrays, opera ponto a ponto.

    Returns:
        dict com soma (ia+ib+ic), magnitude (|I|), rel (soma relativa à
        corrente) e imbalanced (bool, rel > 5%).
    """
    ia = np.asarray(ia, dtype=float)
    ib = np.asarray(ib, dtype=float)
    ic = np.asarray(ic, dtype=float)
    soma = ia + ib + ic
    i_alpha, i_beta = clarke(ia, ib, ic)
    mag = magnitude(i_alpha, i_beta)
    with np.errstate(divide="ignore", invalid="ignore"):
        rel = np.where(mag > 1e-9, np.abs(soma) / (1.5 * mag), 0.0)
    return {
        "soma": soma,            # ia+ib+ic (deveria ser ~0)
        "magnitude": mag,        # |I| de referência
        "rel": rel,              # |soma| relativo à corrente (0 = equilibrado)
        "imbalanced": rel > 0.05,  # >5% já indica desbalanceio/sensor ruim
    }
