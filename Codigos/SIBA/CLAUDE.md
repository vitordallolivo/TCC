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
│       ├── Battery_Manager.c/h # SOC polinomial, contagem de células (auto-detect via lookup table), relé, limites
│       ├── Propeller_Manager.c/h # Protege motor/ESC: nunca deixa i_batt > limite, sobrescreve/reduz throttle
│       ├── Security_Manager.c/h# Limites operacionais, emergency stop
│       ├── Log_Manager.c/h     # Agrega TELEMETRY_LOG, envia 10Hz via OSSI
│       └── LoadCell_Manager.h  # TODO: stub (sem .c ainda — calibração persistente)
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
SYS  ←  System_Manager, Battery_Manager, Propeller_Manager, Security_Manager, Log_Manager
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
Propeller_Manager_Init()
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
HX711[0..5]      → HAL_thread_HX711_fn → OSSI→SYS → System_Manager
ADC (5 canais)   → HAL_thread_ADC_fn   → OSSI→SYS → System_Manager   → Log_Manager → OSSI→NCI → UDP TX → PC
  (v_batt/i_batt/temperature_battery/air_speed já convertidos — ver tabela de sensores)         ↗
RPM/TIM3         → HAL_thread_RPM_fn   → OSSI→SYS → System_Manager   ↗
                                                   → Battery_Manager   → SOC (poly + nº células) → OSSI→SYS → Log_Manager
                                                   → Propeller_Manager → limita corrente do motor (sobrescreve PWM do ESC)
UDP RX → NCI_RX → OSSI→CMD → CMD_Core → HAL / Battery / Propeller / Security
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
| `FUNC_ESC`     | `INSTR_WRITE` → `Propeller_Manager_Request_Throttle` (arma o ESC se preciso e **sobrescreve/reduz o throttle se a corrente do motor ultrapassar o limite** — ver Battery/Propeller Manager) |
| `FUNC_BATTERY` | `BAT_ACTION_P0..P4_OVERWRITE` (polinômio SOC), `BAT_ACTION_TEMP_LIMIT_OVERWRITE`, `BAT_ACTION_RELAY` (`BAT_ACTION_CELL_COUNT_OVERWRITE` ainda não existe no `SIBA.yaml` — ver TODO) |

> Telemetria (`TELEMETRY_LOG`) hoje é **sempre transmitida** — não há `FUNC_START/STOP`
> que ligue/desligue o stream (continua na lista de TODOs).

---

## Hardware — Periféricos e Pinos

**👉 Não confie nesta seção para detalhes de pino — veja [`HW.md`](HW.md) e o `.dts`.**
Resumo de alto nível (status pode ter mudado, `HW.md` é atualizado primeiro):

- **HX711 x6** (bit-bang GPIO, ganho 128x): **remapeado para a Porta E**
  (PE2-PE13) — pinagem ainda **placeholder**, aguardando confirmação no
  esquemático. Tabela completa em `HW.md`. Foi tirado de PA1-PA6/PB1-PB5/PC0
  para abrir espaço para os novos canais do ADC1 (ver abaixo — ADC1 só
  alcança PA0-PA7).
- **ESC PWM** — TIM4 CH1 (PB6), 50 Hz, pulso 1000-2000 µs, nó `pwm4`/`esc_motor`,
  API `HAL_ESC_Init/Arm/Set_Throttle/Disarm`. Implementado e funcional.
- **RPM Counter** — TIM3 CH3 (PB0), input capture, janela 100ms,
  `(pulsos / 14) * 600` = RPM (motor 14 pólos). Implementado.
- **ADC** — driver custom `drivers/adc/adc_ch32v307.c` (MMIO direto, DMA
  circular, 12 bits, oversample 8x) — é por isso que existe essa pasta em vez
  de usar o driver ADC genérico do Zephyr: **ADC1 e ADC2 do CH32V307 têm muxes
  analógicos físicos distintos** (ADC1 = canais 0-7 = pinos PA0-PA7; ADC2 =
  canais 8+ = PB0/PB1/PC0-PC5), e o driver agora é **multi-instância**
  (cada nó `wch,adc` aloca seu próprio canal DMA1 via `dmas = <&dma1 N>` —
  ver `HW.md`). Hoje só `adc1` está ativo, em **scan multicanal (5 canais)**,
  todos dentro do range físico PA0-PA7: PA0 (expansão), PA1→`v_batt` (divisor
  de tensão), PA2→`i_batt` (ACS758), PA3→`temperature_battery` (termistor
  NTC, lookup table), PA4→`air_speed` (MPXV7002DP, pressão diferencial →
  Bernoulli). Pinagem dos 4 canais novos é **placeholder** — ver `HW.md`.
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
| 6  | v_batt                 | HAL_thread_ADC_fn (ADC1 CH1/PA1 — divisor de tensão) |
| 7  | i_batt                 | HAL_thread_ADC_fn (ADC1 CH2/PA2 — sensor ACS758) |
| 8  | soc                    | Battery_Manager (polinômio sobre v_batt, por célula) |
| 9  | temperature_battery    | HAL_thread_ADC_fn (ADC1 CH3/PA3 — termistor NTC, lookup table) |
| 10 | air_speed              | HAL_thread_ADC_fn (ADC1 CH4/PA4 — MPXV7002DP, pressão diferencial) |
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
| 50 | System_Manager              | 5          |
| 60 | CMD_Core                    | 4          |

7 threads ativas, pool TCKEC com 10 slots — folga de 3 para futuras expansões.

> **Toda a camada SYS roda em uma única thread** (`System_Manager_thread_fn`,
> ID 50): Battery_Manager, Propeller_Manager, Security_Manager e Log_Manager
> não têm threads próprias — são chamados diretamente a partir do dispatch de
> `fila_sys` (ou, no caso do Log_Manager, via `Log_Manager_Transmit()`
> disparado periodicamente dentro do mesmo loop, substituindo a thread
> dedicada de 10 Hz que existia antes). Mantém o pool TCKEC enxuto e reduz o
> número de stacks de `MANAGED_THREAD_STACK_SIZE` (2048 B) alocadas — só
> adicione uma thread nova em SYS se houver necessidade real de concorrência.

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

## Modo Mock (native_sim) — testar sem placa/W-Link

Quando não há acesso ao hardware (placa/W-Link), dá para compilar e rodar o
firmware como um **binário POSIX comum no host** usando a board `native_sim`
do Zephyr — sem QEMU, sem emulação de CPU, basicamente "Zephyr rodando como
processo Linux". Serve para achar bugs de **thread/lógica** (filas OSSI,
TCKEC, System_Manager, Battery/Propeller/Security/Log_Manager, CMD_Core,
Test_Shell) — bugs de hardware (pinagem, timing de periférico, DMA, etc.)
continuam exigindo a placa real.

```bash
make native_sim      # west build -b native_sim -d build_native_sim
make native_sim_run  # builda (se preciso) e roda build_native_sim/zephyr/zephyr.exe
make native_sim_clean
```

No prompt do shell que aparece (`uart:~$`), use os comandos `siba ...`
(mesmos do `Test_Shell.c`/CONFIG_SHELL — ver tabela de threads) para
exercitar sensores/ESC/bateria sem precisar do link UDP. Ctrl+] sai do
processo.

**Rede REAL via UDP/loopback**: diferente de uma simulação "isolada", o
binário `native_sim` troca pacotes UDP de verdade com o PC em `127.0.0.1`
(mesmas portas 5000/5555 da placa real — ver protocolo UDP). Isso é
explicado em detalhes na seção "Como funciona" abaixo e em
`Aplicacao_Pi/CLAUDE.md` — o app Python consegue se conectar nesse binário
exatamente como se conectaria à placa física, só que em loopback.

### Como funciona

`native_sim` (arquitetura POSIX, `CONFIG_ARCH_POSIX=y`) não tem o
CH32V307 nem nenhum periférico real — então **não dá para usar `HAL.c`**
(driver ADC custom MMIO+DMA, GPIO bit-bang do HX711, PWM/TIM4 do ESC,
TIM3 input-capture do RPM). `NCI.c`, por outro lado, **continua o mesmo
nos dois alvos** — ver abaixo por quê. O `CMakeLists.txt` detecta
`CONFIG_ARCH_POSIX` e troca só o HAL por um substituto que implementa a
**mesma API pública** (`HAL.h`) sem tocar hardware:

- **`lib/OS/HAL_mock.c`** — gera dados sintéticos plausíveis e os publica
  pelo MESMO caminho OSSI→`fila_sys` (`TARGET_SYS`/`REQ_TYPE_SYS_UPDATE_SENSOR`)
  que `HAL_thread_ADC/HX711/RPM_fn` usam de verdade — então toda a cadeia
  SYS/CMD_Core/Test_Shell roda igual. Modelo (intencionalmente simples, só
  para exercitar lógica): RPM persegue o throttle do ESC armado (1ª ordem);
  corrente ∝ throttle² + ruído; tensão drena por Coulomb counting simplificado
  (acelerado por `HAL_MOCK_TIME_SCALE`); temperatura sobe com a corrente;
  air_speed ∝ RPM; células de carga seguem um modelo de empuxo ∝ RPM². Reusa
  exatamente o mesmo `adc_calib[]`/`hal_adc_apply_calib`/`hx711_state[]` da
  implementação real — então os comandos de calibração (`siba sensor`,
  `siba tare`) exercitam o código de verdade contra valores sintéticos.
- **`lib/OS/NCI.c` (sem mock!)** — `boards/native_sim.conf` liga os
  "Offloaded sockets" do Native Simulator (NSOS, ver
  `doc/connectivity/networking/native_sim_setup.rst` "Using offloaded
  sockets"): `CONFIG_NET_SOCKETS_OFFLOAD=y` + `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y`
  redirecionam `zsock_socket/bind/sendto/recvfrom` de `NCI.c` DIRETO para
  os sockets BSD reais do host Linux — sem TAP, sem root, sem stack de rede
  do Zephyr. O processo do firmware vira literalmente "mais um programa" no
  `127.0.0.1`, escutando `CMD` na porta 5000 e mandando `TELEMETRY_LOG` para
  a porta 5555 — **exatamente como a placa real faz** (mesmo protocolo,
  mesmas portas, só que em loopback em vez de link-local 169.254.x.x). Isso
  significa que `Aplicacao_Pi` consegue testar o link UDP completo (TX/RX,
  parsing de `CMD`, geração de `TELEMETRY_LOG`) contra **firmware de
  verdade**, não contra um fake Python — ver `Aplicacao_Pi/CLAUDE.md`.
- **`boards/native_sim.conf`** — overlay de Kconfig (mesclado sobre
  `prj.conf` quando o board é `native_sim`) que liga a rede via NSOS
  (apontando `CONFIG_NET_CONFIG_MY/PEER_IPV4_ADDR` para `127.0.0.1`) e
  desliga ADC/PWM/COUNTER/PINCTRL (específicos da `siba_board`/CH32V307,
  não existem em `native_sim`). `CONFIG_GPIO` continua ligado (há
  `gpio_emul`), mas o alias `relay0` não existe no `.dts` do `native_sim`
  — `Battery_Manager` cai no fallback "relé GPIO não disponível", como já
  acontece hoje na placa real (ver TODO do relé).

> ⚠ **Limitações**: nada que dependa de timing real de periférico, DMA,
> DTS/pinagem, ou do MAC Ethernet/driver de rede físico é exercitado nesse
> modo (a rede em si — protocolo UDP/sockets — É real, via NSOS). Só a
> lógica das camadas OS (acima do HAL)/SYS/CMD/rede é exercitada. Bugs
> encontrados aqui são "bugs de verdade" (a mesma `System_Manager_thread_fn`,
> `CMD_Core`, `NCI.c`, `Battery_Manager` etc. rodam), mas a ausência de bug
> aqui não significa que o firmware vai funcionar na placa.

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

- [ ] **Confirmar pinagem dos 4 canais ADC novos no esquemático** (`v_batt`/PA1,
      `i_batt`/PA2, `temperature_battery`/PA3, `air_speed`/PA4 — todos
      placeholder, ver `HW.md`). ⚠ Lembrar que o ADC1 só alcança PA0-PA7
      (canais 0-7) — qualquer pino novo para `adc1` tem que vir desse range
      (PA5-PA7 ainda livres). Se mudar, atualizar `adc1_default` no `.dts`
      + `adc_ch_cfg[]`/`HAL_ADC_IDX_*`/`ADC_CH_*` em `HAL.c`/`HAL.h`

- [ ] **Calibrar constantes dos circuitos analógicos** em `HAL.h`/`HAL.c` com
      os componentes reais do esquemático: razão do divisor de tensão
      (`HAL_VBATT_DIVIDER_R1_KOHM`/`R2_KOHM`), variante do ACS758 e seu
      ponto-zero/sensibilidade (`HAL_ACS758_VZERO_MV`/`SENS_MV_PER_A` — hoje
      assume 50A bidirecional 40mV/A; **pode futuramente virar shunt + ampOp**),
      curva do termistor NTC (`hal_ntc_lut[]`) e `Vs` do MPXV7002DP

- [ ] **Definir GPIO do relé** no esquemático e adicionar `aliases { relay0 = &<nó>; }`
      no `.dts` — `Battery_Manager.c` já está pronto para usar o alias assim que existir

### 🟡 Importante (funcionalidade incompleta)

- [ ] **Possível descompasso de numeração `target_type` × `SENSOR_ID_*`** —
      `target_type` (`Common_Types.h`) tem `TARGET_VOLTAGE_DIVIDOR`=7,
      `TARGET_CURRENT_SENSOR`=8, `TARGET_PITOT_TUBE`=9, enquanto `SENSOR_ID_*`
      (`Log_Manager.h`) tem `V_BATT`=6, `I_BATT`=7, `SOC`=8,
      `TEMPERATURE_BATTERY`=9, `AIR_SPEED`=10 — os valores não se alinham 1:1
      (CMD_Core hoje indexa `sensor_cfg[]`/aplica calibração por `SENSOR_ID_*`,
      então `target_type` parece redundante ou desatualizado). Provavelmente
      precisa de ajuste/reconciliação em `SIBA.yaml`/`generate.py`.

- [ ] **`cell_count_lut[]` tem faixas adjacentes sobrepostas** (`Battery_Manager.c`)
      — encontrado rodando o modo mock (`make native_sim_run`, ver "Modo Mock"):
      como `v_max(n) = 4.2*n` e `v_min(n+1) = 3.5*(n+1)`, para `n > 5` vale
      `v_max(n) > v_min(n+1)` (ex.: 6S vai até 25.2V e 7S começa em 24.5V —
      sobreposição de 0.7V nas transições 6S/7S e 7S/8S). Uma tensão de pack
      perto desses limites (bem plausível: 25.2V = 6S "cheio") faz
      `detect_cell_count` "piscar" entre contagens adjacentes a cada leitura
      ruidosa — gera spam de log e `vc = v/cell_count` saltando (ex.: 25.2/6=4.2
      vs. 25.2/7≈3.6), distorcendo o SOC. Precisa de faixas não-sobrepostas
      (ex.: limiar único por transição, tipo ponto médio entre os nominais) ou
      histerese em `detect_cell_count`.

- [ ] **`LoadCell_Manager.c`** — criar `.c` para gerenciar calibração persistente das 6 células
      (atualmente calibração é volátil, perdida no reset — `sensor_cfg[]` vive só em RAM)

- [ ] **`Propeller_Manager.c`** — cálculo de Ct/Cp da hélice a partir de força
      (load cells) + RPM ainda é TODO (a proteção de sobrecorrente do motor
      já está implementada — ver seção Battery/Propeller abaixo)

- [ ] **`BAT_ACTION_CELL_COUNT_OVERWRITE` em `SIBA.yaml`** — adicionar ação no
      `battery_action` (+ regenerar `Common_Types.h`/`CMD.h` via `generate.py`)
      e expor em `CMD_Core::handle_func_battery` para permitir que o usuário
      sobrescreva a contagem de células via CMD chamando
      `Battery_Manager_Set_Cell_Count()` (a API já existe; hoje só roda
      auto-detecção pela lookup table — ver `Battery_Manager.c`)

- [ ] **Persistência de calibração** — parâmetros dos sensores e polinômio SOC
      perdidos no reset (usar Flash NVM via Zephyr settings subsystem)

- [ ] **CMD FUNC_START/STOP telemetria** — não implementado no CMD_Core
      (atualmente telemetria sempre transmite, independente de START/STOP)

### 🟢 Melhorias (qualidade/robustez)

- [ ] **Watchdog** — adicionar IWDG para reset automático em caso de travamento

- [x] ~~**Mock mode**~~ — **implementado**: `make native_sim` / `make native_sim_run`
      compila e roda o firmware como binário POSIX (`HAL_mock.c` no lugar de
      `HAL.c`, selecionado via `CONFIG_ARCH_POSIX` no `CMakeLists.txt`; `NCI.c`
      continua o mesmo, com rede real via NSOS em `127.0.0.1`) — gera dados
      sintéticos e exercita toda a cadeia OSSI→SYS→CMD/Test_Shell/rede UDP sem
      hardware, inclusive contra o app `Aplicacao_Pi` de verdade. Ver seção
      "Modo Mock (native_sim)" e `Aplicacao_Pi/CLAUDE.md`.

- [ ] **`App.h`** — definir máquina de estados global do sistema (IDLE→ARMED→RUNNING→FAULT)

- [ ] **Validação de CMD** — CMD_Core não verifica `Max_Valid_Value`/`Minimum_Valid_Value`
      de `sensor_cfg[]` antes de aplicar valores recebidos

- [ ] **NCI RX filtro de IP** — aceita pacotes de qualquer origem, deveria filtrar pelo IP do PC

- [ ] **Log overflow** — `log_status` tem `LOG_OVERFLOW` mas nunca é setado

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
HAL.c           ←── depende de HAL.h, OSSI.h, Log_Manager.h (sensor_id), drivers/adc, gpio, pwm — pinos: ver HW.md + .dts
CMD_Core.c      ←── depende de CMD.h, HAL.h, Battery_Manager.h, Propeller_Manager.h, Security_Manager.h, Log_Manager.h
System_Manager.c←── depende de OSSI.h, Log_Manager.h, Battery_Manager.h, Propeller_Manager.h, Security_Manager.h
Battery_Manager.c←─ depende de Log_Manager.h, OSSI.h, Security_Manager.h, gpio (relay0 — ver HW.md)
Propeller_Manager.c←depende de HAL.h (HAL_ESC_*), Log_Manager.h (SENSOR_ID_I_BATT), OSSI.h
Security_Manager.c←─depende de HAL.h, Log_Manager.h
Log_Manager.c   ←── depende de Log_Manager.h, OSSI.h
main.c          ←── depende de tudo
```
