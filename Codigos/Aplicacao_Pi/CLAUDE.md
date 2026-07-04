# Aplicacao_Pi — Contexto do Projeto para Claude Code

## Visão Geral

Aplicação PySide6 (GUI) que roda no PC/Raspberry Pi e se comunica com as placas
do projeto:

- **SIBA** (gerenciamento de energia/telemetria aerodinâmica, `../SIBA/`) e
  **SAME** (controle de inversor/motor, `../SAME/`) — via **UDP/Ethernet** com o
  protocolo compacto (`CMD` 8 bytes + `TELEMETRY_LOG`).
- **SPMM** (bancada provisória de caracterização mecânica do motor BLDC,
  `../SPMM/`) — via **Telnet (TCP)** para o shell de comandos + **UDP binário**
  para o log rápido de coast-down. Protocolo *diferente* do da SIBA/SAME (ver
  `../SPMM/CLAUDE.md`).

As quatro pastas (`Aplicacao_Pi/`, `SIBA/`, `SAME/`, `SPMM/`) são irmãs dentro de
`Codigos/`. A GUI é o **cliente unificado** das três placas — ver "Plano de Ação"
ao final.

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
├── Communication_Data/          # Camadas 1–3 (transporte / domínio / serviço)
│   ├── Ethernet.py              # SHIM de compat: EthernetManager=LinkManager, BoardWorker, ...
│   ├── registry.py              # [domínio] BoardConfig + BoardRegistry (lê Config/<board>.yaml)
│   ├── codecs.py                # [domínio] encode/decode CMD, TELEMETRY_LOG, log SPMM (puro)
│   ├── manager.py               # [serviço] LinkManager (genérico, dirigido pelo registry)
│   ├── discovery.py             # [serviço] DiscoveryService (ping/pong por transporte)
│   ├── estimation.py            # [serviço] EstimationRunner (roda scripts de ../Equations)
│   ├── native_sim.py            # NativeSimProcess (modo 3 — subprocesso do firmware)
│   ├── links/                   # [transporte] BoardLink (ABC)
│   │   ├── base.py              #   contrato: start/stop/send/is_alive_link + contadores
│   │   ├── udp_struct_link.py   #   UdpStructLink — SIBA/SAME (CMD 8B / TELEMETRY_LOG)
│   │   └── telnet_link.py       #   TelnetLink — SPMM (Telnet TCP + UDP binário; parada segura)
│   └── simulators/
│       └── board_simulator.py   #   BoardSimulator (modo 2 — fake Python)
├── Config/
│   ├── SIBA.yaml / SAME.yaml    # Protocolo + bloco `link:` (endereçamento, fonte de verdade)
│   ├── SPMM.yaml                # Idem SPMM (transport: telnet, IP .150, Ts, shell_commands)
│   ├── commands.yaml            # Schema dos comandos do TinyTerminal (siba/same/spmm ...)
│   ├── plots.yaml               # Mapeamento telemetria -> widgets de gráfico
│   ├── user_prefs.yaml          # Preferências persistidas da GUI
│   └── generate.py              # Gera headers C para SIBA/SAME a partir dos .yaml
├── GUI/                         # Camada 4 (apresentação)
│   ├── mainAlbatrozGUI.py       # Janela principal — instancia o LinkManager (via shim)
│   ├── TinyTerminal.py          # Shell embutido — comandos siba/same/spmm/mock/native/...
│   ├── BoardManagerDialog.py    # Popout: procurar placas, editar IP/porta (runtime), LED verde/vermelho
│   └── EstimationDialog.py      # Popout (menu Motor): estima ganhos SMO — R/L, custo/iterações ao vivo, aplica no SAME.yaml
├── Scripts/                     # Scripts de estimação (trazidos de ../Equations)
│   ├── autotune_cost.py         #   ganhos SMO — recebe --R/--L/--Ts; emite PROGRESS/GEN/RESULT
│   ├── test_coeff_auto.py       #   simulação do SMO (R_s/L_s injetados antes do 1º njit)
│   ├── plot_pi.py / helice_coeff.py
└── mainApp.py                   # Entry point
```

> **Arquitetura em camadas (refactor do antigo `Ethernet.py` monolítico).**
> Transporte (`links/`, agnóstico à placa) ↔ Domínio (`codecs`/`registry`,
> funções puras + `BoardConfig` dirigido por YAML) ↔ Serviço (`LinkManager`/
> `DiscoveryService`/`EstimationRunner`) ↔ Apresentação (`GUI/`). O
> `LinkManager` itera o **registry** (sem `if board == "SIBA"` hardcoded) e
> escolhe o `BoardLink` por `transport`. `Ethernet.py` virou um **shim** que
> re-exporta os nomes antigos — imports existentes não quebram.

---

## Comunicação UDP — Endereçamento e Portas

O esquema de portas é **fixo e simétrico** entre PC e placa (definido tanto em
`Ethernet.py::EthernetManager.__init__` quanto nos firmwares — ver `SIBA/CLAUDE.md`
"Protocolo de Comunicação UDP"):

| Placa | IP padrão (`DEFAULT_IP_BOARD`) | PC escuta (`*_rx`, telemetria) | Placa escuta (`*_tx`, comandos) |
|-------|-------------------------------|-------------------------------|----------------------------------|
| SIBA  | `169.254.97.200` (link-local) | `5555`                        | `5000`                           |
| SAME  | `169.254.97.100` (link-local) | `4445`                        | `4444`                           |
| SPMM  | `169.254.97.100` (link-local) ⚠️ | UDP log `5000`             | shell **Telnet TCP `23`**        |

- **TX (PC → Placa)**: `CMD` — 8 bytes, `<BBBfb>` (`func, instr, target, value, action`)
- **RX (Placa → PC)**: `TELEMETRY_LOG` — struct packed little-endian, definido
  em `structs.TELEMETRY_LOG` no `.yaml` de cada placa, a 10 Hz

> ⚠️ **SPMM não segue esse protocolo.** Comandos vão por **Telnet (TCP 23)** em
> texto (`arm`, `throttle <us>`, `coast`, `log start <hz>`…) e o log sai como
> **struct binário empacotado** por UDP na porta `5000` — cabeçalho
> `<HH>` (`count, ts_us`) + N amostras `<Iff>` (`n, omega, te`). Ver
> `../SPMM/CLAUDE.md` "Protocolo UDP de log". **Além disso, o IP padrão da SPMM
> (`169.254.97.100`) colide com o da SAME** — decisão em aberto no Plano de Ação.

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

> ⚠ **Só Linux.** `zephyr.exe` é um ELF POSIX de Linux — `native_sim_supported()`
> guarda `start_native_sim`, e o botão "Sim. Nativa" do Board Manager fica
> desabilitado (com tooltip) fora do Linux.
>
> ⚠ **native_sim é genérico por transporte** (`LinkManager.start_native_sim`,
> `start_native_sim_auto`):
> - **udp_struct (SIBA/SAME)**: comandos por UDP; só swap do IP p/ 127.0.0.1.
> - **telnet (SPMM)**: em native o firmware usa o **shell serial via stdin**
>   (`-uart_stdinout`), pois o backend Telnet TCP fica OFF em native_sim (ver
>   `SPMM/boards/native_sim.conf`). Por isso os comandos de texto vão pelo
>   **stdin do processo** (`NativeSimProcess.write_stdin` → `TelnetLink.command_sink`),
>   não por socket TCP. A telemetria continua por UDP (NSOS 127.0.0.1:5000).
>
> ⚠ **Hoje só a SIBA tem binário `native_sim`** (`make native_sim` em `SIBA/`).
> SAME/SPMM: o código já é genérico, mas falham com "binário não encontrado"
> até `make native_sim` existir/rodar em cada uma.

> 🐛 **Bug corrigido — `sizeof(CMD)` no native_sim (pacote curto).** O firmware
> checa `received < sizeof(CMD)` (NCI). `func_type`/`instruct_type` são **enums
> C**, cujo tamanho é impl-definido: **4 bytes** no gcc host/native_sim (enum=int),
> **1 byte** só com `-fshort-enums` (placa RISC-V). Com 4 bytes, `sizeof(CMD)`
> dava **14**, mas o PC manda o formato compacto `<BBBfb>` = **8 bytes** → o
> firmware rejeitava ("pacote curto"). Correção: `Config/generate.py` +
> `Common_template.h.j2` agora marcam **só os enums usados no CMD**
> (`func_type`, `instruct_type`) com `__attribute__((packed))` → 1 byte →
> `sizeof(CMD)=8` em qualquer compilador (`log_status` da telemetria fica 4
> bytes, intacto). **Regenerar** com `python Config/generate.py` e **rebuildar**
> o firmware (`make native_sim`). Validado por `gcc` host: 14 → 8 bytes.

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

## `LinkManager` — referência rápida (`Communication_Data/manager.py`)

> `EthernetManager` é um **alias** do `LinkManager` (via `Ethernet.py`), mantido
> por compat. Novo código deve usar `LinkManager`. Ele é **genérico**: itera o
> `BoardRegistry` e cria o link certo por `transport` (UDP struct ou Telnet).

| Método | O que faz |
|--------|-----------|
| `start_link(board)` (alias `start_worker`) | Cria/inicia o `BoardLink` (thread I/O) da placa, conforme o transporte do registry |
| `send_command(board, data)` | Enfileira um `CMD` (SIBA/SAME) **ou** texto de shell (SPMM) para TX |
| `update_link(board, ip, ports)` | Atualiza IP/portas **em runtime** (Board Manager) — não grava YAML |
| `set_mock_mode(board, bool)` | Liga/desliga o `BoardSimulator` (fake Python) — modo 2 (só udp_struct) |
| `start_native_sim(board, exe)` / `stop_native_sim(board)` | Sobe/encerra o firmware real (`native_sim`) — modo 3 |
| `test_io(board, timeout)` | Testa RX/TX isoladamente via contadores do link |
| `check_connectivity(ip)` / `check_subnet(ip)` | Ping / verificação de sub-rede (modo 1, hardware real) |
| `stop()` | Encerra links (parada segura da SPMM), simuladores e `native_sim` |

`mock_enabled[board]`/`native_enabled[board]` são dicts de estado consultáveis
pela GUI. `registry.get(id)` devolve o `BoardConfig` (ip/ports/transport/Ts).
Descoberta de placas: `DiscoveryService(manager).probe(id)` / `.probe_all()`.

---

## Plano de Ação — GUI unificada (SPMM + SIBA + SAME) + scripts de estimação

> **Objetivo geral.** Transformar a `Aplicacao_Pi` no **cliente único** de todas
> as placas. O usuário escolhe um **"Modo de Operação"** — `SPMM`, `SIBA`,
> `SAME` ou `SIBA+SAME` — e a GUI adapta transporte, comandos e telemetria. A GUI
> também passa a **rodar os scripts de estimação** (ganhos do SMO, coeficiente da
> hélice, parâmetros do motor) como subprocessos, mostrando/plotando os
> resultados e perguntando ao usuário antes de aplicá-los.

### Fase 1 — Modularizar `Ethernet.py` (registry de placas + abstração de link)

Hoje `EthernetManager` tem `SIBA`/`SAME` **hardcoded** (IPs, portas e o
`BoardWorker` assumem o protocolo `CMD`/`TELEMETRY_LOG`). Como a SPMM fala um
protocolo **diferente** (Telnet + UDP binário), precisamos separar *transporte*
de *gerência*:

- **Registry de placas** (dict, idealmente alimentado por um `Config/<board>.yaml`
  por placa): cada entrada tem `id`, `transport`, `ip`, portas e o schema do
  protocolo. Remover os `if board == "SIBA"` espalhados.
- **Abstração `BoardLink`** (classe base) com duas implementações:
  - `UdpStructLink` — o `BoardWorker` atual **generalizado** (SIBA/SAME): TX
    `CMD` 8 bytes, RX `TELEMETRY_LOG` do YAML.
  - `TelnetLink` — **novo** (SPMM): mantém a sessão **Telnet (TCP 23)** para
    comandos de texto e um socket **UDP (5000)** que faz `parse` do struct
    binário (`<HH>` + N×`<Iff>`) → entrega ao mesmo `callback`
    (`process_network_data`) que a GUI já usa.
- **Identificador por placa + uma thread por placa.** Manter o modelo de *uma
  thread de I/O por placa* (já é o padrão e evita contenção entre placas), mas
  com a classe **agnóstica à placa** (dirigida pelo registry), não duplicada.
  *(Decisão em aberto: o pedido menciona "thread único" — confirmar se é
  literal (1 thread multiplexando todas as placas via `select`) ou "um worker
  genérico" como descrito aqui. Recomendação: worker genérico por placa.)*
- `EthernetManager` vira genérico: `register_board(cfg)`, `start_worker(id)`,
  `send_command(id, ...)`, `stop()` iterando sobre o registry.

### Fase 2 — SPMM como placa de primeira classe

- **`Config/SPMM.yaml`** (fonte de verdade, como SIBA/SAME): IP, porta UDP de log,
  porta Telnet, `Ts` de amostragem, o dicionário de comandos do shell
  (`arm`/`disarm`/`throttle`/`ramp`/`rpm`/`iq`/`read`/`patamar`/`coast`/`log`)
  e o layout do pacote binário (`count, ts_us` + `n, omega, te`).
- **`TelnetLink`**: envia comandos de bancada por Telnet; reconstrói o tempo como
  `t = n·Ts` (contador de amostra, **nunca** o horário de chegada do pacote —
  ver `../SPMM/CLAUDE.md` "Base de tempo").
- **Segurança (crítico).** O desligamento `disarm` + `throttle 1000` roda
  **sempre** no `stop()`/`finally` do link SPMM — o motor nunca pode ficar
  girando se a GUI cair. Replicar a filosofia do script Python do SPMM.
- **Comandos no `TinyTerminal`**: adicionar `spmm ...` ao `commands.yaml` no mesmo
  molde de `siba`/`same` (incluindo `spmm mock`/`native` quando fizer sentido).

### Fase 3 — Descoberta / identificação de placas conectadas (ping/pong)

- Ao abrir a GUI (ou no "conectar"), **descobrir** quais placas respondem, para
  popular o "Modo de Operação" só com o que está presente:
  - SIBA/SAME: `check_connectivity` (ping ICMP) + `test_io` (CMD neutro → espera
    `TELEMETRY_LOG`) — infra que **já existe**.
  - SPMM: abrir a sessão Telnet e mandar um comando leve (`rpm`) esperando
    resposta, ou ping no IP. Um **"pong"** confirma presença.
- Refletir na UI o estado de cada placa (presente / mock / native_sim).

### Fase 4 — Rodar scripts de estimação pela GUI (aba "Motor")

- **Aba/menu "Motor"** com os parâmetros do motor: `R`, `L`, e coeficientes
  (`Ke`/`Kt`, ganhos SMO `Ks1/Ks2/Kt`, `kQ` da hélice). Fonte primária:
  `SAME.yaml → motors` (o `on_load_motor` já lê essa lista).
- **Se faltar parâmetro → botão "Estimar"**, que roda o script apropriado como
  **subprocesso** (mesmo padrão do `start_native_sim`: `subprocess.Popen`,
  relay de stdout pro terminal):
  - Ganhos do SMO → `../Equations/autotune_cost.py` (gera `smo_gains.json` +
    figuras de convergência/sensibilidade/FFT).
  - *(R/L não são estimados pelo `autotune_cost.py` — ele otimiza ganhos do
    observador. Decisão em aberto: definir a fonte de R/L — medição de bancada
    já caracterizada em `../SPMM/CLAUDE.md`, ou um estimador dedicado.)*
- **Fluxo de confirmação** (pedido explícito): rodar → **mostrar os valores** e
  perguntar *"usar os resultados diretamente?"*; e perguntar *"quer que eu plote
  os resultados como no script?"*. Só grava no `SAME.yaml`/motor após o "sim".

### Fase 5 — Coeficiente da hélice a partir de `.csv`

- Adaptar `../Equations/helice_coeff.py` para **consumir um `.csv`** de ensaio
  (RPM, torque — vindos do SPMM/coast-down/patamares) em vez da simulação
  embutida (`test_coeff_auto.py`).
- **Com velocidade do ar**: usar o modelo que inclui `V` (avanço `J = V/(nD)`);
  **sem velocidade do ar** (estático): cair no algoritmo simples `T = kQ·ω²`
  (identificação de `kQ` por patamar, como o script já faz).
- Mesmo fluxo da Fase 4: estimar → plotar → mostrar → perguntar antes de aplicar.

### Fase 6 — UI: "Modo de Operação"

- Combo/menu **"Modo de Operação"**: `SPMM` | `SIBA` | `SAME` | `SIBA+SAME`,
  populado pela descoberta da Fase 3. Ele define quais links sobem e qual conjunto
  de gráficos/comandos aparece.
- `plots.yaml` ganha o mapeamento das telemetrias da SPMM (`omega`/`RPM`, `Iq`,
  `Te`) para widgets, no mesmo esquema já usado por SIBA/SAME.

### Ordem sugerida de implementação
1. Fase 1 (refactor sem quebrar SIBA/SAME — validar com mock/native_sim).
2. Fase 2 (`SPMM.yaml` + `TelnetLink` + parser binário).
3. Fase 3 (descoberta) + Fase 6 (Modo de Operação).
4. Fase 4 (runner de scripts + aba Motor) e Fase 5 (hélice via CSV).

### Decisões em aberto (confirmar antes de codar)
- **Conflito de IP SPMM × SAME** (`169.254.97.100` para ambos): trocar o IP de
  uma delas, ou tratar como mutuamente exclusivas (nunca no mesmo modo).
- **"Thread único"**: worker genérico por placa (recomendado) vs. 1 thread
  multiplexando todas.
- **Origem de R/L do motor**: qual script/medida os produz (o `autotune_cost.py`
  só dá ganhos do SMO).
- **SPMM define seu protocolo em `Config/SPMM.yaml`?** (recomendado, p/
  consistência com a fonte-de-verdade YAML).

---

## Status da implementação (refactor em camadas + SPMM)

- [x] **Fase 1** — `Ethernet.py` fatiado em camadas; `LinkManager` dirigido por
      `BoardRegistry` (YAML). Shim de compat mantém `EthernetManager`/`BoardWorker`.
- [x] **Fase 2** — `TelnetLink` (SPMM): Telnet TCP + UDP binário (`decode_spmm_log`,
      `t = n·Ts`), **parada segura** (disarm + throttle 1000 no `stop()`).
      `Config/SPMM.yaml` criado; `spmm ...` no `commands.yaml`/TinyTerminal.
- [x] **Board Manager (pedido do usuário)** — `GUI/BoardManagerDialog.py`: procurar
      placas, editar IP/porta em runtime, LED verde/vermelho/cinza (menu Config).
- [x] **Fase 3 (parcial)** — `DiscoveryService` (ping/pong por transporte) usado
      pelo Board Manager e por `spmm detect`.
- [x] **Fase 4 (backend+UI)** — scripts trazidos para `Scripts/`;
      `autotune_cost.py` recebe **R/L/Ts** (`--R/--L/--Ts`) e emite
      `PROGRESS`/`GEN`/`RESULT`. `EstimationRunner` roda como subprocesso e faz
      stream das linhas. `EstimationDialog` (menu **Motor**): campos R/L, custo
      e iterações **em tempo real** (curva pyqtgraph), checkbox "plotar
      resultado", e "Aplicar ao SAME.yaml" (bloco `smo_gains`, com confirmação).
- [x] **Fase 5 (hélice via CSV)** — `Scripts/helice_coeff.py` ganhou modo
      `--csv` (estático `T=kQ·ω²`, ajuste por mínimos quadrados + R²; V/D
      opcionais p/ razão de avanço). `HeliceDialog` (menu Motor → "Estimar
      Coeficiente da Hélice"): escolhe CSV, roda, mostra kQ/R² e aplica no
      SAME.yaml (bloco `helice`).
- [x] **Menus em português (topo)** — barra reorganizada por `_build_menus()`:
      **Motor** (Carregar/Criar/Estimar Ganhos SMO/Estimar Hélice), **Bateria**,
      **Células de Carga** (Calibrar/Configurar), **Gerenciador de Placas**
      (Abrir), **Ajuda** — todos no topo (fora do antigo "Config"), traduzidos.
      Menus criados como `QMenu(title, menubar)` (posse em C++) — `addMenu(str)`
      some da barra por GC.
- [x] **Calibração de célula de carga (HX711)** — `LoadCellDialog` (menu Células
      de Carga): média das últimas N medições ao vivo; **Tara** → `offset =
      média` (`siba set <cell> offset`); **Ganho** → `peso / média`
      (`siba set <cell> linear`); **Reset**. Aplica os CMD reais reaproveitando
      o parser do TinyTerminal (`get_recent_average` lê os buffers da GUI).
- [x] **Fase 6 (UI) — Modo de Operação** — combo no canto da barra de menus
      (`SPMM|SIBA|SAME|SIBA+SAME`); define quais links sobem (`_boards_in_mode`)
      e para qual placa vai o throttle (`_motor_board`).
- [x] **Gráficos por modo** — `_apply_mode_plots` mostra só os widgets cujas
      variáveis pertencem às placas do modo (`_var_boards` derivado dos
      TELEMETRY_LOG + telemetry_fields da SPMM).
- [x] **Ensaios da SPMM (B, τc, J)** — `SPMMEnsaioDialog` (menu Motor) porta o
      cliente de bancada de `../SPMM/CLAUDE.md`: **Atrito** (patamares → regressão
      `Te=B·ω+τc`, R²) e **Inércia** (coast-down → `J=-(B·ω+τc)/(dω/dt)` em
      janelas, com `t=n·Ts` via gancho `_spmm_sample_sink`). Roda em thread,
      salva CSV incremental (repetições), e **parada segura** (`disarm`+`throttle
      1000`) sempre no `finally`/closeEvent. Matemática em
      `Communication_Data/spmm_analysis.py`.
- [x] **Controles da tela principal ligados** — slider `Motor_action` = throttle:
      SPMM → `arm` + `throttle <us>` (1000–2000); SAME → CMD `FUNC_START` com
      value=duty. "Atualizar (Motor)"/soltar o slider **envia**; "Desligar
      (Motor)" faz parada segura (SPMM `throttle 1000`+`disarm`; SAME `FUNC_STOP`).
      "Conectar" sobe os links do modo atual.
- [ ] **IP da SPMM** — trocado para `169.254.97.150` no lado PC (`SPMM.yaml`);
      falta alinhar o **firmware** em `../SPMM/` para o mesmo IP.

## TODO / Observações em aberto

- [ ] **SPMM UI** — Modo de Operação + aba Motor (Fases 6 e 4-5 na apresentação).
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
