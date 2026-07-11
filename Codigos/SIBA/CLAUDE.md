# SIBA — Contexto do Projeto para Claude Code

## Visão Geral

SIBA (Sistema Integrado da Bancada Aerodinâmica) é o firmware de uma placa customizada
baseada no **CH32V307VTC6** (RISC-V, 144MHz) rodando **Zephyr RTOS v4.4.0**.

A placa mede parâmetros aerodinâmicos (6 células de carga HX711, pitot, RPM) e
gerencia a interface com bateria LiPo, além de expor I2C/DAC crus para expansão.
Comunica com um PC via UDP/Ethernet, e também tem um shell de diagnóstico via
UART (`Test_Shell.c`, ver seção própria abaixo).

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

**Status atual: o firmware compila e builda** (`make build` / `west build -b
siba_board`), a pinagem real do esquemático já foi aplicada ao `.dts`/`HAL.c`
(ver seção Hardware), e o shell de diagnóstico via UART já está implementado.
O que falta agora é principalmente **validação em bancada** — testar cada
funcionalidade com o hardware físico.

> ⚠ **Fonte de verdade de pinagem**: a seção "Hardware — Periféricos e Pinos"
> deste arquivo é o resumo legível; o `.dts` real
> (`boards/wch/siba_board/siba_board.dts` + `dts/bindings/`) manda em caso de
> divergência. **Sempre que alterar pinagem/periféricos, atualize os dois
> juntos.**

---

## Estrutura de Diretórios

```
SIBA/
├── boards/wch/siba_board/      # Board definition (DTS, Kconfig, cmake)
│   ├── siba_board.dts          # Pinagem, periféricos, clock
│   └── siba_board_defconfig    # Kconfig padrão da board
├── drivers/
│   └── adc/
│       └── adc_ch32v307.c      # Driver ADC custom (MMIO+DMA, multi-instância)
├── dts/bindings/
│   ├── gpio/siba,gpio-array.yaml    # Binding genérico p/ arrays de GPIO (HX711, RPM)
│   └── pwm/siba,pwm-consumer.yaml   # Binding p/ consumidor de PWM nomeado (ESC)
├── lib/
│   ├── Application/
│   │   ├── App.h                # vazio — TODO: máquina de estados global (ver TODO)
│   │   └── main.h               # vazio — não usado
│   ├── OS/
│   │   ├── OSSI.c/h             # siba_wq (fila de trabalho única) + 2 filas pequenas
│   │   ├── TCKEC.c/h            # ARQUIVOS VAZIOS (1 linha cada) — mantidos só p/ não quebrar o CMakeLists
│   │   ├── NCI.c/h              # UDP TX (socket) + thread RX bloqueante
│   │   ├── HAL.c/h              # ADC, HX711 x6, ESC PWM, RPM counter, I2C1, DAC1
│   │   ├── HAL_prm.h            # flags de habilitação por módulo (bissecção de crash)
│   │   ├── CMD_Core.c/h         # Parser/dispatcher de comandos binários (struct CMD)
│   │   ├── CMD_Core_prm.h       # tabela central de calibração/faixa por sensor (SIBA_SENSOR_TABLE)
│   │   ├── CMD.h                # AUTO-GERADO por generate.py
│   │   └── Common_Types.h       # AUTO-GERADO por generate.py
│   └── SYS/                     # managers — structs + funções, SEM thread própria
│       ├── Sys.c/h               # ARQUIVOS VAZIOS (1 linha cada) — despacho migrou p/ OSSI.c
│       ├── Battery_Manager.c/h   # SOC polinomial, auto-detecção de nº de células, relé, limites
│       ├── Propeller_Manager.c/h # Protege motor/ESC (nunca deixa i_batt > limite) + calibração das células
│       ├── Security_Manager.c/h  # Código de erro agregado (bitmask) + emergency stop
│       ├── Log_Manager.c/h       # Monta TELEMETRY_LOG, TX periódico via NCI
│       ├── Test_Shell.c/h        # Comandos de diagnóstico via Zephyr Shell (UART)
│       └── LoadCell_Manager.h    # vazio — TODO: calibração persistente
├── src/
│   └── main.c                   # Ponto de entrada, inicializa tudo em ordem
├── CMakeLists.txt
├── prj.conf
└── Makefile                     # Atalhos: make build / flash / clean / menuconfig / native_sim
```

---

## Arquitetura de Software

Nenhuma thread fala diretamente com outra: sensores e comandos de rede passam
por uma **fila de trabalho única, `siba_wq`** (`k_work_q` do Zephyr,
prioridade 5), que serializa todo o processamento de lógica. Só as threads
que precisam bloquear em hardware/socket continuam como threads Zephyr
normais:

```
Application  ← main.c (ordem de init), Test_Shell.c (comandos via UART)
     ↓
Managers     ← Battery_Manager, Propeller_Manager, Security_Manager,
               Log_Manager — chamados DIRETO pelo despacho da siba_wq
               (sem thread própria, sem mutex entre eles)
     ↓
OS           ← OSSI (siba_wq + 2 filas pequenas), NCI (rede), HAL (hw),
               CMD_Core (parser de CMD)
     ↓
HW           ← CH32V307 + driver ADC custom + GPIO + PWM(TIM4) + Capture(TIM3)
               + I2C1 + DAC1
```

### Threads do sistema (inventário completo)

| Thread/mecanismo | Prioridade | Tipo | O que faz |
|---|---|---|---|
| `hal_adc` | 7 (mais alta) | `k_thread_create` | Lê ADC1 (scan 4 canais via DMA), converte, chama `OSSI_Submit_Sensor` |
| `hal_hx711` | 6 | `k_thread_create` | Bit-bang das 6 células de carga, chama `OSSI_Submit_Sensor` |
| `nci_rx` | 5 | `k_thread_create` | `recvfrom()` bloqueante na porta 5000, `OSSI_Submit_CMD` |
| `siba_wq` | 5 | `k_work_q` | Único work-queue: roda `sensor_dispatch_fn`, `cmd_dispatch_fn`, `log_tx_work_fn` |
| RPM | — | ISR (TIM3 capture) + `k_timer` 100 ms | Conta pulsos, converte, `OSSI_Submit_Sensor` (não é thread própria) |
| Log TX | — | `k_timer` (`LOG_MANAGER_PERIOD_MS=100`) | Dispara `log_tx_work` na `siba_wq` a cada 100 ms (10 Hz) |

Só **3 threads bloqueantes** + 1 work-queue no sistema inteiro — sem pool de
threads gerenciadas dinamicamente.

### `siba_wq` — o despachante central (`lib/OS/OSSI.c`)

- `OSSI_Submit_Sensor(sensor_id, value)`: põe o item numa `sensor_queue`
  (`k_msgq`, 40 slots × 8 B) e submete `sensor_dispatch_work` à `siba_wq`.
  `sensor_dispatch_fn` drena a fila inteira e, para cada item:
  1. `Log_Manager_Update(id, value)` — **sempre**, grava o campo do
     `TELEMETRY_LOG`;
  2. se `id` ∈ {`V_BATT`, `I_BATT`, `TEMPERATURE_BATTERY`}: `Battery_Manager_Update`;
  3. se `id == I_BATT` ou `id <= LOAD_CELL_5`: `Propeller_Manager_Update`;
  4. `Security_Manager_Check(id, value)` — **sempre**, valida contra a faixa
     central (`CMD_Core_prm.h`) e dispara emergência se necessário.
- `OSSI_Submit_CMD(buf, len)`: põe o pacote numa `cmd_queue` (4 slots × 12 B)
  e submete `cmd_dispatch_work`; `cmd_dispatch_fn` drena e chama
  `CMD_Core_Dispatch` para cada um.
- `sensor_dispatch_fn`/`cmd_dispatch_fn`/`log_tx_work_fn` rodam sempre na
  **mesma thread** (`siba_wq`) — nunca em paralelo entre si, o que elimina
  necessidade de mutex entre managers (só `Log_Manager`/`Propeller_Manager`
  ainda usam mutex interno, por serem lidos também pelo `Test_Shell`/HAL).

### Ordem de inicialização (`main.c`)

```
OSSI_Init()                — arranca a siba_wq
Security_Manager_Init()
Battery_Manager_Init()     — configura GPIO do relé
Propeller_Manager_Init()   — zera mutex/estado
Log_Manager_Init()         — arranca k_timer 100ms (10Hz)
CMD_Core_Init()            — carrega SIBA_SENSOR_TABLE em sensor_cfg[]
NCI_Init()                 — socket TX + thread nci_rx
HAL_Init()                 — ADC/HX711/ESC/RPM/I2C/DAC + threads hal_adc/hal_hx711
NCI_Set_Connection_Status(true)
```
`main()` então retorna; `siba_wq` + as threads bloqueantes seguem sob o
scheduler do Zephyr. `Test_Shell.c` não aparece nesta lista — os comandos
`siba ...` são registrados estaticamente pelo subsistema Shell do Zephyr no
boot, sem chamada explícita.

### Fluxo de dados completo

```
HX711[0..5] ──┐
ADC (4 canais)├─ OSSI_Submit_Sensor(id,val) → sensor_queue → siba_wq:
RPM (ISR)    ─┘     sensor_dispatch_fn:
                       Log_Manager_Update(id,val)                 [sempre]
                       Battery_Manager_Update(id,val)              [v_batt/i_batt/temp]
                       Propeller_Manager_Update(id,val)            [i_batt ou células]
                       Security_Manager_Check(id,val)              [sempre]

Log_Manager (k_timer 100ms) → Log_Manager_Transmit() → NCI_Transmit → UDP:5555 → PC

PC → UDP:5000 → NCI RX thread → OSSI_Submit_CMD → cmd_queue → siba_wq:
                   cmd_dispatch_fn → CMD_Core_Dispatch → handlers (ver tabela abaixo)
```

---

## Protocolo de Comunicação UDP

### TX: Board → PC
- Porta destino: `5555` (PC escuta)
- Payload: `TELEMETRY_LOG` serializado como struct packed (little-endian)
- Rate: 10 Hz (`LOG_MANAGER_PERIOD_MS = 100`, reconfigurável só recompilando)
- Tamanho: `sizeof(TELEMETRY_LOG)` bytes

### RX: PC → Board
- Porta local: `5000` (board escuta)
- Payload: `CMD` serializado como struct packed (little-endian)
- Formato CMD: `func_type(1B) | instruct_type(1B) | target_id(1B) | value(4B float) | action_or_status(1B)`
- Total: 8 bytes

### Structs (AUTO-GERADAS por generate.py a partir de SIBA.yaml)
```c
// CMD.h
typedef struct __attribute__((packed)) {
    func_type      function;        // FUNC_START/STOP/RESET/SENSOR/ESC/BATTERY
    instruct_type  instruction;     // INSTR_READ/WRITE/NONE/DEFAULT
    int8_t         target_id;       // índice do sensor/target
    float          value;           // dado (throttle %, ganho, offset...)
    int8_t         action_or_status;// default_action ou battery_action ou status_type
} CMD;

// Log_Manager.h — também define SENSORS (tabela de calibração) e SENSOR_ID_*
typedef struct __attribute__((packed)) {
    uint32_t   timestamp;           // k_uptime_get_32() em ms
    float      load_cell_0;         // N ou kgf
    float      load_cell_1;
    float      load_cell_2;
    float      load_cell_3;
    float      load_cell_4;
    float      load_cell_5;
    float      v_batt;              // V
    float      i_batt;              // A
    float      soc;                 // 0-100%
    float      temperature_battery; // °C
    float      air_speed;           // m/s (pitot)
    float      rpm;                 // RPM
    log_status status;              // LOG_VALID/INVALID/OVERFLOW/CRITICAL_ALARM/DENIED_ACTION
    uint32_t   error_code;          // bitmask agregado — ver "Segurança" abaixo
} TELEMETRY_LOG;
```

### `target_type` × `SENSOR_ID_*` — já alinhados 1:1
`target_type` (`Common_Types.h`): `TARGET_LOAD_CELL_0..5`(0-5),
`TARGET_VOLTAGE_DIVIDOR`(6), `TARGET_CURRENT_SENSOR`(7),
`TARGET_BATTERY_SOC`(8), `TARGET_TEMPERATURE_SENSOR`(9),
`TARGET_PITOT_TUBE`(10), `TARGET_DEFAULT`(11) — mesma numeração de
`SENSOR_ID_*` (`Log_Manager.h`: `LOAD_CELL_0..5`, `V_BATT`=6, `I_BATT`=7,
`SOC`=8, `TEMPERATURE_BATTERY`=9, `AIR_SPEED`=10, `RPM`=11). `FUNC_ESC` não
usa `target_id` (ignora completamente).

### `CMD_Core` — tabela de dispatch (`lib/OS/CMD_Core.c`)

| `func_type` | Efeito |
|---|---|
| `FUNC_START` | Log informativo — célula de carga/telemetria já rodam desde o boot |
| `FUNC_STOP` | Se `target_id == TARGET_DEFAULT`: `Security_Manager_Trigger_Emergency()` (desarma o ESC) |
| `FUNC_RESET` | `sys_reboot(SYS_REBOOT_COLD)` |
| `FUNC_SENSOR` | Calibração (célula de carga OU sensor ADC) — ver tabela abaixo |
| `FUNC_ESC` | `INSTR_WRITE` → `Propeller_Manager_Request_Throttle(value)` — arma o ESC se preciso e nunca deixa a corrente do motor passar do limite |
| `FUNC_BATTERY` | `battery_action`: `P0..P4_OVERWRITE` (polinômio SOC), `TEMP_LIMIT_OVERWRITE`, `RELAY` |

**`FUNC_SENSOR` — `default_action` × tipo de sensor:**

| `default_action` | Célula de carga (id 0-5) | Sensor ADC (id 6,7,9,10) |
|---|---|---|
| `..._LINEAR_COEF` | `value` = **peso conhecido** → calibra escala (`Propeller_Manager_Calibrate_Cell_Weight`) | `value` = ganho linear (`HAL_ADC_Set_Calib`) |
| `..._OFFSET_COEF` | **tara** (offset = raw atual; `value` ignorado) | `value` = offset afim |
| `..._RESET_DEFAULT` | Restaura scale/tare default + retara | Restaura linear=1.0/offset=0.0 |
| `..._MAX_VALID` / `..._MIN_VALID` | Grava limite em `sensor_cfg[]` (cache local — usado por `Security_Manager_Check`) | idem |

Modelo de calibração: célula de carga é canônico
`grams = (raw - tare) / scale`; sensores ADC (`v_batt`/`i_batt`/
`temperature_battery`/`air_speed`) são todos afins,
`y = Linear_Coeff · (mV no pino) + Offset_Coeff` — **sem** fórmula física
embutida (divisor resistivo, ACS758, NTC, MPXV7002DP foram removidos): o
usuário calibra com os valores reais medidos na bancada (via `CMD` ou shell).

> Telemetria (`TELEMETRY_LOG`) é **sempre transmitida** — não há
> `FUNC_START/STOP` que ligue/desligue o stream.

---

## Shell serial (`siba ...`) — diagnóstico via UART

Via **USART1 / console Zephyr** (`CONFIG_SHELL`), independente da rede —
funciona mesmo sem UDP/PC conectado. Registrado estaticamente em
`Test_Shell.c` (basta o arquivo estar linkado) e fala **direto com
Sys/HAL**, nunca passa pelo `CMD_Core`/`fila_cmd`. Digite `siba` + TAB no
terminal serial (115200 8N1) para autocompletar.

| Comando | Camada acessada diretamente |
|---|---|
| `siba read` | `Log_Manager_Get_Snapshot()` — snapshot do `TELEMETRY_LOG` |
| `siba sensors` | `HAL_*_Get_*` — lista bruto + convertido de cada canal |
| `siba error` | `Security_Manager_Get_Error_Code()` decodificado bit a bit (13 bits nomeados) |
| `siba rpm` | `HAL_RPM_Get()` |
| `siba esc <0-100>` | `Propeller_Manager_Request_Throttle(pct)` |
| `siba arm` / `siba disarm` | `HAL_ESC_Arm/Disarm()` — direto no HAL, ignora limite de corrente |
| `siba start <0-5>` | Informativo (célula já ativa desde o boot) |
| `siba stop` | `Security_Manager_Trigger_Emergency()` |
| `siba reset` | `sys_reboot(SYS_REBOOT_COLD)` |
| `siba tare <0-5>` | `Propeller_Manager_Tare_Cell(idx)` |
| `siba sensor <id> linear\|offset <val>` / `<id> reset` | Célula → `Propeller_Manager_*`; ADC → `HAL_ADC_Set_Calib` |
| `siba batt poly <0-4> <val>` | `Battery_Manager_Set_Poly` |
| `siba batt templimit <val>` | `Battery_Manager_Set_Temp_Limit` |
| `siba batt relay on\|off` | `Battery_Manager_Set_Relay` |
| `siba batt cells <n>` | `Battery_Manager_Set_Cell_Count` (0 = auto-detectar) — **só existe via shell**, sem `CMD` de rede equivalente |
| `siba prop maxcurrent <amps>` | `Propeller_Manager_Set_Max_Current` — **só existe via shell** |
| `siba i2c scan` / `read <addr> <len>` / `write <addr> <b0>...` | `HAL_I2C_*` — infra crua, sem decisão de protocolo |
| `siba dac <0-4095>` | `HAL_DAC_Set_Value` — MMIO direto (sem driver Zephyr) |

O shell é estritamente **mais poderoso** que o protocolo de rede — cobre
diagnóstico de hardware bruto (I2C, DAC) e ajustes de bancada
(`batt cells`, `prop maxcurrent`) que ainda não têm `CMD` equivalente.

---

## Segurança — código de erro agregado (bitmask)

`Security_Manager_Error_Detect(bit)`/`_Erase(bit)` setam/limpam **por
transição** (só logam na borda). O agregado vai inteiro a cada 100 ms no
campo `TELEMETRY_LOG.error_code` (`siba error` decodifica o mesmo valor):

| Bit | Nome | Origem |
|---|---|---|
| 0 | `BATT_LOW_SOC` | Battery_Manager — SOC abaixo do limite de alerta |
| 1 | `BATT_POLY_MISCONFIGURED` | Battery_Manager — polinômio SOC produz valor absurdo |
| 2 | `BATT_LOW_VOLTAGE` | Battery_Manager — tensão por célula perto do limite |
| 3 | `BATT_RELAY_MISMATCH` | Battery_Manager — corrente fluindo com relé desligado |
| 4 | `BATT_OVER_TEMPERATURE` | Battery_Manager — temperatura acima do limite (dispara emergência) |
| 8 | `PROP_LOAD_CELL_NOT_CONFIGURED` | Propeller_Manager — célula sem calibração explícita |
| 9 | `PROP_LOAD_CELL_NOT_CONNECTED` | Propeller_Manager — HX711 devolve `raw==-1` (todos 24 bits em 1) |
| 10 | `PROP_OVERCURRENT` | Propeller_Manager — corrente do motor acima do limite |
| 16 | `SEC_VOLTAGE_OUT_OF_RANGE` | Security_Manager — `v_batt` fora da faixa operacional |
| 17 | `SEC_CURRENT_EXCESSIVE` | Security_Manager — `i_batt` acima do limite de segurança |
| 18 | `SEC_RPM_EXCESSIVE` | Security_Manager — `rpm` acima do limite de segurança |
| 19 | `SEC_EMERGENCY_ACTIVE` | Security_Manager — emergency stop disparado |
| 24 | `OS_GPIO_INIT_FAILED` | Algum módulo GPIO não inicializou |

Quando `v_batt`/`i_batt`/`rpm` saem da faixa `[Minimum_Valid_Value,
Max_Valid_Value]` (tabela central `CMD_Core_prm.h`), `Security_Manager_Check`
dispara `Trigger_Emergency()` **automaticamente**, sem precisar de CMD do
usuário.

### Bateria — SOC e auto-detecção de células

`eval_soc(v_batt)`: se `cell_count` não foi fixado pelo usuário (via
`siba batt cells <n>`), `detect_cell_count(v)` busca numa lookup table de
faixas de tensão (1S a 8S) a contagem cuja faixa "segura" contém `v`. Daí
`Vc = v_batt/cell_count` e `SOC = p0+p1·Vc+p2·Vc²+p3·Vc³+p4·Vc⁴` (polinômio
configurável via `siba batt poly`/`FUNC_BATTERY`), clampado em `[0,100]`.

> ⚠️ **`cell_count_lut[]` tem faixas adjacentes que se sobrepõem**
> (6S termina em 25.2V, 7S começa em 24.5V) — uma tensão de pack perto
> desses limites pode fazer a contagem "piscar" entre 6S/7S a cada leitura
> ruidosa, distorcendo o SOC. Fixar `cell_count` manualmente evita o
> problema em bancada (ver TODO).

### Hélice/Motor — proteção de sobrecorrente

`Propeller_Manager_Request_Throttle(pct)` aplica o pedido do usuário, mas se
`i_batt` já estiver acima de `max_current_a` (padrão 90 A, ajustável via
`siba prop maxcurrent`) e o pedido for um aumento, o pedido fica **retido**
no throttle atualmente aplicado. A cada leitura nova de `i_batt`,
`Propeller_Manager_Update` corrige o throttle em passos de 2%
(`PROPELLER_THROTTLE_STEP_PCT`) — reduzindo se `i_batt > max_current_a`,
restaurando gradualmente em direção ao pedido original quando a corrente
normaliza. A corrente do motor nunca fica acima do limite por mais de um
ciclo de leitura.

`check_load_cells_health()` roda a cada leitura de qualquer célula de carga
e mantém `PROP_LOAD_CELL_NOT_CONFIGURED`/`_NOT_CONNECTED` atualizados.

---

## Hardware — Periféricos e Pinos

MCU: **CH32V307VTC6** (RISC-V, 144 MHz) — Zephyr board `siba_board`.
A pinagem abaixo reflete o esquemático real (KiCad) e já está aplicada ao
`.dts`/`HAL.c`/`HAL.h`.

### Tabela geral de pinos

| Pino | Periférico | Função | Nó DTS / label |
|--------|-------------------|---------------------------------|------------------------------------------|
| PA0 | ADC1 CH0 | `v_batt` (tensão da bateria) | `adc1` (`adc1_default` group1) |
| PA1 | ADC1 CH1 | `i_batt` (corrente da bateria) | `adc1` (`adc1_default` group2) |
| PA2 | ADC1 CH2 | `air_speed` (tubo de Pitot) | `adc1` (`adc1_default` group3) |
| PA3 | ADC1 CH3 | Ensaio_Bateria_Voltage (bancada de ensaio de bateria, fora do `TELEMETRY_LOG`) | sem nó funcional |
| PA4 | ADC1 CH4 | Ensaio_Bateria_DAC (bancada) | sem nó funcional |
| PA5 | DAC1 OUT1 | Saída analógica (DAC) | `HAL_DAC_*` (MMIO direto, sem nó DTS) |
| PA6 | ADC1 CH6 | `temperature_battery` (uso duplo: também Ensaio_Bateria_Termistor_1) | `adc1` (`adc1_default` group4) |
| PA7 | ADC1 CH7 | Ensaio_Bateria_Termistor_2 (bancada) | sem nó funcional |
| PA8 | GPIO reservado | Ensaio_Bateria_Corrente — fora do range físico de ADC1/ADC2 | `pa8_reserved`, sem leitura implementada |
| PA9 | USART1 TX | Console / shell / log | `usart1_default` |
| PA10 | USART1 RX | Console / shell / log | `usart1_default` |
| PB0 | TIM3 CH3 (capture) | Contador de pulsos RPM | `rpm_capture_pin` / `tim3_ch3_pb0` |
| PB6 | TIM4 CH1 (PWM) | Sinal ESC (50 Hz, 1000-2000 µs) | `pwm4` / `esc_motor` |
| PB8 | I2C1 SCL (remap=1) | I2C1 — leitura/escrita/scan crus | `i2c1` (`i2c1_default` group1) |
| PB9 | I2C1 SDA (remap=1) | I2C1 — idem | `i2c1` (`i2c1_default` group2) |
| PB11-PB14 | GPIO (saída) | Relés genéricos — uso individual TBD | `relays_generic` |
| PB15 | GPIO (saída) | Relé de potência (bateria) | `relay_batt`, alias `relay0` |
| PC6-PC9 | ETH (MAC interno) | RXP/RXN/TXP/TXN | `mac_default` |
| PD2 | GPIO (entrada, reservado) | Feedback do SSR de 100A (porta AND com `MCU_Sig_Ready`) | `gpio_pd2_and_out`, sem leitura implementada |
| PE2-PE7 | GPIO (saída) | HX711[0-5] PD_SCK | `hx711_sck_pins` |
| PE8-PE13 | GPIO (entrada) | HX711[0-5] DOUT | `hx711_dout_pins` |
| PE15 | GPIO (saída) | LED "Sistema pronto" / `MCU_Sig_Ready` | `ready_pin`, alias `led0` |

> **ADC1 só alcança os canais 0-7 (PA0-PA7)** — canais 8+ (PB0/PB1/PC0-PC5)
> pertencem ao mux físico do ADC2, que o driver custom não usa hoje. Por
> isso o HX711 fica na Porta E, deixando PA0-PA7 livres para ADC1/DAC/
> bancada de ensaio.
>
> **Nomenclatura "Ensaio_Bateria_\*"**: canais de uma bancada de ensaio de
> bateria (carga/descarga controlada), distintos dos sensores de voo
> (`v_batt`/`i_batt`/`temperature_battery`/`air_speed` do `TELEMETRY_LOG`).
> Ficam fora do `TELEMETRY_LOG` por decisão de design — documentados aqui
> mas sem nó funcional/leitura no `.dts`/`HAL.c`. `temperature_battery`
> (PA6/CH6) tem uso duplo: mesmo circuito físico de
> `Ensaio_Bateria_Termistor_1`, mas já entra no `TELEMETRY_LOG`.

### Detalhe por periférico

**USART1 — Console/Shell/Log**: TX=PA9, RX=PA10, 115200 8N1. Canal do
console (`LOG_INF`/`LOG_ERR`) e do `Test_Shell.c`.

**ADC1 — scan circular multicanal (4 canais) via DMA**: driver custom
out-of-tree `drivers/adc/adc_ch32v307.c` (MMIO direto, DMA circular,
12 bits, oversample 8x), binding `compatible = "wch,adc"` (upstream do
Zephyr). O driver suporta múltiplas instâncias (ADC1/ADC2), cada uma com
seu próprio canal DMA1 — hoje só `adc1` está ativo:

| idx | Canal | Pino | Grandeza | Conversão |
|-----|-------|------|----------|-----------|
| 0 | CH0 | PA0 | `v_batt` | `HAL_ADC_Get_VBatt()` |
| 1 | CH1 | PA1 | `i_batt` | `HAL_ADC_Get_IBatt()` |
| 2 | CH2 | PA2 | `air_speed` (Pitot) | `HAL_ADC_Get_Air_Speed()` |
| 3 | CH6 | PA6 | `temperature_battery` | `HAL_ADC_Get_Temperature_Batt()` |

Calibração genérica `y = linear·x + offset` para as 4 grandezas (mesmo
padrão do HX711) — sem fórmula física fixa; ajuste via
`siba sensor <id> linear|offset <valor>` ou `CMD FUNC_SENSOR`.

**HX711 x6 — Células de carga (bit-bang GPIO, Porta E)**: protocolo serial
síncrono manual, ganho 128x (canal A). Pinos definidos via binding genérico
`compatible = "siba,gpio-array"`. Rate ~10 Hz por célula. **Pinagem ainda
placeholder** — confirmar no esquemático real (ver TODO).

**ESC PWM — TIM4 CH1 (PB6)**: nó `pwm4`→`esc_motor`, 50 Hz, pulso 1000µs
(arm/mín)–2000µs (máx). API `HAL_ESC_Init/Arm/Set_Throttle/Disarm`.

**RPM Counter — TIM3 CH3 (PB0)**: leitura direta de registradores
(`HAL_RPM_*`, sem driver `counter` dedicado), pull-up. Janela de medição
100 ms. `(pulsos / 14) × 600` = RPM (motor 14 pólos).

**I2C1 — remapeado PB8(SCL)/PB9(SDA), remap=1**: driver `wch,i2c`
(in-tree Zephyr), 100 kHz. Infra mínima e crua, sem decisão de protocolo —
substituirá o HX711 no futuro: `HAL_I2C_Init/Read/Write/Scan`, comandos
`siba i2c scan/read/write`.

**DAC1 — canal 1 (PA5/DAC_OUT1), MMIO direto**: sem driver/binding Zephyr
— acesso via MMIO bruto (`DAC_BASE = 0x40007400`). `HAL_DAC_Init()`,
`HAL_DAC_Set_Value(0-4095)`, comando `siba dac <valor>`. ⚠ Configuração do
GPIO de PA5 para modo "entrada analógica" ainda precisa validação em
bancada.

**Ethernet (MAC interno CH32V307)**: pinos fixos PC6/PC7/PC8/PC9, PHY
interno. IP board `169.254.97.200` / IP PC `169.254.97.139`, link-local sem
DHCP (`prj.conf`).

**LED / `MCU_Sig_Ready` — PE15**: uso duplo — indicador de "sistema pronto"
(LED) e sinal lógico que alimenta uma porta AND física (junto com PD2) que
habilita o SSR de 100A da bateria.

**Relés — PB11-PB15**: `relay_batt` (PB15, alias `relay0`) é o relé de
potência — `Battery_Manager_Set_Relay()` já tem efeito real no GPIO.
`relays_generic` (PB11-PB14) são saídas reservadas, função individual ainda
não detalhada.

**PD2 — feedback do SSR**: entrada reservada (`gpio_pd2_and_out`) para o
MCU eventualmente ler de volta o sinal que habilita o SSR de 100A — sem
lógica de leitura implementada ainda.

### Bindings DTS custom (`dts/bindings/`)

| Binding | Compatible | Uso |
|---|---|---|
| `gpio/siba,gpio-array.yaml` | `siba,gpio-array` | Arrays de GPIO genéricos (HX711 SCK/DOUT, RPM, reservados) |
| `pwm/siba,pwm-consumer.yaml` | `siba,pwm-consumer` | Consumidor de PWM nomeado (ESC) |

O ADC (`compatible = "wch,adc"`) usa o binding **upstream** do Zephyr — sem
cópia local (edtlib proíbe `compatible` duplicado).

### Memória
SRAM: 128 KB · Flash: 198 KB.

### `HAL_prm.h` — módulos habilitados por alvo

Ferramenta de bissecção de crash em bancada: cada módulo do HAL pode ser
desligado individualmente (`#define HAL_DISABLE_*` em `HAL_prm.h`) sem
quebrar a build — vira no-op + stub seguro (getters retornam 0), útil para
isolar qual periférico trava o boot.

| Flag | Estado hoje |
|---|---|
| `HAL_DISABLE_ADC` | comentada — ADC ligado (HW real + native_sim) |
| `HAL_DISABLE_HX711` | comentada — ligado nos dois alvos |
| `HAL_DISABLE_ESC` / `HAL_DISABLE_RPM` | comentadas fora de `CONFIG_ARCH_POSIX` — ligados também no HW real |
| `HAL_DISABLE_I2C` / `HAL_DISABLE_DAC` | comentadas — sempre ligados |
| `HAL_SIM_NOISE_SCALE` | `0.0f` — leituras sintéticas estáveis (ideal para testar calibração); `1.0f` = ruído "realista" |

⚠ `ADC`/`I2C` também são *devices* Zephyr (`status="okay"` no `.dts`) —
desabilitar aqui impede só o **uso** pelo `HAL.c`, não o **driver** de
inicializar no boot; para isolar o driver em si, marque também
`status = "disabled"` no `.dts`.

---

## Geração de Código (generate.py)

Localização: `../Aplicacao_Pi/Config/generate.py`
Fonte: `SIBA.yaml` → templates Jinja2 (`*_template.h.j2`) → headers C

```bash
cd ../Aplicacao_Pi/Config
python generate.py
# Gera:
#   ../../SIBA/lib/OS/Common_Types.h
#   ../../SIBA/lib/OS/CMD.h
#   ../../SIBA/lib/SYS/Log_Manager.h   (struct TELEMETRY_LOG + SENSORS + SENSOR_ID_*)
```

**Sempre rodar generate.py após modificar SIBA.yaml antes de compilar.**
Os `.h` gerados nunca são editados à mão. `CMD_Core_prm.h` (**não gerado**,
escrito à mão) complementa: tabela `SIBA_SENSOR_TABLE` com defaults de
calibração, faixa válida e bit de erro por sensor — fonte única para
`CMD_Core_Init`, `CMD_Core_Validate_Sensor` e `Security_Manager_Check`.

---

## Build e Flash

```bash
make build      # west build -b siba_board
make flash      # openocd WCH via WCH-LinkE
make clean      # build pristine
make menuconfig # Kconfig TUI
```

Toolchain: Zephyr SDK 1.0.1, riscv64-zephyr-elf-gcc.
OpenOCD: PlatformIO tool-openocd-riscv-wch.

---

## Modo Mock (native_sim) — testar sem placa/W-Link

Quando não há acesso ao hardware, dá para compilar e rodar o firmware como
um **binário POSIX comum no host** usando a board `native_sim` do Zephyr —
sem QEMU, sem emulação de CPU. Serve para achar bugs de **thread/lógica**
(`siba_wq`, managers, `CMD_Core`, `Test_Shell`) — bugs de hardware
(pinagem, timing de periférico, DMA) continuam exigindo a placa real.

```bash
make native_sim      # west build -b native_sim -d build_native_sim
make native_sim_run  # builda (se preciso) e roda build_native_sim/zephyr/zephyr.exe
make native_sim_clean
```

No prompt do shell que aparece (`uart:~$`), use os mesmos comandos
`siba ...` do `Test_Shell.c` para exercitar sensores/ESC/bateria sem
precisar do link UDP. Ctrl+] sai do processo.

**Rede REAL via UDP/loopback**: o binário `native_sim` troca pacotes UDP de
verdade com o PC em loopback (mesmas portas 5000/5555 da placa real). A
SIBA usa o loopback **`127.0.0.200`** (último octeto do IP real
`169.254.97.200`), distinto do da SAME (`127.0.0.100`), para as duas
rodarem ao mesmo tempo sem colidir — ver `Aplicacao_Pi/CLAUDE.md`
(`_native_loopback`).

### Como funciona

`native_sim` (`CONFIG_ARCH_POSIX=y`) não tem o CH32V307 nem periférico
real algum. Em vez de manter um `HAL` paralelo, **`HAL.c` roda nos dois
alvos sem duplicação de arquivo**: só troca o "miolo" de cada função por
`#ifdef CONFIG_ARCH_POSIX`, ou (no caso do ADC/I2C) substitui o *driver*
por um emulador in-tree do Zephyr, mantendo a mesma API pública (`HAL.h`):

| Periférico real | Equivalente em `native_sim` |
|---|---|
| ADC1 — driver custom `adc_ch32v307` (MMIO+DMA) | `zephyr,adc-emul` sob o mesmo nodelabel `adc1` — `HAL.c` usa a API genérica do Zephyr sem mudança; uma thread `hal_adc_stimulus_thread_fn` (só em `native_sim`) alimenta o emulador com valores de mV sintéticos, passando pelo mesmo `adc_calib[]`/calibração reais |
| HX711 x6 (bit-bang GPIO) | `#ifdef CONFIG_ARCH_POSIX` em `hx711_read_one`: sintetiza o raw 24 bits (empuxo ∝ RPM² + ruído) |
| ESC PWM (TIM4 CH1) | `#ifdef CONFIG_ARCH_POSIX` em `HAL_ESC_*`: throttle guardado em RAM, sem PWM real |
| RPM Counter (TIM3 CH3) | `#ifdef CONFIG_ARCH_POSIX` em `HAL_RPM_*`: RPM sintético persegue o throttle do ESC (1ª ordem + ruído) |
| I2C1 (`wch,i2c`) | `zephyr,i2c-emul-controller` sob o mesmo nodelabel `i2c1` — sem dispositivos no barramento emulado, `scan`→0 e `read`/`write`→`-EIO` (comportamento esperado) |
| DAC1 (MMIO direto) | `#ifdef CONFIG_ARCH_POSIX` em `HAL_DAC_*`: valor guardado em RAM, sem periférico real |
| Ethernet (link-local) | rede REAL via NSOS (Offloaded Sockets) em loopback `127.0.0.200:5000`/`:5555` — mesmo `NCI.c`, mesmo protocolo, só o endereço muda |
| Relé de potência (`relay0`=PB15) | alias `relay0` não existe no `.dts` do `native_sim` → fallback "relé GPIO não disponível" |
| LED `ready_pin`/`led0` | `gpio_emul` ligado, mas sem alias `led0` — sem efeito visível |
| USART1 console/shell | pseudo-TTY do host (`/dev/pts/N`) — comandos `siba ...` funcionam normalmente |

`HAL_SIM_NOISE_SCALE` (`HAL_prm.h`) controla o nível de ruído desses
modelos sintéticos.

**NCI.c não é trocado** — `boards/native_sim.conf` liga os "Offloaded
sockets" do Native Simulator (NSOS): `zsock_socket/bind/sendto/recvfrom`
de `NCI.c` vão direto para os sockets BSD reais do host Linux, sem TAP,
sem root, sem stack de rede do Zephyr. O restante do firmware
(`siba_wq`/managers/`CMD_Core`/`Test_Shell`) roda **idêntico** nos dois
alvos.

> ⚠ **Limitações**: nada que dependa de timing real de periférico, DMA,
> DTS/pinagem, ou do driver físico de rede é exercitado nesse modo (a rede
> em si — protocolo UDP/sockets — é real, via NSOS). Bugs encontrados aqui
> são bugs de verdade (a mesma lógica roda), mas ausência de bug aqui não
> garante que o firmware funcione na placa.

---

## TODO — Lista de Tarefas Pendentes

### 🔴 Bloqueia validação em bancada

- [ ] **Confirmar pinagem HX711 no esquemático** e, se mudar, atualizar
      `siba_board.dts` (`hx711_sck_pins`/`hx711_dout_pins`) + comentários/
      arrays em `HAL.c`.

- [ ] **Calibrar constantes dos circuitos analógicos** com os componentes
      reais do esquemático — hoje a calibração é só `linear`/`offset`
      genéricos (sem fórmula física fixa), então isso é feito via
      `siba sensor <id> linear|offset` com os valores medidos na bancada,
      não recompilando.

### 🟡 Importante (funcionalidade incompleta)

- [ ] **`cell_count_lut[]` tem faixas adjacentes sobrepostas**
      (`Battery_Manager.c`) — `v_max(n)=4.2·n` e `v_min(n+1)=3.5·(n+1)`
      se sobrepõem para `n>5` (6S vai até 25.2V, 7S começa em 24.5V). Uma
      tensão de pack perto desses limites faz `detect_cell_count` "piscar"
      entre contagens adjacentes a cada leitura ruidosa, distorcendo o
      SOC. Precisa de faixas não-sobrepostas ou histerese — ou fixar
      `cell_count` manualmente via `siba batt cells <n>` em bancada.

- [ ] **`LoadCell_Manager.c`** — criar `.c` para calibração persistente das
      6 células (hoje é volátil, perdida no reset — `sensor_cfg[]` e a
      calibração do HAL vivem só em RAM).

- [ ] **`Propeller_Manager` — cálculo de Ct/Cp da hélice** a partir de
      força (load cells) + RPM ainda não implementado (a proteção de
      sobrecorrente do motor já está pronta).

- [ ] **`BAT_ACTION_CELL_COUNT_OVERWRITE`** — hoje `siba batt cells` só
      existe via shell; adicionar a ação equivalente em `SIBA.yaml`
      (+ regenerar headers) para expor via `CMD` de rede também.

- [ ] **Persistência de calibração** — parâmetros dos sensores e polinômio
      SOC perdidos no reset (usar Flash NVM via Zephyr settings subsystem).

- [ ] **CMD `FUNC_START`/`FUNC_STOP` telemetria** — não implementado
      (telemetria sempre transmite, independente de START/STOP).

### 🟢 Melhorias (qualidade/robustez)

- [ ] **Watchdog** — adicionar IWDG para reset automático em caso de
      travamento.

- [ ] **`App.h`** — hoje vazio; definir máquina de estados global do
      sistema (IDLE→ARMED→RUNNING→FAULT) se for necessário.

- [ ] **Validação de CMD** — `CMD_Core` não verifica
      `Max_Valid_Value`/`Minimum_Valid_Value` de `sensor_cfg[]` antes de
      aplicar valores recebidos por `FUNC_SENSOR`.

- [ ] **NCI RX filtro de IP** — aceita pacotes de qualquer origem, deveria
      filtrar pelo IP do PC.

- [ ] **Log overflow** — `log_status` tem `LOG_OVERFLOW` mas nunca é
      setado.

---

## Dependências entre arquivos

```
Common_Types.h  ←── gerado por generate.py (SIBA.yaml)
CMD.h           ←── gerado por generate.py (SIBA.yaml), inclui Common_Types.h
Log_Manager.h   ←── gerado por generate.py (SIBA.yaml); define TELEMETRY_LOG, SENSORS, SENSOR_ID_*
CMD_Core_prm.h  ←── escrito à mão; usa SENSOR_ID_* e siba_error_bit_t

OSSI.c          ←── depende de Log_Manager.h, Battery/Propeller/Security_Manager.h, CMD_Core.h, CMD.h
NCI.c           ←── depende de OSSI.h, Log_Manager.h
HAL.c           ←── depende de HAL.h, HAL_prm.h, OSSI.h, Log_Manager.h — pinos: ver seção Hardware + .dts
CMD_Core.c      ←── depende de CMD.h, CMD_Core_prm.h, HAL.h, Battery/Propeller/Security_Manager.h, Log_Manager.h
Battery_Manager.c   ←── depende de Log_Manager.h, Security_Manager.h, gpio (relay0)
Propeller_Manager.c ←── depende de HAL.h, Log_Manager.h, Security_Manager.h
Security_Manager.c  ←── depende de CMD_Core.h (CMD_Core_Get_Sensor_Cfg — faixa central), HAL.h, Log_Manager.h
Log_Manager.c   ←── depende de Log_Manager.h, NCI.h, Security_Manager.h
Test_Shell.c    ←── depende de HAL.h, Battery/Propeller/Security/Log_Manager.h
main.c          ←── depende de tudo

Sys.c/h, TCKEC.c/h — arquivos de 1 linha, sem código (despacho migrou para OSSI.c)
```

## Checklist ao alterar hardware/pinagem

1. Edite `boards/wch/siba_board/siba_board.dts` (e bindings em
   `dts/bindings/` se necessário).
2. Atualize a tabela de pinos na seção "Hardware" deste arquivo.
3. Se o pino afeta um array/índice usado em código (HX711, RPM, ADC, etc.),
   atualize também os comentários e specs em `lib/OS/HAL.c` / `HAL.h` /
   `lib/SYS/Battery_Manager.c` / `Propeller_Manager.c`.
4. Rode `west build -b siba_board -p` (pristine) para garantir que o DTS
   compila.
