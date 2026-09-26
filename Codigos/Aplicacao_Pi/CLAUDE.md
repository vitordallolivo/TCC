# Aplicacao_Pi — Contexto do Projeto para Claude Code

## Visão Geral

Aplicação PySide6 (GUI) que roda no PC/Raspberry Pi e é o **cliente único**
das placas do projeto:

- **SIBA** (gerenciamento de energia/telemetria aerodinâmica, `../SIBA/`) e
  **SAME** (controle de inversor/motor, `../SAME/`) — via **UDP/Ethernet** com
  o protocolo compacto (`CMD` 8 bytes + `TELEMETRY_LOG`).
- **SPMM** (bancada provisória de caracterização mecânica do motor BLDC,
  `../SPMM/`) — via **Telnet (TCP)** para o shell de comandos + **UDP binário**
  para o log rápido. Protocolo *diferente* do da SIBA/SAME (ver
  `../SPMM/CLAUDE.md`).

As quatro pastas (`Aplicacao_Pi/`, `SIBA/`, `SAME/`, `SPMM/`) são irmãs dentro
de `Codigos/`. O usuário escolhe um **Modo de Operação** (`SPMM`/`SIBA`/
`SAME`/`SIBA+SAME`) e a GUI adapta transporte, comandos e telemetria.

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

> Os structs/enums do protocolo (`CMD`, `TELEMETRY_LOG`, `func_type`,
> `target_type`, etc.) são definidos em `Config/SIBA.yaml`/`Config/SAME.yaml`
> e replicados (gerados por `generate.py`, ver `SIBA/CLAUDE.md`) nos headers
> C de cada firmware — **os `.yaml` aqui são a fonte de verdade do protocolo**,
> tanto para o app Python quanto para o firmware.

---

## Estrutura de arquivos

```
Aplicacao_Pi/
├── Communication_Data/              # Camadas 1–3 (transporte / domínio / serviço)
│   ├── Ethernet.py                  # shim de compat: EthernetManager=LinkManager, BoardWorker=UdpStructLink
│   ├── registry.py                  # [domínio] BoardConfig + BoardRegistry (lê Config/<board>.yaml)
│   ├── codecs.py                    # [domínio] encode/decode CMD, TELEMETRY_LOG, log SPMM (funções puras)
│   ├── manager.py                   # [serviço] LinkManager (genérico, dirigido pelo registry)
│   ├── discovery.py                 # [serviço] DiscoveryService (ping/pong por transporte)
│   ├── estimation.py                # [serviço] EstimationRunner (roda scripts de Scripts/ como subprocesso)
│   ├── native_sim.py                # [serviço] NativeSimProcess (modo 3 — subprocesso do firmware)
│   ├── motor_config.py              # [domínio] perfis de motor (Config/motors/*.yaml) + build_motor_cmds
│   ├── motor_math.py                # [domínio] Clarke/Park (numpy) para recalcular Iq/Id offline
│   ├── spmm_analysis.py             # [domínio] regressão B/τc, derivada/J do coast-down (numpy puro)
│   ├── errors.py                    # [domínio] decodifica error_code (bitfield) POR PLACA em pt-BR (tabela vinda da seção error_bits do yaml de cada placa)
│   ├── links/                       # [transporte] BoardLink (ABC)
│   │   ├── base.py                  #   contrato: send()* abstrato, is_alive_link(), stop()
│   │   ├── udp_struct_link.py       #   UdpStructLink — SIBA/SAME (CMD 8B / TELEMETRY_LOG)
│   │   └── telnet_link.py           #   TelnetLink — SPMM (Telnet TCP + UDP binário; parada segura)
│   └── simulators/
│       └── board_simulator.py       #   BoardSimulator (modo 2 — fake Python)
├── Config/
│   ├── SIBA.yaml / SAME.yaml        # Protocolo + bloco `link:` (endereçamento, fonte de verdade)
│   ├── SPMM.yaml                    # Idem SPMM (transport: telnet, IP .150, Ts)
│   ├── motors/*.yaml                # Perfis de motor salvos (Motor → Criar/Carregar)
│   ├── commands.yaml                # Schema dos comandos do TinyTerminal (siba/same/spmm/graph...)
│   ├── plots.yaml                   # Mapeamento telemetria -> widgets de gráfico
│   ├── user_prefs.yaml              # Preferências persistidas da GUI
│   └── generate.py                  # Gera headers C para SIBA/SAME a partir dos .yaml
├── GUI/                             # Camada 4 (apresentação)
│   ├── mainAlbatrozGUI.py           # Janela principal — menus, gráficos, slider do motor, buffers
│   ├── TinyTerminal.py              # Shell embutido — comandos siba/same/spmm/mock/native/test_io/...
│   ├── BoardManagerDialog.py        # Popout: procurar placas, editar IP/porta (runtime), LED
│   ├── EstimationDialog.py          # Popout (Estimativas): ganhos SMO — R/L, custo/iterações ao vivo
│   ├── HeliceDialog.py              # Popout (Estimativas): coeficientes CT/CQ/J via CSV
│   ├── SPMMEnsaioDialog.py          # Popout (Estimativas): ensaios de atrito (B,τc) e inércia (J)
│   ├── LoadCellDialog.py            # Popout (Células de Carga): tara/ganho por peso conhecido
│   ├── SAMEConfigDialog.py          # Popout (Motor → Criar): perfil de motor + aplica no hardware
│   ├── SensorConfigDialog.py        # Popout (Opções): ganho/offset/limites de todos os sensores em lote
│   ├── ErrorPanelDialog.py          # Popout (Opções): lâmpadas com os erros/alarmes de CADA placa (SIBA, SAME, …)
│   └── InfoDialog.py                # Popout (Ajuda): guia do usuário em HTML
├── Scripts/                         # Scripts de estimação (trazidos de ../Equations)
│   ├── autotune_cost.py             #   ganhos SMO — recebe --R/--L/--Ts; emite PROGRESS/GEN/RESULT
│   ├── test_coeff_auto.py           #   simulação do SMO (dependência do autotune)
│   ├── helice_coeff.py              #   coeficientes CT/CQ/J — modo --csv ou simulação
│   └── plot_pi.py
├── mainApp.py                       # Entry point da GUI
└── native_cli.py                    # banco de teste CLI: sobe SIBA/SAME em native_sim e conversa por UDP
```

> **Arquitetura em camadas** (refactor do antigo `Ethernet.py` monolítico):
> Transporte (`links/`, agnóstico à placa) ↔ Domínio (`codecs`/`registry`/
> `motor_config`/`motor_math`/`spmm_analysis`/`errors`, funções puras) ↔
> Serviço (`LinkManager`/`DiscoveryService`/`EstimationRunner`) ↔
> Apresentação (`GUI/`). O `LinkManager` itera o **registry** (sem
> `if board == "SIBA"` hardcoded) e escolhe o `BoardLink` por `transport`.
> `Ethernet.py` é um **shim** que re-exporta os nomes antigos
> (`EthernetManager`, `BoardWorker`) — código legado continua funcionando.

---

## Comunicação — Endereçamento e Portas

| Placa | IP padrão | PC escuta (telemetria) | Placa escuta (comandos) |
|-------|-------------------------------|-------------------------------|----------------------------------|
| SIBA  | `169.254.97.200` (link-local) | `5555`                        | `5000`                           |
| SAME  | `169.254.97.100` (link-local) | `4445`                        | `4444`                           |
| SPMM  | `169.254.97.150` (link-local) | UDP log `5000`                | shell **Telnet TCP `23`**        |

- **TX (PC → Placa)**: `CMD` — 8 bytes, `<BBBfb>` (`func, instr, target, value, action`)
- **RX (Placa → PC)**: `TELEMETRY_LOG` — struct packed little-endian, definido
  em `structs.TELEMETRY_LOG` no `.yaml` de cada placa, a 10 Hz

> **SPMM não segue esse protocolo.** Comandos vão por **Telnet (TCP 23)** em
> texto (`arm`, `throttle <us>`, `coast`, `log start <hz>`…) e o log sai como
> **struct binário empacotado** por UDP na porta `5000` — cabeçalho `<HH>`
> (`count, ts_us`) + N amostras `<I11f>` (medição completa: n, omega, te, iq,
> id, theta_e, ia, ib, ic, va, vb, vc). Ver `../SPMM/CLAUDE.md` "Protocolo UDP
> de log".

`BoardLink` (`links/base.py` + subclasses) é a thread que faz RX/TX real por
placa; `LinkManager` gerencia um link por placa (`workers["SIBA"]`, etc.).

---

## Três formas de "ligar" uma placa (modos do `LinkManager`)

Todas as placas suportam os mesmos três modos, escolhidos individualmente:

### 1. Hardware real (`<board> connect`)
`_hardware_connect_flow` → `eth.start_link(board)` aponta o link para o
IP link-local da placa física, via cabo Ethernet. Requer placa ligada + PC
na mesma sub-rede (`check_subnet`).

### 2. Simulador Python (`<board> mock start/stop`)
`set_mock_mode(board, True)` → sobe um `BoardSimulator` (thread Python que
**finge** ser o firmware: gera telemetria senoidal+ruído e responde a `CMD`
com lógica simplificada de string-matching em `target_type`/`default_action`
— ver `BoardSimulator._generate_telemetry_packet`/`_handle_command`).
Redireciona o link para `127.0.0.1`. **É um fake**: não roda nenhuma linha
do firmware real — só serve para testar a GUI/gráficos sem nenhuma placa.
Só disponível para placas `udp_struct` (SIBA/SAME).

### 3. Firmware real via `native_sim` (`<board> native start/stop`)
`start_native_sim(board, exe_path)` → sobe o **binário real do firmware**,
compilado para a board `native_sim` do Zephyr (roda como processo POSIX no
host — ver `SIBA/CLAUDE.md`/`SAME/CLAUDE.md`/`SPMM/CLAUDE.md`, seção
"native_sim"), como subprocesso, e redireciona o link para um **loopback
distinto por placa** (`_native_loopback(cfg)` = `127.0.0.<último octeto do
IP real>`: SIBA `127.0.0.200`, SAME `127.0.0.100`), para SIBA e SAME
rodarem **juntas** sem compartilhar `127.0.0.1`. Diferente do modo 2:
**não é fake** — é o `CMD_Core`/`NCI`/`OSSI`(`_wq`)/`Observer_Manager`
reais rodando, trocando pacotes UDP de verdade via NSOS (Native Simulator
Offloaded Sockets) nas MESMAS portas do hardware físico. Isso permite
confirmar que o link UDP fim-a-fim (parsing de `CMD`, geração de
`TELEMETRY_LOG`, threads/filas) funciona de verdade, sem precisar de
bancada — sem o risco de "a GUI passar mas o firmware estar quebrado" que
o simulador Python (modo 2) não consegue detectar.

> ⚠ **Só Linux.** `zephyr.exe` é um ELF POSIX de Linux —
> `native_sim_supported()` guarda `start_native_sim`, e o botão "Sim.
> Nativa" do Board Manager fica desabilitado (com tooltip) fora do Linux.
>
> **`native_sim` é genérico por transporte** (`LinkManager.start_native_sim`):
> - **udp_struct (SIBA/SAME)**: comandos por UDP; swap do IP para o loopback
>   distinto da placa. Assim as duas rodam juntas sem colidir no `127.0.0.1`.
> - **telnet (SPMM)**: em native o firmware usa o **shell serial via stdin**
>   (`-uart_stdinout`), pois o backend Telnet TCP fica OFF em `native_sim`.
>   Os comandos de texto vão pelo **stdin do processo**
>   (`NativeSimProcess.write_stdin` → `TelnetLink.command_sink`), não por
>   socket TCP. A SPMM fica em `127.0.0.1` (o filtro de udp_log casa
>   `127.0.0.1`); a telemetria continua por UDP (NSOS).
>
> ✅ **SIBA, SAME e SPMM têm binário `native_sim`** — `make native_sim` em
> cada pasta gera `build_native_sim/zephyr/zephyr.exe`. O filtro de RX do
> `UdpStructLink` aceita `self.ip` OU qualquer loopback
> (`addr[0].startswith('127.')`), pois o kernel reporta a origem como
> `127.0.0.1` mesmo com destino `127.0.0.x`.

### Como rodar o modo 3 (SIBA)
```bash
cd ../SIBA
make native_sim          # builda build_native_sim/zephyr/zephyr.exe
```
Depois, no `TinyTerminal` da GUI:
```
siba native start        # sobe o processo, conecta o link em 127.0.0.200:5000/5555
siba test_io             # testa RX/TX isoladamente (ver abaixo)
siba native stop         # encerra o processo e restaura IP original
```
(equivalente para `same ...` em `127.0.0.100:4444/4445`, e `spmm native
start/stop` — via stdin, log em `127.0.0.1:5000`. Todas podem rodar ao
mesmo tempo.)

### Bugs históricos já corrigidos (relevantes se algo parecido reaparecer)

- **`sizeof(CMD)` no `native_sim` (pacote curto)**: `func_type`/
  `instruct_type` são enums C, cujo tamanho é impl-definido (4 bytes no gcc
  host, 1 byte só com `-fshort-enums` na placa RISC-V). `generate.py` +
  `Common_template.h.j2` marcam **só os enums usados no `CMD`** com
  `__attribute__((packed))` → 1 byte → `sizeof(CMD)=8` em qualquer
  compilador (`log_status` da telemetria continua em 4 bytes, sem
  `packed`). Se `sizeof(CMD)` voltar a divergir, regenere com
  `python Config/generate.py` e rebuilde o firmware.
- **Gráficos "somem" ao plotar SIBA+SAME juntas**: a GUI
  (`mainAlbatrozGUI._update_internal_buffers`) tem um único
  buffer/ptr/base de tempo partilhado por todas as placas. Cada pacote só
  traz as variáveis da sua placa — sem tratamento, as variáveis da outra
  placa ficavam `NaN` naquele índice e o pyqtgraph desenha `NaN` como
  buraco. Corrigido com **sample-and-hold** (forward-fill): antes de
  gravar o pacote, cada buffer carrega o último valor conhecido, então
  todo índice tem valor para toda variável (a placa mais lenta vira
  degrau).

---

## Testando RX/TX individualmente — `eth.test_io(board)` / `<board> test_io`

Para responder objetivamente "a placa está recebendo E enviando?" sem
depender de inspecionar logs/gráficos da GUI, `LinkManager.test_io(board,
timeout)`:

1. Envia um `CMD` neutro (`FUNC_START`/`INSTR_NONE`/`TARGET_DEFAULT`,
   inofensivo para qualquer placa) — confirma **TX** (PC → placa)
   observando `BoardLink.tx_count`.
2. Aguarda até `timeout` segundos por qualquer pacote recebido — confirma
   **RX** (placa → PC) observando `BoardLink.rx_count`.

Retorna `{"tx_ok", "rx_ok", "rx_packets"}`. Funciona **independente do
modo** (hardware real / mock Python / native_sim) — só observa contadores
no socket, não depende de parsing/callback da GUI.

```
siba test_io     # ou: same test_io
```

---

## `LinkManager` — referência rápida (`Communication_Data/manager.py`)

> `EthernetManager` é um **alias** do `LinkManager` (via `Ethernet.py`),
> mantido por compat. Ele é **genérico**: itera o `BoardRegistry` e cria o
> link certo por `transport` (UDP struct ou Telnet).

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

`mock_enabled[board]`/`native_enabled[board]` são dicts de estado
consultáveis pela GUI. `registry.get(id)` devolve o `BoardConfig` (ip/
ports/transport/Ts). Descoberta de placas: `DiscoveryService(manager).
probe(id)` / `.probe_all()`.

---

## Funcionalidades da GUI

- **Modo de Operação** (combo no canto da barra de menus:
  `SPMM|SIBA|SAME|SIBA+SAME`) — define quais links sobem
  (`_boards_in_mode`), para qual placa vai o throttle do slider
  (`_motor_board`) e quais gráficos ficam visíveis (`_apply_mode_plots`,
  que reordena os widgets `Plot_N` — os relevantes ao modo atual vão para
  o topo).
- **Papéis SIBA/SAME quanto ao motor**: a **SIBA aciona o ESC** (recebe o
  throttle via `CMD FUNC_ESC`; `_motor_board` = SIBA nos modos SIBA e
  SIBA+SAME). A **SAME é só leitura** das correntes/tensões de fase do
  ESC + estimativa (SMO) — no modo SAME sozinho, o slider/controles de
  motor da tela principal ficam desabilitados.
- **Controle do motor (slider `Motor_action`)**: `sliderReleased`/botão
  "Atualizar" envia o throttle atual (0-100% → 1000-2000µs); SPMM manda
  `arm`+`throttle <us>` por texto, SIBA manda `CMD FUNC_ESC`. "Desligar
  (Motor)" faz parada segura (SPMM: `throttle 1000`+`disarm`; SIBA:
  `FUNC_ESC(1000)`+`FUNC_STOP`) e zera o slider.
- **Menus (barra em português)**: **Motor** (Carregar/Criar perfil de
  motor), **Estimativas** (Ganhos SMO / Coeficiente da Hélice / Ensaios
  SPMM), **Bateria**, **Células de Carga** (Calibrar), **Opções**
  (Configuração de Sensores, Gerenciador de Placas, Painel de Erros,
  Ajuda).
- **`SPMMEnsaioDialog`** — ensaios de atrito (patamares → regressão
  `Te=B·ω+τc`, R²) e inércia (coast-down → `J=-(B·ω+τc)/(dω/dt)` em
  janelas). Aceita como fonte dos dados tanto a bancada SPMM quanto
  SIBA(RPM)+SAME(correntes, com `Te=Kt·Iq` recalculado via Clarke/Park em
  `motor_math.py`). Roda em thread separada da UI, salva CSV incremental,
  parada segura sempre no `finally`/`closeEvent`.
- **`LoadCellDialog`** — calibração das células de carga (HX711): tara
  (offset = raw atual) e ganho por **peso conhecido** (não um ganho
  calculado à mão — o firmware resolve `scale=(raw-tare)/peso`).
- **`SensorConfigDialog`** — ganho/offset/limites de todos os sensores da
  SIBA numa tabela só, aplicando apenas as células editadas.
- **`ErrorPanelDialog`** — lâmpadas para cada bit de erro/alarme, **uma seção por
  placa** (SIBA, SAME, …). `Communication_Data/errors.py` traduz o bitmask para
  português usando a tabela de CADA placa (bits iguais têm significado diferente
  entre placas), montada da seção `error_bits` do yaml — a mesma que gera o enum
  C `<placa>_error_bit_t` no firmware.
- **`BoardManagerDialog`** — procurar placas (ping/pong via
  `DiscoveryService`), editar IP/porta em runtime, LED verde/vermelho/
  cinza, e o botão "Simulação Nativa" (native_sim, só Linux).
- **`EstimationDialog`**/**`HeliceDialog`** — rodam os scripts de
  estimação (`Scripts/autotune_cost.py`, `Scripts/helice_coeff.py`) como
  subprocessos, com progresso em tempo real (pyqtgraph) e confirmação
  antes de gravar no `SAME.yaml`.
- **`TinyTerminal`** — shell embutido com comandos `siba`/`same`/`spmm`/
  `graph`/`ping`/`sudo`/etc. Ver detalhe completo no
  [`README.md`](README.md#8-tinyterminal--shell-embutido-da-gui) (árvore
  de comandos, parser de `CMD`, aliases de `target_type`).

---

## Banco de teste por linha de comando — `native_cli.py`

Sobe o `zephyr.exe` da SIBA ou da SAME e conversa com ele pelo MESMO caminho
da GUI (UDP real em loopback, CMD de 8 bytes montado a partir de
`Config/<placa>.yaml`), sem abrir a interface gráfica. Reaproveita
`registry`/`codecs`/`native_sim`, então o protocolo continua vindo de uma
fonte só.

```bash
python native_cli.py SIBA                          # sobe e abre o prompt
python native_cli.py SAME --no-spawn               # usa um já em execução
python native_cli.py SIBA -c arm -c tele -c off    # roteiro e sai
python native_cli.py SIBA -c "shell btn on" -c arm # `shell` fala com o shell do firmware
```

Comandos: `arm`, `off`, `force on|off`, `estop`, `esc <us>`, `reset`, `tele`,
`watch [n]`, `shell <linha>`, `raw f i t v a`, `sleep <s>`, `help`, `quit`.
Como sobe o firmware com `-uart_stdinout`, o `shell` dá acesso aos comandos de
diagnóstico do próprio firmware — inclusive o `btn on|off` da SIBA, que finge
o botão de emergência e só existe em `native_sim`.

## Observações em aberto

- **BUG ABERTO — gráficos não voltam ao normal depois de reconectar.**
  Conectar, desconectar e conectar de novo deixa os gráficos em estado
  inconsistente (não retomam a plotagem normal). Reproduzido pelo autor em
  26/09/2026. Ainda não investigado: provavelmente o buffer/curva do
  `pyqtgraph` não é rearmado no segundo `start_link`, ou o timer de replot
  fica preso ao worker antigo. Reiniciar a aplicação contorna.

- **`BoardSimulator` (modo mock) pode divergir do protocolo real** — ele
  simula calibração com lógica simplificada de string-matching em
  `target_type`/`default_action`; qualquer mudança no `.yaml`/firmware
  real pode não se refletir nele. O modo `native_sim` (modo 3) é a forma
  confiável de validar contra o protocolo real — preferir esse quando a
  dúvida for "isso bate com o firmware?".
- **Origem de R/L do motor**: `Scripts/autotune_cost.py` só produz ganhos
  do SMO (Γ1/Γ2/Γt) — R/L do motor vêm de medição de bancada (ensaio de
  rotor bloqueado, ver `../SAME/CLAUDE.md`) e são digitados manualmente no
  `SAMEConfigDialog`/`EstimationDialog`, não estimados pela GUI.
- **$K_v$ de placa é de linha**: os 310 RPM/V do motor já são grandeza de
  linha-linha — **não** dividir por √3 ao converter para a constante de FEM.
  `Scripts/test_coeff_auto.py` fazia `Kv = 310.0/math.sqrt(3)`; corrigido em
  22/08/2026 para `310.0`, com o autotuning rodado de novo e as figuras da
  monografia regeradas. Qualquer anotação que fale em "constante de FEM da
  simulação 3× maior que a medida" é anterior a essa correção.
- **Arquivos de `Scripts/` são CRLF**: preservar as quebras de linha ao
  editar (abrir/gravar em binário ou com `newline=''`), senão o diff acusa o
  arquivo inteiro alterado.
