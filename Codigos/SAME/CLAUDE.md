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
│   └── native_sim.conf/.overlay     # adc-emul p/ adc1+adc2, NSOS (UDP no host, loopback 127.0.0.100)
├── drivers/adc/adc_ch32v307.c       # driver custom (multi-instância ADC1/ADC2) — reciclado da SIBA
├── lib/OS/
│   ├── OSSI.{h,c}                   # same_wq (despacho por eventos: telemetria + CMD)
│   ├── NCI.{h,c}                    # UDP: TX telemetria (4445), RX CMD (4444)
│   ├── HAL.{h,c} + HAL_prm.h        # 2 ADCs → Clarke (3 termos) → Observer_Step + checagens (thread de aquisição)
│   ├── CMD_Core.{h,c}               # despacho de CMD (calibração / params / ganhos)
│   ├── CMD_Core_prm.h               # TABELA de sensores (currents/voltages) — como na SIBA
│   ├── Common_Types.h / CMD.h       # GERADOS do SAME.yaml
│   └── TCKEC.h                      # stub (sem thread de polling — só eventos)
├── lib/SYS/
│   ├── Log_Manager.{h,c}            # TELEMETRY_LOG + timer de TX (Ts_udp); .h GERADO
│   ├── Observer_Manager.{h,c}       # SMO + PLL + torque (núcleo — ver abaixo)
│   ├── Observer_Sin_LUT.h           # LUT512 do V5 — GERADA (gen_sin_lut.py), .rodata
│   ├── Security_Manager.{h,c}       # error_code agregado (diagnóstico, sem emergência)
│   ├── Error_Codes.h                # enum same_error_bit_t — GERADO (error_bits do yaml)
│   └── Security_prm.h               # limiares SM_* dos checadores — GERADO (security_limits)
└── tests/                           # não entram no build do firmware
    ├── observer_golden/             # A/B numérico V0 x V5 (host, shim de Zephyr)
    └── observer_bench/              # instret + footprint em rv32imafc (qemu)
```

**Os dois testes são complementares e nenhum sozinho fecha a questão:**
`observer_golden` compila o `Observer_Manager.c` **real** contra um shim mínimo
de Zephyr e roda no host — valida a matemática (é x86_64, não diz nada sobre
custo). `observer_bench` compila o mesmo arquivo para o ISA do alvo e conta
`minstret` no qemu — valida custo e footprint, não a integração. E os dois
juntos ainda não cobrem DMA, registrador do ADC nem ciclo real: isso só na
placa.

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

## Base reaproveitada da SIBA

Os drivers do **CH32V307VCT6 não estão disponíveis diretamente no Zephyr**,
então a SAME reaproveita a base que a SIBA já resolveu, em vez de refazer:

- **ADC**: usa o mesmo **driver de ADC customizado** da SIBA
  (`../SIBA/drivers/adc/adc_ch32v307.c`), não o driver genérico do Zephyr —
  `CONFIG_ADC=y` neste micro puxaria o driver do CH32V003, que conflita com
  o registrador de Ethernet do CH32V307VCT6 e quebraria a telemetria UDP.
- **`native_sim`**: mesmo esquema da SIBA (`boards/native_sim.conf`, HAL
  synthetic, `make native_sim`) — permite achar bugs de aplicação (SMO,
  parsing do `CMD`) no PC, sem flashar a placa.
- **Makefile**: mesmos atalhos (`build`/`flash`/`native_sim`/`debug`) da
  SIBA.
- **Arquitetura em camadas**: OS/SYS/HAL, `OSSI`/`NCI` para a rede,
  `generate.py` a partir do `SAME.yaml` — ver
  [`../SIBA/CLAUDE.md`](../SIBA/CLAUDE.md) para o modelo de referência
  (hoje ambas convergem para o mesmo esquema de fila única +
  poucas threads bloqueantes).

> Diferente da SIBA, a SAME **não tem shell de diagnóstico via UART** — todo
> ajuste (parâmetros do motor, ganhos do SMO, calibração de sensores) passa
> exclusivamente pelo `CMD` de rede (ver protocolo abaixo).

---

## Aquisição — cadeia ADC/DMA e dimensionamento de `Ts_adc`

> Referência: **CH32F/V20x_V30x_V31x Reference Manual, cap. 12** (ADC) —
> cópia local em `~/Documentos/Projetos/MCU_or_OS/CH32V307VTC6/ADC.pdf`.
> **Fonte secundária obrigatória:** o exemplo oficial da WCH em
> `~/Documentos/Projetos/MCU_or_OS/CH32V307VTC6/ch32v307-main/EVT/EXAM/ADC/DualADC_RegSimul/`
> e a SDK em `EVT/EXAM/SRC/Peripheral/src/ch32v30x_adc.c`. O RM diz o que os
> bits fazem; o exemplo mostra a **ordem** validada e as compensações de
> software que o RM não menciona (ver "Calibração" nas decisões em aberto — o
> offset residual da autocalibração só aparece lá).
>
> Esta seção fixa a taxa efetiva da malha, que é o que define
> `Ts_adc` → `K = Γ/Ts` e o passo do observador.

### Topologia de conversão

6 canais em **modo dual simultâneo de hardware** (`DUALMOD = 0110`, Regular
Simultaneous), 3 canais por ADC, com as sequências **pareadas por fase**:

```
ADC1_RSQR3: SQ1=V_a, SQ2=V_b, SQ3=V_c   (L[3:0]=2)
ADC2_RSQR3: SQ1=i_a, SQ2=i_b, SQ3=i_c   (L[3:0]=2)
```

Cada transferência de DMA de 32 bits carrega um par `(V_x, i_x)` da **mesma
fase, amostrado no mesmo instante** — não há defasagem V↔i entrando no Clarke.
Um frame completo = **3 transferências**.

- `ADC1_RDATAR[31:16]` = ADC2 (corrente), `[15:0]` = ADC1 (tensão).
  Separar é `w & 0x0FFF` e `(w >> 16) & 0x0FFF` — ~15–20 ciclos por frame,
  desprezível. **Não existem dois streams para demultiplexar.**
- `ALIGN = 0` (alinhamento à direita), DMA com *peripheral* e *memory size*
  em **32 bits**, canal 1 do DMA1.
- O manual exige **tempo de conversão igual nos dois grupos** em modo
  simultâneo: mesmo `SMPx` em ADC1 e ADC2, mesmo número de canais em cada.

### Registradores — mapa de bits que o driver toca

Bases: **ADC1 = `0x40012400`**, **ADC2 = `0x40012800`** (mesmos offsets).

| Offset | Registrador | Papel na SAME |
|---|---|---|
| `0x00` | `STATR` | flags `EOC`/`STRT` (não usar com DMA) |
| `0x04` | `CTLR1` | `DUALMOD`, `SCAN`, `PGA`, `BUFEN` |
| `0x08` | `CTLR2` | `ADON`, `CONT`, `DMA`, `ALIGN`, `CAL`, `RSTCAL`, `EXTSEL` |
| `0x0C` | `SAMPTR1` | `SMP10`–`SMP17` |
| `0x10` | `SAMPTR2` | `SMP0`–`SMP9` ← **canais usados estão aqui** |
| `0x2C` | `RSQR1` | `L[3:0]` = nº de conversões − 1 |
| `0x34` | `RSQR3` | `SQ1`–`SQ6` (a sequência de 3 canais cabe aqui) |
| `0x4C` | `RDATAR` | resultado; em dual, ADC2 nos 16 bits altos |

**`CTLR1` (`0x04`)**
```
[28:27] PGA[1:0]   00=x1  01=x4  10=x16  11=x64   (exige BUFEN=1)
[26]    BUFEN      1 = buffer de entrada ligado
[19:16] DUALMOD    0110 = Regular Simultaneous  ← só no ADC1
[8]     SCAN       1 = varre a sequência do RSQRx
[5]     EOCIE      0 (a sinalização vem da ISR do DMA, não do EOC)
```
> `DUALMOD` é **reservado no ADC2** — escrever só no `ADC1_CTLR1`. O manual
> exige que a alteração seja feita com o modo dual **desabilitado**.

**`CTLR2` (`0x08`)**
```
[22]    SWSTART    início por software
[20]    EXTTRIG    1 = habilita disparo por evento externo
[19:17] EXTSEL     111 = SWSTART | 100 = TRGO do TIM3 | 000..010 = CC1..CC3 do TIM1
[11]    ALIGN      0 = alinhamento à DIREITA  ← usar 0 (dado em [11:0])
[8]     DMA        1 — habilitar no ADC1 (o ADC2 não tem linha própria)
[3]     RSTCAL     1 = inicializa registrador de calibração (limpa por HW)
[2]     CAL        1 = dispara autocalibração (limpa por HW ao terminar)
[1]     CONT       1 = conversão contínua (free-running)
[0]     ADON       0→1 acorda o ADC; 1→1 inicia conversão
```
> **`ADON` é sensível a escrita conjunta:** "quando *apenas* `ADON` muda no
> registrador, uma conversão é iniciada; se outros bits mudarem junto, a
> conversão **não** inicia". Configure tudo primeiro, depois escreva `ADON`
> sozinho num acesso separado.

**`SAMPTR2` (`0x10`)** — 3 bits por canal, `SMPn` em `[3n+2 : 3n]`:
```
SMP0=[2:0]  SMP1=[5:3]  SMP2=[8:6]  SMP3=[11:9]  SMP4=[14:12]  SMP5=[17:15]
000=1,5  001=7,5  010=13,5  011=28,5  100=41,5  101=55,5  110=71,5  111=239,5
```
Com o mapa atual (PA0–PA2 → CH0–CH2 no ADC1; PA3–PA5 → CH3–CH5 no ADC2), os
seis campos usados são `SMP0`–`SMP5`, **todos no `SAMPTR2`**, e com o **mesmo
valor nos dois ADCs**.

**`RSQR1` (`0x2C`) / `RSQR3` (`0x34`)**
```
RSQR1[23:20] L[3:0]   = (nº de conversões − 1) → 3 canais ⇒ L = 2
RSQR3[4:0]   SQ1      RSQR3[9:5] SQ2      RSQR3[14:10] SQ3
```

**`RDATAR` (`0x4C`)** — no ADC1, em modo dual:
```
[31:16] ADC2DATA  → corrente (ADC2)
[15:0]  DATA      → tensão   (ADC1)
```

> Se um dia a amostragem for sincronizada ao PWM por timer: em modo dual com
> disparo externo, o manual exige `EXTTRIG=1` nos **dois** ADCs, com o trigger
> real no mestre e **software trigger no escravo**, para não disparar conversão
> espúria no ADC2.
>
> `ADCx_AUX` (`0x54`, tempos de 2,5–5,5 ciclos) **só existe em lotes
> específicos** (`CH32V30x_D8`/`D8C` sem zero no sexto dígito do lote). Não
> depender dele.

### Inicialização e calibração

São **duas calibrações distintas** e as duas são obrigatórias antes de liberar
o observador.

**1. Autocalibração de hardware (`RSTCAL` → `CAL`)** — corrige o descasamento do
banco de capacitores interno do SAR. É **por ADC** e deve rodar em **modo
independente**, com **`BUFEN = 0`**:

```
para cada ADC (1 e 2), com DUALMOD = 0000 e BUFEN = 0:
  1. ADON: 0 → 1            # acorda; aguardar tSTAB
  2. aguardar ≥ 2 ciclos de ADCCLK com ADON = 1   ← exigência do manual
  3. RSTCAL = 1  → aguardar o HW limpar
  4. CAL    = 1  → aguardar o HW limpar
```
> O manual é explícito: se `BUFEN`/`TKENABLE`/`TSVREFE` estiverem ligados o
> buffer não pode ser desligado, então **a calibração tem de vir antes** de
> habilitar buffer ou PGA. Ordem: `CAL` → `BUFEN`/`PGA` → `DUALMOD` → `DMA`.

**2. Calibração de offset dos sensores** — zera o ponto de operação do
front-end. Roda **depois** que o modo dual e o DMA já estão ativos, com o **ESC
parado**.

> ⚠️ **O ADC2 (correntes) usa sensor Hall e precisa de ~2 s de estabilização
> antes de amostrar o offset.** O Hall parte em `Vcc/2` e deriva durante o
> aquecimento inicial do ASIC; medir cedo trava um offset errado, que aparece
> depois como corrente DC fantasma nas três fases — e essa corrente entra
> direto no Clarke e vira BEMF espúria no SMO.

```
1. concluir a autocalibração de HW dos dois ADCs
2. configurar DUALMOD / sequências / DMA e começar a converter
3. AGUARDAR ≥ 2000 ms  (estabilização do Hall)   ← não pular
4. acumular N ≥ 4096 frames com o ESC parado
5. offset[ch] = média(N)  para os 6 canais
6. gravar via o mesmo caminho de DEFAULT_ACTION_CALIBRATE_OFFSET
   (TARGET_CURRENT_A/B/C e TARGET_VOLTAGE_A/B/C)
7. só então: status = pronto → liberar Observer_Step
```

**Enquanto o passo 7 não concluir:**
- `Observer_Step` **não roda** (ou roda com estado zerado e saída descartada) —
  senão o PLL trava em ângulo errado e o `reconfig` não resolve, porque o erro
  está na entrada, não nos ganhos.
- Os checadores do `Security_Manager` que dependem de offset ficam
  **mascarados** — em especial `i_a+i_b+i_c≈0` e o de faixa de sensor, que
  disparariam falso-positivo com o Hall ainda derivando. Candidato a um bit
  próprio em `error_bits`: *calibração em andamento* / *calibração falhou*.
- Se o desvio-padrão dos N frames do passo 4 exceder o limiar, **falhar a
  calibração** em vez de gravar: ruído alto ali significa ESC energizado,
  sensor mal alimentado ou terra aberto.

### Taxa por canal

`T_CONV = tempo de amostragem + 11·T_ADCCLK` (§12.2.2), logo com 3 canais/ADC:

```
f_s = f_ADCCLK / ((SMPx + 11) × 3)
```

| `SMPx` | Total | @14 MHz | `Ts` | @12 MHz | `Ts` |
|---|---|---|---|---|---|
| 1,5 | 12,5 | 373 kSps | 2,68 µs | 320 kSps | 3,13 µs |
| **7,5** | **18,5** | **252 kSps** | **3,96 µs** | **216 kSps** | **4,63 µs** |
| 13,5 | 24,5 | 190 kSps | 5,25 µs | 163 kSps | 6,12 µs |
| 28,5 | 39,5 | 118 kSps | 8,46 µs | 101 kSps | 9,87 µs |
| 41,5 | 52,5 | 89 kSps | 11,25 µs | 76 kSps | 13,1 µs |

**⚠️ Os ~333 kSps/canal antes assumidos no projeto não são atingíveis.** Só
`SMPx = 1,5` chega perto (373 kSps @14 MHz), e 1,5 ciclos é o pior caso de
impedância de fonte e **viola a recomendação de ≥7,5T quando `BUFEN=1`** (que é
obrigatório se o PGA for usado nos shunts). O valor realista é **252 kSps
(`SMPx = 7,5`)**, ou menos se o orçamento de CPU não fechar (ver abaixo).

**Verificar o `ADCCLK` antes de fixar `Ts_adc`:** o limite do ADC é 14 MHz, mas
com PCLK2 = 144 MHz os divisores `ADCPRE[1:0]` (/2 a /8) dão no mínimo 18 MHz —
**acima do limite**. É preciso usar o prescaler estendido do RCC ou baixar o
APB2 para cair em 12 MHz. **Todas as taxas escalam linearmente com o `ADCCLK`
real** — medir/confirmar antes de gravar `Ts_adc`.

### Orçamento de CPU — restrição dominante

Como o observador roda **colado ao evento do ADC, sem decimação**, o período do
frame é o orçamento inteiro de `Observer_Step` (núcleo a 144 MHz):

| `f_s` | Período | Ciclos disponíveis |
|---|---|---|
| 373 kSps | 2,68 µs | 386 |
| 252 kSps | 3,96 µs | 571 |
| 190 kSps | 5,25 µs | 756 |
| 118 kSps | 8,46 µs | 1218 |
| 101 kSps | 9,87 µs | 1421 |

**O gargalo é a trigonometria do PLL + torque.** O passo atual avalia `f(·)`
seis vezes por iteração — três em `g_α/g_β` (PLL) e três em `f_a/f_b` (torque).
Com `sinf` de libm (~100–300 ciclos cada) isso sozinho custa **600–1800
ciclos**, o que **estoura o orçamento em qualquer taxa acima de ~100 kSps**.

Dois caminhos para destravar 252 kSps, em ordem de custo:

1. **Reaproveitar `g_α/g_β` como `f_a/f_b`** quando a compensação de atraso
   estiver desligada (`θ_out == θ̂`): elimina 3 das 6 avaliações de graça.
2. **LUT de seno** (256–1024 pontos + interpolação linear, ~15–25 ciclos por
   consulta) no lugar de `sinf`. Leva as 6 avaliações a ~150 ciclos e coloca
   `Observer_Step` na casa dos ~350 ciclos.

Enquanto isso não for feito, **fixar `Ts_adc` na faixa de 100 kSps**
(`SMPx = 28,5`) — que ainda dá ~125 amostras por período elétrico na rotação
máxima (ver abaixo), folgado para o observador.

> **Medir, não estimar:** GPIO toggle na entrada/saída da ISR do ADC e
> comparar com o período do frame no osciloscópio antes de fechar `SMPx`.

### Coerência do buffer circular (ping-pong)

O DMA é **circular**, mas um buffer de exatamente 3 words gera corrida: o DMA
começa a sobrescrever a word 0 enquanto a ISR ainda lê o frame. O sintoma é
amostra misturada entre frames, **intermitente e pior em rotação alta**.

Buffer de **6 words (2 frames)** com as duas interrupções:

| Evento | DMA escrevendo | ISR processa |
|---|---|---|
| Half-transfer | words 3–5 | words 0–2 |
| Transfer-complete | words 0–2 | words 3–5 |

24 bytes de RAM e a coerência fica garantida por construção; a ISR recebe só o
ponteiro da metade estável.

> **Não há flag de overrun nos canais regulares desta geração de ADC.** Se a ISR
> estourar o orçamento e um transfer for perdido, o dado é sobrescrito **em
> silêncio** — o observador recebe uma amostra deslocada no tempo sem nenhum
> sinal de erro. Manter um **contador de sequência** na ISR e expor descartes no
> `error_code` (candidato a novo bit em `error_bits` do `SAME.yaml`).
>
> Não fazer polling de `EOC` junto com DMA: a leitura de `RDATAR` pelo DMA já
> limpa o flag.

### Filtro antialiasing

O sinal medido é a saída do ESC (PWM chaveado, **~30 kHz**), então o filtro fica
entre a fundamental elétrica e a portadora:

- **Fundamental máxima**: `f_e = RPM · p / 60`. Com o A-5025-310Kv (`p = 7`) em
  6S, `RPM ≈ 6900` → **`f_e ≈ 800 Hz`**. A janela até 30 kHz é larga (~1,5
  década), então dá para filtrar com folga sem distorcer a fundamental.
- **`f_c ≈ 5–8 kHz, 2ª ordem`** é o ponto de operação: preserva a fundamental
  com atraso de fase pequeno e atenua a portadora em ~35–45 dB na `f_s/2` das
  taxas da tabela.
- O capacitor do RC (dezenas de nF contra os ~8 pF internos do ADC) é quem
  entrega a carga no instante de amostragem — por isso `SMPx` curto **não**
  custa exatidão aqui, e a impedância dos divisores/shunts deixa de ser
  limitante.

Em 252 kSps a fundamental é amostrada ~314 vezes por período elétrico; em
101 kSps, ~125. **O requisito de sinal está folgado nas duas** — o que muda com
a taxa é a espessura efetiva da camada de contorno do SMO (chattering), não a
capacidade de reconstruir a BEMF.

---

## `Ts` fixo e auditoria CMD × compile-time

### `Ts` deixa de ser configurável

`Ts` passa a ser **uma constante única, igual ao período de frame do ADC**.
Menos é mais: sem target, sem GUI, sem conversão em lugar nenhum.

**Remover de cada camada:**

| onde | o que sai |
|---|---|
| `Aplicacao_Pi/Config/SAME.yaml` | entrada `TARGET_TS_ADC` do enum de targets |
| `Config/generate.py` | nada (regenerar após editar o YAML) |
| GUI | campo/slider de `Ts`, e a leitura dele no menu Estimativas |
| `autotune_cost.py` | `Ts_sim` vira constante importada, não argumento |
| `CMD_Core.c` | despacho de `TARGET_TS_ADC` |
| `Observer_Manager.c` | campo `ts` de `obs_params`; casos em `write_pending()` e `default_for_target()`; `DEF_TS` vira `#define OBS_TS` |

> ⚠️ **Remover uma entrada do meio do enum renumera todas as seguintes.** O
> `CMD` é `udp_struct` com o target como byte cru — GUI e firmware fora de sincronia
> passam a escrever no alvo errado **sem erro nenhum**. Duas saídas: reservar o
> slot (`TARGET_RESERVED_TS_ADC` mantendo o valor) ou regenerar os dois lados no
> mesmo commit e subir um byte de versão de protocolo. A primeira é mais barata.

**O acoplamento que sobra:** Γ carrega `Ts` embutido (`Γ = K·Ts`, feito no
Python). Com `Ts` fixo, `OBS_TS` e o `Ts_sim` do autotuning viram **a mesma
constante** — se divergirem, os Γ otimizados não descrevem o observador
embarcado. Definir num único lugar e importar dos dois lados.

### O que o CMD alcança — e o que a otimização pode congelar

Auditoria dos campos de `obs_params` contra os targets de `CMD_Core_prm.h`:

| campo | target | constantes derivadas | pode congelar? |
|---|---|---|---|
| `Rs` | `RESISTANCE` | `Phi_i` | **não** |
| `Ls` | `INDUCTANCE` | `Phi_i`, `Gamma_v` | **não** |
| `Kv` | `KV_RATING` | `ke_mec` → `Ke`, `Kt` | **não** |
| `pole_pairs` | `POLES_PAIRS` | `inv_p` | **não** |
| `gamma1/2/t` | `GAMMA1/2/T` | `Gamma_s1/s2`, `Phi_e` | **não** |
| `torque_lpf` | `TORQUE_LPF_FREQUENCY` | `alpha_tq` | **não** |
| `bemf_lpf` | `BEMF_LPF_FREQUENCY` | `alpha_bemf` | **não** |
| `ts` | ~~`TS_ADC`~~ | — | **sim** → `#define OBS_TS` |
| `bl` | *nenhum* | `inv_bl` | **não — ver abaixo** |
| `switch_func` | *nenhum* | branch de `switch_fn` | sim |
| `bemf_kind` | *nenhum* | forma de `f(·)` | **sim, com guarda** |
| `PLL_WN`/`ZETA` | *nenhum* | `Kp_pll`, `Ki_pll` | sim → `#define` |

Nenhuma das constantes que o CMD alcança está no caminho quente do V5 — todas
são recalculadas em `Observer_Recompute()`, que roda no `Observer_Apply_Pending`,
fora do laço. **A otimização não reduz nada que o `CMD_Core` mexe.** Três
ressalvas, porém:

**1. `bemf_kind` é a armadilha real.** A identidade que sustenta o V5 —

```
(1/√3)·(sin(θ−2π/3) − sin(θ−4π/3)) ≡ −cos(θ)
```

— vale **só para BEMF senoidal**. Para a trapezoidal ela é falsa. E "senoidal
vs. trapezoidal" ainda está nas decisões em aberto deste documento. Se alguém
virar `DEF_BEMF_KIND` para 0, o V5 passa a produzir ângulo e torque errados
**sem erro de compilação e sem erro em runtime**. Travar:

```c
_Static_assert(DEF_BEMF_KIND == 1,
    "V5: o par (sin, -cos) assume BEMF senoidal. "
    "Para trapezoidal, usar a LUT com 3 consultas de f_shape.");
```

Se a bancada indicar trapezoidal, o caminho é LUT com 3 consultas (ainda muito
mais barato que 3 `sinf`), não reverter para libm.

**2. Não congelar `BL`.** É tentador — não tem target. Mas o limite de
estabilidade é `BL > Γ₁/(1+Φᵢ)`, e **Γ₁ é settable por CMD**. Com `BL`
constante, um Γ₁ vindo de um autotuning novo pode cair dentro do chattering
(com Γ₁=31,62 a folga é de só 1,4×). Como `inv_bl` é calculado em
`Observer_Recompute`, mantê-lo em runtime **custa zero no caminho quente** —
o ganho do V5 vem de tirá-lo do laço, não de virar `#define`. Derivar:
`bl = fmaxf(DEF_BL, gamma1/(1+Phi_i)*MARGEM)`.

**3. `TARGET_POLES_PAIRS` não afeta mais o torque** — e isso está certo. Como
`Te = 1.5·Ke·p` com `Ke = ke_mec/p`, o `p` cancela; `Kt = 1.5·ke_mec` é
algebricamente idêntico ao que o V0 calcula. `pole_pairs` continua valendo para
`rpm_same` via `inv_p`. Documentado aqui para ninguém "consertar" isso depois.

> Efeito colateral do colapso: o V0 zerava `Te` quando `p ≤ 0` (pela guarda
> `pp = (inv_p>0)?1/inv_p:0`); com `Kt` pré-calculado isso não acontece mais.
> `Observer_Params_Physical_Valid()` já detecta `p ≤ 0`, mas é diagnóstico — se
> quiser o mesmo comportamento, a guarda tem de ir para `Observer_Recompute`.

---


> Medições feitas em **rv32imafc/ilp32f** (o alvo real), compiladas com
> `riscv64-unknown-elf-gcc -O2` e executadas em `qemu-system-riscv32
> -machine virt -icount shift=0`. `instret` = instruções retiradas por
> chamada. **Não é ciclo real** — ver "O que QEMU não mede" abaixo.

### Catálogo de mudanças

| # | mudança | instret | ΔTe | flash | RAM | risco |
|---|---|---|---|---|---|---|
| — | **V0 atual** | **1105** | — | base | base | — |
| 1 | `Kt = 1.5·ke_mec` pré-calculado | −15 | 0 | ≈0 | 0 | nenhum |
| 2 | `inv_bl` em `obs_const` | −12 | 0 | ≈0 | 0 | nenhum |
| 3 | identidade `g_β ≡ −cos θ` | −490 | 0 | −1,5 kB | 0 | só senoidal |
| 4 | `wrap_2pi` sem `floorf` | −8 | 0 | −100 B | 0 | precisa fallback |
| | **V1 = 1+2+3+4 (ainda libm)** | **590** | 0,54 %¹ | | | |
| 5 | LUT512 + interpolação | −238 | 0 | ver abaixo | ver abaixo | tabela |
| | **V2 = V1+5** | **352** | 0,54 %¹ | | | |
| 6 | sigmoide no lugar de `tanhf` | −188 | +0,34 % | −1,2 kB | 0 | reotimizar Γ |
| | **V3 = V2+6** | **164** | 0,88 % | | | |
| 7 | 2º `sincos` pós-update (corrige ¹) | +5 | −0,53 % | ≈0 | 0 | nenhum |
| | **V5 = V3+7 ← recomendado** | **169** | **0,34 %** | **−3056 B** | **0 B** | |

### ✅ Implementado e MEDIDO (`OBS_IMPL` em `Observer_Manager.c`)

O V5 está implementado; o V0 fica selecionável por `-DOBS_IMPL=0` **de
propósito**, porque é ele que dá sentido ao A/B. Números reproduzidos neste
repo com `riscv64-zephyr-elf-gcc -O2 -march=rv32imafc_zicsr_zifencei
-mabi=ilp32f` + `qemu-system-riscv32` (`tests/observer_bench`, alvo `make`),
já descontado o custo do laço/estímulo, e com a **PLL travada** (medir antes do
lock subestima o custo, porque o passo pula o `sincos` inteiro):

| variante | instret | Δ flash | Δ RAM | libm restante | ΔTe em regime vs V0 |
|---|---|---|---|---|---|
| V0 (libm + tanh) | **1027** | base | base | 9 símbolos, ~4,7 kB | — |
| **V5 + tanh** | **394** | **−3866 B** | **0 B** | só `tanhf` (218 B) | **0,0015 %** |
| **V5 + sigmoide** | **183** | **−5046 B** | **0 B** | **nenhum** | **2,03 %** |

Confere com o catálogo (1105/169 previstos × 1027/183 medidos). `obs_sin_lut`
sai como símbolo `r` de 2052 B (`.rodata`) e o `.bss` fica **idêntico** nas três
variantes — a LUT não foi para a RAM.

**Footprint no binário de verdade** (`make build`, com a pilha Ethernet junto —
que é o que decide se sobra espaço, não o observador):

| | ROM | RAM |
|---|---|---|
| V0 | 127244 B (62,76 %) | 59628 B (45,49 %) |
| **V5** | **123376 B (60,85 %)** | **59628 B (45,49 %)** |
| Δ | **−3868 B** | **0 B** |

Os critérios de aceite do plano de validação estão **todos cumpridos** no
`same_board`: flash cai ~3,9 kB, RAM não muda, e `sinf`, `cosf`, `tanhf`,
`floorf`, `__kernel_sinf`, `__kernel_cosf`, `__rem_pio2f`, `__kernel_rem_pio2f`,
`npio2_hw` e `PIo2` **sumiram todos** do ELF — nada mais no firmware chama a
trigonometria da libm, então a economia se realiza inteira.

**A decomposição muda a recomendação.** O A/B em `tests/observer_golden`
separa as duas fontes de desvio e elas não são comparáveis:

| o que muda | rpm (regime) | Te (regime) |
|---|---|---|
| LUT + identidade + 2 `sincos` + `Kt`/`inv_bl` (tanh nos dois lados) | 0,0001 % | **0,0015 %** |
| só trocar tanh → sigmoide (LUT nos dois lados) | 0,0093 % | **2,03 %** |

Ou seja: **todo o desvio vem da comutação, nenhum vem da LUT.** E os 2,03 %
medidos são 6× o «+0,34 %» estimado no catálogo — acima da tolerância de 1 %
do próprio plano de validação. Daí:

- **V5 + tanh é a escolha default**: 2,6× mais rápido que o V0, −3,8 kB de
  flash e **numericamente indistinguível** (0,0015 % no torque, dentro do ruído
  do `float`). Não exige reotimizar Γ, não exige nada da bancada.
- **V5 + sigmoide** só depois de rodar o `autotune_cost.py` com
  `SMO_SWITCH_FUNC = 2`. Enquanto os Γ vierem de um autotuning feito com tanh,
  a sigmoide é uma troca de 211 instret por 2 % de erro sistemático no torque —
  ruim, já que o torque é a grandeza que o TCC entrega.

Orçamento resultante a 144 MHz (CPI médio que cada variante aguenta):

| taxa | orçamento | CPI máx V0 | CPI máx **V5+tanh** | CPI máx V5+sigmoide |
|---|---|---|---|---|
| 101 kSps | 1425 | 1,39 | **3,6** | 7,8 |
| 190 kSps | 757 | **0,74** | **1,92** | 4,1 |
| 252 kSps | 571 | 0,56 | 1,45 | 3,1 |
| 373 kSps | 386 | 0,38 | 0,98 | 2,1 |

O V0 é impossível acima de ~100 kSps (piso de CPI é 1,0). **O V5+tanh destrava
190 kSps sem mexer em nada de numérico**; 252 kSps com tanh fica no limite
(CPI ≤ 1,45, apertado com wait state de flash) e é aí — só aí — que a sigmoide
passa a valer a reotimização dos Γ.

> Continua valendo: **instret não é ciclo**. Quem fecha o `SMPx` é o GPIO
> toggle na ISR do ADC, na placa.

### Como rodar os dois testes

```bash
# equivalência numérica (host, roda o Observer_Manager.c real via shim Zephyr)
cd tests/observer_golden && make        # A/B com os Γ do autotuning
make isolate                            # separa erro da LUT x erro da comutação
make factory                            # mesmo A/B com os DEF_GAMMA* de fábrica

# custo e footprint no ISA real (rv32imafc + qemu)
cd tests/observer_bench && make         # instret por chamada
make sizes                              # text/rodata/bss + libm remanescente
```

⚠️ **O A/B só é informativo com os Γ do autotuning.** Com os `DEF_GAMMA*` de
fábrica o observador não converge (`î` não segue `i`) e as duas variantes são
comparadas num regime degenerado: `make factory` dá 58 % de desvio no torque e
não quer dizer nada sobre a otimização. Ver o item «`DEF_GAMMA*` divergem do
autotuning» abaixo — é o mesmo defeito, agora com número.

¹ **A reutilização de `g_α/g_β` como `f_a/f_b` NÃO é exata.** No V0 o torque usa
o ângulo *depois* do update da PLL e a PLL usa o de *antes*; reusar um único
`sincos` introduz atraso de uma amostra (0,022 rad a 3000 rpm → 0,53 % no
torque). A mudança 7 põe um segundo `sincos` na posição correta e recupera isso
por **5 instruções**. Não economizar aqui.

### Flash e RAM — medido, não estimado

Esta é a parte que preocupa com a pilha Ethernet no mesmo binário. O resultado
é o oposto do esperado:

| build | text | bss | flash total | Δ flash | Δ RAM |
|---|---|---|---|---|---|
| V0 (libm) | 6580 | 1056 | 6596 | base | base |
| **V5, LUT `const` em flash** | **3524** | **1056** | **3540** | **−3056 B** | **0 B** |
| V5, LUT gerada em RAM no init | 5720 | 3104 | 5736 | −860 B | **+2048 B** |

**A LUT de 2 kB se paga sozinha.** Ao eliminar `sinf`/`cosf`/`tanhf`/`floorf`
some junto a bagagem delas — `__kernel_sinf`, `__kernel_cosf`,
`__kernel_rem_pio2f` (1394 B só esse), `__rem_pio2f` — cerca de 5 kB. Sobram
**3 kB de flash livres** e **zero** de RAM adicional.

> ⚠️ **A LUT tem de ser `static const`**, para o linker colocá-la em `.rodata`
> (flash). Se for gerada no init com `sinf`, ela vai para `.bss` (+2048 B de RAM)
> **e** obriga a manter `sinf` linkada — pior nos dois eixos.
>
> Contrapartida a medir: leitura da tabela vem da flash, com wait states a
> 144 MHz. Se o profiling na placa mostrar stall, copiar a `const` para RAM no
> boot (aí sim +2 kB de RAM), mantendo a versão em flash como fonte.

Medido com picolibc. **O Zephyr de vocês provavelmente linka newlib**, cuja
`sinf`/`tanhf` costumam ser maiores — o que só aumenta a economia do V5. Os
números de V2–V5 não dependem da libc.

### O que o QEMU não mede

`instret` conta instruções, não ciclos. Não modela latência de `FDIV.S`/
`FSQRT.S`, wait states da flash, nem misprediction. O que dá para afirmar é
**quanto CPI médio cada variante aguenta** antes de estourar o frame:

| taxa | orçamento | CPI máx V0 | CPI máx V5 |
|---|---|---|---|
| 101 kSps | 1425 | 1,29 | 8,4 |
| 190 kSps | 757 | **0,68** | 4,5 |
| 252 kSps | 571 | 0,52 | 3,4 |
| 373 kSps | 386 | 0,35 | 2,3 |

O V0 exigiria CPI < 0,68 para rodar a 190 kSps — impossível, o piso é 1,0.
**O código atual está limitado a ~100 kSps por construção.**

### FPU: `mstatus.FS` não vem ligado

No reset `mstatus.FS = Off` e **todo** `flw`/`fsw`/`fadd.s` trapeia como
instrução ilegal. O `CONFIG_FPU=y` do Zephyr cuida disso no boot, mas se
`Observer_Step` for chamado de um contexto sem FP context save configurado, o
sintoma é trap de instrução ilegal — não erro numérico. Sequência mínima:

```asm
li   t0, 0x6000      /* mstatus.FS = Dirty */
csrs mstatus, t0
fscsr zero
```

### Plano de validação — o que vai em cada alvo

As duas metades são complementares e **nenhuma sozinha fecha a questão**:

**1. `make build` (hardware) — orçamento de flash/RAM**

```bash
make build
west build -t rom_report   # quebra por símbolo
west build -t ram_report
```

Critérios de aceite:
- Comparar `FLASH`/`RAM` do relatório **antes e depois** do V5. O esperado é
  flash cair ~3 kB e RAM ficar igual. Se a RAM subir, a LUT não ficou `const`.
- Conferir no `rom_report` que `__kernel_rem_pio2f`, `__rem_pio2f`, `sinf`,
  `cosf` e `tanhf` **sumiram**. Se ainda aparecerem, algo mais no firmware
  ainda chama libm e a economia não se realiza.
- Somar o buffer ping-pong do ADC (24 B) e conferir que a folga de RAM continua
  cobrindo os pools de `net_buf` da pilha Ethernet — é o consumidor dominante,
  e é ele que decide se sobra espaço, não o observador.

**2. `make native_sim` — equivalência numérica**

`native_sim` roda em x86_64: **não** valida ciclos, footprint, DMA nem
registradores do ADC. O que ele valida bem é a matemática.

Teste de regressão a implementar: alimentar V0 e V5 com a **mesma** sequência
fixa de `(v_αβ, i_αβ)` — gerada por `../Aplicacao_Pi/Scripts/test_coeff_auto.py`
e gravada como vetor golden — e comparar as saídas:

| grandeza | tolerância |
|---|---|
| `rpm_same` | < 0,01 % |
| `torque_eletrico` | < 1,0 % |
| `Observer_State_Is_Finite()` | true em todos os passos |

Rodar em pelo menos 3 patamares de rotação (os mesmos do `autotune_cost.py`:
~1000, ~3000, ~1000 rpm) para pegar transitório, não só regime.

**3. Placa, com osciloscópio — o número que decide `Ts_adc`**

GPIO toggle na entrada/saída da ISR do ADC. É a única medida de ciclo real:
compara o tempo de `Observer_Step` contra o período do frame e fecha o `SMPx`.
Fazer isso **antes** de gravar `TARGET_TS_ADC`, porque `K = Γ/Ts` depende dele.

---


Mesmo protocolo compacto da SIBA (`CMD` 8 bytes + `TELEMETRY_LOG` packed LE):

| | valor |
|---|---|
| `transport` | `udp_struct` |
| IP padrão | `169.254.97.100` (link-local) |
| PC escuta telemetria (`pc_rx`) | `4445` |
| Placa escuta comandos (`board_rx`) | `4444` |
| `CMD` (PC→placa) | 8 bytes `<BBBfb>` = `func, instr, target, value, action` |
| `TELEMETRY_LOG` (placa→PC) | struct packed LE, ~10 Hz |

> **native_sim**: o IP link-local vira o loopback **`127.0.0.100`** (último
> octeto do IP real), distinto do da SIBA (`127.0.0.200`), para as duas rodarem
> juntas no host sem colidir. As portas 4444/4445 não mudam. Ver
> `boards/native_sim.conf` e `Aplicacao_Pi/.../manager.py::_native_loopback`.

### TELEMETRY_LOG (o que a SAME envia)
`timestamp`; **medidas ADC/shunts**: `V_a,V_b,V_c`, `i_a,i_b,i_c`; **referencial
estacionário αβ** (entrada do SMO): `V_alpha,V_beta`, `i_alpha,i_beta`;
**estimados pelo SMO**: `rpm_same`, `torque_eletrico` (Te), `torque_helice`;
`status`; **`error_code`** (uint32, bitfield de diagnóstico — ver abaixo).

> **Clarke:** `HAL.c` usa a forma **completa de 3 termos** (amplitude-invariante)
> — `α=⅔(a−½b−½c)`, `β=(b−c)/√3` — pra V e i, aproveitando as três fases medidas
> (rejeita modo comum/sequência-zero em vez de assumir `a+b+c=0`).

### Diagnóstico / erros (`error_code` — sistema espelhado da SIBA)
O **`Security_Manager`** agrega 1 bit por condição (`same_error_bit_t`) e expõe o
`uint32_t error_code` na telemetria. Como a SAME **só observa** (não aciona o
ESC), é **puro diagnóstico — sem parada de emergência**. Checagens (rodam no
ritmo da telemetria, na thread do HAL): desequilíbrio de corrente/tensão
(`i_a+i_b+i_c≈0`), sobrecorrente, faixa de sensor, saúde do observador (BEMF
baixa/PLL sem lock/divergência NaN/RPM implausível), params inválidos e sinais
de Γ. **Fonte única no YAML:** a seção `error_bits` do `SAME.yaml` **gera** o enum
C (`lib/SYS/Error_Codes.h`) **e** alimenta a decodificação pt-BR da GUI
(`Communication_Data/errors.py`, por placa); os limiares `SM_*` vêm de
`security_limits` → `lib/SYS/Security_prm.h`. Edite o YAML e regenere.

### CMD — targets (o que a SAME recebe)
- **Correntes/tensões** (calibração dos sensores, uma por fase): `TARGET_CURRENT_A/B/C`,
  `TARGET_VOLTAGE_A/B/C`.
- **Parâmetros do motor**: `TARGET_POLES_PAIRS`, `TARGET_RESISTANCE`,
  `TARGET_INDUCTANCE`, `TARGET_KV_RATING`.
- **Ganhos do SMO (na forma Γ — ver abaixo)**: `TARGET_GAMMA1`, `TARGET_GAMMA2`,
  `TARGET_GAMMAT`, e as frequências de filtro `TARGET_BEMF_LPF_FREQUENCY`,
  `TARGET_TORQUE_LPF_FREQUENCY`.
- `TARGET_DEFAULT` (start/stop/reset). Ações: calibração `..._LINEAR/OFFSET/
  RESET/MAX_VALID/MIN_VALID` e, para ganhos/params, `DEFAULT_ACTION_SET_VALUE` /
  `..._VALUE_RESET_DEFAULT`.

> 🔑 **Os ganhos configuráveis são Γ (Gamma), NÃO Ks1/Ks2/Kt.** A conversão
> `Γ = K·Ts` é feita **uma única vez, no Python**, e o firmware usa Γ **direto**
> na atualização discreta (`î += Γ_s1·s`, `Φ_e = 1 − Γ_t`) — o micro nunca
> divide por `Ts`. Decisão de projeto: tira a conversão do caminho embarcado e
> com ela o erro de arredondamento em `float`. Por isso o target é `Γ1, Γ2, Γt`
> e o autotuning/menu na `../Aplicacao_Pi/` trabalham em Γ.
>
> **Acoplamento que sobra:** como Γ carrega o `Ts` embutido, o
> `autotune_cost.py` tem de rodar com **`Ts_sim` igual ao `OBS_TS` do firmware**.
> Com `Ts` fixo isso é uma constante compartilhada, não um parâmetro — ver a
> seção de `Ts` fixo.

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

> ⏱️ **Custo por iteração:** as seis avaliações de `f(·)` (três no passo 4, três
> no passo 5) dominam o tempo de `Observer_Step`. Ver
> "Orçamento de CPU" na seção de aquisição — é o que limita `Ts_adc`.

---

## Implementações de referência (em `../Aplicacao_Pi/`)

- **`Scripts/test_coeff_auto.py`** — simulação completa planta+SMO+PLL (as
  equações acima). **Modelo do que o firmware da SAME deve embarcar.** Parâmetros
  no topo (`R_s=17.3 mΩ`, `L_s=7 µH`, `P=14`/`p=7`, `Kv=310/√3`).

  **Duas bases de tempo (a planta é contínua, o observador não é):**

  | | símbolo | valor |
  |---|---|---|
  | passo da planta | `Ts_plant` | `TS_ADC / N_SUB` = 0,833 µs |
  | frame do ADC = passo do SMO | `TS_ADC` (`Ts`) | 10 µs — **igual ao `OBS_TS`** |
  | sub-passos por frame | `N_SUB` | 12 (múltiplo de 3: são 3 conversões) |

  Com `ADC_MODEL = 1` o caminho de medição reproduz o hardware: filtro
  antialiasing **por fase** (+ polo do Hall na corrente) → condicionamento para
  tensão no pino → **amostragem sequencial dentro do frame** (V_x e i_x da mesma
  fase caem juntos, mas A, B e C são convertidas uma após a outra) →
  quantização de 12 bits com saturação → Clarke de 3 termos, a **mesma** do
  `HAL.c`. `ADC_MODEL = 0` volta ao comportamento antigo (sinais ideais).

  Efeitos que só aparecem com isso ligado: o `i_a+i_b+i_c` medido **não é
  zero** (pico de ~8 A nas comutações, 2,2 A rms — é o skew de 6,67 µs entre a
  1ª e a 3ª conversão do frame), e o front-end atrasa a corrente o bastante
  para importar frente ao `di/dt` de 2,1 MA/s do inversor.

  As figuras `ADC_amostragem.png`, `ADC_impacto_estimativa.png` e
  `ADC_ideal_vs_amostrado.png` decompõem isso: planta → após o front-end →
  retido pelo ADC.

  > ⚠️ `TS_ADC` **tem** de ser o `OBS_TS` do firmware: Γ = K·Ts carrega o passo
  > embutido. Mudou um, roda `autotune_cost.py --Ts <novo>` — não reescale à mão.
- **`Scripts/autotune_cost.py`** — otimiza **Γ** (invariante a `Ts`) e emite
  `RESULT ... Gamma_s1=… Gamma_s2=… Gamma_t=…`; rodado pela GUI (menu
  **Estimativas → Estimar Ganhos SMO**).

  **A função de custo é configurável pela GUI** (grupo "Função de custo") e por
  CLI: `--time-metric {mae,mse,rmse,mape,huber}`, `--w-tq`, `--w-rpm`. Os três
  ficam gravados no bloco `config` do `smo_gains.json` — sem isso não dá para
  saber depois sob que critério aqueles Γ foram escolhidos.

  > ⚠️ **`huber` re-pondera os termos por conta própria.** Ele é quadrático
  > abaixo de `HUBER_DELTA` e linear acima, então comprime o termo de resíduo
  > **menor**. Medido nesta bancada (resíduos normalizados: torque r ≈ 0,32–0,56,
  > rpm r ≈ 0,78–1,06):
  >
  > | métrica | termo torque (W=1,0) | termo rpm (W=0,5) | razão rpm/torque |
  > |---|---|---|---|
  > | mae | 0,548 | 0,528 | 0,96 |
  > | rmse | 0,635 | 0,645 | 1,02 |
  > | **huber** | **0,199** | **0,323** | **1,62** |
  >
  > Com Huber o rpm pesa **1,6× o torque apesar de `W_TQ` ser o dobro de
  > `W_RPM`** — e justo contra o torque, que é o que o TCC entrega. Por isso o
  > default passou a ser **`rmse`**, onde os pesos declarados valem.

> O `Ts=10 µs` do script coincide com `SMPx = 28,5 @12 MHz` (9,87 µs) — é o
> ponto de operação conservador e o mais fácil de correlacionar simulação ×
> bancada enquanto a LUT de seno não estiver implementada.

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
- ~~**DMA do ADC2 no CH32V307**...~~ resolvido: confirmado no manual de
  referência (CH32FV2x_V3xRM-EN, Table 11-2 "Peripheral mapping table of
  each DMA1 channel", cap. 11.2.3) que **ADC2 não tem linha de DMA própria**
  — não aparece em nenhum dos 7 canais do DMA1 (só ADC1 aparece, canal 1).
  Um ADC2 independente com `dmas` próprio nunca completaria uma
  transferência (timeout sempre, na placa real). `adc_ch32v307.c` agora
  roda ADC1+ADC2 em **modo dual simultâneo de hardware** (DUALMOD=Regular
  Simultaneous, ADC1_CTLR1) — ADC1 dispara os dois ADCs no mesmo instante
  (V_x pareado com i_x da mesma fase) e o resultado sai por uma única
  transferência de DMA de 32 bits (ADC2 nos 16 bits altos, ADC1 nos baixos)
  no canal do ADC1. `HAL.c` não precisou mudar — a API `adc_read()` de cada
  instância continua igual, só que agora ADC2 é sinalizado pela ISR do
  ADC1. **Ainda não validado em bancada** (só compila; não dá pra testar
  DMA/registrador combinado no `native_sim`, que emula o ADC por outro
  caminho).
- **`ADCCLK` efetivo**: com PCLK2 = 144 MHz os divisores `ADCPRE[1:0]` (/2–/8)
  não chegam ao limite de 14 MHz do ADC (mínimo 18 MHz). Definir se cai em
  12 MHz pelo prescaler estendido do RCC ou baixando o APB2 — **todas as taxas
  da tabela de `Ts_adc` escalam com esse valor.**
- 🔴 **`Ts_adc` real do firmware está 6,3× fora do `OBS_TS` do observador.**
  Auditado no código (não é estimativa de projeto — é o que `make build` gera
  hoje). São três contribuições independentes, todas em `drivers/adc/adc_ch32v307.c`
  e `lib/OS/HAL.c`:

  1. **`adc_set_sample_times()` fixa `SMPx = 239,5` para todo canal** (grava
     `0x7` em cada campo). A tabela de `Ts_adc` acima toda assume `SMPx`
     escolhível; 239,5 nem aparece nela. `T_CONV = 250,5` ADCCLK × 3 canais =
     **751,5 ADCCLK por frame**.
  2. **`adc_rcc_enable()` usa `ADCPRE = /6`** → com PCLK2 = 144 MHz dá
     **ADCCLK = 24 MHz**, 1,7× acima do limite de 14 MHz do periférico (o
     próprio comentário do driver admite isso). Nesse clock, o frame sai em
     **31,3 µs → 31,9 kSps/canal**.
  3. **`hal_read_raw()` gasta DOIS transfer-complete por passo do observador.**
     `adc_ch32v307_read()` faz `k_sem_reset()` antes do `k_sem_take()`, então o
     `adc_read(adc2)` **descarta o semáforo que a ISR do frame já tinha dado** e
     espera o frame seguinte. O pareamento V↔i sobrevive (a ISR escreve os dois
     `user_buf` a cada TC), mas a taxa do observador cai pela metade:
     **período efetivo ≈ 62,6 µs.**

  Contra `DEF_TS = 10 µs`, isso é **6,3× de erro** — e ele entra duas vezes:
  `Φ_i = 1 − Rs·Ts/Ls` e `Γ_v = Ts/Ls` são calculados com o `ts` da constante
  (0,975 e 1,43) enquanto o hardware amostra a 62,6 µs (deveria ser 0,845 e
  8,94), **e** os Γ do autotuning carregam `Ts = 10 µs` embutido (`Γ = K·Ts`),
  então o `K` efetivo sai 6,3× menor que o otimizado. Pior: como o laço é
  «espera 2 TCs», o passo **não é fixo** — jitta com o tempo de execução da
  thread, e Euler-forward com passo variável não é o modelo que os Γ descrevem.

  **Estado: (1) e (3) corrigidos, (2) é decisão de sistema — ver abaixo.**

  - (3) corrigido: em modo dual o escravo não espera mais. `adc_ch32v307_read()`
    devolve imediatamente para o ADC2, porque quando `adc_read(adc1)` retorna o
    buffer do ADC2 **já foi preenchido pela mesma ISR**. Vale 2× na taxa.
  - (1) corrigido: `SMPx` virou `CH32_ADC_SMP`, default `011` (28,5 ciclos), e
    `adc_set_sample_times()` agora **limpa o campo antes de gravar** — antes só
    fazia `|=` com `0x7`, o que travava o campo em 239,5 para sempre.
  - (2) melhorado (`/6` → `/8`), mas **não resolvido — e resolver custa caro**.
    O prescaler estendido do RCC **não existe** nesta família: conferido na SDK
    da WCH (`ch32v30x_rcc.c`: `CFGR0_ADCPRE_Reset_Mask = 0xFFFF3FFF` limpa só
    `[15:14]`; `ch32v30x_rcc.h` define só `RCC_PCLK2_Div2/4/6/8`). Com
    PCLK2 = 144 MHz, o divisor máximo `/8` dá **18 MHz — 1,29× acima dos
    14 MHz** do manual. `/8` é também o que o exemplo oficial da WCH usa.

  **Ponto de operação atual:**

  | | valor |
  |---|---|
  | PCLK2 | 144 MHz (**PPRE2 não é tocado**) |
  | ADCCLK | 18 MHz ⚠️ (limite 14 MHz) |
  | `SMPx` | 28,5 ciclos (`CH32_ADC_SMP = 011`) |
  | frame (3 canais) | **6,583 µs** |
  | taxa | 151,9 kSps/canal |

  > 🚫 **Não baixe o PPRE2 sem decidir isso explicitamente.** Pôr o ADC em
  > spec exige PCLK2 = 72 MHz, e o PCLK2 é **compartilhado**: como
  > `clock_control_wch_rcc.c` devolve sempre o clock de AHB em
  > `clock_control_get_rate()` ignorando PPRE2, o driver de USART calcularia
  > `divn = 144 MHz / current-speed` com o PCLK2 real em 72 MHz e o console
  > iria para metade do baud, exigindo compensar `current-speed` no DTS.
  > **Isso quebra um console que funciona** para ganhar 18 → 12 MHz. A troca
  > existe e está implementada atrás de `CH32_ADC_LOWER_APB2`, mas fica
  > DESLIGADA até alguém decidir que vale.

  **Consequência que sobra:** o frame de 6,583 µs não é o `OBS_TS` de 10 µs.
  Nenhum `SMPx` fecha 10 µs a 18 MHz (41,5 → 8,75 µs; 55,5 → 11,08 µs), então
  o caminho é o inverso: **fixar `OBS_TS_NS` no período real do frame e rodar
  o `autotune_cost.py` com `Ts_sim` igual a ele**, já que Γ = K·Ts. Enquanto
  isso não for feito, o driver dispara um `LOG_WRN` no boot dizendo exatamente
  qual é o `Ts` real e qual o observador assume.

  O driver avisa no boot nos dois casos: um `LOG_WRN` se `ADCCLK > 14 MHz` e
  outro se o período do frame divergir de `OBS_TS_NS` (exportado por
  `Observer_Manager.h`) em mais de 2 %. O `Ts` do hardware deixou de ser
  invisível — que era o problema de origem.
- ~~**LUT de seno em `Observer_Manager.c`**~~ implementado: `OBS_IMPL=5` em
  `Observer_Manager.c` + `lib/SYS/Observer_Sin_LUT.h` (gerado por
  `tests/observer_golden/gen_sin_lut.py`). Medido em **1027 → 394 instret**
  (V5+tanh, desvio de 0,0015 % no torque) ou **183** (V5+sigmoide, 2,03 %),
  **−3,8/−5,0 kB de flash e 0 B de RAM**. Ver a seção de otimização — a
  recomendação passou a ser **V5+tanh**, porque a sigmoide sozinha responde por
  100 % do desvio e exige reotimizar Γ. Falta só o `rom_report`/`ram_report` na
  placa (o footprint aqui foi medido no bench bare-metal, não no binário com a
  pilha Ethernet).
- 🔴 **`DEF_GAMMA*` divergem do autotuning** — confirmado em bancada de teste
  (`tests/observer_golden`, planta `v = R_s·i + L_s·di/dt + e`, torque real
  0,612 N·m): com os defaults do `.c` (Γ₁=5, Γ₂=−1, Γₜ=0,05) o observador
  estima **0,96 N·m a 1000 rpm e 1,87 N·m a 3000 rpm** — 57 % e 205 % de erro,
  e dependente da rotação, quando o torque real é constante nos dois patamares.
  O `î` simplesmente não segue o `i`. Com os Γ do `smo_gains.json`
  (Γ₁=31,6228, Γ₂=−10, Γₜ=0,066771) cai para 0,660 / 0,757 N·m (8 % e 24 %) e a
  rotação fica exata (999,99 / 2999,99 rpm). **Alinhar os `DEF_GAMMA*` com o
  `smo_gains.json` é a maior melhoria isolada de exatidão disponível hoje** — e
  é uma linha. O erro residual de 8–24 % é outro assunto (Γ otimizado com
  `w_tq`/`w_rpm`, não para exatidão de torque em regime).
- **Limites a checar em `Observer_Set_Param`**: `Ts < 2·Ls/Rs`;
  `0 < Γₜ < 1`; `BL > Γ₁/(1+Φᵢ)`; `|Γ₂|/Γₜ ≥ BEMF_max`. O terceiro é o mais
  frágil — `DEF_BL` é constante fixa enquanto Γ₁ vem do autotuning, e com
  Γ₁=31,62 a folga até o início do chattering é de só 1,4×. **BL deveria ser
  derivado de Γ₁.**
- ~~**Buffer de DMA em ping-pong**~~ implementado: `g_dual_dma_buf` passou a
  2 frames (`CNTR = 2·ch_count`) com `DMA_CFGR_HTIE` **e** `DMA_CFGR_TCIE`. A
  ISR processa a metade estável (half-transfer → 1ª metade;
  transfer-complete → 2ª), então DMA e ISR nunca tocam o mesmo frame. Como
  não há flag de overrun nos canais regulares, as duas flags **simultâneas**
  são a única evidência de frame perdido e viraram o contador
  `dropped_frames`, exposto por `adc_ch32v307_get_stats()`. **Falta ligar esse
  contador a um bit de `error_bits`** no `SAME.yaml` e regenerar — é a parte
  que cruza para a `Aplicacao_Pi`.
- 🔴 ~~**Calibração de HW**~~ corrigida — e o defeito era **maior do que a
  ordem**. Conferido contra o cap. 12 do RM (`ADC.pdf`) e contra o exemplo
  oficial da WCH (`EVT/EXAM/ADC/DualADC_RegSimul`):

  **A autocalibração de hardware sozinha NÃO zera o erro.** `RSTCAL`/`CAL`
  deixa um resíduo, e o próprio fabricante o compensa em software: a rotina
  `Get_CalibrationValue()` da SDK roda a autocalibração **dez vezes**, lê o
  código que o HW deixa em `RDATAR` (§12.2.2 item 4), ordena, **descarta os 2
  menores e os 2 maiores**, tira a média dos 6 do meio e devolve
  `2048 − mediana` — um offset com sinal que tem de ser somado em **todas** as
  amostras, com os extremos 0 e 4095 passando sem correção (são saturações).

  O driver não fazia nada disso — usava uma única passada de `RSTCAL`/`CAL` e
  jogava o código fora. **Num nó de medição elétrica esse resíduo não é
  cosmético:** ele é comum às três fases, atravessa a Clarke como componente
  de sequência zero e vira BEMF fantasma no SMO — o mesmo mecanismo que o
  documento já descrevia para o offset do Hall. Agora está implementado
  (`adc_get_calibration_value()` + `adc_apply_cal()`), com o offset de cada
  ADC aplicado na ISR e registrado no log de boot.

  A **ordem** também foi refeita, seguindo o exemplo oficial (não a intuição):
  `DUALMOD`/`SCAN` + `BUFEN`/`PGA` zerados → `CTLR2` sem `ADON` → `ADON`
  sozinho + tSTAB → **calibração** → **só então o canal de DMA sobe** → `SWSTART`.
  O canal de DMA vem por último porque a calibração lê `RDATAR`, e um DMA já
  ativo consumiria esse código antes. `EXTTRIG` + `EXTSEL = 111` passaram a ser
  programados **nos dois** ADCs: o §12.2.7 exige trigger habilitado no mestre
  e no escravo, com o escravo em software trigger, "to prevent spurious
  triggers to start unwanted slave conversion". O caminho independente (SIBA)
  recebeu a mesma sequência.

  Conferidos contra o RM e batendo com o driver: `PGA[28:27]`, `BUFEN[26]`,
  `DUALMOD[19:16]=0110`, `SCAN[8]`, `SWSTART[22]`, `EXTTRIG[20]`,
  `EXTSEL[19:17]`, `ALIGN[11]`, `DMA[8]`, `RSTCAL[3]`, `CAL[2]`, `CONT[1]`,
  `ADON[0]`, `L[23:20] = n−1`, `SMPx` de 3 bits em `SAMPTR2`, e
  `CONT=1 + SCAN=1` = "continuous scan mode". Também confirmado que o bit de
  DMA vai **só no ADC1** (o exemplo oficial faz `ADC_DMACmd(ADC1, ENABLE)` e
  nada no ADC2) e que `RDATAR[31:16]` = ADC2, `[15:0]` = ADC1.

  > ⚠️ Se o PGA for usado nos shunts: `BUFEN=1` pede tempo de amostragem
  > **≥ 7,5T** (§12.3.2). O default atual (28,5T) já satisfaz.
- **O `native_sim` não exercita NADA da cadeia de ADC.** O overlay declara
  `adc1`/`adc2` como `zephyr,adc-emul` e o `CMakeLists.txt` diz que «HAL.c usa
  só a API genérica», mas `HAL.c` sob `CONFIG_ARCH_POSIX` desvia para o gerador
  senoidal sintético e **nunca chama `adc_read()`** — o `adc_stimulus` citado
  no comentário do overlay não existe. Consequência prática: nenhum dos
  defeitos de aquisição acima é detectável no `native_sim` como ele está.
  Fazer o caminho POSIX passar pelo `adc-emul` (via
  `adc_emul_const_value_set()`) faria o `native_sim` pegar pelo menos o
  sequenciamento do `adc_read()` — que é onde está o bug de 2×.
- ~~**Calibração na inicialização**~~ implementada (`HAL.c`, seção "Calibração
  de offset do sensor Hall"): espera `SM_CAL_SETTLE_MS` (2 s), acumula
  `SM_CAL_FRAMES` (4096) com o ESC parado, valida pelo desvio-padrão
  (`SM_CAL_STD_MAX_MV`) e grava o offset por fase. `ERROR_CAL_IN_PROGRESS` e
  `ERROR_CAL_FAILED` entraram no `error_bits`; o `Observer_Step` fica
  **bloqueado** até concluir.

  > A média/variância usa **Welford em `float`**, não somatório+soma de
  > quadrados. Com 4096 amostras de ~2238 mV, Σx² chega a 2,05e10, onde o ulp
  > do `float` é 2048: a variância sairia de `E[x²]−E[x]²` entre dois números
  > de ~5e6 cujo resultado esperado é da ordem de dezenas — cancelamento
  > catastrófico. Fazer em `double` resolveria e custaria **+9 kB de flash**
  > em soft-float (medido), já que o CH32V307 só tem FPU de precisão simples.

  Validado no `native_sim`, que agora sintetiza **tensão no pino em mV** (não
  V/A prontos) e parte com o motor PARADO: a calibração mede o repouso, remove
  a corrente fantasma e só então libera o observador — com o motor girando
  desde o boot ela corretamente **reprova** (desvio ~95 mV = os 8 A de pico na
  cadeia).

## Condicionamento dos sensores — constantes de projeto

Fonte: monografia, `Textuais/Metodologia/08_SAME.tex` e o ensaio do Apêndice A.
Os valores vivem no `sensor_defaults` do `SAME.yaml` (geram `CMD_Core_prm.h`);
`HAL_prm.h` repete só o que o gerador sintético do `native_sim` precisa.

| | corrente | tensão de fase |
|---|---|---|
| sensor | SS49E, N=1 (condutor pelo furo do toróide) | divisor resistivo |
| divisor | R1=2k0 / R2=18k → k = 0,900 | R1=130k / R2=7k5 → k = 0,0545 |
| ganho da cadeia | 18,750 × 0,900 = **16,875 mV/A** | **54,55 mV/V** |
| fundo de escala | ±60 A (satura em +62,9 A) | 60,5 V |
| resolução | 47,7 mA/LSB | 14,8 mV/LSB |
| repouso | 2,4866 V × 0,900 = **2,238 V** | 0 V |
| polo do front-end | 40,2 kHz (+ 56 kHz do próprio Hall) | 40,1 kHz |

O SS49E é ratiométrico e repousa em Vcc/2 — por isso o divisor: ±60 A levaria a
saída a 3,625 V, acima dos 3,3 V do conversor.

> 🔴 **O repouso não é constante e é por isso que a calibração de boot existe.**
> Nos três ensaios do Apêndice A o valor a 0 A caiu em 2,48570 / 2,48640 /
> 2,49150 V — **5,8 mV de espalhamento**, que na cadeia valem **0,31 A de
> corrente fantasma**. E o erro é comum às três fases, então **não se cancela
> no Clarke de 3 termos**: entra como sequência zero e vira BEMF espúria no
> SMO. Gravar offset de catálogo não resolve.
- **Filtro antialiasing**: `f_c ≈ 5–8 kHz`, 2ª ordem, entre a fundamental
  (`f_e ≈ 800 Hz` em 6S) e a portadora do ESC (~30 kHz). Confirmar a topologia
  no esquemático do front-end.
- **Forma de BEMF** (`f`): senoidal (default) vs. trapezoidal — casar com o motor
  real (`DEF_BEMF_KIND` em `Observer_Manager.c`).
- **`torque_helice`**: hoje espelha `Te` (em regime, carga da hélice = torque
  eletromagnético). Subtrair atrito `(B·ω + τc)` quando `B/τc` (compartilhados
  com a SPMM) forem embarcados.
- ~~**Calibração de tensão**: o `SAME.yaml` só tem `VOLTAGE_AB/BC`...~~ resolvido:
  `SAME.yaml` agora tem `TARGET_VOLTAGE_A/B/C` (3 alvos, um por fase, calibração
  independente) — mesmo esquema das 3 correntes.
