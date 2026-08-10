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
└── lib/SYS/
    ├── Log_Manager.{h,c}            # TELEMETRY_LOG + timer de TX (Ts_udp); .h GERADO
    ├── Observer_Manager.{h,c}       # SMO + PLL + torque (núcleo — ver abaixo)
    ├── Security_Manager.{h,c}       # error_code agregado (diagnóstico, sem emergência)
    ├── Error_Codes.h                # enum same_error_bit_t — GERADO (error_bits do yaml)
    └── Security_prm.h               # limiares SM_* dos checadores — GERADO (security_limits)
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

> Referência: **CH32F/V20x_V30x_V31x Reference Manual, cap. 12** (ADC).
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

> ⏱️ **Custo por iteração:** as seis avaliações de `f(·)` (três no passo 4, três
> no passo 5) dominam o tempo de `Observer_Step`. Ver
> "Orçamento de CPU" na seção de aquisição — é o que limita `Ts_adc`.

---

## Implementações de referência (em `../Aplicacao_Pi/`)

- **`Scripts/test_coeff_auto.py`** — simulação completa planta+SMO+PLL (as
  equações acima). **Modelo do que o firmware da SAME deve embarcar.** Parâmetros
  no topo (`R_s=17.3 mΩ`, `L_s=7 µH`, `P=14`/`p=7`, `Kv=310/√3`, `Ts=10 µs`).
- **`Scripts/autotune_cost.py`** — otimiza **Γ** (invariante a `Ts`) e emite
  `RESULT ... Gamma_s1=… Gamma_s2=… Gamma_t=…`; rodado pela GUI (menu
  **Estimativas → Estimar Ganhos SMO**).

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
- **`Ts_adc` real** (define `K = Γ/Ts` e o passo do observador): a meta anterior
  de ~333 kSps/canal **não é atingível** com 3 canais/ADC (máximo prático
  252 kSps em `SMPx = 7,5`). Enquanto `Observer_Step` usar `sinf` de libm, o
  teto é ~100 kSps (`SMPx = 28,5`). Fixar após medir a ISR com GPIO toggle.
  Configurável por `TARGET_TS_ADC`.
- **LUT de seno em `Observer_Manager.c`**: substituir as 6 avaliações de `f(·)`
  por tabela + interpolação linear, e reaproveitar `g_α/g_β` como `f_a/f_b`
  quando a compensação de atraso estiver desligada. É o que destrava
  `Ts_adc` ≤ 4 µs.
- **Buffer de DMA em ping-pong**: 6 words (2 frames) com half-transfer +
  transfer-complete, em vez de 3 words. Sem isso há corrida entre DMA e ISR.
  Adicionar contador de sequência + bit de descarte em `error_bits`, já que o
  ADC **não tem flag de overrun** nos canais regulares.
- **Calibração na inicialização**: implementar as duas etapas (autocalibração de
  HW `RSTCAL`/`CAL` por ADC em modo independente com `BUFEN=0`, depois offset
  dos sensores). **O ADC2 exige espera de ≥2 s para o Hall de corrente
  estabilizar** antes de amostrar o offset. Precisa de: bit de status
  *calibrando*/*falhou* em `error_bits`, mascaramento dos checadores
  dependentes de offset, e gate no `Observer_Step` até concluir.
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
