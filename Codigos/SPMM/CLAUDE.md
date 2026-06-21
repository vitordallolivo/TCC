# SPMM — Sistema Provisório de Medição Mecânica

## Contexto e propósito

O **SPMM** é uma placa de bancada **provisória e descartável** para caracterização
mecânica do motor BLDC (coeficiente de atrito viscoso `B`, atrito de Coulomb
`τc` e momento de inércia `J`), enquanto as placas definitivas **SAME** e
**SIBA** ainda não estão prontas.

Roda em um **CH32V307VCT6** dedicado, separado das placas finais, sobre **Zephyr
RTOS**. O acionamento do motor é feito por um **ESC comercial**; o SPMM apenas
**comanda o throttle do ESC** (PWM de servo), **mede as correntes de fase** e
**lê a rotação**. O SPMM **NÃO** gera PWM trifásico nem aciona o motor
diretamente.

A interação é feita por um **shell por rede (Telnet)** do Zephyr, com comandos
customizados de bancada. O streaming de dados rápido (rpm, Iq ao longo do tempo)
sai por **UDP** em paralelo.

### Princípio de projeto
O firmware embarcado apenas **comanda, mede e transmite**. Todo o
pós-processamento (regressão linear de `Te = B·ω + τc`, derivada `dω/dt`,
cálculo de `J`) é feito **offline no PC, em Python**. Nada de mínimos quadrados,
diferenciação numérica ou ajuste de curva embarcado.

### Base de tempo (importante)
- **Orquestração e patamares:** usar o **relógio do PC**. O instante exato não
  importa em regime permanente, e o PC controla quando mandar comandos e por
  quanto tempo esperar.
- **Coast-down (derivada `dω/dt`):** **NÃO** usar o horário de chegada do pacote
  UDP — o jitter de rede + SO corromperia o `Δt` da derivada, e perda/reordenação
  de pacotes geraria `Δt` inconsistente. Em vez disso, o micro envia um
  **contador de amostra `n`** em cada amostra, e o PC reconstrói o tempo como
  `t = n · Ts`, onde `Ts` é o período de amostragem (preciso, vem do timer do
  micro). Assim o `Δt` da derivada vem do cristal do micro, não da rede, e
  perda/reordenação de pacotes fica **detectável** pelos saltos em `n`.

## O que reaproveitar da SIBA

**Copiar o setup de ADC e de PWM da pasta da SIBA.** A SIBA já tem esses
periféricos configurados e validados para o CH32V307 sob Zephyr; reaproveitar
evita reescrever Devicetree, Kconfig e a inicialização de baixo nível.

- **ADC (aquisição de corrente):** copiar a configuração de ADC + DMA da SIBA
  (leitura em background, sem intervenção da CPU). Ajustar apenas os canais para
  as correntes de fase usadas no SPMM (no mínimo `Ia` e `Ib`).
- **PWM:** copiar o setup de PWM da SIBA como base, **mas reconfigurar para PWM
  de servo** (~50 Hz, pulso 1000–2000 µs) destinado ao throttle do ESC — e não
  para o PWM rápido de potência. É um único canal.

Verificar, ao copiar, os nós de Devicetree (`&adc`, `&dma`, `&pwmX`), os
`prj.conf`/Kconfig correspondentes e os pinos (pinctrl) — adaptar para o
mapeamento da placa de bancada.

### ⚠️ AVISOS CRÍTICOS ao reaproveitar da SIBA

- **ADC — NÃO usar o driver padrão do Zephyr.** `CONFIG_ADC=y` neste micro pode
  puxar o **driver do CH32V003**, que **conflita com o registrador de Ethernet
  do CH32V307VCT6**. É **obrigatório** usar o **driver de ADC customizado** já
  criado para o CH32V307 (o mesmo que a SIBA usa), e não o driver genérico que o
  `CONFIG_ADC` seleciona por padrão. Conferir o Kconfig/overlay da SIBA para ver
  como o driver customizado é selecionado e replicar — caso contrário a Ethernet
  (e portanto o shell Telnet e o log UDP) quebra.

- **Copiar o setup de POSIX da SIBA.** A SIBA tem uma configuração de build para
  **POSIX/native** que permite encontrar **bugs de código não relacionados ao
  kernel nem ao Zephyr** (lógica de aplicação, parsing do shell, manipulação de
  buffers, etc.) rodando no PC, fora do hardware. Copiar esse setup para o SPMM:
  acelera muito a depuração da lógica do shell e do processamento, sem precisar
  flashar a placa a cada teste.

- **Copiar o Makefile da SIBA.** A SIBA tem um **Makefile com comandos especiais**
  (atalhos de build, flash, etc.). Copiar e adaptar para o SPMM em vez de montar
  os comandos do zero.

## Arquitetura

```
PC  ──Telnet (TCP)──►  SPMM (CH32V307)  ──servo PWM──►  ESC ──► BLDC
 ▲                        │
 └────── UDP (log) ───────┤── ADC + DMA ──► Ia, Ib  → Clarke/Park → Iq
                          └── Input Capture ──► pulsos do sensor → ω
```

### Threads (prioridades importam)
- **Aquisição** — prioridade ALTA (cooperativa ou preemptiva alta). ADC via DMA;
  a cada bloco pronto calcula `Iq` (Clarke + Park) com o `θ` do sensor. O período
  de amostragem deve permanecer constante; esta thread não pode ser atrasada
  pelo shell.
- **Shell (Telnet)** — prioridade BAIXA (preemptiva). Fica bloqueada esperando
  comando; custo de CPU ocioso ~zero. Não pode preemptar a aquisição.
- **Telemetria/Log (UDP)** — prioridade BAIXA/MÉDIA. Empacota as amostras
  `(n, ω, Te)` em pacotes binários e envia por UDP quando o log está ativo.

### Leitura de rotação
- Sensor independente do acionamento (óptico com disco **ou** Hall nos ímãs do
  rotor — 14 polos = 7 pulsos/volta). **Necessário** porque no ensaio de
  coast-down a excitação é cortada e o ZCP das fases desaparece.
- Lida por **Input Capture** (timer em hardware): registra os timestamps de
  pulsos sucessivos → período → `ω`. Sem carga de CPU.

### Cálculo de Iq
- Correntes de pelo menos duas fases (`Ia`, `Ib`); a terceira por
  `Ic = -(Ia + Ib)` (sem neutro).
- Aplicar Clarke + Park com `θ` do sensor.
- Devido à comutação 6-step do ESC, `Iq` tem ripple harmônico — usar **valor
  médio filtrado** de `Iq` (componente fundamental, torque médio em regime
  permanente).

## Shell de rede — comandos de bancada

Registrar via `SHELL_CMD_ARG_REGISTER`. Conjunto mínimo:

| Comando | Função |
|---|---|
| `arm` / `disarm` | Sequência de armar/desarmar o ESC |
| `throttle <us>` | Define o PWM do ESC (1000 = parado, 2000 = máx) |
| `ramp <us> <s>` | Rampa suave de throttle (evita degrau no ESC) |
| `rpm` | Lê rotação atual |
| `iq` | Lê `Iq` médio atual |
| `read` | Dump: rpm, Iq, Te |
| `patamar <us> <s>` | Vai ao throttle, espera estabilizar, grava ponto → ensaio B/τc |
| `coast` | Corta throttle e dispara captura de `ω(t)` → ensaio J |
| `log start <hz>` / `log stop` | Streaming binário de amostras `(n, ω, Te)` por UDP |

### Exemplo de registro de comando
```c
static int cmd_throttle(const struct shell *sh, size_t argc, char **argv)
{
    int us = atoi(argv[1]);
    set_servo_pwm(us);
    shell_print(sh, "throttle = %d us", us);
    return 0;
}
SHELL_CMD_ARG_REGISTER(throttle, NULL, "Set ESC throttle [us]", cmd_throttle, 2, 0);
```

## Procedimento dos ensaios (via shell)

### Atrito viscoso (B, τc) — regime permanente em patamares
```
arm
patamar 1200 3      # throttle 1200us, espera 3s estabilizar, grava (ω, Iq)
patamar 1300 3
patamar 1400 3
...                 # vários pontos cobrindo a faixa de rotação
```
Em regime permanente (`dω/dt = 0`): `Te = B·ω + τc`. O PC faz a **regressão
linear** sobre os pares `(ω, Te)` para extrair `B` e `τc`.

### Momento de inércia (J) — coast-down
```
arm
throttle 1800       # sobe até rotação alta
# (espera estabilizar)
coast               # corta throttle; SPMM grava e streama ω(t) do decaimento
```
Sem excitação: `J·(dω/dt) = -B·ω - τc`. O PC calcula a **derivada** de `ω(t)` na
região de decaimento e isola `J = -(B·ω + τc)/(dω/dt)`, usando o `B` e `τc` do
ensaio anterior.

## Fórmulas dos ensaios (o que o script implementa)

Notação: `ω` = velocidade mecânica [rad/s], `Te` = torque eletromagnético [N·m],
`B` = atrito viscoso [N·m·s/rad], `τc` = atrito de Coulomb [N·m], `J` = momento
de inércia [kg·m²]. Conversão de RPM: `ω = RPM · 2π / 60`.

### Atrito viscoso e de Coulomb (B, τc)
Em regime permanente (`dω/dt = 0`), o torque eletromagnético equilibra as perdas
mecânicas:
```
Te = B·ω + τc
```
Com os pares `(ω_k, Te_k)` de cada patamar `k`, ajusta-se uma **reta** por
mínimos quadrados (`numpy.polyfit(omega, Te, 1)`):
- coeficiente angular → `B`
- coeficiente linear (intercepto) → `τc`
- qualidade do ajuste → `R²` (deve ser > 0,98)

### Momento de inércia (J) — coast-down
Sem excitação (`Te = 0`), a dinâmica de parada é:
```
J·(dω/dt) = -(B·ω + τc)
```
Isolando, para cada ponto da curva de decaimento:
```
J = -(B·ω + τc) / (dω/dt)
```
onde `dω/dt < 0` na desaceleração (logo `J > 0`). Usa o `B` e `τc` do ensaio
anterior. A derivada `dω/dt` é calculada a partir de `ω(t)` com `t = n·Ts`
(contador de amostra × período), preferencialmente sobre `ω(t)` suavizada ou por
ajuste de curva, pois a diferenciação amplifica ruído.

**Validação por janelas (corte recomendado):** a fórmula de `J` vale em todo
ponto da curva. Calcular `J` em **várias janelas de RPM** ao longo do
coast-down; um `J` constante entre janelas valida a medição. Variação de `J` com
a rotação indica `B`/`τc` incorretos ou perda mecânica não modelada.

### Alternativa para J (constante de tempo, opcional)
Se o atrito de Coulomb for desprezível (`τc ≈ 0`), a parada vira exponencial
`ω(t) = ω₀·e^(-t/T)` com `T = J/B`, e:
```
J = B · T      (T = constante de tempo do decaimento, ajustada da curva)
```
Útil como verificação cruzada do método da derivada quando `τc` é pequeno.



## Protocolo UDP de log (binário, overhead mínimo)

O log do coast-down sai como **struct binário empacotado** (não texto), para
minimizar overhead. Dados brutos no pacote, traduzidos no PC por uma struct
espelhada. Little-endian, sem padding.

```c
// firmware — uma amostra (12 bytes)
#pragma pack(push, 1)
typedef struct {
    uint32_t n;        // indice da amostra; tempo = n * Ts (reconstruido no PC)
    float    omega;    // rad/s
    float    te;       // N.m
} spmm_sample_t;

// cabecalho do pacote + amostras em lote (batched ate o MTU)
typedef struct {
    uint16_t      count;   // numero de amostras neste pacote
    uint16_t      ts_us;   // Ts em microssegundos (PC reconstroi t = n*Ts)
    spmm_sample_t s[];     // count amostras
} spmm_pkt_t;
#pragma pack(pop)
```

No PC, desempacotar com o módulo `struct`:
```python
import struct
HDR = struct.Struct("<HH")      # count, ts_us
SMP = struct.Struct("<Iff")     # n(uint32), omega(float), te(float) -> 12 bytes

def parse_pkt(data):
    count, ts_us = HDR.unpack_from(data, 0)
    Ts = ts_us * 1e-6
    out, off = [], HDR.size
    for _ in range(count):
        n, omega, te = SMP.unpack_from(data, off)
        out.append((n, n * Ts, omega, te))   # tempo do indice, nao da rede
        off += SMP.size
    return out
```

Detectar perda/reordenação pelos saltos em `n`: se `n` pular ou voltar, marcar
o gap em vez de corromper o `Δt` da derivada.

## Script de teste automático (PC, Python)

Cliente que orquestra os ensaios pelo shell e processa os dados. Requisitos:

### Comportamento obrigatório
- **Dois modos de ensaio:** o script tem um modo para **atrito** (patamares) e
  um para **inércia** (coast-down). O usuário escolhe qual rodar no início, e
  pode trocar de modo pelo menu pós-ensaio sem reiniciar o script.
- **Parametrização por input do usuário:** o script pergunta (ou aceita por
  argumento/config) os tempos de duração e demais parâmetros — duração de cada
  patamar, lista de throttles, throttle do coast-down, tempo de estabilização,
  taxa de log, etc. O usuário deve conseguir definir esses valores sem editar o
  código. O **nome do arquivo CSV** também é definido pelo usuário.
- **Menu interativo pós-ensaio:** após salvar cada ensaio, o script pergunta o
  que fazer: repetir igual, reconfigurar os parâmetros, trocar de modo, mudar o
  nome do arquivo, ou sair. Não repete cegamente — sempre devolve o controle ao
  usuário entre repetições.
- **Repetição N vezes:** o usuário pode repetir o mesmo ensaio quantas vezes
  quiser (escolhendo "repetir" no menu). Cada repetição vira uma entrada nova nos
  dados (com índice de repetição), permitindo média e desvio — validação por
  redundância, igual aos múltiplos capacitores na medição de `L`.
- **Cancelamento simples e SEGURO (Ctrl+C):** o cancelamento é apenas um
  **`Ctrl+C`**. Ao capturar `KeyboardInterrupt`, o script **imediatamente**:
  1. envia `disarm`;
  2. envia `throttle 1000` (PWM de desligamento, motor parado);
  3. fecha as conexões;
  e só então encerra. Isso **NUNCA** pode ser pulado — o motor não pode ficar
  girando se o script morrer. Usar `try/finally` envolvendo todo o ensaio, com a
  rotina de desligamento no `finally`, garantindo execução mesmo em exceção
  inesperada (não só Ctrl+C).
- **Salvamento em CSV:** todos os dados de todas as repetições são salvos em
  arquivo `.csv`. Patamares e coast-down podem ir em CSVs separados ou num só com
  coluna de tipo de ensaio + índice de repetição. Salvar incrementalmente (a cada
  repetição) para não perder dados se o ensaio for cancelado no meio.

### Esqueleto de referência
```python
import telnetlib, time, socket, struct, csv, sys
import numpy as np

HOST = "192.168.0.50"     # IP do SPMM (parametrizar)

class SPMM:
    def __init__(self, host, tn_port=23, udp_port=5000):
        self.tn = telnetlib.Telnet(host, tn_port, timeout=5)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("", udp_port))
        self.udp.settimeout(1.0)

    def cmd(self, line, wait=0.2):
        self.tn.write((line + "\n").encode())
        time.sleep(wait)
        return self.tn.read_very_eager().decode(errors="ignore")

    def shutdown(self):
        """Desligamento seguro — SEMPRE chamado no finally."""
        try:
            self.cmd("disarm")
            self.cmd("throttle 1000")   # PWM de motor parado
        finally:
            self.tn.close(); self.udp.close()

def ensaio_atrito(spmm, throttles, settle_s):
    pares = []
    spmm.cmd("arm"); time.sleep(2)
    for us in throttles:
        spmm.cmd(f"throttle {us}")
        time.sleep(settle_s)
        resp = spmm.cmd("read")          # "rpm=... te=... iq=..."
        d = parse_kv(resp)
        omega = d["rpm"] * 2*np.pi/60
        pares.append((omega, d["te"]))
    spmm.cmd("throttle 1000")
    return pares

def ensaio_coast(spmm, throttle_alto, estab_s, log_hz, dur_s):
    spmm.cmd("arm"); time.sleep(2)
    spmm.cmd(f"throttle {throttle_alto}")
    time.sleep(estab_s)
    spmm.cmd(f"log start {log_hz}")
    spmm.cmd("coast")
    amostras = coletar_udp(spmm, dur_s)   # usa parse_pkt; tempo = n*Ts
    spmm.cmd("log stop"); spmm.cmd("throttle 1000")
    return amostras

def main():
    spmm = SPMM(HOST)
    rep = 0
    try:
        # 1) escolha do modo de ensaio
        modo = escolher_modo()        # "atrito" ou "inercia" (input do usuario)
        cfg  = pedir_config(modo)     # parametros do modo escolhido
        arquivo = pedir_nome_arquivo(modo)   # nome do CSV

        while True:                   # repete ate o usuario decidir parar
            rep += 1
            print(f"\n=== {modo} — Repeticao {rep} ===")

            if modo == "atrito":
                dados = ensaio_atrito(spmm, **cfg)
            else:  # inercia
                dados = ensaio_coast(spmm, **cfg)

            salvar_csv(arquivo, dados, rep, modo)   # incremental
            relatorio(modo, dados)                   # cortes de validacao

            # 2) menu pos-ensaio — o que fazer agora?
            print("\nO que deseja fazer?")
            print("  [Enter] repetir o mesmo ensaio")
            print("  [c] reconfigurar parametros")
            print("  [m] trocar de modo (atrito/inercia)")
            print("  [a] mudar o nome do arquivo")
            print("  [q] sair")
            op = input("> ").strip().lower()

            if op == "q":
                break
            elif op == "c":
                cfg = pedir_config(modo)
            elif op == "m":
                modo = escolher_modo()
                cfg  = pedir_config(modo)
                arquivo = pedir_nome_arquivo(modo)
                rep = 0
            elif op == "a":
                arquivo = pedir_nome_arquivo(modo)
            # Enter (string vazia) ou qualquer outra -> repete

    except KeyboardInterrupt:
        print("\nCancelado pelo usuario (Ctrl+C).")
    finally:
        spmm.shutdown()       # disarm + throttle 1000 SEMPRE

if __name__ == "__main__":
    main()
```

### Fluxo de interação (resumo)
1. Escolhe o **modo** (atrito ou inércia).
2. Configura os **parâmetros** do modo (durações, throttles, taxa de log, etc.).
3. Define o **nome do arquivo** CSV.
4. Roda o ensaio → salva incremental → mostra relatório de validação.
5. **Menu pós-ensaio:** repetir igual / reconfigurar / trocar de modo / mudar
   nome do arquivo / sair. O laço continua até o usuário escolher sair ou apertar
   Ctrl+C.
6. Em **qualquer** saída, o `finally` desliga o motor (`disarm` + `throttle
   1000`).

> Os dois modos compartilham o mesmo cliente e a mesma lógica de segurança; só
> mudam a função de ensaio (`ensaio_atrito` vs `ensaio_coast`), os parâmetros e o
> relatório de validação correspondente.

> O `try/finally` com `spmm.shutdown()` no `finally` é o ponto de segurança mais
> importante do script: qualquer saída — Ctrl+C, exceção, fim normal — passa pelo
> desligamento do motor.

### Validação (cortes automáticos)
O script deve, além de coletar, **validar** (ver filosofia de fases):
- **Atrito:** regressão `Te = B·ω + τc`, reportar `R²` (alerta se < 0,98) e `τc`
  (alerta se negativo).
- **Inércia:** calcular `J` em **várias janelas de RPM** da mesma curva de
  coast-down e reportar média + dispersão (alerta se a dispersão indicar `J`
  variando com a rotação — sinal de modelo de atrito incompleto).
- **Repetibilidade:** com as N repetições, reportar média e desvio de cada
  parâmetro.



```
# Shell por rede (Telnet)
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_TELNET=y
CONFIG_SHELL_BACKEND_SERIAL=n   # opcional: manter UART para debug local

# Rede (TCP para shell, UDP para log)
CONFIG_NETWORKING=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_SOCKETS=y

# Periféricos (reaproveitar da SIBA)
CONFIG_ADC=y          # ATENCAO: usar o driver ADC CUSTOMIZADO do CH32V307,
                      # NAO o padrao (que puxa o driver do CH32V003 e
                      # conflita com o registrador de Ethernet). Ver SIBA.
CONFIG_DMA=y
CONFIG_PWM=y
```
> Observação: o shell Telnet usa **TCP**; o log rápido usa **UDP**. As duas
> pilhas coexistem. O custo do shell é principalmente **memória** (stack da
> thread + buffers + pilha TCP), não CPU — a thread do shell fica bloqueada
> esperando entrada.

## Parâmetros do motor sob ensaio

Motor: **Scorpion A-5025-310Kv** (enrolamento **6T/7T Delta**, 14 polos → `p = 7`).

Parâmetros elétricos já caracterizados (referência, não usados diretamente nos
ensaios mecânicos):
- `R_Y ≈ 8,69 mΩ` (datasheet: 8,65 mΩ — erro 0,46%)
- `L_Y ≈ 6,95 µH`

## Restrições e lembretes

- O SPMM é **provisório**: priorizar simplicidade e rapidez de desenvolvimento
  sobre elegância. Não é o firmware final.
- **Não** acionar o motor diretamente — o ESC comercial faz isso. O SPMM só
  comanda throttle.
- A thread de aquisição **nunca** pode ser preemptada pelo shell ou pela
  telemetria — conferir prioridades.
- Pós-processamento (regressão, derivada, `J`) é **sempre** no PC, nunca
  embarcado.
- Ao copiar ADC/PWM da SIBA, **reconfigurar o PWM para servo** (50 Hz), não
  deixar no modo de potência.
- **Segurança (lado PC):** o desligamento do motor (`disarm` + `throttle 1000`)
  roda **sempre** num `finally` — Ctrl+C, exceção ou fim normal. O motor nunca
  pode ficar girando se o script encerrar.
- **Base de tempo:** patamares usam o relógio do PC; o coast-down usa
  `t = n · Ts` (contador de amostra do micro), nunca o horário de chegada do
  pacote UDP.
- O log UDP é **binário** (struct empacotado), traduzido no PC por struct
  espelhada — overhead mínimo.
- **ADC:** usar o **driver customizado do CH32V307**, nunca o padrão (conflito
  com Ethernet via driver do CH32V003).
- Copiar da SIBA também o **setup POSIX/native** (debug de lógica fora do
  hardware) e o **Makefile com comandos especiais**.
- **Script Python:** dois modos (atrito/inércia), menu interativo pós-ensaio
  (repetir / reconfigurar / trocar modo / mudar arquivo / sair), nome do CSV
  definido pelo usuário, salvamento incremental.