# SIBA — Contexto do Projeto para Claude Code

## Visão Geral

SIBA (Sistema Integrado da Bancada Aerodinâmica) é o firmware de uma placa customizada
baseada no **CH32V307VTC6** (RISC-V, 144MHz) rodando **Zephyr RTOS v4.4.0**.

A placa mede parâmetros aerodinâmicos (6 células de carga HX711, pitot, RPM) e
gerencia a interface com bateria LiPo 6S. Comunica com um PC via UDP/Ethernet.

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

**Status atual: o firmware compila e builda (`make build` / `west build -b siba_board`).**
O que falta agora é principalmente **validação em bancada** — testar cada
funcionalidade (sensores, ESC, relé, comandos) sem depender só do link UDP com o PC.

> ⚠ **Hardware/pinagem mudou de lugar**: antes de assumir qualquer pino, **leia
> [`HW.md`](HW.md)** (referência completa de pinos) e o `.dts` real em
> `boards/wch/siba_board/siba_board.dts` + `dts/bindings/`. Esta seção do
> CLAUDE.md pode estar desatualizada — `HW.md` + `.dts` são a fonte de verdade.
> **Sempre que alterar pinagem/periféricos, atualize `HW.md` e o `.dts` juntos.**

---

## Estrutura de Diretórios

```
SIBA/
├── boards/wch/siba_board/      # Board definition (DTS, Kconfig, cmake)
│   ├── siba_board.dts          # Pinagem, periféricos, clock — ver HW.md
│   └── siba_board_defconfig    # Kconfig padrão da board
├── drivers/
│   └── adc/
│       └── adc_ch32v307.c      # Driver ADC custom (MMIO+DMA, sem HAL WCH)
├── dts/bindings/
│   ├── adc/wch,adc.yaml             # Binding para compatible="wch,adc"
│   ├── gpio/siba,gpio-array.yaml    # Binding genérico p/ arrays de GPIO (HX711, RPM)
│   └── pwm/siba,pwm-consumer.yaml   # Binding p/ consumidor de PWM nomeado (ESC)
├── lib/
│   ├── OS/                     # Camada Vcore (mais baixa)
│   │   ├── OSSI.c/h            # Barramento IPC central (4 filas k_msgq)
│   │   ├── TCKEC.c/h           # Gerenciador de threads (pool de 10 slots)
│   │   ├── NCI.c/h             # UDP TX/RX via Zephyr sockets
│   │   ├── HAL.c/h             # ADC, HX711 x6, ESC PWM, RPM counter
│   │   ├── CMD_Core.c/h        # Parser/dispatcher de comandos binários (struct CMD)
│   │   ├── CMD.h               # AUTO-GERADO por generate.py
│   │   └── Common_Types.h      # AUTO-GERADO por generate.py
│   └── SYS/                    # Camada System (acima do OS)
│       ├── System_Manager.c/h  # Roteador: consome fila_sys
│       ├── Battery_Manager.c/h # SOC polinomial, relé, limites
│       ├── Security_Manager.c/h# Limites operacionais, emergency stop
│       ├── Log_Manager.c/h     # Agrega TELEMETRY_LOG, envia 10Hz via OSSI
│       ├── LoadCell_Manager.h  # TODO: stub (sem .c ainda)
│       └── Propeller_Manager.h # TODO: stub (sem .c ainda)
├── src/
│   └── main.c                  # Ponto de entrada, inicializa tudo em ordem
├── CMakeLists.txt
├── prj.conf
└── Makefile                    # Atalhos: make build / flash / clean / menuconfig
```

> `src/Vcore.c` foi removido (estava vazio/órfão do esqueleto antigo) — não existe mais.

---

## Arquitetura de Software (Camadas)

```
APP  ←  main.c
 ↓
SYS  ←  System_Manager, Battery_Manager, Security_Manager, Log_Manager
 ↓
 OS  ←  OSSI (barramento), TCKEC (threads), NCI (rede), HAL (hw), CMD_Core
 ↓
 HW  ←  CH32V307 + driver ADC + GPIO + PWM + Counter
```

### OSSI — Barramento central
- Nenhuma thread fala diretamente com outra
- 4 filas: `fila_cmd`, `fila_tckec`, `fila_sys`, `fila_nci`
- Função única: `OSSI_Send(ossi_msg_t *msg)`
- `ossi_msg_t` tem union de payloads: `cmd_req`, `sys_req`, `tckec_req`, `raw[128]`

### TCKEC — Gerenciador de threads
- Pool estático de **10 slots** (`MAX_MANAGED_THREADS = 10`) — já ajustado
- Stack de cada thread gerenciada: `MANAGED_THREAD_STACK_SIZE = 2048` — já ajustado
- Cria/mata threads dinamicamente via mensagens OSSI

### Ordem de inicialização (`main.c`)
```
TCKEC_Init()
Security_Manager_Init()
Battery_Manager_Init()
Log_Manager_Init()
System_Manager_Init()
CMD_Core_Init()
NCI_Init()
HAL_Init()                       → registra threads ADC/HX711/RPM
NCI_Set_Connection_Status(true)  → habilita tráfego de rede
```
A `main()` depois entra num loop de heartbeat (`k_sleep` + `LOG_INF` a cada 10s);
o Zephyr scheduler assume o controle a partir daí.

### Fluxo de dados completo
```
HX711[0..5] → HAL_thread_HX711_fn → OSSI→SYS → System_Manager
ADC CH0     → HAL_thread_ADC_fn   → (futuro: OSSI→SYS)          → Log_Manager → OSSI→NCI → UDP TX → PC
RPM/TIM3    → HAL_thread_RPM_fn   → OSSI→SYS → System_Manager   ↗
                                              → Battery_Manager → SOC → OSSI→SYS → Log_Manager
UDP RX → NCI_RX → OSSI→CMD → CMD_Core → HAL / Battery / Security
```

---

## Protocolo de Comunicação UDP

### TX: Board → PC
- Porta destino: `5555` (PC escuta)
- Payload: `TELEMETRY_LOG` serializado como struct packed (little-endian)
- Rate: 10 Hz (definido em `LOG_MANAGER_PERIOD_MS = 100`)
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
} TELEMETRY_LOG;
```

### CMD_Core — funções já implementadas (`lib/OS/CMD_Core.c`)
| `func_type`    | Comportamento atual                                                    |
|----------------|------------------------------------------------------------------------|
| `FUNC_START`   | Apenas log (células de carga já rodam automaticamente via thread HX711) |
| `FUNC_STOP`    | Se `target_id == TARGET_DEFAULT`: dispara `Security_Manager_Trigger_Emergency()` |
| `FUNC_RESET`   | `sys_reboot(SYS_REBOOT_COLD)`                                           |
| `FUNC_SENSOR`  | `DEFAULT_ACTION_SENSE_LINEAR_COEF` / `..._OFFSET_COEF` / `..._RESET_DEFAULT` → atualiza `sensor_cfg[]` e, para células 0-5, chama `HAL_HX711_Set_Calib`/`Tare` |
| `FUNC_ESC`     | `INSTR_WRITE` → `HAL_ESC_Set_Throttle`; se não armado, arma e tenta de novo |
| `FUNC_BATTERY` | `BAT_ACTION_P0..P4_OVERWRITE` (polinômio SOC), `BAT_ACTION_TEMP_LIMIT_OVERWRITE`, `BAT_ACTION_RELAY` |

> Telemetria (`TELEMETRY_LOG`) hoje é **sempre transmitida** — não há `FUNC_START/STOP`
> que ligue/desligue o stream (continua na lista de TODOs).

---

## Hardware — Periféricos e Pinos

**👉 Não confie nesta seção para detalhes de pino — veja [`HW.md`](HW.md) e o `.dts`.**
Resumo de alto nível (status pode ter mudado, `HW.md` é atualizado primeiro):

- **HX711 x6** (bit-bang GPIO, ganho 128x): pinagem ainda **placeholder**, aguardando
  confirmação no esquemático. Tabela completa em `HW.md`.
- **ESC PWM** — TIM4 CH1 (PB6), 50 Hz, pulso 1000-2000 µs, nó `pwm4`/`esc_motor`,
  API `HAL_ESC_Init/Arm/Set_Throttle/Disarm`. Implementado e funcional.
- **RPM Counter** — TIM3 CH3 (PB0), input capture, janela 100ms,
  `(pulsos / 14) * 600` = RPM (motor 14 pólos). Implementado.
- **ADC** — ADC1 CH0 (PA0), driver custom `drivers/adc/adc_ch32v307.c`
  (DMA1 canal 1, modo circular, 12 bits, oversample 8x). 1 canal ativo hoje.
- **Ethernet (MAC interno)** — IP board `169.254.97.100`, IP PC `169.254.97.139`,
  link-local sem DHCP.
- **Relé de potência** — código já espera alias DTS `relay0`
  (`Battery_Manager.c` usa `DT_ALIAS(relay0)` com fallback seguro), **mas o alias
  ainda não existe no `.dts`** → `Battery_Manager_Set_Relay()` não tem efeito real
  até o GPIO ser definido no esquemático.
- **LED** — `ready_pin` / alias `led0`, indicador de "sistema pronto".

---

## IDs de Sensor (sensor_id no OSSI sys_req)

| ID | Campo TELEMETRY_LOG    | Produzido por       |
|----|------------------------|---------------------|
| 0  | load_cell_0            | HAL_thread_HX711_fn |
| 1  | load_cell_1            | HAL_thread_HX711_fn |
| 2  | load_cell_2            | HAL_thread_HX711_fn |
| 3  | load_cell_3            | HAL_thread_HX711_fn |
| 4  | load_cell_4            | HAL_thread_HX711_fn |
| 5  | load_cell_5            | HAL_thread_HX711_fn |
| 6  | v_batt                 | futuro: ADC externo  |
| 7  | i_batt                 | futuro: ADC externo  |
| 8  | soc                    | Battery_Manager (a partir de v_batt) |
| 9  | temperature_battery    | futuro: sensor temp  |
| 10 | air_speed              | futuro: pitot/ADC    |
| 11 | rpm                    | HAL_thread_RPM_fn   |

(constantes `SENSOR_ID_*` definidas em `Log_Manager.h`)

---

## IDs de Thread (TCKEC)

| ID | Thread                      | Prioridade |
|----|-----------------------------|------------|
| 10 | NCI TX                      | 5          |
| 11 | NCI RX                      | 5          |
| 30 | HAL ADC                     | 7          |
| 31 | HAL HX711                   | 6          |
| 32 | HAL RPM                     | 6          |
| 40 | Log_Manager                 | 6          |
| 50 | System_Manager              | 5          |
| 60 | CMD_Core                    | 4          |

8 threads ativas, pool TCKEC com 10 slots — folga de 2 para futuras expansões
(ex.: thread do shell de teste, ver TODO abaixo).

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
Os templates (`CMD_template.h.j2`, `Common_template.h.j2`, `LOG_template.h.j2`) e
o próprio `generate.py` foram recentemente alterados — se algo gerado parecer
desalinhado com este documento, regenere antes de investigar mais a fundo.

---

## Build e Flash

```bash
make build      # west build -b siba_board
make flash      # openocd WCH via WCH-LinkE
make clean      # build pristine
make menuconfig # Kconfig TUI
```

Toolchain: Zephyr SDK 1.0.1, riscv64-zephyr-elf-gcc
OpenOCD: PlatformIO tool-openocd-riscv-wch

**O firmware builda com sucesso no estado atual.**

---

## TODO — Lista de Tarefas Pendentes

### 🔴 Crítico / bloqueia testes em bancada

- [ ] **Shell de teste simplificado via UART** — adicionar em `main.c` (ou módulo
      próprio) um shell/CLI simplificado sobre `usart1` (já é `zephyr,console` e
      `zephyr,shell-uart`) que permita exercitar manualmente as mesmas ações que
      hoje só chegam via `CMD` pela rede UDP — ex.: ler valor de cada sensor,
      armar/setar throttle do ESC, ligar/desligar relé, forçar START/STOP/RESET,
      sobrescrever calibração/polinômio SOC. Pensar nisso como um "CMD.h
      improvisado pela serial": útil para validar HX711/ESC/RPM/Battery sem
      precisar do link UDP com o PC. Pode reusar `CMD_Core` internamente
      (montar uma `CMD` a partir da entrada do shell e injetar em `fila_cmd`,
      ou chamar os handlers/HAL diretamente).
      Avaliar `CONFIG_SHELL` (Zephyr shell subsystem) vs. parser próprio leve.

- [ ] **Confirmar pinagem HX711 no esquemático** e, se mudar, atualizar
      `HW.md` + `siba_board.dts` (`hx711_sck_pins`/`hx711_dout_pins`) +
      comentários/arrays em `HAL.c` (ver checklist em `HW.md`)

- [ ] **Definir GPIO do relé** no esquemático e adicionar `aliases { relay0 = &<nó>; }`
      no `.dts` — `Battery_Manager.c` já está pronto para usar o alias assim que existir

### 🟡 Importante (funcionalidade incompleta)

- [ ] **`LoadCell_Manager.c`** — criar `.c` para gerenciar calibração persistente das 6 células
      (atualmente calibração é volátil, perdida no reset — `sensor_cfg[]` vive só em RAM)

- [ ] **`Propeller_Manager.c`** — criar `.c` para calcular Ct/Cp da hélice com dados de força+RPM

- [ ] **Pitot tube** — sensor de velocidade do ar não implementado
      (precisa definir: ADC analógico ou protocolo digital?)

- [ ] **Tensão e corrente de bateria** — sensor_id 6 e 7 nunca são publicados
      (precisa de: divisor de tensão no ADC + sensor de corrente no ADC ou I2C;
      `ADC_NUM_CHANNELS` em `HAL.c` ainda é 1)

- [ ] **Temperatura de bateria** — sensor_id 9 nunca publicado
      (NTC no ADC ou sensor digital?)

- [ ] **Persistência de calibração** — parâmetros dos sensores e polinômio SOC
      perdidos no reset (usar Flash NVM via Zephyr settings subsystem)

- [ ] **CMD FUNC_START/STOP telemetria** — não implementado no CMD_Core
      (atualmente telemetria sempre transmite, independente de START/STOP)

### 🟢 Melhorias (qualidade/robustez)

- [ ] **Watchdog** — adicionar IWDG para reset automático em caso de travamento

- [ ] **Mock mode** — implementar flag de mock no firmware para gerar dados sintéticos
      (útil para teste da GUI sem hardware — pode reaproveitar a infra do shell de teste)

- [ ] **`App.h`** — definir máquina de estados global do sistema (IDLE→ARMED→RUNNING→FAULT)

- [ ] **Validação de CMD** — CMD_Core não verifica `Max_Valid_Value`/`Minimum_Valid_Value`
      de `sensor_cfg[]` antes de aplicar valores recebidos

- [ ] **NCI RX filtro de IP** — aceita pacotes de qualquer origem, deveria filtrar pelo IP do PC

- [ ] **Log overflow** — `log_status` tem `LOG_OVERFLOW` mas nunca é setado

- [ ] **ADC multicanal** — `HAL.c` tem `ADC_NUM_CHANNELS=1`, expandir para v_batt/i_batt/pitot

- [ ] **SAME board** — criar estrutura equivalente para `../SAME/` baseada na SIBA
      (SAME.yaml já existe, gerar headers e firmware base)

---

## Dependências entre arquivos

```
Common_Types.h  ←── gerado por generate.py (SIBA.yaml)
CMD.h           ←── gerado por generate.py (SIBA.yaml), inclui Common_Types.h
Log_Manager.h   ←── gerado por generate.py (SIBA.yaml); define TELEMETRY_LOG, SENSORS, SENSOR_ID_*

OSSI.h          ←── depende de zephyr/kernel.h
TCKEC.c         ←── depende de OSSI.h
NCI.c           ←── depende de OSSI.h, NCI.h, Log_Manager.h
HAL.c           ←── depende de HAL.h, OSSI.h, drivers/adc, gpio, pwm — pinos: ver HW.md + .dts
CMD_Core.c      ←── depende de CMD.h, HAL.h, Battery_Manager.h, Security_Manager.h, Log_Manager.h
System_Manager.c←── depende de OSSI.h, Log_Manager.h, Battery_Manager.h, Security_Manager.h
Battery_Manager.c←─ depende de Log_Manager.h, OSSI.h, Security_Manager.h, gpio (relay0 — ver HW.md)
Security_Manager.c←─depende de HAL.h, Log_Manager.h
Log_Manager.c   ←── depende de Log_Manager.h, OSSI.h
main.c          ←── depende de tudo
```
