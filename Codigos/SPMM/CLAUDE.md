# SPMM — Sistema Provisório de Medição Mecânica

## Contexto e propósito

O **SPMM** é uma placa de bancada **provisória e descartável** para caracterização
mecânica do motor BLDC (coeficiente de atrito viscoso `B`, atrito de Coulomb
`τc` e momento de inércia `J`), usada enquanto as placas definitivas **SAME** e
**SIBA** eram desenvolvidas.

Roda em um **CH32V307VCT6** dedicado, separado das placas finais, sobre **Zephyr
RTOS**. O acionamento do motor é feito por um **ESC comercial**; o SPMM apenas
**comanda o throttle do ESC** (PWM de servo), **mede as tensões/correntes das
3 fases** e **lê a rotação**. O SPMM **não** gera PWM trifásico nem aciona o
motor diretamente.

A interação é feita por um **shell por rede (Telnet)** do Zephyr, com comandos
customizados de bancada. O streaming de dados (posição/velocidade/torque/
correntes/tensões, ao longo do tempo) sai por **UDP** em paralelo, contínuo
desde o boot.

### Princípio de projeto
O firmware embarcado apenas **comanda, mede e transmite**. Todo o
pós-processamento (regressão linear de `Te = B·ω + τc`, derivada `dω/dt`,
cálculo de `J`) é feito **offline no PC, em Python** —
`Aplicacao_Pi/Communication_Data/spmm_analysis.py` (ver seção própria
abaixo). Nada de mínimos quadrados, diferenciação numérica ou ajuste de
curva embarcado.

### Base de tempo (importante)
- **Orquestração e patamares:** usa o **relógio do PC**. O instante exato não
  importa em regime permanente, e o PC controla quando mandar comandos e por
  quanto tempo esperar.
- **Coast-down (derivada `dω/dt`):** **não** usa o horário de chegada do pacote
  UDP — o jitter de rede + SO corromperia o `Δt` da derivada, e perda/reordenação
  de pacotes geraria `Δt` inconsistente. Em vez disso, o micro envia um
  **contador de amostra `n`** em cada amostra, e o PC reconstrói o tempo como
  `t = n · Ts`, onde `Ts` é o período de amostragem (vem do próprio pacote,
  campo `ts_us`). Assim o `Δt` da derivada vem do timer do micro, não da rede,
  e perda/reordenação de pacotes fica **detectável** pelos saltos em `n`.

## Base reaproveitada da SIBA

O ADC e o PWM reaproveitam a configuração já validada da SIBA para o
CH32V307 — evita reescrever Devicetree, Kconfig e inicialização de baixo
nível:

- **ADC**: mesmo **driver de ADC customizado** (`drivers/adc/adc_ch32v307.c`,
  multi-instância ADC1/ADC2) — não o driver genérico do Zephyr, que puxaria
  o driver do CH32V003 e conflitaria com o registrador de Ethernet do
  CH32V307VCT6 (quebraria Ethernet/shell Telnet/log UDP).
- **PWM**: reconfigurado para **servo** (~50 Hz, pulso 1000–2000 µs,
  1 canal), não o modo de potência.
- **`native_sim`**: mesmo esquema da SIBA — `make native_sim` compila para
  POSIX no host.
- **Makefile**: mesmos atalhos (`build`/`flash`/`native_sim`) da SIBA.

## Arquitetura

```
PC  ──Telnet (TCP 23)──►  SPMM (CH32V307)  ──servo PWM──►  ESC ──► BLDC
 ▲                            │
 └────── UDP:5000 (log) ──────┤── ADC1 (Ia,Ib,Ic) + ADC2 (Va,Vb,Vc) ──► Clarke/Park ──► Id, Iq, θe
                              └── Input Capture (TIM3) ──► pulsos do sensor ──► ω
```

### Threads (prioridades — número menor = mais prioritário no Zephyr)

| Thread | Prioridade | Período | Papel |
|---|---|---|---|
| `acq` (aquisição) | 4 (ALTA) | `k_sleep(1ms)` — ~1 kHz | Lê os 2 ADCs, roda Clarke+Park, atualiza `Id`/`Iq`/`θe`. **Nunca** é preemptada pelo shell/telemetria |
| `rpm` | 6 (MÉDIA) | `k_sleep(100ms)` | Conta pulsos do TIM3 capture, converte para RPM |
| `telemetry` | 7 (BAIXA) | variável (`Ts` do log) ou dorme em semáforo | Monta lotes de amostras e envia por UDP |
| shell (Telnet) | baixa, gerida pelo subsistema `CONFIG_SHELL_BACKEND_TELNET` | bloqueada em I/O | Interpreta comandos de texto |

O custo do shell Telnet é principalmente **memória** (stack + buffers +
pilha TCP), não CPU — a thread fica bloqueada esperando entrada.

### Leitura de rotação

Sensor independente do acionamento (óptico ou Hall nos ímãs do rotor — 14
polos → 7 pulsos/volta). Necessário porque no coast-down a excitação é
cortada e o ZCP das fases desaparece. Lido por **Input Capture** (TIM3 CH3
/ PB0, MMIO direto): conta pulsos numa janela fixa de 100 ms (sem carga de
CPU) e converte para RPM.

### Medição elétrica — dq completo (3 fases medidas, não reconstruídas)

Diferente de um esquema com 2 correntes medidas + reconstrução
`Ic=-(Ia+Ib)`, o SPMM mede as **3 correntes** (ADC1: Ia, Ib, Ic) **e as 3
tensões** de fase (ADC2: Va, Vb, Vc) diretamente:

1. Clarke nas tensões → `(Vα, Vβ)` → `θe = atan2(Vβ, Vα)` — o **vetor de
   tensão** dá o ângulo elétrico (não há sensor de posição absoluta, só o
   de 7 pulsos/volta, grosseiro demais para Park em malha fechada).
2. Clarke nas correntes → `(Iα, Iβ)`.
3. Park com `θe` → `Id`, `Iq`.
4. `Iq` filtrado (média móvel exponencial, `α=0.02`) remove o ripple
   harmônico da comutação 6-step do ESC — usa-se o valor médio filtrado
   (componente fundamental, torque médio em regime permanente).
5. `Te = KT · Iq_filtrado`, com `KT = 9.5493/Kv`.

Mesmo pipeline de processamento (`process_measurement`) roda idêntico na
placa real e em `native_sim` — só a origem das 6 fases muda (ADC real vs.
senoides sintéticas defasadas de um ângulo de carga fixo).

## Shell de rede — comandos de bancada

Comandos **top-level** (sem prefixo, diferente do `siba ...` agrupado da
SIBA), acessíveis por **Telnet (TCP 23)**:

| Comando | Função |
|---|---|
| `arm` | Sequência de armar o ESC (pulso mínimo 2s) |
| `disarm` | Desarma o ESC |
| `throttle <us>` | Define o PWM do ESC (1000 = parado, 2000 = máx) |
| `ramp <us> <s>` | Rampa suave de throttle (evita degrau no ESC) |
| `rpm` | Lê rotação atual (`rpm`, `omega`) |
| `iq` | Lê `Iq` médio filtrado atual |
| `read` | Dump completo: rpm, omega, Te, Iq, Id, θe, e as 6 fases (ia,ib,ic,va,vb,vc) |
| `patamar <us> <s>` | Aplica o throttle, espera `<s>` segundos estabilizar, imprime o ponto de regime `(ω, Te)` → ensaio B/τc |
| `coast` | Corta o throttle (1000µs) → dispara o decaimento livre (`ω(t)`) → ensaio J |
| `log start <hz>` / `log stop` | Reconfigura/pausa a taxa do streaming UDP (ativo por padrão a 100 Hz desde o boot) |

## Procedimento dos ensaios (via shell, ou pela GUI — ver abaixo)

### Atrito viscoso (B, τc) — regime permanente em patamares
```
arm
patamar 1200 3      # throttle 1200us, espera 3s estabilizar, imprime (ω, Te)
patamar 1300 3
patamar 1400 3
...                 # vários pontos cobrindo a faixa de rotação
```
Em regime permanente (`dω/dt = 0`): `Te = B·ω + τc`. O PC faz a **regressão
linear** sobre os pares `(ω, Te)` para extrair `B` e `τc`.

### Momento de inércia (J) — coast-down
```
arm
throttle 1800       # sobe até rotação alta
log start 1000      # (opcional) sobe a taxa de log para o coast-down
coast               # corta throttle; o log corrente streama ω(t) do decaimento
```
Sem excitação: `J·(dω/dt) = -B·ω - τc`. O PC calcula a **derivada** de
`ω(t)` na região de decaimento e isola `J = -(B·ω + τc)/(dω/dt)`, usando o
`B` e `τc` do ensaio anterior.

## Fórmulas dos ensaios

Notação: `ω` = velocidade mecânica [rad/s], `Te` = torque eletromagnético [N·m],
`B` = atrito viscoso [N·m·s/rad], `τc` = atrito de Coulomb [N·m], `J` = momento
de inércia [kg·m²]. Conversão de RPM: `ω = RPM · 2π / 60`.

### Atrito viscoso e de Coulomb (B, τc)
Em regime permanente (`dω/dt = 0`), o torque eletromagnético equilibra as perdas
mecânicas:
```
Te = B·ω + τc
```
Com os pares `(ω_k, Te_k)` de cada patamar `k`, ajusta-se uma **reta** por
mínimos quadrados (`Aplicacao_Pi/Communication_Data/spmm_analysis.py::fit_atrito`,
forma fechada — sem LAPACK, robusta a dados degenerados):
- coeficiente angular → `B`
- coeficiente linear (intercepto) → `τc`
- qualidade do ajuste → `R²` (alerta se < 0,98)

### Momento de inércia (J) — coast-down
Sem excitação (`Te = 0`), a dinâmica de parada é:
```
J·(dω/dt) = -(B·ω + τc)
```
Isolando, para cada ponto da curva de decaimento:
```
J = -(B·ω + τc) / (dω/dt)
```
onde `dω/dt < 0` na desaceleração (logo `J > 0`). Usa o `B` e `τc` do ensaio
anterior. `spmm_analysis.py::compute_J` calcula `J` em **várias janelas de
RPM** ao longo do decaimento — um `J` constante entre janelas valida a
medição; variação indica `B`/`τc` incorretos ou perda mecânica não
modelada.

### Alternativa para J (constante de tempo, opcional)
Se o atrito de Coulomb for desprezível (`τc ≈ 0`), a parada vira exponencial
`ω(t) = ω₀·e^(-t/T)` com `T = J/B`, e `J = B · T` (constante de tempo do
decaimento) — útil como verificação cruzada quando `τc` é pequeno.

## Protocolo UDP de log (binário, overhead mínimo)

Struct binário empacotado (não texto), little-endian, sem padding
(`#pragma pack(push,1)`), definido em `src/spmm.h`:

```c
typedef struct {
    uint32_t n;        // indice da amostra; tempo = n * Ts (reconstruido no PC)
    float    omega;    // rad/s (mecanico)
    float    te;       // N.m  (= KT * iq)
    float    iq;       // A (eixo q filtrado — corrente de torque)
    float    id;       // A (eixo d)
    float    theta_e;  // rad (angulo eletrico estimado da tensao)
    float    ia, ib, ic;  // A — correntes de fase instantaneas
    float    va, vb, vc;  // V — tensoes de fase instantaneas
} spmm_sample_t;       // 48 bytes (medicao completa)

typedef struct {
    uint16_t      count;   // numero de amostras neste pacote
    uint16_t      ts_us;   // Ts em microssegundos (PC reconstroi t = n*Ts)
    spmm_sample_t s[];     // count amostras (ate 20 por pacote, MTU)
} spmm_pkt_hdr_t;
```

No PC (`Aplicacao_Pi/Communication_Data/codecs.py::decode_spmm_log`),
desempacotado com o módulo `struct` (`<HH>` + `count`×`<I11f>`). Perda/
reordenação é detectável pelos saltos no campo `n`.

## Integração com a GUI (`Aplicacao_Pi`)

O cliente de bancada roda dentro da GUI, não como script solto:

- **`Communication_Data/links/telnet_link.py`** — sessão Telnet (TX de
  comandos de texto) + socket UDP (RX do log binário); parada segura
  (`disarm`+`throttle 1000`) sempre no `finally`, inclusive se a GUI cair.
- **`GUI/SPMMEnsaioDialog.py`** (menu Estimativas → Ensaios SPMM) — interface
  para os dois modos de ensaio (atrito/inércia), com parâmetros
  configuráveis, salvamento incremental em CSV (com índice de repetição) e
  parada segura garantida no fechamento do diálogo.
- **`Communication_Data/spmm_analysis.py`** — `fit_atrito`/`compute_J`
  (regressão e derivada, funções puras).

Ver [`../Aplicacao_Pi/README.md`](../Aplicacao_Pi/README.md) para o
detalhe completo dessa integração.

## Parâmetros do motor sob ensaio

Motor: **Scorpion A-5025-310Kv** (enrolamento 6T/7T Delta, 14 polos →
`p = 7`).

Parâmetros elétricos já caracterizados (referência, não usados diretamente
nos ensaios mecânicos):
- `R_Y ≈ 8,69 mΩ` (datasheet: 8,65 mΩ — erro 0,46%)
- `L_Y ≈ 6,95 µH`

## Restrições de projeto

- O SPMM é **provisório**: prioriza simplicidade e rapidez de
  desenvolvimento sobre elegância — não é o firmware final.
- Não aciona o motor diretamente — o ESC comercial faz isso; o SPMM só
  comanda throttle.
- A thread de aquisição (`acq`, prio 4) nunca é preemptada pelo shell ou
  pela telemetria (prio 6/7).
- Pós-processamento (regressão, derivada, `J`) é sempre no PC, nunca
  embarcado.
- **Segurança**: o desligamento do motor (`disarm` + `throttle 1000`) roda
  sempre no `finally`/`closeEvent` do lado PC — o motor nunca pode ficar
  girando se o script/GUI encerrar.
- Calibração de corrente/tensão (`SPMM_I_OFFSET_MV`/`SPMM_I_AMPS_PER_MV`/
  `SPMM_V_VOLTS_PER_MV` em `spmm.h`) é **provisória** — calibrar na
  bancada com os componentes reais.
