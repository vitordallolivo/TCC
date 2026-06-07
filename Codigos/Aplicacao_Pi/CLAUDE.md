# Aplicacao_Pi — Contexto do Projeto para Claude Code

## Visão Geral

Aplicação PySide6 (GUI) que roda no PC/Raspberry Pi e se comunica via **UDP/Ethernet**
com as placas **SIBA** (gerenciamento de energia/telemetria aerodinâmica,
`../SIBA/`) e **SAME** (controle de inversor/motor, `../SAME/`) — as três pastas
(`Aplicacao_Pi/`, `SIBA/`, `SAME/`) são irmãs dentro de `Codigos/`.

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

> Os structs/enums do protocolo (`CMD`, `TELEMETRY_LOG`, `func_type`,
> `target_type`, etc.) são definidos em `Config/SIBA.yaml`/`Config/SAME.yaml`
> e replicados (gerados por `generate.py`, ver `SIBA/CLAUDE.md`) nos headers
> C de cada firmware — **os `.yaml` aqui são a fonte de verdade do protocolo**,
> tanto para o app Python quanto para o firmware.

---

## Estrutura relevante

```
Aplicacao_Pi/
├── Communication_Data/
│   └── Ethernet.py          # EthernetManager, BoardWorker, BoardSimulator
├── Config/
│   ├── SIBA.yaml            # Protocolo + endereçamento da SIBA (fonte de verdade)
│   ├── SAME.yaml            # Idem para a SAME
│   ├── commands.yaml        # Schema dos comandos do TinyTerminal (siba/same ...)
│   ├── plots.yaml           # Mapeamento TELEMETRY_LOG -> widgets de gráfico
│   ├── user_prefs.yaml      # Preferências persistidas da GUI
│   └── generate.py          # Gera headers C para SIBA/SAME a partir dos .yaml
├── GUI/
│   ├── mainAlbatrozGUI.py   # Janela principal — instancia EthernetManager
│   └── TinyTerminal.py      # Shell embutido — comandos siba/same/mock/native/...
└── mainApp.py               # Entry point
```

---

## Comunicação UDP — Endereçamento e Portas

O esquema de portas é **fixo e simétrico** entre PC e placa (definido tanto em
`Ethernet.py::EthernetManager.__init__` quanto nos firmwares — ver `SIBA/CLAUDE.md`
"Protocolo de Comunicação UDP"):

| Placa | IP padrão (`DEFAULT_IP_BOARD`) | PC escuta (`*_rx`, telemetria) | Placa escuta (`*_tx`, comandos) |
|-------|-------------------------------|-------------------------------|----------------------------------|
| SIBA  | `169.254.97.200` (link-local) | `5555`                        | `5000`                           |
| SAME  | `169.254.97.100` (link-local) | `4445`                        | `4444`                           |

- **TX (PC → Placa)**: `CMD` — 8 bytes, `<BBBfb>` (`func, instr, target, value, action`)
- **RX (Placa → PC)**: `TELEMETRY_LOG` — struct packed little-endian, definido
  em `structs.TELEMETRY_LOG` no `.yaml` de cada placa, a 10 Hz

`BoardWorker` (em `Ethernet.py`) é a thread que faz esse RX/TX real por placa;
`EthernetManager` gerencia um worker por placa (`workers["SIBA"]`/`["SAME"]`).

---

## Três formas de "ligar" uma placa (modos do `EthernetManager`)

A pergunta do `TODO.txt` ("é preciso verificar se algo está realmente
enviando dados para a rede ou só fingindo") motivou deixar isso bem explícito —
hoje existem **três modos**, escolhidos por placa, todos compatíveis com o
mesmo `BoardWorker`/protocolo/portas:

### 1. Hardware real (`<board> connect`)
`_hardware_connect_flow` → `eth.start_worker(board)` aponta o worker para o
`DEFAULT_IP_BOARD` (link-local `169.254.97.x`) da placa física, via cabo
Ethernet. Requer placa ligada + PC na mesma sub-rede (`check_subnet`).

### 2. Simulador Python (`<board> mock start/stop`)
`set_mock_mode(board, True)` → sobe um `BoardSimulator` (thread Python que
**finge** ser o firmware: gera telemetria senoidal+ruído e responde a `CMD`
com lógica simplificada de ganho/offset — ver `BoardSimulator._generate_telemetry_packet`/
`_handle_command`). Redireciona o worker para `127.0.0.1`. **É um fake**:
não roda nenhuma linha do firmware real — útil só para testar a GUI/gráficos
sem nenhuma placa.

### 3. Firmware real via `native_sim` (`<board> native start/stop`) — NOVO
`start_native_sim(board, exe_path)` → sobe o **binário real do firmware**,
compilado para a board `native_sim` do Zephyr (roda como processo POSIX no
host — ver `SIBA/CLAUDE.md`/`HW.md` "Modo Mock (native_sim)"), como
subprocesso, e redireciona o worker para `127.0.0.1`. A diferença crucial
em relação ao modo 2: **não é fake** — é o `System_Manager`/`CMD_Core`/`NCI`/
`OSSI` reais rodando, trocando pacotes UDP de verdade via NSOS (Native
Simulator Offloaded Sockets) nas MESMAS portas do hardware físico
(5000/5555 para SIBA). Isso responde diretamente à dúvida do `TODO.txt`:
com esse modo dá pra confirmar que o link UDP fim-a-fim (parsing de `CMD`,
geração de `TELEMETRY_LOG`, threads OSSI) funciona de verdade, sem precisar
de bancada — e sem o risco de "a GUI passar mas o firmware estar quebrado"
que o simulador Python (modo 2) não consegue detectar.

> ⚠ **Hoje só a SIBA tem alvo `native_sim`** (`make native_sim` em `SIBA/`,
> gera `SIBA/build_native_sim/zephyr/zephyr.exe`). A SAME ainda não tem essa
> estrutura — é um TODO em aberto (`SIBA/CLAUDE.md` → "SAME board: criar
> estrutura equivalente para `../SAME/` baseada na SIBA"). `start_native_sim`/
> os comandos `same native start|stop` já existem no código (genéricos),
> mas vão falhar com "binário não encontrado" até esse trabalho ser feito.

### Como rodar o modo 3 (SIBA)
```bash
cd ../SIBA
make native_sim          # builda build_native_sim/zephyr/zephyr.exe
```
Depois, no `TinyTerminal` da GUI:
```
siba native start        # sobe o processo, conecta worker em 127.0.0.1:5000/5555
siba test_io             # testa RX/TX isoladamente (ver abaixo)
siba native stop         # encerra o processo e restaura IP original
```
(equivalente para `same ...`, quando o alvo `native_sim` da SAME existir)

---

## Testando RX/TX individualmente — `eth.test_io(board)` / `<board> test_io`

Para responder objetivamente "a placa está recebendo E enviando?" sem depender
de inspecionar logs/gráficos da GUI, `EthernetManager.test_io(board, timeout)`:

1. Envia um `CMD` neutro (`FUNC_START`/`INSTR_NONE`/`TARGET_DEFAULT`,
   inofensivo para qualquer placa) — confirma **TX** (PC → placa) observando
   `BoardWorker.tx_count`
2. Aguarda até `timeout` segundos por qualquer pacote recebido — confirma
   **RX** (placa → PC, tipicamente `TELEMETRY_LOG`) observando `BoardWorker.rx_count`

Retorna `{"tx_ok", "rx_ok", "rx_packets"}`. Funciona **independente do modo**
(hardware real / mock Python / native_sim) — só observa contadores no socket,
não depende de parsing/callback da GUI. Use isso para isolar onde está o
problema: TX falha → socket/porta/firewall; RX falha → firmware não está
respondendo ou telemetria não está sendo gerada.

```
siba test_io     # ou: same test_io
```

---

## `EthernetManager` — referência rápida (`Communication_Data/Ethernet.py`)

| Método | O que faz |
|--------|-----------|
| `start_worker(board)` | Cria/inicia o `BoardWorker` (thread RX/TX UDP real) para a placa |
| `set_mock_mode(board, bool)` | Liga/desliga o `BoardSimulator` (fake Python) — modo 2 |
| `start_native_sim(board, exe_path)` / `stop_native_sim(board)` | Sobe/encerra o binário real do firmware (`native_sim`) como subprocesso — modo 3 |
| `test_io(board, timeout)` | Testa RX/TX isoladamente via contadores do worker (ver acima) |
| `send_command(board, raw_bytes)` | Enfileira um `CMD` (8 bytes) para TX |
| `check_connectivity(ip)` / `check_subnet(ip)` | Ping / verificação de sub-rede (modo 1, hardware real) |
| `stop()` | Encerra workers, simuladores Python e processos `native_sim` |

`mock_enabled[board]`/`native_enabled[board]` são dicts de estado consultáveis
pela GUI para refletir o modo ativo de cada placa na interface.

---

## TODO / Observações em aberto

- [ ] **SAME — alvo `native_sim`**: criar estrutura equivalente à da SIBA
      (`SAME/boards/native_sim.conf`, `HAL_mock.c`, `make native_sim` no
      `Makefile` da SAME) para que `same native start/stop` funcione de
      verdade — ver TODO espelhado em `SIBA/CLAUDE.md`.
- [ ] **`BoardSimulator` (modo mock) pode divergir do protocolo real** —
      ele simula calibração com lógica simplificada de string-matching em
      `target_type`/`default_action` (`_handle_command`); qualquer mudança
      no `.yaml`/firmware real pode não se refletir nele. O modo `native_sim`
      (modo 3) é a forma confiável de validar against o protocolo real —
      preferir esse quando a dúvida for "isso bate com o firmware?".
- [ ] **`TODO.txt`** registra que o "SAME config dialog" ainda usa o método
      MOCK — ao portar para `native_sim` (item acima), revisar esse dialog
      para oferecer os três modos de forma consistente com a SIBA.
