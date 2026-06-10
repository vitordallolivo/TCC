# SIBA — Referência de Hardware (Pinagem)

> Fonte de verdade real: `boards/wch/siba_board/siba_board.dts` + `dts/bindings/**`.
> Este arquivo é um resumo legível para humanos — **se houver divergência, o `.dts` manda**.
> Sempre que um pino mudar no `.dts`, atualize esta tabela no mesmo commit.

MCU: **CH32V307VTC6** (RISC-V, 144 MHz) — Zephyr board `siba_board`.

> ✅ **Pinagem real (esquemático, atualização 2026-06-07) já APLICADA** ao
> `.dts`/`HAL.c`/`HAL.h` — ver tabela "geral" abaixo (já atualizada) e a
> seção "Pinagem real" mais abaixo (mantida como registro do levantamento
> original do esquemático, agora marcada como aplicada). Itens que ainda
> dependem de confirmação física na bancada (constantes de calibração,
> `cell_count_lut`, etc.) continuam listados no TODO de `CLAUDE.md`.

---

## Tabela geral de pinos usados

| Pino   | Periférico        | Função                          | Nó DTS / label                          | Status |
|--------|-------------------|---------------------------------|------------------------------------------|--------|
| PA0    | ADC1 CH0          | v_batt                          | `adc1` (`adc1_default` pinctrl, group1)  | OK |
| PA1    | ADC1 CH1          | i_batt                          | `adc1` (`adc1_default` pinctrl, group2)  | OK |
| PA2    | ADC1 CH2          | air_speed (tubo de Pitot)       | `adc1` (`adc1_default` pinctrl, group3)  | OK |
| PA3    | ADC1 CH3          | Ensaio_Bateria_Voltage (bancada)| sem nó funcional — documentado apenas    | documentado |
| PA4    | ADC1 CH4          | Ensaio_Bateria_DAC (bancada)    | sem nó funcional — documentado apenas    | documentado |
| PA5    | DAC1 OUT1         | Saída analógica (DAC)           | `HAL_DAC_*` (MMIO direto, sem nó DTS)    | OK |
| PA6    | ADC1 CH6          | temperature_battery (uso duplo: também Ensaio_Bateria_Termistor_1) | `adc1` (`adc1_default` pinctrl, group4) | OK |
| PA7    | ADC1 CH7          | Ensaio_Bateria_Termistor_2 (bancada) | sem nó funcional — documentado apenas | documentado |
| PA8    | GPIO (reservado)  | Ensaio_Bateria_Corrente — fora do range ADC1/ADC2, função TBD | `pa8_reserved` | reservado |
| PA9    | USART1 TX         | Console / shell / log           | `usart1_default` (`USART1_TX_PA9_0`)     | OK |
| PA10   | USART1 RX         | Console / shell / log           | `usart1_default` (`USART1_RX_PA10_0`)    | OK |
| PB0    | TIM3 CH3 (capture)| Contador de pulsos RPM          | `rpm_capture_pin` / `tim3_ch3_pb0`       | OK |
| PB6    | TIM4 CH1 (PWM)    | Sinal ESC (50 Hz, 1000-2000 µs) | `pwm4` / `esc_motor` (`tim4_ch1_default`)| OK |
| PB8    | I2C1 SCL (remap=1)| I2C1 — leitura/escrita/scan crus| `i2c1` (`i2c1_default` pinctrl, group1)  | OK |
| PB9    | I2C1 SDA (remap=1)| I2C1 — leitura/escrita/scan crus| `i2c1` (`i2c1_default` pinctrl, group2)  | OK |
| PB11   | GPIO (saída)      | Relé genérico 0 — uso TBD       | `relays_generic` idx 0                   | reservado |
| PB12   | GPIO (saída)      | Relé genérico 1 — uso TBD       | `relays_generic` idx 1                   | reservado |
| PB13   | GPIO (saída)      | Relé genérico 2 — uso TBD       | `relays_generic` idx 2                   | reservado |
| PB14   | GPIO (saída)      | Relé genérico 3 — uso TBD       | `relays_generic` idx 3                   | reservado |
| PB15   | GPIO (saída)      | Relé de potência (`relay0`)     | `relay_batt` (alias `relay0`)            | OK |
| PC6    | ETH (MAC interno) | RXP                             | `mac_default` / `mac_internal_rxp`       | OK |
| PC7    | ETH (MAC interno) | RXN                             | `mac_default` / `mac_internal_rxn`       | OK |
| PC8    | ETH (MAC interno) | TXP                             | `mac_default` / `mac_internal_txp`       | OK |
| PC9    | ETH (MAC interno) | TXN                             | `mac_default` / `mac_internal_txn`       | OK |
| PD2    | GPIO (entrada, reservado) | Saída da porta AND (MCU_Sig_Ready AND ...) → habilita SSR 100A — feedback/diagnóstico, sem leitura ainda | `gpio_pd2_and_out` | reservado |
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
| PE15   | GPIO (saída)      | LED "Sistema pronto" / `MCU_Sig_Ready` (porta AND → SSR 100A) | `ready_pin` (alias `led0`) | OK |

> ⚠ **ADC1 só alcança os canais 0-7 (pinos PA0-PA7)** — canais 8+ (PB0/PB1/
> PC0-PC5) pertencem ao mux físico do **ADC2**, que o driver custom
> `adc_ch32v307` (único suportado, ver seção ADC1 abaixo) não acessa. Por
> isso o HX711 segue na **Porta E** (PE2-PE13, ainda placeholder), deixando
> PA0-PA7 livres para ADC1/DAC/bancada de ensaio.

---

## Pinagem real (atualização 2026-06-07 — JÁ APLICADA)

Repassada pelo usuário a partir do esquemático (KiCad) real da placa e
**já refletida no `.dts`/`HAL.c`/`HAL.h`** (tabela "geral" acima já
atualizada). Mantida aqui como registro do levantamento original do
esquemático e da decisão de design para cada item.

| Pino      | Função real (esquemático)                          | Observações |
|-----------|----------------------------------------------------|-------------|
| PA0       | Tensão da bateria (`v_batt`)                       | ADC1 CH0 |
| PA1       | Corrente da bateria (`i_batt`)                     | ADC1 CH1 |
| PA2       | Tubo de Pitot (`air_speed`)                        | ADC1 CH2 |
| PA3       | Ensaio_Bateria_Voltage                             | ADC1 CH3 — canal de bancada de ensaio de bateria (não confundir com `v_batt`/PA0) |
| PA4       | Ensaio_Bateria_DAC                                 | ADC1 CH4 |
| PA5       | DAC (saída analógica)                              | periférico DAC do CH32V307 — sem uso em `HAL.c` ainda |
| PA6       | Ensaio_Bateria_Termistor_1                         | ADC1 CH6 |
| PA7       | Ensaio_Bateria_Termistor_2                         | ADC1 CH7 |
| PA8       | Ensaio_Bateria_Corrente                            | ⚠ **fora do range PA0-PA7 do ADC1** — reservado/ocupado no `.dts` (`pa8_reserved`, `siba,gpio-array`), sem leitura implementada (ver `HAL_RESERVED_PA8_IDX`) |
| PA9       | USART1 TX (console/shell)                          | mantido — já é o padrão |
| PA10      | USART1 RX (console/shell)                          | mantido — já é o padrão |
| PB6       | PWM do ESC (TIM4 CH1)                              | mantido |
| PB8       | I2C1 SCL — barramento remapeado (remap=1)          | driver `wch,i2c` já existente no Zephyr, instanciado via `i2c1_default`/`I2C1_SCL_PB8_1`. Infra crua: `HAL_I2C_Read/Write/Scan` + `siba i2c scan/read/write` |
| PB9       | I2C1 SDA — barramento remapeado (remap=1)          | idem PB8 (`I2C1_SDA_PB9_1`) |
| PB11-PB14 | Relés (genéricos)                                  | GPIO saída (`relays_generic`) — função/uso individual ainda não detalhado, reservados |
| PB15      | Relé da bateria (`relay0`)                         | GPIO saída (`relay_batt`) — alias `relay0` já definido no `.dts`; `Battery_Manager_Set_Relay()` agora tem efeito real |
| PD2       | Comando "iniciar ensaio"                           | usuário aperta um botão físico → sinal PD2 entra numa porta lógica AND física (junto com outros sinais de segurança) → habilita o SSR de 100A DC da bateria. Ver hierarchical_label `MCU_Sig_Ready` no esquemático — o **MCU também sinaliza "pronto"** de volta para essa lógica AND via PE15 (ver abaixo). PD2 reservado como entrada (`gpio_pd2_and_out`) para feedback futuro |
| PC6-PC9   | Ethernet (MAC interno) — RXP/RXN/TXP/TXN           | **mantido, sem alteração** (ver seção Ethernet abaixo) |

> **PA8 ("Ensaio_Bateria_Corrente")**: confirmado que está fora do range
> ADC1 (PA0-PA7) e do mux do ADC2 (PB0/PB1/PC0-PC5) — função exata ainda não
> usada. Reservado no `.dts` (`pa8_reserved`) + slot de índice em `HAL.h`
> (`HAL_RESERVED_PA8_IDX`), sem leitura implementada.
>
> **Nomenclatura "Ensaio_Bateria_*"**: canais de uma **bancada de ensaio de
> bateria** (testes de carga/descarga controlados), distintos dos sensores
> de voo (`v_batt`/`i_batt`/`temperature_battery`/`air_speed` do
> `TELEMETRY_LOG`). Por decisão de design, **ficam fora do `TELEMETRY_LOG`
> por enquanto** — PA3/PA4/PA7 documentados aqui mas sem nó funcional/leitura
> no `.dts`/`HAL.c`. `temperature_battery` (PA6/CH6) tem **uso duplo**:
> mesmo circuito físico de `Ensaio_Bateria_Termistor_1`, mas já entra no
> `TELEMETRY_LOG` via ADC1 CH6.

### Itens resolvidos nesta migração
- [x] PA8 confirmado fora do range ADC1/ADC2 → reservado (`pa8_reserved`),
      sem leitura implementada
- [x] Canais "Ensaio_Bateria_*" (PA3/PA4/PA7) documentados, fora do
      `TELEMETRY_LOG`/`SIBA.yaml` por enquanto — sem nó funcional
- [x] `adc1_default` (pinctrl) e `adc_ch_cfg[]`/`HAL_ADC_IDX_*`/`ADC_CH_*`
      atualizados em `.dts`/`HAL.c`/`HAL.h` (CH0/CH1/CH2/CH6 → PA0/PA1/PA2/PA6)
- [x] `aliases { relay0 = &relay_batt; }` (PB15) adicionado ao `.dts` +
      nós GPIO `relays_generic` para PB11-PB14 (reservados)
- [x] I2C1 remapeado (PB8/PB9, remap=1) habilitado via `wch,i2c` +
      `i2c1_default`; `HAL_I2C_Init/Read/Write/Scan` + `siba i2c scan/read/write`
- [x] Nó GPIO `gpio_pd2_and_out` (PD2) reservado para feedback futuro;
      uso duplo de PE15 (`led0`/`MCU_Sig_Ready`) documentado
- [x] DAC (PA5/DAC_OUT1) implementado via MMIO direto (`HAL_DAC_*`) +
      `siba dac <valor>` — ver seção "DAC1" abaixo

---

## Detalhe por periférico

### USART1 — Console / Shell / Log
- TX = PA9, RX = PA10, 115200 8N1
- `chosen { zephyr,console = &usart1; zephyr,shell-uart = &usart1; }`
- Usado para log do sistema (`LOG_INF`/`LOG_ERR` etc.) e é o canal planejado para
  o **shell de teste simplificado** (ver TODO em `CLAUDE.md`).

### ADC1 — scan circular multicanal (4 canais) via DMA
- Driver custom out-of-tree: `drivers/adc/adc_ch32v307.c` (MMIO direto, DMA circular)
- Binding DTS: `compatible = "wch,adc"` (`dts/bindings/adc/wch,adc.yaml`)
- Resolução 12 bits, oversample 8x (média), `ADC_NUM_CHANNELS = 4` (`HAL_ADC_IDX_*` em `HAL.h`)
- O driver já fazia DMA circular desde o início — a mudança nesta rodada foi
  puramente de configuração no `HAL.c` (canais em `adc_ch_cfg[]`/`adc_seq`)
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

| idx | Canal ADC1 | Pino | Grandeza             | Conversão (`HAL.c`)              |
|-----|------------|------|----------------------|-----------------------------------|
| 0   | CH0        | PA0  | `v_batt`             | `HAL_ADC_Get_VBatt()`             |
| 1   | CH1        | PA1  | `i_batt`             | `HAL_ADC_Get_IBatt()`             |
| 2   | CH2        | PA2  | `air_speed` (Pitot)  | `HAL_ADC_Get_Air_Speed()`         |
| 3   | CH6        | PA6  | `temperature_battery`| `HAL_ADC_Get_Temperature_Batt()`  |

(CH3/CH4/CH5/CH7 — PA3/PA4/PA5/PA7 — não entram no scan do `adc1`: PA3/PA4/PA7
são canais "Ensaio_Bateria_*" da bancada, fora do `TELEMETRY_LOG` por
enquanto; PA5 é usado pelo DAC1 — ver seções abaixo.)

- **Calibração genérica `y = linear*x + offset`** — todas as 4 grandezas
  (`v_batt`/`i_batt`/`temperature_battery`/`air_speed`) usam **o mesmo padrão
  linear** do HX711 (`adc_calib[]`/`hal_adc_apply_calib` em `HAL.c`), aplicado
  sobre a tensão bruta do pino em mV (`x`). **Não há mais fórmulas físicas
  embutidas** (divisor resistivo, ACS758, NTC, MPXV7002DP/Bernoulli — removidas
  junto com `HAL_VBATT_DIVIDER_*`/`HAL_ACS758_*`/`hal_ntc_lut[]`/
  `HAL_MPXV7002DP_VS_MV`/`HAL_AIR_DENSITY_KG_M3`/`hal_sqrtf()`). A calibração
  (`linear`/`offset` por sensor) é ajustada via CMD/shell
  (`siba sensor <id> linear|offset <valor>`) com os valores reais medidos na
  bancada — ver `HAL_ADC_Set_Calib`/`HAL_ADC_Get_Calib` e
  `CMD_Core::handle_func_sensor`.
- Em `native_sim`, `hal_adc_stimulus_thread_fn` alimenta o `adc_emul` com
  valores de mV sintéticos diretamente (sem inverter fórmula física nenhuma)
  — ver `boards/native_sim.overlay`.

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

### I2C1 — remapeado PB8(SCL)/PB9(SDA), remap=1
- Driver: `wch,i2c` (já existente in-tree no Zephyr, `drivers/i2c/i2c_wch.c`) —
  só precisou ser habilitado e apontado para o pinctrl remapeado
- Pinctrl: `i2c1_default` → `I2C1_SCL_PB8_1` / `I2C1_SDA_PB9_1`
  (`bias-pull-up` + `drive-open-drain` em ambos)
- `clock-frequency = I2C_BITRATE_STANDARD` (100 kHz)
- Infra **mínima e crua** — sem decisão de protocolo, vai substituir o HX711
  no futuro:
  - `HAL_I2C_Init()` — verifica `device_is_ready()`
  - `HAL_I2C_Read(addr, buf, len)` / `HAL_I2C_Write(addr, buf, len)` —
    `i2c_read`/`i2c_write` genéricos do Zephyr
  - `HAL_I2C_Scan(found_addrs, max, out_count)` — varre
    `HAL_I2C_SCAN_ADDR_MIN..HAL_I2C_SCAN_ADDR_MAX` (0x03-0x77)
  - Shell: `siba i2c scan` / `siba i2c read <addr> <len>` / `siba i2c write
    <addr> <byte0> [byte1...]`
- Em `native_sim`: `i2c1` é `zephyr,i2c-emul-controller`
  (`boards/native_sim.overlay`) — sem dispositivos no barramento, então
  `scan` retorna 0 e `read`/`write` retornam `-EIO` (comportamento esperado,
  confirma que a infra não trava/crasha)

### DAC1 — canal 1 (PA5/DAC_OUT1), MMIO direto
- Sem driver/binding Zephyr para o DAC do CH32V307 — acesso via MMIO bruto
  (mesmo padrão do RPM/TIM3), registradores em `DAC_BASE = 0x40007400`
  (`DAC_CTLR`, `DAC_R12BDHR1`), clock habilitado via `RCC_APB1PCENR` bit 29
- `HAL_DAC_Init()` — habilita clock, `EN1=1`/`BOFF1=0` (buffer de saída ligado)
- `HAL_DAC_Set_Value(uint16_t value_12bit)` — clamp 0-4095, grava em `DAC_R12BDHR1`
- Shell: `siba dac <0-4095>`
- ⚠ Configuração do GPIO de PA5 para modo "entrada analógica" (necessária
  para o DAC dirigir o pino) ainda precisa ser verificada/ajustada na bancada
- Em `native_sim`: substituto synthetic guarda o valor numa variável
  (`hal_sim_dac_value`), sem periférico real — `siba dac <valor>` imprime
  `DAC: <valor> / 4095`

### Ethernet (MAC interno CH32V307)
- Pinos fixos do MAC interno: PC6 (RXP), PC7 (RXN), PC8 (TXP), PC9 (TXN) — `mac_default`
- PHY interno (`phy-connection-type = "internal"`), sem pinos MDIO externos
- IP board: `169.254.97.100` / IP PC: `169.254.97.139` — link-local, sem DHCP
  (definido em `prj.conf`: `CONFIG_NET_CONFIG_MY_IPV4_ADDR` / `CONFIG_NET_CONFIG_PEER_IPV4_ADDR`)

### LED / `MCU_Sig_Ready` — PE15 (`ready_pin`, alias `led0`)
- `compatible = "gpio-leds"`, ativo em nível alto
- **Uso duplo**: indicador de "Sistema pronto" (LED) **e** sinal lógico
  `MCU_Sig_Ready` que alimenta a porta AND física — ver PD2 abaixo
- Sem nó dedicado para `MCU_Sig_Ready` (é o mesmo pino físico que `led0`)

### Relés — PB11-PB15
- `relay_batt` (PB15) — relé de potência (bateria), `aliases { relay0 =
  &relay_batt; }` já definido no `.dts`. `Battery_Manager.c` usa
  `DT_ALIAS(relay0)` + `GPIO_DT_SPEC_GET_OR(..., {0})` —
  `Battery_Manager_Set_Relay()` agora tem efeito real na placa
- `relays_generic` (PB11-PB14) — saídas GPIO reservadas, função individual
  ainda não detalhada no esquemático

### PD2 — feedback do SSR (porta AND)
- `gpio_pd2_and_out` (`siba,gpio-array`) — entrada reservada para o MCU
  eventualmente ler de volta o sinal que habilita o SSR de 100A (saída da
  porta AND que combina `MCU_Sig_Ready`/PE15 com outra condição de
  segurança). Sem lógica de leitura implementada ainda.

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
Ethernet físico. Em vez de manter um `HAL` paralelo (abordagem antiga,
`HAL_mock.c` — removido), **`HAL.c` roda nos dois alvos sem duplicação**:
- **ADC** — substituído por driver: o emulador in-tree `zephyr,adc-emul`
  (`CONFIG_ADC_EMUL`) é instanciado sob o MESMO nodelabel `adc1`
  (`boards/native_sim.overlay`), então `HAL.c` enxerga "um adc1" pela API
  genérica do Zephyr (`adc_channel_setup`/`adc_read_async`) sem alteração —
  uma thread `hal_adc_stimulus_thread_fn` (só registrada em native_sim)
  alimenta o emulador com valores em mV, invertendo as mesmas fórmulas
  físicas que `HAL_ADC_Get_*` usa, então o pipeline ADC bruto → conversão →
  calibração roda de verdade.
- **ESC PWM / RPM / HX711** — sem substituto in-tree viável em firmware de
  produção (`zephyr,fake-pwm` depende do framework de teste FFF —
  `subsys/testsuite/include/zephyr/fff.h`, inexistente fora de builds
  Twister/ztest; RPM usa MMIO bruto em endereço físico fixo 0x40000400; o
  HX711 referencia nós DT que não existem em `native_sim`). Esses três
  ganham, **dentro do próprio `HAL.c`**, pequenos blocos isolados
  `#ifdef CONFIG_ARCH_POSIX` com modelos synthetic equivalentes ao do antigo
  `HAL_mock.c` (RPM persegue o throttle do ESC; corrente/empuxo seguem RPM)
  — só essas três peças realmente não têm como rodar "sem mudança nenhuma"
  em POSIX.

O resto do firmware (System_Manager/Sys, Battery/Propeller/Security/
Log_Manager, CMD_Core, Test_Shell) roda **inalterado e idêntico** nos dois
alvos — esse é o objetivo do "driver-mock": zero código duplicado.

`NCI.c`, por outro lado, **não é trocado** — `boards/native_sim.conf` liga
os "Offloaded sockets" do Native Simulator (NSOS), que repassam
`zsock_socket/bind/sendto/recvfrom` direto para os sockets BSD reais do
host Linux. Ou seja, **a rede UDP é real**: o binário escuta `CMD` em
`127.0.0.1:5000` e manda `TELEMETRY_LOG` para `127.0.0.1:5555` — mesmo
protocolo e portas da placa física, só que em loopback. Ver CLAUDE.md
"Modo Mock (native_sim)" e `Aplicacao_Pi/CLAUDE.md`.

Tabela de equivalência "o que vira o quê" no modo native_sim — `HAL.c` é o
MESMO arquivo nos dois alvos; só o que muda é descrito abaixo:

| Real (`HW.md`)                          | native_sim (driver-mock — dentro do mesmo `HAL.c`)        |
|------------------------------------------|-----------------------------------------------------------|
| ADC1 — driver custom `adc_ch32v307` (MMIO+DMA) | `zephyr,adc-emul` sob nodelabel `adc1`, `nchannels=7` (`native_sim.overlay`) — `HAL.c` usa a API genérica `adc_channel_setup`/`adc_read_async` sem mudança; `hal_adc_stimulus_thread_fn` alimenta o emulador com valores de mV sintéticos diretamente (sem fórmula física — `v_batt` drena por Coulomb count, `i_batt` ∝ throttle², `temperature_battery` segue corrente, `air_speed` ∝ RPM, todos com ruído), passando pelo mesmo `adc_calib[]`/`hal_adc_apply_calib` reais |
| HX711 x6 (bit-bang GPIO, Porta E)        | `#ifdef CONFIG_ARCH_POSIX` em `hx711_read_one`: sintetiza o "raw" 24 bits direto (modelo de empuxo ∝ RPM² + ruído) — `HAL_thread_HX711_fn`/`HAL_HX711_Get_*`/`Set_Calib`/`Tare` continuam os mesmos, só a leitura do transdutor muda |
| ESC PWM — TIM4 CH1 (PB6)                 | `#ifdef CONFIG_ARCH_POSIX` em `HAL_ESC_*`: throttle armazenado em `hal_sim_throttle_x100` (RAM) — sem PWM real; o RPM sintético persegue esse throttle |
| RPM Counter — TIM3 CH3 (PB0)             | `#ifdef CONFIG_ARCH_POSIX` em `HAL_RPM_*`/`HAL_thread_RPM_fn`: RPM sintético persegue o throttle do ESC com resposta de 1ª ordem + ruído |
| I2C1 — `wch,i2c` remapeado (PB8/PB9)     | `zephyr,i2c-emul-controller` sob nodelabel `i2c1` (`native_sim.overlay`, `CONFIG_I2C_EMUL`) — `HAL_I2C_Init/Read/Write/Scan` rodam sem mudança; sem dispositivos no barramento emulado, `scan`→0 e `read`/`write`→`-EIO` |
| DAC1 canal 1 (PA5/DAC_OUT1, MMIO direto) | `#ifdef CONFIG_ARCH_POSIX` em `HAL_DAC_*`: valor guardado em `hal_sim_dac_value` (RAM), sem periférico/pino real |
| Ethernet (MAC interno, IP link-local 169.254.x.x) | rede REAL via NSOS em loopback (`127.0.0.1:5000`/`:5555`) — mesmo `NCI.c`, mesmo protocolo `CMD`/`TELEMETRY_LOG`; só o endereço muda (link-local físico → loopback do host). `Aplicacao_Pi` se conecta normalmente |
| Relé de potência (`relay0` = PB15 no `.dts` real) | alias `relay0` não existe no `.dts` do `native_sim` → `Battery_Manager` cai no fallback "relé GPIO não disponível" (mesmo comportamento de antes) |
| LED `ready_pin`/`led0`                   | `CONFIG_GPIO`/`gpio_emul` ligados, mas sem alias `led0` no `.dts` do `native_sim` — sem efeito visível |
| USART1 console/shell                     | console/shell do `native_sim` rodam sobre um pseudo-TTY do host (`uart connected to pseudotty: /dev/pts/N`) — os comandos `siba ...` funcionam normalmente |

> Por que essa divisão e não um `HAL_mock.c` paralelo: ADC/I2C têm
> emuladores in-tree genéricos e utilizáveis em firmware de produção
> (`adc_emul`/`i2c-emul-controller`) — então vira troca de **driver**, zero
> duplicação de lógica. PWM/Counter/HX711/DAC não têm equivalente in-tree
> viável (ver motivos na tabela), então ganham um "miolo" sintético mínimo
> isolado por `#ifdef`, mantendo a API pública e o fluxo OSSI idênticos — o
> oposto de reescrever o HAL inteiro.

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
