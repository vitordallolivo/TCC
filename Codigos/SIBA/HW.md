# SIBA — Referência de Hardware (Pinagem)

> Fonte de verdade real: `boards/wch/siba_board/siba_board.dts` + `dts/bindings/**`.
> Este arquivo é um resumo legível para humanos — **se houver divergência, o `.dts` manda**.
> Sempre que um pino mudar no `.dts`, atualize esta tabela no mesmo commit.

MCU: **CH32V307VTC6** (RISC-V, 144 MHz) — Zephyr board `siba_board`.

---

## Tabela geral de pinos usados

| Pino   | Periférico        | Função                          | Nó DTS / label                          | Status |
|--------|-------------------|---------------------------------|------------------------------------------|--------|
| PA0    | ADC1 CH0          | Entrada analógica (expansão)    | `adc1` (`adc1_default` pinctrl)          | OK |
| PA1    | GPIO (saída)      | HX711[0] PD_SCK                 | `hx711_sck_pins` idx 0                   | placeholder |
| PA2    | GPIO (entrada)    | HX711[0] DOUT                   | `hx711_dout_pins` idx 0                  | placeholder |
| PA3    | GPIO (saída)      | HX711[1] PD_SCK                 | `hx711_sck_pins` idx 1                   | placeholder |
| PA4    | GPIO (entrada)    | HX711[1] DOUT                   | `hx711_dout_pins` idx 1                  | placeholder |
| PA5    | GPIO (saída)      | HX711[2] PD_SCK                 | `hx711_sck_pins` idx 2                   | placeholder |
| PA6    | GPIO (entrada)    | HX711[2] DOUT                   | `hx711_dout_pins` idx 2                  | placeholder |
| PA9    | USART1 TX         | Console / shell / log           | `usart1_default` (`USART1_TX_PA9_0`)     | OK |
| PA10   | USART1 RX         | Console / shell / log           | `usart1_default` (`USART1_RX_PA10_0`)    | OK |
| PB0    | TIM3 CH3 (capture)| Contador de pulsos RPM          | `rpm_capture_pin` / `tim3_ch3_pb0`       | OK |
| PB1    | GPIO (saída)      | HX711[3] PD_SCK                 | `hx711_sck_pins` idx 3                   | placeholder |
| PB2    | GPIO (entrada)    | HX711[3] DOUT                   | `hx711_dout_pins` idx 3                  | placeholder |
| PB3    | GPIO (saída)      | HX711[4] PD_SCK                 | `hx711_sck_pins` idx 4                   | placeholder |
| PB4    | GPIO (entrada)    | HX711[4] DOUT                   | `hx711_dout_pins` idx 4                  | placeholder |
| PB5    | GPIO (saída)      | HX711[5] PD_SCK                 | `hx711_sck_pins` idx 5                   | placeholder |
| PB6    | TIM4 CH1 (PWM)    | Sinal ESC (50 Hz, 1000-2000 µs) | `pwm4` / `esc_motor` (`tim4_ch1_default`)| OK |
| PC0    | GPIO (entrada)    | HX711[5] DOUT                   | `hx711_dout_pins` idx 5                  | placeholder |
| PC6    | ETH (MAC interno) | RXP                             | `mac_default` / `mac_internal_rxp`       | OK |
| PC7    | ETH (MAC interno) | RXN                             | `mac_default` / `mac_internal_rxn`       | OK |
| PC8    | ETH (MAC interno) | TXP                             | `mac_default` / `mac_internal_txp`       | OK |
| PC9    | ETH (MAC interno) | TXN                             | `mac_default` / `mac_internal_txn`       | OK |
| PE15   | GPIO (saída)      | LED "Sistema pronto" (`led0`)   | `ready_pin` (alias `led0`)               | OK |
| ?      | GPIO (saída)      | Relé de potência (`relay0`)     | **alias não definido no DTS**            | ⚠ TODO |

---

## Detalhe por periférico

### USART1 — Console / Shell / Log
- TX = PA9, RX = PA10, 115200 8N1
- `chosen { zephyr,console = &usart1; zephyr,shell-uart = &usart1; }`
- Usado para log do sistema (`LOG_INF`/`LOG_ERR` etc.) e é o canal planejado para
  o **shell de teste simplificado** (ver TODO em `CLAUDE.md`).

### ADC1 — PA0 (canal 0)
- Driver custom out-of-tree: `drivers/adc/adc_ch32v307.c` (MMIO + DMA1 canal 1, modo circular)
- Binding DTS: `compatible = "wch,adc"` (`dts/bindings/adc/wch,adc.yaml`)
- Resolução 12 bits, oversample 8x (média), `ADC_NUM_CHANNELS = 1`
- Reservado para expansão analógica futura (v_batt / i_batt / pitot ainda **não** ligados ao ADC)

### HX711 x6 — Células de carga (bit-bang GPIO)
Protocolo serial síncrono manual (sem driver Zephyr dedicado).
Pinos definidos via binding genérico `compatible = "siba,gpio-array"`
(`dts/bindings/gpio/siba,gpio-array.yaml`), nós `hx711_sck_pins` / `hx711_dout_pins`.

| Célula | PD_SCK (saída) | DOUT (entrada) |
|--------|----------------|----------------|
| 0      | PA1            | PA2            |
| 1      | PA3            | PA4            |
| 2      | PA5            | PA6            |
| 3      | PB1            | PB2            |
| 4      | PB3            | PB4            |
| 5      | PB5            | PC0            |

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

## Checklist ao alterar hardware/pinagem

1. Edite `boards/wch/siba_board/siba_board.dts` (e bindings em `dts/bindings/` se necessário).
2. Atualize esta tabela em `HW.md`.
3. Se o pino afeta um array/índice usado em código (HX711, RPM, etc.), atualize
   também os comentários e specs em `lib/OS/HAL.c` / `lib/SYS/Battery_Manager.c`.
4. Rode `west build -b siba_board -p` (pristine) para garantir que o DTS compila.
