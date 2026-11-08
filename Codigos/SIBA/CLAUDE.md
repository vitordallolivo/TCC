# SIBA — Contexto do Projeto para Claude Code

## Visão Geral

SIBA (Sistema Integrado da Bancada Aerodinâmica) é o firmware de uma placa customizada
baseada no **CH32V307VTC6** (RISC-V, 144 MHz) rodando **Zephyr RTOS v4.4.0**.

A placa mede parâmetros aerodinâmicos (6 células de carga HX711, pitot, RPM, temperatura
motor) e gerencia a interface com bateria LiPo (ADC de tensão/corrente, relay com
precharge de 3 s). Comunica com PC via UDP/Ethernet (10 Hz telemetria), e tem shell de
diagnóstico via UART (`Test_Shell.c`).

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

**Status atual:** firmware compila e builda (`make build` / `west build -b siba_board`).
ETH: IF_UP dispara, TX ainda sem frames no fio (diagnóstico em andamento).
NVM/EEPROM funciona. Shell UART funciona. Validação em bancada em andamento.

> **Pinagem:** `boards/wch/siba_board/siba_board.dts`.
> Em caso de divergência com este arquivo, o DTS manda.
---

## Estrutura de Diretórios

```
SIBA/
├── boards/wch/siba_board/
│   ├── siba_board.dts          # Pinagem, periféricos, clock
│   └── siba_board_defconfig    # Kconfig padrão da board
├── drivers/
│   └── adc/
│       └── adc_ch32v307.c      # Driver ADC custom (MMIO+DMA, dual-simultâneo)
├── dts/bindings/
│   ├── gpio/siba,gpio-array.yaml
│   └── pwm/siba,pwm-consumer.yaml
├── lib/
│   ├── OS/
│   │   ├── OSSI.c/h             # siba_wq (work queue única)
│   │   ├── NCI.c/h              # UDP TX/RX + LED de status de rede (PE15)
│   │   ├── HAL.c/h              # ADC dual, HX711 x6, ESC PWM, RPM, I2C2, Relay
│   │   ├── HAL_prm.h            # flags de habilitação por módulo
│   │   ├── CMD_Core.c/h         # Parser/dispatcher de comandos binários
│   │   ├── CMD_Core_prm.h       # tabela de calibração/faixa (AUTO-GERADO)
│   │   ├── CMD.h                # AUTO-GERADO por generate.py
│   │   └── Common_Types.h       # AUTO-GERADO por generate.py
│   └── SYS/
│       ├── Battery_Manager.c/h
│       ├── Propeller_Manager.c/h
│       ├── Security_Manager.c/h
│       ├── Log_Manager.c/h      # Log_Manager.h AUTO-GERADO
│       ├── Test_Shell.c/h
│       ├── NVM.c/h              # Calibração persistente em EEPROM I2C2 (0x50)
│       └── Error_Codes.h        # AUTO-GERADO
├── src/
│   └── main.c
├── CMakeLists.txt
├── prj.conf
└── Makefile
```

---

## Hardware — Periféricos e Pinos

MCU: **CH32V307VTC6** (RISC-V, 144 MHz).

### Tabela geral de pinos

| Pino | Periférico | Função | Nó DTS |
|------|-----------|--------|--------|
| PA0 | ADC1 CH0 | air_speed (Pitot) | `adc1` group1 |
| PA1 | ADC1 CH1 | temperature_battery (termistor bateria) | `adc1` group2 |
| PA2 | (SPI1 CS) | reserva W25Q — SPI1 desligado | `spi1` (disabled) |
| PA3 | ADC1 CH3 | temperature_motor (termistor motor) | `adc1` group3 |
| PA5 | (SPI1 SCK) | reserva W25Q — SPI1 desligado | `spi1` (disabled) |
| PA6 | (SPI1 MISO) | reserva W25Q — SPI1 desligado | `spi1` (disabled) |
| PA7 | (SPI1 MOSI) | reserva W25Q — SPI1 desligado | `spi1` (disabled) |
| PA9 | USART1 TX | Console / shell / log | `usart1` |
| PA10 | USART1 RX | Console / shell / log | `usart1` |
| PA15 | GPIO (saída) | LED vermelho de status (ativa alta) | `led_red` |
| PB0 | TIM3 CH3 (capture) | Contador de pulsos RPM (sem pull) | `rpm_capture_pin` |
| PB4 | GPIO (saída) | LED verde de status (ativa alta) | `led_green` |
| PB6 | TIM4 CH1 (PWM) | Sinal ESC (50 Hz, 1000-2000 µs) | `pwm4` / `esc_motor` |
| PB10 | I2C2 SCL | I2C2 — EEPROM FT24C32A (0x50) | `i2c2` |
| PB11 | I2C2 SDA | I2C2 — EEPROM FT24C32A (0x50) | `i2c2` |
| PB12 | GPIO (saída) | Relay principal de potência — **invertido** (0 = liga) | `relay_power` |
| PB13 | GPIO (saída) | Relay de pré-carga — **invertido** (0 = liga) | `relay_precharge` |
| PC0 | reservado | PHY LINK LED interno — não usar como ADC | — |
| PC1 | reservado | PHY ACT LED interno — não usar como ADC | — |
| PC2 | ADC2 CH12 | spare (não mapeado no TELEMETRY_LOG) | `adc2` group3 |
| PC3 | ADC2 CH13 | v_batt (tensão bateria) | `adc2` group1 |
| PC4 | ADC2 CH14 | i_batt (corrente bateria) | `adc2` group2 |
| PC6-PC9 | ETH (MAC) | RXP/RXN/TXP/TXN | `mac` |
| PD1 | GPIO (entrada) | Sinal de emergência: 0 V = desliga, 3 V = liga (pull-down) | `emergency_button` |
| PE0-PE5 | GPIO (saída) | HX711[0-5] PD_SCK | `hx711_sck_pins` |
| PE6-PE11 | GPIO (entrada) | HX711[0-5] DOUT (sem pull) | `hx711_dout_pins` |
| PE15 | GPIO (saída) | LED "rede online" / MCU_Sig_Ready | `led0` (alias) |

### ADC — dual simultâneo via DMA

> ⚠ **Desativado nesta versão da placa** (circuitos analógicos não montados
> por tempo de montagem): `#define HAL_DISABLE_ADC` em `lib/OS/HAL_prm.h`,
> vale para HW e native_sim. v_batt/i_batt/temperaturas/air_speed saem 0 na
> telemetria e o `Security_Manager` não checa a faixa da bateria. A
> descrição abaixo vale quando for religado.

Driver out-of-tree `drivers/adc/adc_ch32v307.c` (MMIO direto, DMA circular,
12 bits). ADC1 é master (com DMA), ADC2 é slave simultâneo. Cada word DMA
entrega `[31:16]=ADC2, [15:0]=ADC1`.

**ADC1** (scan order: CH0, CH1, CH3):

| idx | Canal | Pino | Grandeza | SENSOR_ID |
|-----|-------|------|----------|-----------|
| 0 | CH0 | PA0 | air_speed | `SENSOR_ID_AIR_SPEED` (10) |
| 1 | CH1 | PA1 | temperature_battery | `SENSOR_ID_TEMPERATURE_BATTERY` (9) |
| 2 | CH3 | PA3 | temperature_motor | `SENSOR_ID_TEMPERATURE_MOTOR` (12) |

**ADC2** (scan order: CH13, CH14, CH12):

| idx | Canal | Pino | Grandeza | SENSOR_ID |
|-----|-------|------|----------|-----------|
| 0 | CH13 | PC3 | v_batt | `SENSOR_ID_V_BATT` (6) |
| 1 | CH14 | PC4 | i_batt | `SENSOR_ID_I_BATT` (7) |
| 2 | CH12 | PC2 | spare | — (não submetido) |

**Calibração ADC:**
- Afim para todos: `y = Linear_Coeff * mV + Offset_Coeff`, exceto:
- `AIR_SPEED` (Pitot): `v = Linear_Coeff * sqrt(mV - Offset_Coeff)` — lei de Bernoulli.
  Padrão (calibrado em túnel IPT): Linear=1.691423, Offset=2664.4 mV.

> **PC0/PC1 são pinos de LED do PHY interno** — reservados, nunca usar como ADC.
> O driver upstream `adc_ch32v00x.c` também usa `DT_DRV_COMPAT "wch,adc"` —
> desabilitado com `CONFIG_ADC_CH32V00X=n` em `prj.conf`.

### HX711 x6 — Células de carga (bit-bang GPIO, Porta E)

Protocolo serial síncrono manual, ganho 128x (canal A, 25 pulsos SCK).
`grams = (raw - tare) / scale`.

- SCK: PE0..PE5 (saídas)
- DOUT: PE6..PE11 (entradas)

### ESC — TIM4 CH1 (PB6)

50 Hz, pulso 1000 µs (arm/mín) — 2000 µs (máx). `HAL_ESC_Arm/Disarm/Set_Throttle`.

### RPM Counter — TIM3 CH3 (PB0)

Input capture, janela 100 ms. `(pulsos / 14) * 600 = RPM`. MMIO direto (sem driver Zephyr).

### I2C2 — PB10 (SCL) / PB11 (SDA)

100 kHz. Conectado à EEPROM FT24C32A-ETR-T (4 KB, endereço 0x50).
`HAL_I2C_Init/Read/Write/Scan` — infra crua sem protocolo embutido.

> **Aviso upstream:** `wch_i2c_finish_transfer` faz busy-wait infinito em
> `STAR2 & I2C_STAR2_BUSY` se o barramento travar. Se o boot travar no I2C,
> fazer power cycle na placa para liberar o barramento.

### Relay — PB12 (power) / PB13 (precharge)

Saídas **invertidas** (0 no pino = relé fechado), declaradas como
`GPIO_ACTIVE_LOW` no DTS — o HAL segue escrevendo 1 para fechar.

Sequência obrigatória:
- **Energizar:** PB13 ON → `k_sleep(3000 ms)` → PB12 ON
- **Desenergizar:** PB12 OFF → PB13 OFF (sem delay)

`HAL_Relay_Energize()` bloqueia a thread chamadora durante os 3 s.
Ambos abertos no boot. Relay só fechado via CMD/shell.

### ETH — MAC interno CH32V307

IP board: `169.254.97.51` / IP PC: `169.254.255.255` (broadcast), sem DHCP.
PLL3 configurado pelo próprio `eth_wch_init` upstream ao detectar
`phy-connection-type = "internal"` no DTS. O `phy_pll3_preinit.c` foi
removido — era tentativa de fix sem efeito.
**TX morto:** IF_UP dispara mas nenhum frame sai do fio. Diagnóstico ativo
(`CONFIG_ETHERNET_LOG_LEVEL=4`, `printk` em NCI_Transmit para ver retorno de sendto).

### LED status de rede — PE15

`NCI.c` controla PE15 via callback `NET_EVENT_IF_UP/DOWN`:
- Link UP → PE15 HIGH
- Link DOWN → PE15 LOW

### Emergência — PD1

O botão de emergência fica no caminho de 12 V que alimenta as bobinas dos
relés (12 V → botão → relés) — o corte é **físico**, sem porta AND nem
participação do micro. O PD1 só **lê** a saída do botão através de um divisor
12 V → 3 V: **0 V = desliga (apertado), 3 V = liga**. O PE15 virou apenas LED.
DTS `GPIO_ACTIVE_LOW` (o nó descreve a *emergência*), pull-down interno — fio
solto cai em emergência. `HAL_Emergency_Is_Pressed()` → 1 quando o pino está
em 0 V; o `Security_Manager` lê a cada despacho de sensor (HX711/RPM, já que o
ADC está desligado), dispara a parada e a libera quando o sinal volta a 3 V.

### SPI1 / W25Q Flash — PA5/PA6/PA7/PA2

**PA5 é SPI1 SCK** (não é DAC1 — DAC1 foi removido do design).
W25Q disponível no DTS mas sem uso no firmware atual.

---

## Arquitetura de Software

```
main.c → ordem de init
         ↓
Managers (Battery, Propeller, Security, Log) — sem thread própria
         ↓
OS (OSSI: siba_wq, NCI: UDP, HAL: periféricos, CMD_Core: parser)
         ↓
HW (CH32V307, driver ADC, GPIO, PWM, TIM3, I2C2, ETH)
```

### Threads do sistema

| Thread | Prioridade | Tipo | Função |
|--------|-----------|------|--------|
| `hal_hx711` | 6 | `k_thread_create` | Bit-bang das 6 células, `OSSI_Submit_Sensor` |
| `nci_rx` | 5 | `k_thread_create` | `recvfrom()` bloqueante porta 5000, `OSSI_Submit_CMD` |
| `siba_wq` | 5 | `k_work_q` | Despacha sensor/CMD/log_tx works |
| ADC ISR | — | ISR (DMA) + k_work | Callback do driver, media 1500 frames, `OSSI_Submit_Sensor` |
| RPM | — | ISR (TIM3) + k_timer 100ms | Conta pulsos, `OSSI_Submit_Sensor` |
| Log TX | — | k_timer 100ms | Dispara `log_tx_work` na `siba_wq` |

### Ordem de init (main.c)

```
OSSI_Init()
Security_Manager_Init()
Battery_Manager_Init()
Propeller_Manager_Init()
Log_Manager_Init()
CMD_Core_Init()
NCI_Init()
HAL_Init()             ← cria thread hal_hx711, arranca ADC ISR, RPM timer
NVM_Init(i2c2, 0x50)  ← restaura calibração do EEPROM
```

### Sensor IDs (SENSOR_ID_*)

| ID | Nome | Origem |
|----|------|--------|
| 0-5 | LOAD_CELL_0..5 | HX711 thread |
| 6 | V_BATT | ADC2 CH13/PC3 |
| 7 | I_BATT | ADC2 CH14/PC4 |
| 8 | SOC | Battery_Manager |
| 9 | TEMPERATURE_BATTERY | ADC1 CH1/PA1 |
| 10 | AIR_SPEED | ADC1 CH0/PA0 |
| 11 | RPM | TIM3 CH3/PB0 |
| 12 | TEMPERATURE_MOTOR | ADC1 CH3/PA3 |

### Protocolo UDP

**TX Board→PC:** porta 5555, `TELEMETRY_LOG` packed (little-endian), 10 Hz, 64 bytes.
**RX PC→Board:** porta 5000, `CMD` packed (little-endian), 8 bytes.

`CMD`: `func_type(1B) | instruct_type(1B) | target_id(1B) | value(4B float) | action_or_status(1B)`

### Segurança — error_code (bitmask uint32_t)

| Bit | Nome | Origem |
|-----|------|--------|
| 0 | BATT_LOW_SOC | Battery_Manager |
| 1 | BATT_POLY_MISCONFIGURED | Battery_Manager |
| 2 | BATT_LOW_VOLTAGE | Battery_Manager |
| 3 | BATT_RELAY_MISMATCH | Battery_Manager |
| 4 | BATT_OVER_TEMPERATURE | Battery_Manager |
| 8 | PROP_LOAD_CELL_NOT_CONFIGURED | Propeller_Manager |
| 9 | PROP_LOAD_CELL_NOT_CONNECTED | Propeller_Manager |
| 10 | PROP_OVERCURRENT | Propeller_Manager |
| 16 | SEC_VOLTAGE_OUT_OF_RANGE | Security_Manager |
| 17 | SEC_CURRENT_EXCESSIVE | Security_Manager |
| 18 | SEC_RPM_EXCESSIVE | Security_Manager |
| 19 | SEC_EMERGENCY_ACTIVE | Security_Manager |
| 24 | OS_GPIO_INIT_FAILED | HAL |
| 25 | MCU_POWER_READY | Security_Manager (relay aberto ao boot) |

### NVM — calibração persistente (EEPROM I2C2 0x50)

Layout: 32 B header (magic "SIBA", version, write_count, CRC32) + 128 B calibração
(13 sensores × 9 B). `NVM_Init` restaura calibração no boot; `NVM_Save_Calibration`
persiste após ajustes via CMD/shell.

---

## Shell UART (`siba ...`)

USART1 / console Zephyr (115200 8N1). Registrado estaticamente em `Test_Shell.c`.

| Comando | Efeito |
|---------|--------|
| `siba read` | Snapshot do TELEMETRY_LOG |
| `siba sensors` | Lista ADC + HX711 bruto e calibrado |
| `siba error` | Decodifica error_code bit a bit |
| `siba rpm` | RPM atual |
| `siba esc <0-100>` | Throttle via Propeller_Manager |
| `siba arm` / `siba disarm` | HAL_ESC direto (ignora limite de corrente) |
| `siba tare <0-5>` | Tara célula de carga |
| `siba sensor <id> linear\|offset <val>` / `reset` | Calibração afim |
| `siba batt cells <n>` | Número de células (0=auto-detectar) |
| `siba batt relay on\|off` | Relay de potência |
| `siba batt poly <0-4> <val>` | Coeficiente polinômio SOC |
| `siba batt templimit <val>` | Limite de temperatura da bateria |
| `siba prop maxcurrent <amps>` | Limite de corrente do motor |
| `siba i2c scan\|read\|write` | Infra I2C crua |
| `siba dac <0-4095>` | DAC MMIO (legado — PA5 é SPI agora) |
| `siba stop` | Emergency stop |
| `siba reset` | `sys_reboot` |

---

## Modo Mock (native_sim)

```bash
make native_sim      # compila
make native_sim_run  # roda build_native_sim/zephyr/zephyr.exe
```

IP loopback: `127.0.0.200` (distinto da SAME: `127.0.0.100`).

| Periférico real | native_sim |
|----------------|-----------|
| ADC1+2 (MMIO+DMA) | `zephyr,adc-emul` + timer 50ms sintetiza mV |
| HX711 bit-bang | `hx711_read_one` sintético (empuxo ∝ RPM²) |
| ESC PWM (TIM4) | throttle em RAM |
| RPM (TIM3) | RPM persegue throttle (1ª ordem) |
| I2C2 | `zephyr,i2c-emul-controller` |
| ETH | NSOS offloaded sockets, loopback real |
| Relay GPIO | stubs no-op |

`HAL_SIM_NOISE_SCALE` em `HAL_prm.h` controla ruído sintético (0=estável).

---

## Geração de Código (generate.py)

```bash
cd ../Aplicacao_Pi/Config
python generate.py
# Gera: Common_Types.h, CMD.h, Log_Manager.h, CMD_Core_prm.h, Error_Codes.h
```

**Sempre rodar após modificar `SIBA.yaml`.** Nunca editar os `.h` gerados à mão.
A fonte única de sensor defaults/limites/bits de erro é `SIBA.yaml`.

---

## Build e Flash

```bash
make build      # west build -b siba_board
make flash      # openocd WCH via WCH-LinkE
make clean      # build pristine
make menuconfig
```

Toolchain: Zephyr SDK 1.0.1, `riscv64-zephyr-elf-gcc`.

---

## Stacks e Memória

SRAM: 128 KB · Flash: ~168 KB (ROM inclui shell ~33 KB).

Com `CONFIG_FPU_SHARING=y`, cada preempção por thread de maior prioridade (ADC pri=7,
HX711 pri=6) salva 128 B de contexto FPU na stack da thread interrompida.

| Configuração | Valor | Motivo |
|-------------|-------|--------|
| `CONFIG_MAIN_STACK_SIZE` | 8192 | NVM_Init + FPU_SHARING preemptions |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 8192 | ETH carrier_on + ADC ISR |
| `CONFIG_ISR_STACK_SIZE` | 4096 | FPU save (32 regs × 4 B) |
| `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE` | 2048 | Formatação %f |
| `CONFIG_IDLE_STACK_SIZE` | 512 | Evita overflow no fim da RAM |

### `HAL_prm.h` — bissecção de crash

Descomentar `#define HAL_DISABLE_*` para desabilitar módulos individualmente.
Módulo desabilitado vira no-op + stub seguro (getters retornam 0).

---

## Checklist ao alterar hardware/pinagem

1. Edite `boards/wch/siba_board/siba_board.dts` (e bindings se necessário).
2. Atualize a tabela "Hardware — Periféricos e Pinos" neste arquivo.
3. Se afeta array/índice em código, atualize comentários em `HAL.c`/`HAL.h`.
4. `west build -b siba_board -p` (pristine) para confirmar que o DTS compila.

---

## TODO

### Bloqueia validação em bancada

- [ ] **Calibrar sensores ADC** com valores medidos nos circuitos reais:
  `siba sensor 6 linear <val>` (v_batt, divisor resistivo),
  `siba sensor 7 linear <val>` (i_batt, ACS758 ou shunt),
  `siba sensor 9 linear <val>` (temp_batt, NTC),
  `siba sensor 12 linear <val>` (temp_motor, NTC no estator).

### Funcionalidade incompleta

- [ ] `cell_count_lut[]` tem faixas sobrepostas em `Battery_Manager.c` — usar
  `siba batt cells <n>` fixo em bancada enquanto não é corrigido.
- [ ] Propeller_Manager: cálculo Ct/Cp da hélice não implementado.
- [ ] PHY ACT/LINK LEDs (PC0/PC1) não mapeados no pinctrl — LEDs físicos não acendem.
- [ ] `BAT_ACTION_CELL_COUNT_OVERWRITE` via CMD de rede (hoje só via shell).
- [ ] NCI RX: filtrar pacotes pelo IP do PC.

### Melhorias

- [ ] Watchdog (IWDG) para reset automático em travamento.
- [ ] `App.h` — máquina de estados global (IDLE→ARMED→RUNNING→FAULT).
