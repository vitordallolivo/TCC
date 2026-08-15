/**
 * @file harness.c
 * @brief Alimenta o Observer_Manager.c REAL (o mesmo arquivo do firmware, via
 * shim de Zephyr) com uma sequência fixa de (v_αβ, i_αβ) e despeja as saídas
 * em CSV. Compilado uma vez por variante de OBS_IMPL; o compare.py confronta
 * os CSVs.
 *
 * Objetivo: equivalência numérica V0 x V5 (plano de validação do CLAUDE.md,
 * item 2 — "make native_sim / equivalência numérica"). Roda no host porque a
 * matemática é idêntica: float IEEE-754 single, mesmas operações. O que o
 * host NÃO cobre — ciclos, footprint, DMA, registradores do ADC — está no
 * bench de qemu (tests/observer_bench) e na placa.
 *
 * Estímulo: motor SPM em regime, montado analiticamente em αβ. Para um
 * conjunto trifásico balanceado, a Clarke amplitude-invariante dá
 *     α = X·sin(θ)        β = −X·cos(θ)
 * (o β vem da mesma identidade (1/√3)(sin(θ−2π/3) − sin(θ−4π/3)) ≡ −cos θ que
 * o V5 usa no caminho quente — aqui ela só constrói o estímulo).
 * A tensão de terminal é a equação da máquina, não só a BEMF:
 *     v = R_s·i + L_s·di/dt + e
 * sem isso î não teria como seguir i e o teste não exercitaria o SMO.
 */

#include "Observer_Manager.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Planta do estímulo: mesmos parâmetros dos defaults do observador ---- */
#define PL_TS        10.0e-6           /* == DEF_TS */
#define PL_P         7.0               /* pares de polos */
#define PL_RS        17.3e-3
#define PL_LS        7.0e-6
#define PL_KV_Y      (310.0 / 1.7320508075688772)
#define PL_I_AMP     8.0               /* A (pico de fase) */
#define PL_LOAD_ANG  0.30              /* rad — atraso corrente x BEMF */

/* Três patamares de rotação (os mesmos do autotune_cost.py): pega regime
 * E transitório de degrau nos dois sentidos. */
static const double plateau_rpm[]   = { 1000.0, 3000.0, 1000.0 };
#define N_PLATEAU   (int)(sizeof(plateau_rpm) / sizeof(plateau_rpm[0]))
#define STEPS_PER_PLATEAU  30000       /* 0,3 s por patamar @ Ts = 10 µs */

/* Γ do autotune_cost.py (Aplicacao_Pi/Scripts/smo_gains.json, Ts_sim = 10 µs,
 * o mesmo DEF_TS). Os DEF_GAMMA* de fábrica do Observer_Manager.c NÃO fazem o
 * observador convergir — com eles î não segue i e o A/B compararia duas
 * variantes ambas em regime degenerado, o que não testa nada. Ver CLAUDE.md,
 * "DEF_GAMMA* divergem do autotuning". */
#define TUNED_GAMMA1   31.622776601683793f
#define TUNED_GAMMA2  (-10.0f)
#define TUNED_GAMMAT   0.01313873777f

int main(int argc, char **argv)
{
    const char *out_path = NULL;
    int tuned = 0;

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--tuned") == 0) {
            tuned = 1;
        } else if (argv[a][0] != '-') {
            out_path = argv[a];
        } else {
            fprintf(stderr, "uso: %s [saida.csv] [--tuned]\n", argv[0]);
            return 2;
        }
    }

    FILE *out = out_path ? fopen(out_path, "w") : stdout;
    if (!out) {
        fprintf(stderr, "erro: não abriu %s\n", out_path);
        return 2;
    }

    Observer_Init();

    if (tuned) {
        /* Pelo caminho real do CMD: agenda + aplica (recalcula constantes e
         * zera o estado), igual ao que a thread do ADC faz. */
        Observer_Set_Param(TARGET_GAMMA1, TUNED_GAMMA1);
        Observer_Set_Param(TARGET_GAMMA2, TUNED_GAMMA2);
        Observer_Set_Param(TARGET_GAMMAT, TUNED_GAMMAT);
        Observer_Apply_Pending();
    }

    const double ke_mec = 60.0 / (2.0 * M_PI * PL_KV_Y);
    double theta_e = 0.0;
    int all_finite = 1;

    fprintf(out, "step,plateau,rpm_ref,rpm_same,torque_eletrico,amp_bemf,phase_err\n");

    for (int k = 0; k < N_PLATEAU; k++) {
        const double rpm  = plateau_rpm[k];
        const double w_m  = rpm * (2.0 * M_PI / 60.0);   /* rad/s mecânico */
        const double w_e  = w_m * PL_P;                  /* rad/s elétrico */
        const double E    = ke_mec * w_m;                /* amplitude da BEMF */

        for (int n = 0; n < STEPS_PER_PLATEAU; n++) {
            const double th  = theta_e;
            const double thi = th - PL_LOAD_ANG;

            /* BEMF em αβ */
            const double e_al =  E * sin(th);
            const double e_be = -E * cos(th);

            /* corrente em αβ e sua derivada analítica */
            const double i_al =  PL_I_AMP * sin(thi);
            const double i_be = -PL_I_AMP * cos(thi);
            const double di_al =  PL_I_AMP * w_e * cos(thi);
            const double di_be =  PL_I_AMP * w_e * sin(thi);

            /* v = R_s·i + L_s·di/dt + e */
            const double v_al = PL_RS * i_al + PL_LS * di_al + e_al;
            const double v_be = PL_RS * i_be + PL_LS * di_be + e_be;

            Observer_Step((float)v_al, (float)v_be, (float)i_al, (float)i_be);

            if (!Observer_State_Is_Finite()) {
                all_finite = 0;
            }

            /* %.9g round-trips float exatamente — o compare.py não perde
             * precisão por causa do formato. */
            fprintf(out, "%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                    k * STEPS_PER_PLATEAU + n, k, rpm,
                    (double)Observer_Get_Rpm(),
                    (double)Observer_Get_Te(),
                    (double)Observer_Get_Bemf_Amplitude(),
                    (double)Observer_Get_Phase_Error());

            theta_e += w_e * PL_TS;
            if (theta_e >= 2.0 * M_PI) theta_e -= 2.0 * M_PI;
        }
    }

    if (out != stdout) fclose(out);

    fprintf(stderr, "harness: OBS_IMPL=%d  ganhos=%s  passos=%d  finito_sempre=%s\n",
            OBS_IMPL, tuned ? "autotune" : "fabrica",
            N_PLATEAU * STEPS_PER_PLATEAU, all_finite ? "sim" : "NAO");

    return all_finite ? 0 : 1;
}
