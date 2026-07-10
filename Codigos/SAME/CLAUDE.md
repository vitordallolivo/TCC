# SAME — Sistema de Aquisição do Motor Elétrico (Contexto para Claude Code)

## Visão geral

**SAME** é o **nó de aquisição elétrica do motor** da bancada de ensaios de
conjuntos motopropulsores BLDC (TCC "Bancada de Medição para Sistemas
Propulsivos Elétricos com BLDC", Albatroz Aerodesign / UDESC Joinville — motor
sob ensaio: **Scorpion A-5025-310Kv**).

A bancada tem **dois nós de aquisição** ligados por Ethernet (UDP) a um software
de supervisão (`../Aplicacao_Pi/`):

- **SIBA** (`../SIBA/`) — instrumentação **aerodinâmica + bateria**: células de
  carga (empuxo/torque da hélice), tensão/corrente/temperatura da bateria,
  Pitot (velocidade do ar), **RPM** (sensor óptico/hall no eixo) e aciona o ESC.
- **SAME** (esta pasta) — **medição elétrica do motor**: lê correntes e tensões
  de fase do ESC→motor e **estima ω e torque no eixo SEM sensores mecânicos** via
  **Observador por Modos Deslizantes (SMO)** + **PLL** + reconstrução analítica
  da força contraeletromotriz (BEMF).

> A `../Aplicacao_Pi/Config/SAME.yaml` é a **FONTE DE VERDADE do protocolo**
> (enums `CMD`/`TELEMETRY_LOG`, endereçamento). Os headers C do firmware são
> **gerados** dela por `../Aplicacao_Pi/Config/generate.py` (mesmo esquema da
> SIBA). Edite o YAML e regenere; não edite os `.h` gerados à mão.

---

## ✅ Status: FIRMWARE IMPLEMENTADO (compila HW + native_sim)

O firmware Zephyr da SAME existe e **compila nos dois alvos** (`make build` p/
`same_board`, `make native_sim` p/ PC). A SAME continua **só leitura/estimativa**:
não aciona o motor — quem aciona o ESC é a **SIBA** (recebe o throttle).

Estrutura implementada (espelha a arquitetura em camadas da SIBA):

```
SAME/
├── src/main.c                       # ordem de init
├── prj.conf / CMakeLists.txt        # 2 ADCs + rede UDP + FPU
├── boards/
│   ├── wch/same_board/*             # placa real: adc1+adc2 (dual), Ethernet, UART
│   └── native_sim.conf/.overlay     # adc-emul p/ adc1+adc2, NSOS (UDP no host)
├── drivers/adc/adc_ch32v307.c       # driver custom (multi-instância ADC1/ADC2) — reciclado da SIBA
├── lib/OS/
│   ├── OSSI.{h,c}                   # same_wq (despacho por eventos: telemetria + CMD)
│   ├── NCI.{h,c}                    # UDP: TX telemetria (4445), RX CMD (4444)
│   ├── HAL.{h,c} + HAL_prm.h        # 2 ADCs → Clarke → Observer_Step (thread de aquisição)
│   ├── CMD_Core.{h,c}               # despacho de CMD (calibração / params / ganhos)
│   ├── CMD_Core_prm.h               # TABELA de sensores (currents/voltages) — como na SIBA
│   ├── Common_Types.h / CMD.h       # GERADOS do SAME.yaml
│   └── TCKEC.h                      # stub (sem thread de polling — só eventos)
└── lib/SYS/
    ├── Log_Manager.{h,c}            # TELEMETRY_LOG + timer de TX (Ts_udp); .h GERADO
    └── Observer_Manager.{h,c}       # SMO + PLL + torque (núcleo — ver abaixo)
```

**Modelo de eventos (pedido do projeto — poucas threads):** só **duas** threads
de I/O bloqueante — a de aquisição (bloqueia na conversão dos ADCs → roda o
observador colado a esse evento) e a RX do NCI (bloqueia em `recvfrom`). Todo o
resto (escrever telemetria no log, despachar CMD, TX UDP) é `k_work` na
`same_wq`. O observador roda no ritmo do ADC (`Ts_adc`), **fora** da fila.

**Reinício do observador ao mudar uma variável:** um CMD que altera
parâmetro/ganho só marca `reconfig` pendente; a própria thread do ADC aplica no
topo do laço (`Observer_Apply_Pending`), **recalcula as constantes e ZERA o
estado** — o observador reinicia sem arrastar transitório do modelo antigo.

**Desempenho do observador:** `Observer_Step` roda em `float` puro (FPU
single-precision do CH32V307; `double`/`sin` cairiam em soft-float), com estado
e constantes carregados em locais `register` e constantes pré-calculadas
(`hot`/`flatten`). Transcrição direta do laço numba de `test_coeff_auto.py`.

---

## ⚠️ AVISOS CRÍTICOS ao implementar — RECICLAR CÓDIGO DA SIBA

Os drivers do **CH32V307VCT6 não estão disponíveis diretamente no Zephyr**. A
SIBA já resolveu isso; a SAME **deve reaproveitar** essa base em vez de refazer.

- **ADC — NÃO usar o driver padrão do Zephyr.** `CONFIG_ADC=y` neste micro pode
  puxar o **driver do CH32V003**, que **conflita com o registrador de Ethernet
  do CH32V307VCT6**. É **obrigatório** usar o **driver de ADC customizado** já
  criado para o CH32V307 (o mesmo da SIBA — `../SIBA/drivers/adc/adc_ch32v307.c`),
  e não o driver genérico que o `CONFIG_ADC` seleciona por padrão. Conferir o
  Kconfig/overlay/.dts da SIBA para ver como o driver customizado é selecionado e
  replicar — caso contrário a **Ethernet** (e portanto toda a telemetria UDP)
  quebra.

- **Copiar o setup POSIX/native_sim da SIBA.** A SIBA tem um build para
  **POSIX/native** que permite achar **bugs de aplicação** (lógica de estimação,
  parsing do `CMD`, manipulação de buffers) rodando no **PC**, fora do hardware —
  sem flashar a placa a cada teste. Copiar (`boards/native_sim.conf`, HAL
  synthetic, `make native_sim`). Acelera muito a depuração do SMO.

- **Copiar o Makefile da SIBA.** A SIBA tem um **Makefile com comandos especiais**
  (atalhos de `build`/`flash`/`native_sim`/`debug`). Copiar e adaptar para a
  SAME em vez de montar os comandos do zero.

- **Espelhar a arquitetura em camadas** da SIBA (OS/SYS/HAL, NCI/OSSI para a
  rede, `generate.py` a partir do `SAME.yaml`). Ver `../SIBA/CLAUDE.md` e
  `../SIBA/HW.md`.

---

## Protocolo de comunicação (fonte: `../Aplicacao_Pi/Config/SAME.yaml`)

Mesmo protocolo compacto da SIBA (`CMD` 8 bytes + `TELEMETRY_LOG` packed LE):

| | valor |
|---|---|
| `transport` | `udp_struct` |
| IP padrão | `169.254.97.100` (link-local) |
| PC escuta telemetria (`pc_rx`) | `4445` |
| Placa escuta comandos (`board_rx`) | `4444` |
| `CMD` (PC→placa) | 8 bytes `<BBBfb>` = `func, instr, target, value, action` |
| `TELEMETRY_LOG` (placa→PC) | struct packed LE, ~10 Hz |

### TELEMETRY_LOG (o que a SAME envia)
`timestamp`; **medidas ADC/shunts**: `V_a,V_b,V_c`, `i_a,i_b,i_c`; **referencial
estacionário αβ** (entrada do SMO): `V_alpha,V_beta`, `i_alpha,i_beta`;
**estimados pelo SMO**: `rpm_same`, `torque_eletrico` (Te), `torque_helice`;
`status`.

### CMD — targets (o que a SAME recebe)
- **Correntes/tensões** (calibração dos sensores): `TARGET_CURRENT_A/B/C`,
  `TARGET_VOLTAGE_AB/BC`.
- **Parâmetros do motor**: `TARGET_POLES_PAIRS`, `TARGET_RESISTANCE`,
  `TARGET_INDUCTANCE`, `TARGET_KV_RATING`.
- **Ganhos do SMO (na forma Γ — ver abaixo)**: `TARGET_GAMMA1`, `TARGET_GAMMA2`,
  `TARGET_GAMMAT`, e as frequências de filtro `TARGET_BEMF_LPF_FREQUENCY`,
  `TARGET_TORQUE_LPF_FREQUENCY`.
- `TARGET_DEFAULT` (start/stop/reset). Ações: calibração `..._LINEAR/OFFSET/
  RESET/MAX_VALID/MIN_VALID` e, para ganhos/params, `DEFAULT_ACTION_SET_VALUE` /
  `..._VALUE_RESET_DEFAULT`.

> 🔑 **Os ganhos configuráveis são Γ (Gamma), NÃO Ks1/Ks2/Kt.** `Ks*` dependem
> do passo de amostragem `Ts` (`Γ = K·Ts`); `Γ` é **invariante ao passo** e é o
> que se otimiza e transporta entre simulação e firmware. O firmware recupera
> `K = Γ/Ts` com o seu próprio `Ts`. Por isso o target é `Γ1, Γ2, Γt` e o
> autotuning/menu na `../Aplicacao_Pi/` trabalham em Γ.

---

## Parâmetros elétricos R e L (ensaio de rotor bloqueado)

Com o rotor travado (sem BEMF), o motor vira uma malha RL passiva em estrela.
- **Resistência de fase**: injeta CC entre dois terminais →
  **`R_fase = V_DC / (2·I_DC)`** (o `/2` converte linha-a-linha → estrela).
- **Indutância de fase** (método da **ressonância**): capacitor `C` conhecido em
  paralelo com o enrolamento forma um tanque LC; varre-se a frequência até `f_r`
  (impedância máxima) → **`L_linha = 1/(C·(2π f_r)²)`**, **`L_fase = L_linha/2`**.
  Motor SPM (ímãs na superfície) ⇒ `L_d ≈ L_q ≈ L_s`.

## Parâmetros mecânicos B, τc, J (compartilhados com a SPMM)

- **Atrito** (viscoso `B` + Coulomb `τc`): rotação a vazio em patamares; em
  regime (`dω/dt=0`) o torque eletromagnético anula as perdas:
  **`T_e,k = B·ω_m,k + τc`** → regressão linear sobre `(ω_m,k, T_e,k)`.
- **Inércia `J`**: **coast-down** — na rotação máxima corta-se o ESC (torque
  motriz zera); a desaceleração obedece **`J·dω/dt = -B·ω - τc`**; isola-se `J`
  da curva `ω(t)` e da derivada. Ver `../Aplicacao_Pi/Communication_Data/spmm_analysis.py`.

---

## SMO — equações passo a passo (referência: `../Aplicacao_Pi/Scripts/test_coeff_auto.py`)

O SMO opera no referencial estacionário **αβ**. Entradas: tensões `u_αβ` e
correntes `i_αβ` de fase (Clarke). Estados estimados: correntes `î_αβ` e
BEMF `ê_αβ`. Discretização **Euler-forward** com passo `Ts`.

**Constantes de discretização** (motor: `R_s`, `L_s`, `p` pares de polos):
```
Φ_i = 1 − (R_s·Ts / L_s)       # decaimento da corrente
Γ_v = Ts / L_s                 # ganho de entrada (tensão)
Φ_e = 1 − Γ_t                  # decaimento da BEMF  (Γ_t = Kt·Ts)
```
> **Γ_s1 = Ks1·Ts**, **Γ_s2 = Ks2·Ts**, **Γ_t = Kt·Ts** são os **ganhos
> invariantes ao passo** — é o que se configura/otimiza. `Ks* = Γ*/Ts`.

**1. Erro de corrente e função de comutação** (camada de contorno `BL`,
seletor: 0=saturação, 1=tanh, 2=sigmoide):
```
e_α = i_α − î_α ,   e_β = i_β − î_β
sat:      s = clamp(e/BL, −1, +1)
tanh:     s = tanh(e/BL)
sigmoide: s = (e/BL) / (1 + |e/BL|)
```

**2. Observador de corrente** (ganho deslizante `Γ_s1`):
```
î_α ← Φ_i·î_α + Γ_v·(u_α − ê_α) + Γ_s1·s_α
î_β ← Φ_i·î_β + Γ_v·(u_β − ê_β) + Γ_s1·s_β
```

**3. Observador da BEMF** (ganho `Γ_s2`, polo `Φ_e = 1 − Γ_t`):
```
ê_α ← Φ_e·ê_α + Γ_s2·s_α
ê_β ← Φ_e·ê_β + Γ_s2·s_β
```

**4. PLL** — extrai ângulo/velocidade da BEMF estimada. `f(·)` é a forma
normalizada da BEMF (senoidal `sin` ou trapezoidal). `ω_n = 2π·150`, `ζ=0.707`,
`Kp = 2ζω_n`, `Ki = ω_n²`:
```
amp_E = √(ê_α² + ê_β²)
g_α = f(θ̂) ,  g_β = (1/√3)·( f(θ̂ − 2π/3) − f(θ̂ − 4π/3) )
erro_fase = (g_α·ê_β − g_β·ê_α) / amp_E        # (0 se amp_E abaixo do limiar)
I_pll ← I_pll + erro_fase·Ts
ω_e = Kp·erro_fase + Ki·I_pll                  # velocidade ELÉTRICA
θ̂  ← (θ̂ + ω_e·Ts) mod 2π
ω_est = ω_e / p                                # velocidade mecânica (→ rpm_same)
```
(Compensação opcional de atraso do front-end analógico:
`θ_out = θ̂ + atan(ω_e/ω_polo)`, com `ω_polo = 2π·GBW/G_opamp`.)

**5. Torque estimado** (`Ke = Ke_mec/p`, `Ke_mec = 60/(2π·Kv)`), com LPF
(`α_tq = 2π f_c·Ts/(1 + 2π f_c·Ts)`):
```
f_a = f(θ_out) ,  f_b = (1/√3)·( f(θ_out − 2π/3) − f(θ_out − 4π/3) )
Te = 1.5 · Ke · p · ( f_a·î_α + f_b·î_β )
tq_lpf ← α_tq·Te + (1 − α_tq)·tq_lpf           # → torque_eletrico
```

> **Sinais fixos dos ganhos:** `Γ_s1 > 0`, `Γ_s2 < 0`, `Γ_t > 0`.
> Os valores ótimos de Γ vêm do autotuning **offline** que roda na
> `../Aplicacao_Pi/` (menu Estimativas), não no firmware da SAME — o firmware só
> **recebe** `Γ1/Γ2/Γt` por `CMD` e calcula `K = Γ/Ts` internamente.

---

## Implementações de referência (em `../Aplicacao_Pi/`)

- **`Scripts/test_coeff_auto.py`** — simulação completa planta+SMO+PLL (as
  equações acima). **Modelo do que o firmware da SAME deve embarcar.** Parâmetros
  no topo (`R_s=17.3 mΩ`, `L_s=7 µH`, `P=14`/`p=7`, `Kv=310/√3`, `Ts=10 µs`).
- **`Scripts/autotune_cost.py`** — otimiza **Γ** (invariante a `Ts`) e emite
  `RESULT ... Gamma_s1=… Gamma_s2=… Gamma_t=…`; rodado pela GUI (menu
  **Estimativas → Estimar Ganhos SMO**).

## Relação com o resto do repositório

```
Codigos/
├── Aplicacao_Pi/   # GUI + Config/SAME.yaml (protocolo) + scripts de estimação
├── SIBA/           # firmware aero+bateria (Zephyr) — MODELO/DRIVERS p/ a SAME
├── SAME/           # ESTA pasta — firmware do nó do motor (IMPLEMENTADO)
└── SPMM/           # bancada provisória — mesmos ensaios B/τc/J
```

## Decisões em aberto / a validar em bancada
- **Mapa de pinos** (`same_board.dts`): hoje ADC1=PA0/PA1/PA2 (V_a/V_b/V_c),
  ADC2=PA3/PA4/PA5 (i_a/i_b/i_c). Confirmar contra o esquemático real dos shunts
  de corrente e divisores de tensão de fase.
- **DMA do ADC2 no CH32V307**: o driver `adc_ch32v307.c` instancia ADC1 (DMA1
  ch1) e ADC2 (DMA1 ch2) igualmente. Validar em bancada que o ADC2 realmente
  gera requisição de DMA (no STM32F1 clássico ADC2 não tem DMA próprio) — se não
  gerar, ler ADC2 em modo simultâneo pelo registro combinado do ADC1.
- **`Ts_adc` real** (define `K = Γ/Ts` e o passo do observador): fixar conforme a
  taxa efetiva da malha de aquisição (~333 kSps/canal no projeto). Configurável
  por `TARGET_TS_ADC`.
- **Forma de BEMF** (`f`): senoidal (default) vs. trapezoidal — casar com o motor
  real (`DEF_BEMF_KIND` em `Observer_Manager.c`).
- **`torque_helice`**: hoje espelha `Te` (em regime, carga da hélice = torque
  eletromagnético). Subtrair atrito `(B·ω + τc)` quando `B/τc` (compartilhados
  com a SPMM) forem embarcados.
- **Calibração de tensão**: o `SAME.yaml` só tem `VOLTAGE_AB/BC` (2 alvos) mas o
  ADC1 lê 3 tensões de fase — hoje V_c reusa a calibração de `VOLTAGE_BC`.
  Considerar um `TARGET_VOLTAGE_CA` no YAML se os 3 divisores divergirem.
