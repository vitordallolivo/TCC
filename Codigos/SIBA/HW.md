# SIBA — Referência de Hardware (Pinagem)

> Fonte de verdade real: `boards/wch/siba_board/siba_board.dts` + `dts/bindings/**`.
> Este arquivo é um resumo legível para humanos — **se houver divergência, o `.dts` manda**.
> Sempre que um pino mudar no `.dts`, atualize esta tabela no mesmo commit.

MCU: **CH32V307VTC6** (RISC-V, 144 MHz) — Zephyr board `siba_board`.

---

## Tabela geral de pinos usados

| Pino   | Periférico        | Função                          | Nó DTS / label                          | Status |
|--------|-------------------|---------------------------------|------------------------------------------|--------|
| PA0    | ADC1 CH0          | Entrada analógica (expansão)    | `adc1` (`adc1_default` pinctrl, group1)  | OK |
| PA1    | ADC1 CH1          | v_batt (divisor de tensão)      | `adc1` (`adc1_default` pinctrl, group2)  | placeholder |
| PA2    | ADC1 CH2          | i_batt (ACS758)                 | `adc1` (`adc1_default` pinctrl, group3)  | placeholder |
| PA3    | ADC1 CH3          | temperature_battery (NTC)       | `adc1` (`adc1_default` pinctrl, group4)  | placeholder |
| PA4    | ADC1 CH4          | air_speed (MPXV7002DP)          | `adc1` (`adc1_default` pinctrl, group5)  | placeholder |
| PA5    | ADC1 CH5          | livre (expansão futura)         | —                                         | livre |
| PA6    | ADC1 CH6          | livre (expansão futura)         | —                                         | livre |
| PA7    | ADC1 CH7          | livre (expansão futura)         | —                                         | livre |
| PA9    | USART1 TX         | Console / shell / log           | `usart1_default` (`USART1_TX_PA9_0`)     | OK |
| PA10   | USART1 RX         | Console / shell / log           | `usart1_default` (`USART1_RX_PA10_0`)    | OK |
| PB0    | TIM3 CH3 (capture)| Contador de pulsos RPM          | `rpm_capture_pin` / `tim3_ch3_pb0`       | OK |
| PB6    | TIM4 CH1 (PWM)    | Sinal ESC (50 Hz, 1000-2000 µs) | `pwm4` / `esc_motor` (`tim4_ch1_default`)| OK |
| PC6    | ETH (MAC interno) | RXP                             | `mac_default` / `mac_internal_rxp`       | OK |
| PC7    | ETH (MAC interno) | RXN                             | `mac_default` / `mac_internal_rxn`       | OK |
| PC8    | ETH (MAC interno) | TXP                             | `mac_default` / `mac_internal_txp`       | OK |
| PC9    | ETH (MAC interno) | TXN                             | `mac_default` / `mac_internal_txn`       | OK |
| PE2    | GPIO (saída)      | HX711[0] PD_SCK                 | `hx711_sck_pins` idx 0                   | placeholder |
| PE3    | GPIO (saída)      | HX711[1] PD_SCK                 | `hx711_sck_pins` idx 1                   | placeholder |
| PE4    | GPIO (saída)      | HX711[2] PD_SCK                 | `hx711_sck_pins` idx 2                   | placeholder |
| PE5    | GPIO (saída)      | HX711[3] PD_SCK                 | `hx711_sck_pins` idx 3                   | placeholder |
| PE6    | GPIO (saída)      | HX711[4] PD_SCK                 | `hx711_sck_pins` idx 4                   | placeholder |
| PE7    | GPIO (saída)      | HX711[5] PD_SCK                 | `hx711_sck_pins` idx 5                   | placeholder |
| PE8    | GPIO (entrada)    | HX711[0] DOUT                   | `hx711_dout_pins` idx 0                  | placeholder |
| PE9    | GPIO (entrada)    | HX711[1] DOUT                   | `hx711_dout_pins` idx 1                  | placeholder |
| PE10   | GPIO (entrada)    | HX711[2] DOUT                   | `hx711_dout_pins` idx 2                  | placeholder |
| PE11   | GPIO (entrada)    | HX711[3] DOUT                   | `hx711_dout_pins` idx 3                  | placeholder |
| PE12   | GPIO (entrada)    | HX711[4] DOUT                   | `hx711_dout_pins` idx 4                  | placeholder |
| PE13   | GPIO (entrada)    | HX711[5] DOUT                   | `hx711_dout_pins` idx 5                  | placeholder |
| PE15   | GPIO (saída)      | LED "Sistema pronto" (`led0`)   | `ready_pin` (alias `led0`)               | OK |
| ?      | GPIO (saída)      | Relé de potência (`relay0`)     | **alias não definido no DTS**            | ⚠ TODO |

> ⚠ **ADC1 só alcança os canais 0-7 (pinos PA0-PA7)** — canais 8+ (PB0/PB1/
> PC0-PC5) pertencem ao mux físico do **ADC2**, que o driver custom
> `adc_ch32v307` (único suportado, ver seção ADC1 abaixo) não acessa. Por
> isso o HX711 foi remapeado de PA1-PA6/PB1-PB5/PC0 para a **Porta E**
> (PE2-PE13), liberando PA1-PA7 inteiramente para canais analógicos.

---

## Detalhe por periférico

### USART1 — Console / Shell / Log
- TX = PA9, RX = PA10, 115200 8N1
- `chosen { zephyr,console = &usart1; zephyr,shell-uart = &usart1; }`
- Usado para log do sistema (`LOG_INF`/`LOG_ERR` etc.) e é o canal planejado para
  o **shell de teste simplificado** (ver TODO em `CLAUDE.md`).

### ADC1 — scan circular multicanal (5 canais) via DMA
- Driver custom out-of-tree: `drivers/adc/adc_ch32v307.c` (MMIO direto, DMA circular)
- Binding DTS: `compatible = "wch,adc"` (`dts/bindings/adc/wch,adc.yaml`)
- Resolução 12 bits, oversample 8x (média), `ADC_NUM_CHANNELS = 5` (`HAL_ADC_IDX_*` em `HAL.h`)
- O driver já fazia DMA circular desde o início — a mudança nesta rodada foi
  puramente de configuração no `HAL.c` (mais canais em `adc_ch_cfg[]`/`adc_seq`)
- **O driver agora suporta múltiplas instâncias — ADC1 e ADC2 — cada uma com
  seu próprio canal DMA1** (alocado via `dmas = <&dma1 N>` no nó `wch,adc` do
  DTS; registradores/IRQ do canal calculados em `DMA1_CH_BASE`/
  `ADC_CH32V307_DMA_IRQN` a partir de `N`). Hoje só o nó `adc1` está
  instanciado (`status = "okay"`); para usar o ADC2 basta adicionar um nó
  `adc@40012800` com `dmas = <&dma1 <outro-canal>>` — nenhuma mudança no
  driver é necessária.
- ⚠ **Cada periférico ADC só alcança fisicamente um conjunto fixo de pinos**:
  ADC1 = canais 0-7 = PA0-PA7; ADC2 = canais 8+ = PB0/PB1/PC0-PC5 (mux
  separado). **Um nó `adc1` só pode usar canais 0-7 (PA0-PA7); um futuro
  `adc2` ficaria restrito a PB0/PB1/PC0-PC5.** É justamente por causa dessa
  diferença de mapeamento entre ADC1/ADC2 (não suportada pelo driver ADC
  genérico do Zephyr) que existe este driver custom em
  `drivers/adc/adc_ch32v307.c`.

| idx | Canal ADC1 | Pino | Grandeza             | Sensor / circuito                | Conversão (`HAL.c`)              |
|-----|------------|------|----------------------|----------------------------------|----------------------------------|
| 0   | CH0        | PA0  | expansão genérica    | —                                | bruto (`HAL_ADC_Get`)            |
| 1   | CH1        | PA1  | `v_batt`             | divisor resistivo (R1/R2)        | `HAL_ADC_Get_VBatt()`            |
| 2   | CH2        | PA2  | `i_batt`             | ACS758 (Hall, saída analógica)   | `HAL_ADC_Get_IBatt()`            |
| 3   | CH3        | PA3  | `temperature_battery`| termistor NTC (lookup table)     | `HAL_ADC_Get_Temperature_Batt()` |
| 4   | CH4        | PA4  | `air_speed`          | MPXV7002DP (pressão diferencial) | `HAL_ADC_Get_Air_Speed()`        |

(PA5/CH5, PA6/CH6, PA7/CH7 seguem livres para expansão futura — único range
físico restante do ADC1.)

- **`v_batt`** — divisor resistivo `Vbat = Vadc * (R1+R2)/R2`. Constantes
  `HAL_VBATT_DIVIDER_R1_KOHM`/`R2_KOHM` em `HAL.h` (placeholder p/ pack 6S, ajustar
  ao divisor real do esquemático).
- **`i_batt`** — ACS758 (sensor de corrente por efeito Hall, saída analógica
  proporcional, ponto-zero em `Vcc/2`); **futuramente pode ser substituído por
  shunt resistivo + amplificador**. Constantes `HAL_ACS758_VZERO_MV`/`SENS_MV_PER_A`
  em `HAL.h` (ajustar conforme a variante do ACS758 usada — 50U/100U/150B/...).
- **`temperature_battery`** — termistor NTC; conversão por **lookup table**
  (`hal_ntc_lut[]` em `HAL.c`, tensão do divisor → °C, interpolação linear).
  Tabela calculada para NTC 10k @ 25°C / B=3950 / pull-up 10k — **ajustar para
  o termistor real** (regenerar a tabela a partir do datasheet/Steinhart-Hart).
- **`air_speed`** — MPXV7002DP (sensor de pressão diferencial ±2 kPa, tubo de
  Pitot): `P_kPa = (Vout/Vs - 0.5) / 0.2`, depois Bernoulli `v = sqrt(2·ΔP/ρ_ar)`.
  Constantes `HAL_MPXV7002DP_VS_MV`/`HAL_AIR_DENSITY_KG_M3` em `HAL.h`.

⚠ **Pinagem dos 4 canais novos é placeholder** — escolhidos dentro do único
range físico do ADC1 (PA0-PA7), o que exigiu remapear o HX711 (que ocupava
PA1-PA6) para a Porta E. Confirmar no esquemático real e, se mudar,
atualizar `adc1_default` no `.dts` **e** `adc_ch_cfg[]`/`HAL_ADC_IDX_*`/`ADC_CH_*`
em `HAL.c`/`HAL.h`.

### HX711 x6 — Células de carga (bit-bang GPIO)
Protocolo serial síncrono manual (sem driver Zephyr dedicado, sem função
alternada — qualquer GPIO serve). **Remapeado para a Porta E** (estava em
PA1-PA6/PB1-PB5/PC0) para liberar PA1-PA7 inteiramente para o ADC1 — ver
nota "ADC1 só alcança PA0-PA7" acima. Pinos definidos via binding genérico
`compatible = "siba,gpio-array"` (`dts/bindings/gpio/siba,gpio-array.yaml`),
nós `hx711_sck_pins` / `hx711_dout_pins`.

| Célula | PD_SCK (saída) | DOUT (entrada) |
|--------|----------------|----------------|
| 0      | PE2            | PE8            |
| 1      | PE3            | PE9            |
| 2      | PE4            | PE10           |
| 3      | PE5            | PE11           |
| 4      | PE6            | PE12           |
| 5      | PE7            | PE13           |

- Ganho: 128x (canal A, 1 pulso extra após 24 bits) — `HX711_GAIN_PULSES`
- Rate: ~10 Hz por célula (DOUT vai para LOW quando a conversão está pronta)
- **⚠ Pinagem ainda é placeholder** — confirmar no esquemático real e, se mudar,
  atualizar `hx711_sck_pins`/`hx711_dout_pins` no `.dts` **e** os comentários/arrays
  espelhados em `lib/OS/HAL.c` (topo do arquivo + `hx711_sck[]`/`hx711_dout[]`).

### ESC PWM — TIM4 CH1 (PB6)
- Nó: `pwm4` (timer `tim4`, prescaler 143) → consumidor `esc_motor` (`compatible = "siba,pwm-consumer"`)
- Binding: `dts/bindings/pwm/siba,pwm-consumer.yaml`
- Frequência 50 Hz (período 20 ms), pulso 1000 µs (arm/mínimo) → 2000 µs (throttle máximo)
- Pinctrl: `tim4_ch1_default` → `CH32V20X_V30X_PINMUX_DEFINE(PB, 6, TIM4, 0)`
- API: `HAL_ESC_Init()`, `HAL_ESC_Arm()`, `HAL_ESC_Set_Throttle(0.0-100.0)`, `HAL_ESC_Disarm()`

### RPM Counter — TIM3 CH3 (PB0)
- Nó: `tim3` (`compatible = "wch,gptm"`, sem driver `counter` dedicado — leitura direta de registradores em `HAL_RPM_*`)
- Pino configurado como entrada flutuante (`rpm_capture_pin`, pull-up via `tim3_ch3_pb0`)
- Janela de medição: 100 ms (`HAL_RPM_SAMPLE_MS`)
- Conversão: `(pulsos / HAL_RPM_PULSES_PER_REV) * 600` = RPM (motor 14 pólos → `HAL_RPM_PULSES_PER_REV = 14`)

### Ethernet (MAC interno CH32V307)
- Pinos fixos do MAC interno: PC6 (RXP), PC7 (RXN), PC8 (TXP), PC9 (TXN) — `mac_default`
- PHY interno (`phy-connection-type = "internal"`), sem pinos MDIO externos
- IP board: `169.254.97.100` / IP PC: `169.254.97.139` — link-local, sem DHCP
  (definido em `prj.conf`: `CONFIG_NET_CONFIG_MY_IPV4_ADDR` / `CONFIG_NET_CONFIG_PEER_IPV4_ADDR`)

### LED — PE15 (`ready_pin`, alias `led0`)
- `compatible = "gpio-leds"`, ativo em nível alto
- Indicador de "Sistema pronto"

### Relé de potência — **pino indefinido**
- `Battery_Manager.c` já espera o alias `relay0` via `DT_ALIAS(relay0)` +
  `GPIO_DT_SPEC_GET_OR(..., {0})` (fallback seguro se o alias não existir)
- **⚠ TODO crítico**: definir o GPIO real no esquemático e adicionar
  `aliases { relay0 = &<nó>; }` no `.dts` — sem isso, `Battery_Manager_Set_Relay()`
  não tem efeito (log de aviso "relé GPIO não disponível")

---

## Bindings DTS custom (`dts/bindings/`)

| Binding                         | Compatible              | Uso                                             |
|---------------------------------|--------------------------|-------------------------------------------------|
| `adc/wch,adc.yaml`               | `wch,adc`               | Driver ADC custom (`drivers/adc/adc_ch32v307.c`) |
| `gpio/siba,gpio-array.yaml`      | `siba,gpio-array`       | Arrays de GPIO genéricos (HX711 SCK/DOUT, RPM)   |
| `pwm/siba,pwm-consumer.yaml`     | `siba,pwm-consumer`     | Consumidor de PWM nomeado (ESC)                  |

## Memória

- SRAM: 128 KB (`reg = <0x20000000 DT_SIZE_K(128)>`)
- Flash: 198 KB (`reg = <0x08000000 DT_SIZE_K(198)>`)

---

## Modo Mock (native_sim) — testar sem esta placa

Tudo nesta página (`HW.md`) descreve o **hardware real da `siba_board`**
(CH32V307 + periféricos acima). Quando ele não está disponível (placa fora
de mãos, sem W-Link, etc.), dá pra rodar o firmware como **binário POSIX no
host** via a board `native_sim` do Zephyr — `make native_sim_run` (ver
CLAUDE.md "Modo Mock (native_sim)" para o fluxo completo).

Nesse modo **os periféricos físicos desta página não existem de verdade** —
`native_sim` não tem CH32V307, ADC, GPIO bit-bang, PWM/TIM4, TIM3 ou MAC
Ethernet físico. O `CMakeLists.txt` troca `HAL.c`+driver `adc_ch32v307` por
**`lib/OS/HAL_mock.c`** (selecionado via `CONFIG_ARCH_POSIX`), que implementa
a mesma API pública gerando dados sintéticos — então o resto do firmware
(System_Manager, Battery/Propeller/Security/Log_Manager, CMD_Core,
Test_Shell) roda inalterado.

`NCI.c`, por outro lado, **não é trocado** — `boards/native_sim.conf` liga
os "Offloaded sockets" do Native Simulator (NSOS), que repassam
`zsock_socket/bind/sendto/recvfrom` direto para os sockets BSD reais do
host Linux. Ou seja, **a rede UDP é real**: o binário escuta `CMD` em
`127.0.0.1:5000` e manda `TELEMETRY_LOG` para `127.0.0.1:5555` — mesmo
protocolo e portas da placa física, só que em loopback. Ver CLAUDE.md
"Modo Mock (native_sim)" e `Aplicacao_Pi/CLAUDE.md`.

Tabela de equivalência "o que vira o quê" no modo mock:

| Real (`HW.md`)                          | Mock (`HAL_mock.c` + `NCI.c`/NSOS)                        |
|------------------------------------------|-----------------------------------------------------------|
| ADC1 — v_batt/i_batt/temp/air_speed (PA1-4, MMIO+DMA) | modelo sintético: corrente ∝ throttle², tensão drena por Coulomb count, temperatura segue corrente, air_speed ∝ RPM (todos com ruído + `adc_calib[]`/`hal_adc_apply_calib` reais) |
| HX711 x6 (bit-bang GPIO, Porta E)        | "leituras" raw sintéticas com modelo de empuxo ∝ RPM² + variação por célula; usa o mesmo `hx711_state[]{raw,tare,scale,offset}`/fórmula `Get_Grams` reais — `tare`/`Set_Calib` funcionam de verdade |
| ESC PWM — TIM4 CH1 (PB6)                 | `esc_armed`/`esc_throttle_pct` em RAM — sem PWM real; o RPM simulado persegue esse throttle |
| RPM Counter — TIM3 CH3 (PB0)             | RPM sintético: persegue o throttle do ESC com resposta de 1ª ordem + ruído |
| Ethernet (MAC interno, IP link-local 169.254.x.x) | rede REAL via NSOS em loopback (`127.0.0.1:5000`/`:5555`) — mesmo `NCI.c`, mesmo protocolo `CMD`/`TELEMETRY_LOG`; só o endereço muda (link-local físico → loopback do host). `Aplicacao_Pi` se conecta normalmente |
| Relé de potência (`relay0` — ainda não existe no `.dts` real) | mesmo estado: alias `relay0` também não existe no `.dts` do `native_sim` → `Battery_Manager` cai no mesmo fallback "relé GPIO não disponível" |
| LED `ready_pin`/`led0`                   | `CONFIG_GPIO`/`gpio_emul` ligados, mas sem alias `led0` no `.dts` do `native_sim` — sem efeito visível |
| USART1 console/shell                     | console/shell do `native_sim` rodam sobre um pseudo-TTY do host (`uart connected to pseudotty: /dev/pts/N`) — os comandos `siba ...` funcionam normalmente |

> **Use isto para**: achar bugs de thread/lógica/concorrência nas camadas
> OS (acima do HAL)/SYS/CMD — ex.: o próprio modo mock já expôs, ao simular
> `v_batt` perto de 25.2 V, que `cell_count_lut[]` em `Battery_Manager.c` tem
> faixas adjacentes que se sobrepõem (6S termina em 25.2 V, 7S começa em
> 24.5 V) e a contagem de células "pisca" entre 6S/7S a cada leitura com
> ruído — um bug real de limite/histerese, não do mock.
> **Não use isto para**: validar pinagem, timing de periférico, DMA,
> consumo elétrico ou qualquer coisa que dependa do hardware físico — para
> isso a placa real + W-Link continuam sendo necessários.

---

## Checklist ao alterar hardware/pinagem

1. Edite `boards/wch/siba_board/siba_board.dts` (e bindings em `dts/bindings/` se necessário).
2. Atualize esta tabela em `HW.md`.
3. Se o pino afeta um array/índice usado em código (HX711, RPM, ADC, etc.),
   atualize também os comentários e specs em `lib/OS/HAL.c` / `lib/OS/HAL.h` /
   `lib/SYS/Battery_Manager.c` / `lib/SYS/Propeller_Manager.c`.
4. Rode `west build -b siba_board -p` (pristine) para garantir que o DTS compila.
