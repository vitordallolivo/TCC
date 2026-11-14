# Aplicacao_Pi — GUI unificada da bancada (Albatroz)

> [README principal](../../README.md) · [Visão geral do software](../README.md) · [Geração do protocolo](Config/README.md)

**Sumário:** [Como rodar](#como-rodar) · [Estrutura de pastas](#estrutura-de-pastas) · [Modos de conexão](#as-três-formas-de-conectar-em-uma-placa-e-quando-usar-cada-uma) · [Camadas](#estrutura-do-programa-e-o-porquê-de-cada-camada) · [Protocolo](#o-protocolo-de-comandotelemetria-por-dentro) · [TinyTerminal](#tinyterminal--o-shell-embutido-na-interface) · [Telas](#o-que-dá-para-fazer-tela-por-tela) · [Passo a passo](#passo-a-passo-prático-rodando-um-ensaio-do-zero)

## Para que serve este programa

Esta é a aplicação de PC (roda também num Raspberry Pi) que **comanda e lê
as duas placas** da bancada de ensaios de motopropulsão da equipe Albatroz
Aerodesign — [SIBA](../SIBA/README.md) (aerodinâmica + bateria + ESC) e
[SAME](../SAME/README.md) (estimativa elétrica do motor). Sem essa
aplicação, cada placa só fala um protocolo binário de baixo nível —
é ela que transforma esses bytes em gráficos em tempo real, calibração
guiada, e nos números que efetivamente interessam para quem projeta a
aeronave: coeficientes de empuxo e torque da hélice e ganhos do
observador de motor.

**Por que isso importa para quem calcula o desempenho da aeronave**: o
time de desempenho/aerodesign precisa, para prever alcance, autonomia e
envelope de voo, de um modelo confiável do conjunto motor+hélice — não só
"quanto empuxo esse motor dá a plena carga", mas como o empuxo, o torque e
o consumo variam com a rotação e com a velocidade do ar incidente. Sem uma
bancada instrumentada, esses números viriam só de datasheet (que raramente
bate com o conjunto de peças específico realmente montado) ou de voos de
teste (caros, arriscados, e difíceis de variar uma condição de cada vez).
Esta aplicação é o que fecha o laço entre "girar o motor na bancada" e
"ter uma tabela de coeficientes ($C_T$, $C_Q$, ganhos
do observador) prontos para entrar no modelo de desempenho da aeronave" —
sem precisar que quem está calculando desempenho leia telemetria bruta ou
escreva scripts de regressão do zero.

Escrita em **Python com PySide6** (Qt) e **pyqtgraph** para a interface e
os gráficos. Os cálculos da própria GUI usam só **NumPy**; SciPy e Numba ficam restritos aos
scripts de estimação em `Scripts/`, que rodam como subprocessos.

Projeto de TCC — Equipe Albatroz Aerodesign, UDESC Joinville.

## Como rodar

```bash
python mainApp.py     # Python >= 3.11 (dependências em pyproject.toml)
```

Ao abrir, o programa primeiro confere se todas as dependências do
`pyproject.toml` estão instaladas (instalando as que faltarem
automaticamente) e mata qualquer processo `zephyr.exe` órfão de uma
execução anterior que tenha travado sem limpar — isso existe porque, sem
essa limpeza, um processo de `native_sim` esquecido continuaria
respondendo na mesma porta UDP que uma nova instância tentaria usar,
misturando dados de duas fontes diferentes e sendo bem difícil de
diagnosticar (o sintoma seria "a telemetria está estranha" sem pista
nenhuma de que existem dois firmwares rodando ao mesmo tempo).

---

### Estrutura de pastas

```text
Aplicacao_Pi/
├── mainApp.py               ponto de entrada da GUI
├── native_cli.py            cliente de terminal para o firmware em native_sim (sem GUI)
├── build.py                 empacota a aplicação com PyInstaller (--onedir)
├── debug_log.py             logs em arquivo (logs/crash.log, app.log, session.log)
├── debug_run.sh             roda a GUI sob o gdb para capturar segfaults em C/C++
├── pyproject.toml           dependências
├── Config/                  YAMLs do protocolo + generate.py (ver Config/README.md)
├── Communication_Data/      transporte, domínio e serviço (seção "Estrutura do programa")
├── GUI/                     janela principal, TinyTerminal e diálogos
└── Scripts/                 estimação (SMO, hélice) e banco de testes de CMD
```

### Ferramentas de linha de comando

| Comando | Para que serve |
|---|---|
| `python native_cli.py SIBA` | Sobe o `zephyr.exe` da placa e abre um prompt que fala o mesmo protocolo da GUI. `--no-spawn` usa um processo já rodando; `-c "<cmd>"` roteiriza comandos (ex.: `-c arm -c tele`) |
| `python Scripts/cmd_test.py [--board SAME] [--send]` | Monta e decodifica **todos** os `CMD` possíveis; com `--send` sobe o `native_sim` e mede se a telemetria muda após cada comando |
| `python build.py` | Gera o executável com PyInstaller; no Linux embute os binários `native_sim` das duas placas |
| `./debug_run.sh` | Roda a GUI sob o `gdb`; o backtrace vai para `logs/gdb_crash.log` |

---

## As três formas de conectar em uma placa, e quando usar cada uma

Antes de descrever os módulos internos, vale entender essa decisão porque
ela aparece em quase toda tela da aplicação (no combo "Modo de Operação",
no Gerenciador de Placas, no terminal). Para cada placa (SIBA, SAME)
existem três formas independentes de "ligar":

1. **Hardware real** — a placa física, ligada por cabo Ethernet
   num IP link-local fixo (por exemplo, `169.254.97.200` para a SIBA). É o modo
   de verdade, usado em bancada com o motor de fato girando.

2. **Simulador em Python (mock)** — quando não há placa nenhuma
   disponível e só se quer testar a interface (por exemplo, ajustar o
   layout de um gráfico, ou testar um fluxo de calibração sem gastar
   tempo de bancada), a aplicação sobe uma thread Python que **finge** ser
   o firmware: ela decodifica os comandos recebidos e devolve telemetria
   senoidal com ruído. É importante entender que esse modo **não executa
   nenhuma linha do firmware de verdade** — ele simula a calibração com
   uma lógica simplificada de correspondência de nomes entre o comando
   recebido e as variáveis simuladas. Se o `.yaml` do protocolo mudar (um
   campo novo, um enum renomeado), esse simulador pode não perceber a
   mudança e continuar respondendo de um jeito que não bate mais com o
   firmware real — por isso ele só deve ser usado para testar a interface
   gráfica, nunca para validar se "o protocolo está funcionando".

3. **`native_sim`** — o meio-termo mais confiável: o **firmware de
   verdade**, compilado não para o microcontrolador RISC-V, mas para rodar
   como um processo comum do Linux (usando a arquitetura POSIX que o
   próprio Zephyr oferece — sem emulação de CPU nenhuma, é uma compilação
   nativa para x86/ARM do PC). Esse processo troca pacotes UDP reais com a
   aplicação, nas mesmas portas que a placa física usaria, só que em
   loopback (`127.0.0.x`) em vez do IP link-local — e como é o firmware de
   verdade rodando, qualquer bug de protocolo, de parsing, ou de lógica
   (não de hardware físico) aparece exatamente como apareceria na placa
   real. Isso resolve um problema concreto que já aconteceu neste
   projeto: com o simulador Python (modo 2), a interface podia parecer
   estar funcionando perfeitamente enquanto o firmware de verdade, se
   testado, rejeitaria os pacotes por um motivo qualquer de formato — só
   descoberto quando alguém finalmente testasse na bancada física.
   `native_sim` elimina esse risco de falso-positivo sem precisar de
   bancada. Limitação: só funciona no Linux (o binário gerado é um ELF
   POSIX de Linux), e cada placa usa um endereço de loopback diferente
   (SIBA `127.0.0.200`, SAME `127.0.0.100`) para as duas
   poderem rodar ao mesmo tempo sem se confundirem.

```mermaid
stateDiagram-v2
    [*] --> SemLink: (nenhum link ativo)
    SemLink --> HardwareReal: conectar → aponta pro IP físico da placa
    SemLink --> ModoMock: mock start → sobe BoardSimulator (fake)
    SemLink --> ModoNative: native start → sobe zephyr.exe real
    HardwareReal --> SemLink: desconectar
    ModoMock --> SemLink: mock stop — restaura IP original
    ModoNative --> SemLink: native stop — mata processo, restaura IP
    note right of ModoMock
        Python fingindo ser o firmware
        127.0.0.1, sem rodar código real
    end note
    note right of ModoNative
        firmware REAL como processo
        loopback distinto por placa
    end note
```

---

## Estrutura do programa e o porquê de cada camada

O código é organizado em quatro camadas, de baixo para cima. A separação
isola "como os bytes trafegam" de "o que a aplicação faz com eles": quem
gerencia o ciclo de vida de uma conexão (camada de Serviço) só chama
`send`, e a implementação concreta do transporte cuida do resto. Nada no
`LinkManager` é escrito para uma placa específica — tudo vem do
`registry`, lido dos `.yaml`.

```mermaid
flowchart TD
    subgraph GUIL["Apresentação — GUI/"]
        Main["mainAlbatrozGUI.py"]
        Tiny["TinyTerminal.py"]
        Dlgs["8 dialogs (Board Manager, Estimation, Hélice, Load Cell...)"]
    end
    subgraph Serv["Serviço"]
        Mgr["manager.py — LinkManager"]
        Disc["discovery.py — DiscoveryService"]
        EstR["estimation.py — EstimationRunner"]
        NS["native_sim.py — NativeSimProcess"]
    end
    subgraph Dom["Domínio (funções puras)"]
        Reg["registry.py"]
        Cod["codecs.py"]
        MC["motor_config.py"]
        Errs["errors.py"]
    end
    subgraph Trans["Transporte"]
        Base["links/base.py — BoardLink"]
        Udp["links/udp_struct_link.py"]
        Sim["simulators/board_simulator.py"]
    end

    Main --> Tiny & Dlgs
    Main --> Mgr
    Tiny --> Mgr
    Dlgs --> Mgr & Disc & EstR
    Mgr --> Reg & Base & Sim & NS
    Base --> Udp
    Udp --> Cod
```

### Camada de transporte (`Communication_Data/links/` e `simulators/`)

Define o contrato comum a qualquer placa: uma thread própria de entrada/
saída (`BoardLink`, uma classe abstrata), com um método `send()` para
mandar dado e contadores de pacotes recebidos/enviados que servem tanto
para os LEDs de status na tela quanto para o diagnóstico de rede (seção
"Testando a conexão", abaixo). A implementação concreta é:

- **`UdpStructLink`** (SIBA/SAME): um único socket UDP faz recepção e
  envio no mesmo laço — a cada volta do laço, tenta receber um pacote (com
  um timeout curto de 0,1 segundo, para não travar indefinidamente) e, se
  houver algo na fila de envio, manda.

O `BoardSimulator` (`simulators/`) implementa o modo mock: finge ser o
firmware respondendo na mesma porta.

### Camada de domínio (`registry.py`, `codecs.py`, `motor_config.py`, `errors.py`)

São **funções puras** — sem rede, sem thread, sem estado global — que
fazem a tradução entre bytes/YAML e valores com significado:

- `registry.py` lê os arquivos `Config/SIBA.yaml`/`SAME.yaml`
  (o bloco `link:` de cada um) e monta um objeto simples por placa com IP,
  portas e qual protocolo usar — assim, o resto do programa não precisa
  saber, "na unha", que a SIBA usa a porta 5000 e a SAME usa a 4444: essa
  informação vem de um arquivo de configuração, editável sem mexer em
  código.
- `codecs.py` empacota/desempacota os pacotes binários (`CMD` de 8 bytes,
  `TELEMETRY_LOG`) usando o módulo `struct` do Python,
  com o formato `<BBBfb>` (o `<` força little-endian, compatível com o
  RISC-V de destino).
- `motor_config.py` lida com os arquivos de perfil de motor
  (`Config/motors/*.yaml`) e sabe montar a lista de `CMD`s necessários
  para aplicar um perfil inteiro no hardware da SAME.
- `errors.py` traduz o `error_code` (mapa de bits) que cada placa manda na
  telemetria para mensagens em português — por exemplo, o bit 4 vira
  "Bateria superaquecendo (temperatura acima do limite de alarme)" em vez
  de só "bit 4 ativo".

### Camada de serviço (`manager.py`, `discovery.py`, `estimation.py`, `native_sim.py`)

- **`LinkManager`** é o orquestrador central: mantém um `BoardLink` por
  placa, decide qual implementação concreta criar a partir do que o
  `registry` disser sobre aquela placa, e expõe os três modos de conexão
  descritos acima (`start_link`, `set_mock_mode`, `start_native_sim`).
- **`DiscoveryService`** resolve a pergunta "essa placa está presente
  agora?": manda um comando neutro (que não muda nada no estado da placa)
  e espera qualquer pacote de telemetria como confirmação — a ideia de
  "pong" (por analogia a um ping/pong). Se nada chegar, tenta um ping ICMP.
- **`EstimationRunner`** roda os scripts de estimação (ganhos do SMO,
  coeficiente da hélice) como **subprocessos** Python separados — não
  dentro da mesma thread da interface — e retransmite, linha a linha, a
  saída padrão desses scripts para quem quiser mostrar o progresso (os
  diálogos de estimação, ver abaixo). Rodar como subprocesso evita que um
  script de otimização pesado (a busca de ganhos do SMO usa Evolução
  Diferencial, que pode rodar por vários segundos) trave a interface
  gráfica enquanto calcula.
- **`NativeSimProcess`** sobe/derruba o processo `native_sim` do firmware
  e retransmite o log dele para o console.

### Camada de apresentação (`GUI/`)

A janela principal (`mainAlbatrozGUI.py`) e nove janelas auxiliares
("dialogs"), cada uma dedicada a uma tarefa — detalhadas na seção
"O que dá para fazer, tela por tela" abaixo.

---

## O protocolo de comando/telemetria, por dentro

A SIBA e a SAME falam um protocolo idêntico em **formato** (8 bytes de
comando, uma struct de telemetria), mas com conteúdo (quais enums, quais
campos) diferente por placa — cada uma tem seu próprio arquivo `.yaml`.
Veja `../SIBA/README.md` e `../SAME/README.md` para o conteúdo completo de
cada struct; aqui o que importa é como o Python lida com esse formato:

```python
CMD_FORMAT = "<BBBfb"  # func(1B) instr(1B) target(1B) value(4B float) action(1B)

def encode_cmd(func, instr, target, value, action):
    return struct.pack(CMD_FORMAT, func, instr, target, float(value), action)
```

O `<` no formato força **little-endian** — a ordem de bytes que o
microcontrolador RISC-V usa nativamente; sem isso, num PC com arquitetura
que usasse a ordem contrária (big-endian, hoje incomum mas não impossível),
os bytes do `float` sairiam invertidos e o firmware leria um número sem
sentido nenhum. A struct de telemetria (`TELEMETRY_LOG`) é decodificada de
forma semelhante, mas o formato exato (quantos campos, em que ordem) é lido
dinamicamente do `.yaml` da placa em vez de estar escrito fixo no código
Python — isso é o que permite ao mesmo código Python (`codecs.py`) lidar
com o layout diferente da SIBA e da SAME sem duplicar a lógica de
decodificação.

---

## `TinyTerminal` — o shell embutido na interface

O `TinyTerminal` é uma janela de terminal de texto **dentro da própria
aplicação Python** — não confundir com o shell da SIBA (que roda dentro do
firmware, acessado por UART). O `TinyTerminal` roda no PC e fala com as
placas exatamente pela mesma rede que o resto da interface usa — ele
existe para dar acesso rápido a ações de diagnóstico e controle sem
precisar abrir um menu, e para servir como uma ferramenta de depuração
"crua" (mostra o pacote exato em hexadecimal, e a tradução campo a campo,
de cada comando enviado).

### Como um comando digitado vira ação

O texto digitado é dividido em palavras (`raw.split()`), e o terminal
tenta achar um método Python cujo nome corresponda às palavras digitadas,
do mais específico para o mais genérico — por exemplo, digitando
`siba mock start`, ele primeiro procura um método chamado
`_cmd_siba_mock_start`; se não achar, tenta `_cmd_siba_mock`; se também não
achar, tenta só `_cmd_siba`. Isso permite registrar tanto comandos bem
específicos (`siba native start` tem seu próprio método) quanto deixar um
comando mais genérico (`siba set ...`) cair num tratamento comum, sem
precisar de uma árvore de decisão escrita à mão em um único lugar.

Se nenhum método correspondente existir e o comando começar com `siba` ou
`same`, o terminal assume que é um pedido de ajuste de parâmetro
(`siba set <alvo> <ação> <valor>`) e monta o `CMD` binário na hora,
traduzindo os nomes digitados para os índices numéricos dos enums do
`.yaml` daquela placa — mostrando sempre, antes de enviar, tanto os bytes
em hexadecimal quanto a tradução legível de cada campo, para que fique
claro exatamente o que está sendo mandado pela rede.

```mermaid
flowchart TD
    Input["texto digitado"] --> Split["divide em palavras"]
    Split --> Try["procura _cmd_&lt;palavras&gt;, do mais específico ao mais geral"]
    Try -->|achou método| Run["executa o método Python"]
    Try -->|"não achou, root=siba/same"| Parse["monta CMD binário<br/>(_parse_to_cmd_struct)"]
    Parse --> Log["mostra hex + tradução campo a campo"]
    Log --> Send["envia pela rede"]
    Try -->|"não achou, root desconhecido"| Err["ERR: comando desconhecido"]
```

### Comandos disponíveis

**Comandos fixos do próprio terminal** (não dependem de placa nenhuma):

| Comando | O que faz |
|---|---|
| `help [comando]` | Lista todos os comandos, ou detalha os subcomandos de um específico |
| `clear` | Limpa a tela do terminal |
| `sudo` | Relança a aplicação inteira com privilégios de administrador (necessário para o `tcpdump`) |
| `restart` | Reinicia a aplicação |
| `exit` / `exit all` | Fecha só o terminal, ou a aplicação inteira |
| `ping <ip>` | Testa se dá para abrir uma conexão TCP na porta 80 daquele IP (um teste de alcançabilidade simples, não um ping ICMP de verdade) |
| `ipconfig` | Lista as interfaces de rede do PC e seus IPs — útil para conferir se o PC está na mesma sub-rede link-local da placa |
| `listing [porta]` | Diz se uma porta local já está em uso |
| `tcpdump <ip> <segundos>` | Um sniffer de pacotes cru — requer `sudo` antes |
| `connect <ip>` | Conecta a um IP qualquer |
| `logs on` / `logs off` | Liga/desliga a impressão da telemetria das placas no terminal (desligado por padrão, para não esconder as respostas) |

**Comandos por placa** (`siba`/`same`, protocolo `CMD` binário):

| Comando | O que faz |
|---|---|
| `siba connect` / `same connect` | Liga o link apontando para o IP físico da placa (hardware real), e confirma com um ping de sub-rede |
| `siba mock start` / `stop` | Liga/desliga o simulador Python (modo 2) |
| `siba native start` / `stop` | Sobe/derruba o firmware real via `native_sim` (modo 3) — precisa ter rodado `make native_sim` na pasta do firmware antes |
| `siba native shell <cmd>` | Manda uma linha para o shell de UART do firmware da SIBA rodando em `native_sim` (ex.: `siba native shell tele`) |
| `siba force on` / `off` | Sobrescreve/restaura o botão físico de emergência da SIBA (`STATUS_EMERGENCY_OVERRIDE`/`RESTORE`). Fica só no terminal de propósito: é desvio de segurança, vale até a placa reiniciar |
| `siba test_io` / `same test_io` | Ver seção "Testando a conexão" abaixo |
| `siba set <alvo> linear\|offset\|reset\|max\|min <valor>` | Ajusta a calibração de um sensor — o terminal traduz o nome do alvo (`v_batt`, `load_cell_0`, etc.) para o índice correto do enum `target_type`, incluindo alguns apelidos necessários porque o nome que aparece na telemetria não bate literalmente com o nome do enum (por exemplo, `v_batt` na telemetria corresponde a `TARGET_VOLTAGE_DIVIDOR` no protocolo, não a um `TARGET_V_BATT` que não existe) |
| `same set <alvo> ...` | Mesma ideia, para os alvos da SAME (correntes/tensões de fase, parâmetros do motor, ganhos do SMO) |

**Comandos de gráfico** (`graph`): `graph clear` limpa os buffers de
telemetria acumulados; `graph mock_data start/stop` liga/desliga o
simulador (atalho equivalente a `siba mock start` + `same mock start`
juntos).

### Testando a conexão sem depender de olhar gráfico nenhum

`siba test_io` (e o equivalente `same test_io`) resolve uma pergunta
objetiva sem exigir interpretar telemetria: **a placa está realmente
recebendo E enviando dados, ou só uma das duas coisas está acontecendo?**
O comando manda um `CMD` inofensivo (não muda nada no estado da placa) e
espera, por um tempo limite, qualquer pacote de telemetria chegar de
volta — reportando separadamente se o **envio** (TX, do PC para a placa)
funcionou (o socket conseguiu mandar os bytes) e se a **recepção** (RX)
funcionou (algum pacote chegou de volta). Isso funciona **independente**
de qual dos três modos de conexão está ativo — ele só observa contadores
de pacote no nível do socket, não depende de a interface conseguir
decodificar/plotar nada. A utilidade prática: se TX falhar, o problema é
de rede/porta/firewall do lado do PC; se só RX falhar, o problema está do
lado da placa (firmware não está respondendo, ou telemetria não está
sendo gerada) — isolando de forma rápida onde procurar.

---

## O que dá para fazer, tela por tela

```mermaid
flowchart LR
    MB["Barra de menus"]
    MB --> Motor["Motor"] --> MLoad["Carregar"] & MCreate["Criar"]
    MB --> Estim["Estimativas"] --> EstSMO["Ganhos SMO"] & EstHel["Coeficiente da Hélice"]
    MB --> Bat["Bateria"]
    MB --> Cells["Células de Carga"] --> CellsCal["Calibrar"]
    MB --> Opt["Opções"] --> OptSens["Config. de Sensores"] & OptBoard["Gerenciador de Placas"] & OptErr["Painel de Erros"] & OptInfo["Guia do Usuário"]
    MB -.canto direito.-> ModeBox["Modo de Operação"]
```

### Tela principal

Tem o combo **"Modo de Operação"** (`SIBA`, `SAME` ou `SIBA+SAME`), que
decide quais placas ligam e quais gráficos aparecem — no modo
`SIBA+SAME`, aparecem tanto a telemetria aerodinâmica/bateria quanto a
estimativa elétrica do motor. O slider de throttle manda o comando de
acelerar para a SIBA, que é quem aciona o ESC (sozinha ou junto com a
SAME). A SAME **nunca** aciona nada, ela só observa, então no modo `SAME`
sozinho o slider fica desabilitado. O botão "Desligar (Motor)" executa
uma parada segura (throttle mínimo mais `FUNC_STOP`).

### Gerenciador de Placas

Uma linha por placa, com um LED que fica verde quando a placa está
recebendo dados de verdade, um campo de IP/porta editável **só para
aquela sessão** (não grava no arquivo `.yaml` — para tornar uma mudança de
IP permanente, é preciso editar o `.yaml` manualmente), um botão
"Detectar" que dispara o mecanismo de descoberta descrito acima, e o
botão "Simulação Nativa" que liga/desliga o modo `native_sim` daquela
placa (desabilitado automaticamente fora do Linux).

### Calibrar Célula de Carga

Mostra a média das últimas N leituras (ajustável) de uma célula
escolhida, ao vivo, com um diagnóstico automático: se a média estiver
próxima do valor que o HX711 devolve quando não há nada conectado
(saturado, todos os 24 bits em 1), avisa que a célula parece
desconectada; se a média estiver muito acima do que se espera de uma
leitura já calibrada, avisa que provavelmente ainda está com a
calibração de fábrica. O botão "Tarar" zera a célula sem carga nenhuma
em cima; o campo de "peso conhecido" mais o botão "Calibrar pelo peso" é
onde se informa quanto peso está de fato sobre a célula agora — é a
placa (não a interface) quem calcula a escala necessária a partir desse
peso, seguindo o modelo canônico do HX711 explicado em
`../SIBA/README.md`.

### Configuração de Sensores

Uma tabela com todos os sensores calibráveis da SIBA em linhas, e quatro
colunas (ganho, offset/tara, mínimo válido, máximo válido) — só as
células que forem de fato editadas são enviadas ao clicar "Aplicar"
(destacadas em laranja enquanto pendentes), para não correr o risco de
recalibrar um sensor sem querer só por ele estar na tabela.

### Painel de Erros e Alarmes

Uma lâmpada por cada bit do código de erro agregado da SIBA (ver a tabela
completa em `../SIBA/README.md`), agrupadas por área (bateria, motor/
hélice, segurança, sistema), acendendo em vermelho quando aquele alarme
específico está ativo na telemetria mais recente.

### Estimar Ganhos do SMO

Recebe da pessoa que está operando a resistência ($R$) e a indutância
($L$) do motor (medidas na bancada de rotor bloqueado, ver
`../SAME/README.md`), o período de amostragem simulado, e parâmetros do
algoritmo de busca (número de gerações e tamanho da população da Evolução
Diferencial). Ao rodar, mostra em tempo real a curva de custo da
otimização (quanto menor, melhor o ajuste dos ganhos candidatos) conforme
o script avança, e ao terminar mostra os três ganhos ótimos na forma Γ
(explicados em `../SAME/README.md`) — só grava no `SAME.yaml` depois de
uma confirmação explícita, nunca automaticamente.

### Estimar Coeficiente da Hélice

Diferente do anterior, este não roda uma simulação — ele consome um
**CSV já exportado** pela tela principal (telemetria real gravada durante
um ensaio). A pessoa escolhe qual das forças medidas pelas células de
carga representa a **tração** (obrigatório escolher — a interface não
assume qual célula é qual), qual coluna é o torque (por padrão, o
estimado pela SAME) e qual é o RPM, mais o diâmetro da hélice e a
densidade do ar. O script então calcula, para cada ponto do ensaio, os
coeficientes adimensionais clássicos de hélice — coeficiente de tração
$C_T$, de torque $C_Q$, de potência $C_P$ e a eficiência $\eta$ — e devolve
uma **lista de pontos**, não um coeficiente único, porque esses
coeficientes variam com a razão de avanço (a relação entre a velocidade do
ar e a rotação); sem velocidade do ar medida, o cálculo cai para o modelo
estático mais simples, $T = k_Q \omega^2$.

### Motor → Carregar / Criar

"Criar" abre um formulário só com dados de fabricante e de ensaio (polos,
$K_v$ de placa, resistência e indutância de fase, tensão de barramento),
mais os ganhos do observador. As conversões não aparecem na tela: o $K_v$ de
placa é por volt de barramento e o pico de tensão de fase que o inversor
entrega é $V_{bus}/\sqrt3$, então $K_{e1} = 60/(2\pi K_v\sqrt3)$ **sempre**
(independe de o motor ser Y ou Δ) e $K_t = 1{,}5\,K_{e1}$ — mesma conta do
firmware. O formulário permite salvar isso como um arquivo de
perfil (`Config/motors/<nome>.yaml`), reutilizável depois. "Carregar" lista
os perfis salvos e os que já estiverem embutidos no `SAME.yaml`, e permite
aplicar os parâmetros escolhidos direto no hardware, mandando um `CMD` por
parâmetro.

---

## Passo a passo prático: rodando um ensaio do zero

Um exemplo concreto, do começo ao fim, de como chegar a um coeficiente de
hélice a partir de dados reais (adaptável para os outros ensaios trocando
só as telas envolvidas):

1. **Ligue a(s) placa(s)** que vão gerar os dados. Se for testar sem
   bancada física primeiro, compile e suba o `native_sim` da placa
   (`make native_sim` na pasta do firmware, depois `siba native start` ou
   pelo botão "Simulação Nativa" no Gerenciador de Placas). Confirme com
   `siba test_io` (ou `same test_io`) que TX e RX estão OK antes de
   prosseguir — economiza tempo descobrir um problema de conexão aqui, em
   vez de no meio de um ensaio.
2. **Escolha o Modo de Operação** compatível com o que será medido — por
   exemplo, `SIBA+SAME` se o ensaio precisar tanto de empuxo (SIBA) quanto
   de torque estimado (SAME).
3. **Clique em "Conectar"** na tela principal — isso sobe os links das
   placas do modo escolhido.
4. **Rode o ensaio** propriamente dito: acelere o motor manualmente pelo
   slider (variando o throttle em alguns níveis diferentes, se for medir
   uma curva).
5. **Exporte os dados**: botão "Salvar CSV" na tela principal — isso grava
   todo o histórico de telemetria acumulado (da RAM e do que já tiver ido
   para o arquivo de swap em disco, caso o ensaio tenha durado o
   suficiente para encher o buffer de 5000 pontos em RAM) com uma coluna
   de timestamp e uma coluna por variável de telemetria.
6. **Processe o CSV**: abra o menu Estimativas → Estimar Coeficiente da
   Hélice, escolha o CSV recém-exportado, indique qual coluna é a tração
   (qual célula de carga estava medindo empuxo), qual é o torque e o RPM,
   e o diâmetro/densidade do ar — clique em calcular.
7. **Confira e aplique**: a tabela mostra um ponto por nível de operação
   testado, com $C_T$/$C_Q$/$C_P$/eficiência de cada um, mais um resumo
   com a média. Só depois de conferir que os números fazem sentido é que
   se clica em "Aplicar ao SAME.yaml" — nada é gravado automaticamente
   antes dessa confirmação explícita.

O mesmo roteiro geral (ligar → escolher modo → conectar → rodar → exportar
→ processar → confirmar antes de aplicar) se repete para os outros
ensaios (como os ganhos do SMO), só trocando qual diálogo
processa os dados e quais parâmetros de entrada ele pede.

---

Ver [`CLAUDE.md`](CLAUDE.md) para referência técnica adicional voltada a
quem for mexer no código (API completa do `LinkManager`, bugs históricos
já corrigidos, observações em aberto) — o conteúdo essencial de uso e
funcionamento está todo neste documento.

Licença: **MIT** (ver `../../LICENSE`).
