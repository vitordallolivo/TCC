# eth_wch.c — Bug PLL3RDY e Workaround Out-of-Tree

## O Bug

**Arquivo:** `zephyr/drivers/ethernet/eth_wch.c`
**Commit:** `27821b8ea53` — adicionado em 24/02/2026, nunca alterado
**Linha:** 658

### Código bugado
```c
RCC->CTLR |= RCC_PLL3ON;
while (RCC->CTLR & RCC_PLL3RDY) {   // BUG: lógica invertida
    ;
}
```

### Código correto
```c
RCC->CTLR |= RCC_PLL3ON;
while (!(RCC->CTLR & RCC_PLL3RDY)) {   // aguarda PLL3 estabilizar
    ;
}
```

### Por que é um bug

`RCC_PLL3RDY` é o bit 29 do registrador `RCC->CTLR`
(definido em `hal_wch/ch32fun/ch32v30xhw.h` como `((uint32_t)(1<<29))`).

Quando o PLL3 termina de travar, esse bit **seta para 1**.
O código original sai do loop quando o bit está em 1 (PLL pronto),
mas deveria **entrar** no loop enquanto o bit é 0 e sair quando seta.

O código oficial da WCH (em `EVT/EXAM/ETH/MAC_RAW/ETH_Driver/eth_driver_10M.c`) faz:
```c
RCC_PLL3Cmd(ENABLE);
while (RESET == RCC_GetFlagStatus(RCC_FLAG_PLL3RDY));
```

Reverse engineering confirma:
- `RCC_FLAG_PLL3RDY = 0x3D`
- `0x3D >> 5 = 1` -> `statusreg = RCC->CTLR`
- `0x3D & 0x1F = 29` -> bit 29
- `RCC_GetFlagStatus` retorna `SET` quando `RCC->CTLR & (1<<29)` != 0

Portanto `while (RESET == RCC_GetFlagStatus(RCC_FLAG_PLL3RDY))` é
matematicamente idêntico a `while (!(RCC->CTLR & RCC_PLL3RDY))`.

### Sintoma

- Board imprime `PHY mii 0 id 0` no serial
- Reinicia logo após sem nenhum log adicional
- O PHY lê `0x0000` nos registradores `PHYIDR1`/`PHYIDR2` porque o MAC
  foi configurado com clock instável (PLL3 não travado)
- O watchdog dispara durante o init do MAC

---

## Fixes necessários no DTS

Além do bug do driver, o DTS precisava de duas correções:

### 1. Nó pai `&eth` não habilitado

O DTSI define hierarquia: `eth` (controller) -> `mac` + `mdio` (filhos).
Habilitar só `&mac` e `&mdio` não sobe o clock do controller.

```dts
/* ADICIONA ISSO */
&eth {
    status = "okay";
};
```

### 2. `local-mac-address` ausente

Sem MAC fixo o driver tenta ler da OTP interna. Se zerada, trava.

```dts
&mac {
    status = "okay";
    pinctrl-0 = <&mac_default>;
    pinctrl-names = "default";
    phy-connection-type = "internal";
    phy-handle = <&eth_phy>;
    local-mac-address = [02 00 00 01 02 03];
    internal-phy-pllmul = "15";
    internal-phy-prediv = "2";
};
```

---

## Reprodutibilidade — Por que commitar o driver no repositório

O arquivo `drivers/ethernet/eth_wch.c` **deve estar commitado no repositório**.
Não é gerado automaticamente, não é copiado durante o build.

Sem isso, qualquer pessoa que clonar o repositório em uma máquina nova vai
compilar com o driver bugado do Zephyr sem perceber — o `make` vai funcionar,
o flash vai funcionar, mas o hardware vai reiniciar silenciosamente.

Com o arquivo commitado + o `CMakeLists.txt` configurado corretamente, o
comportamento é determinístico: **todo mundo que clonar e rodar `make` vai
usar o driver corrigido do projeto**, independente da versão do Zephyr instalada.

### Fluxo para quem clona o repositório do zero

```bash
# 1. clona o repositório
git clone https://github.com/vitordallolivo/TCC.git
cd TCC/Codigos/SIBA

# 2. configura o workspace Zephyr normalmente
# (west init, west update, SDK — ver README)

# 3. compila — o driver local é usado automaticamente pelo CMakeLists.txt
make

# 4. verifica que o driver local está sendo usado (não o do Zephyr)
find ~/zephyrproject/build -name "eth_wch.c.obj" 2>/dev/null
# deve aparecer APENAS o path dentro do projeto, não do zephyrproject/zephyr
```

### Confirma que o arquivo está no repositório

```bash
git ls-files drivers/ethernet/eth_wch.c
# deve retornar: drivers/ethernet/eth_wch.c
```

Se não retornar nada, o arquivo ainda não foi commitado:

```bash
git add drivers/ethernet/eth_wch.c
git commit -m "drivers: ethernet: add local eth_wch.c with PLL3RDY fix

Copy of zephyr/drivers/ethernet/eth_wch.c (commit 27821b8ea53) with
the PLL3RDY wait condition corrected (line 658):

  - was: while (RCC->CTLR & RCC_PLL3RDY)
  - fix: while (!(RCC->CTLR & RCC_PLL3RDY))

The original driver causes a silent watchdog reset on CH32V307 with
internal PHY. See eth_wch_fix.md for full analysis."
```

---

## Workaround — Driver Out-of-Tree (solução definitiva do projeto)

**Objetivo:** o projeto não deve depender do driver in-tree do Zephyr para
funcionar. Qualquer versão do Zephyr que contenha o bug (ou qualquer versão
futura que quebre de outra forma) não afeta o build — o projeto carrega seu
próprio driver corrigido e o Zephyr simplesmente não compila o dele.

### Por que não basta copiar e corrigir

Se você só copiar o arquivo e adicionar ao `target_sources`, o CMake vai
compilar **os dois** — o in-tree e o seu — e o linker vai reclamar de
símbolos duplicados ou usar o errado silenciosamente. É necessário
explicitamente excluir o in-tree da compilação.

### Estrutura de arquivos esperada no projeto

```
SIBA/
  drivers/
    ethernet/
      eth_wch.c        <-- cópia local com PLL3RDY corrigido (commitada no git)
    adc/
      adc_ch32v307.c
  CMakeLists.txt
  prj.conf
```

### 1. Copia e corrige o driver (só precisa fazer uma vez)

```bash
mkdir -p drivers/ethernet
cp $ZEPHYR_BASE/drivers/ethernet/eth_wch.c drivers/ethernet/eth_wch.c

# aplica a correção do PLL3RDY
sed -i 's/while (RCC->CTLR & RCC_PLL3RDY)/while (!(RCC->CTLR \& RCC_PLL3RDY))/' \
    drivers/ethernet/eth_wch.c

# confirma
grep -n "PLL3RDY" drivers/ethernet/eth_wch.c
# deve mostrar: while (!(RCC->CTLR & RCC_PLL3RDY))
```

Commita imediatamente para que todos que clonarem o repositório tenham
o driver corrigido sem precisar fazer nada:

```bash
git add drivers/ethernet/eth_wch.c
git commit -m "drivers: ethernet: add local eth_wch.c with PLL3RDY fix

Copy of zephyr/drivers/ethernet/eth_wch.c (commit 27821b8ea53) with
the PLL3RDY wait condition corrected (line 658):

  - was: while (RCC->CTLR & RCC_PLL3RDY)
  - fix: while (!(RCC->CTLR & RCC_PLL3RDY))

The original driver causes a silent watchdog reset on CH32V307 with
internal PHY. CMakeLists.txt uses HEADER_FILE_ONLY to exclude the
in-tree version and compile this one instead.
See eth_wch_fix.md for full analysis."
```

### 2. CMakeLists.txt — mecanismo de substituição

O truque é `set_source_files_properties(...HEADER_FILE_ONLY ON)`:
diz ao CMake para tratar o `.c` in-tree como se fosse um header —
ele continua existindo para satisfazer o Kconfig/DTS, mas não é
compilado. O driver local entra no lugar via `zephyr_library_sources_ifdef`.

```cmake
if(NOT CONFIG_ARCH_POSIX)
    # Driver ADC out-of-tree
    zephyr_library_named(adc_ch32v307)
    zephyr_library_sources(drivers/adc/adc_ch32v307.c)
    zephyr_library_link_libraries(zephyr_interface)

    # Override eth_wch.c in-tree:
    #
    # HEADER_FILE_ONLY ON: exclui o .c original do Zephyr da compilação
    # sem desabilitar CONFIG_ETH_WCH — o Kconfig e o DTS continuam
    # funcionando normalmente, só o fonte in-tree é ignorado.
    #
    # zephyr_library_sources_ifdef: compila o driver local corrigido
    # no lugar. zephyr_include_directories garante que o #include "eth.h"
    # interno do driver resolva para o header in-tree correto.
    #
    # Resultado: qualquer versão do Zephyr com o bug (ou versão futura
    # que quebre de outra forma) não afeta o build deste projeto.
    set_source_files_properties(
        ${ZEPHYR_BASE}/drivers/ethernet/eth_wch.c
        PROPERTIES HEADER_FILE_ONLY ON
    )
    zephyr_include_directories(${ZEPHYR_BASE}/drivers/ethernet)
    zephyr_library_sources_ifdef(CONFIG_ETH_WCH
        drivers/ethernet/eth_wch.c
    )
endif()
```

### 3. Verificação após build

**Sempre roda `make clean` antes de verificar** — sem clean o CMake pode
não reprocessar as propriedades e o resultado do `find` é enganoso:

```bash
make clean && make
find ~/zephyrproject/build -name "eth_wch.c.obj" 2>/dev/null
```

é bem provavel que vc vai ter que rodar make +2 vezes por conta de um bug com cmakelist
do ADC (nao é um erro real). 

**Resultado correto — apenas um .obj, dentro do projeto:**
```
/home/vitor/zephyrproject/build/CMakeFiles/adc_ch32v307.dir/drivers/ethernet/eth_wch.c.obj
```

**Resultado errado — dois .obj (in-tree ainda compilando):**
```
/home/vitor/zephyrproject/build/CMakeFiles/adc_ch32v307.dir/drivers/ethernet/eth_wch.c.obj
/home/vitor/zephyrproject/build/zephyr/drivers/ethernet/CMakeFiles/drivers__ethernet.dir/eth_wch.c.obj
```

Se aparecer dois, o `HEADER_FILE_ONLY` não foi aplicado — verifica se
rodou `make clean` antes e se o path em `set_source_files_properties`
bate exatamente com o arquivo in-tree (`${ZEPHYR_BASE}/drivers/ethernet/eth_wch.c`).


Vc deve me avisar quando quiser usar o make flash e usar a interface do screen para verificar a
saida /dev/ttyACM0 (que é default)

---

## prj.conf — configurações necessárias

```kconfig
# Rede
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_UDP=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="169.254.97.100"
CONFIG_NET_CONFIG_PEER_IPV4_ADDR="169.254.97.139"
CONFIG_NET_CONFIG_INIT_TIMEOUT=0
CONFIG_NET_SOCKETS=y

# Buffers mínimos para UDP+IPv4+Ethernet
CONFIG_HEAP_MEM_POOL_SIZE=32768
CONFIG_NET_PKT_RX_COUNT=4
CONFIG_NET_PKT_TX_COUNT=4
CONFIG_NET_BUF_RX_COUNT=8
CONFIG_NET_BUF_TX_COUNT=8

# Debug — deixa ligado até confirmar que funciona
CONFIG_NET_LOG=y
CONFIG_ETH_LOG_LEVEL_DBG=y

# Shell de rede — para testes via uart:~$
CONFIG_NET_SHELL=y
```

---

## Teste via Shell Zephyr

Com `CONFIG_NET_SHELL=y` no `prj.conf`, comandos disponíveis no `uart:~$`:

```
net iface                         # lista interfaces e IPs
net stats                         # estatísticas de pacotes TX/RX
net ping 169.254.97.139           # ping para o PC
net arp                           # tabela ARP
```

Para teste de throughput UDP, habilita o zperf:

```kconfig
CONFIG_NET_ZPERF=y
```

Comandos zperf no shell:
```
zperf udp upload 169.254.97.139 5001 10 1K    # envia UDP para o PC por 10s
zperf udp download 5001                        # escuta UDP na porta 5001
```

---

## Rotina de teste Python

Salva como `test_udp.py` no PC e roda enquanto o firmware está rodando:

```python
#!/usr/bin/env python3
"""
Teste de conectividade UDP com o firmware SIBA.
Uso: python3 test_udp.py
"""
import socket
import time

BOARD_IP = "169.254.97.100"
PC_IP    = "169.254.97.139"
PORT_TX  = 5000   # porta que o firmware escuta (NCI RX)
PORT_RX  = 5001   # porta que o firmware envia (NCI TX)
TIMEOUT  = 3.0


def test_tx(payload: bytes = b"PING"):
    """Envia pacote UDP para o firmware."""
    print(f"[TX] Enviando {payload!r} para {BOARD_IP}:{PORT_TX} ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT)
    try:
        sock.sendto(payload, (BOARD_IP, PORT_TX))
        print("[TX] Enviado OK")
        return True
    except Exception as e:
        print(f"[TX] Erro: {e}")
        return False
    finally:
        sock.close()


def test_rx():
    """Escuta um pacote UDP vindo do firmware."""
    print(f"[RX] Escutando em {PC_IP}:{PORT_RX} ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((PC_IP, PORT_RX))
    sock.settimeout(TIMEOUT)
    try:
        data, addr = sock.recvfrom(1472)
        print(f"[RX] Recebido de {addr}: {data.hex()} ({len(data)} bytes)")
        return True
    except socket.timeout:
        print(f"[RX] TIMEOUT — nenhum pacote recebido em {TIMEOUT}s")
        return False
    finally:
        sock.close()


def test_continuous_rx(duration: float = 10.0):
    """Escuta pacotes por N segundos e conta quantos chegam."""
    print(f"\n[CONTINUOUS RX] Escutando por {duration}s em {PC_IP}:{PORT_RX}")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((PC_IP, PORT_RX))
    sock.settimeout(0.5)

    count = 0
    start = time.time()
    while time.time() - start < duration:
        try:
            data, addr = sock.recvfrom(1472)
            count += 1
            print(f"  [{count:04d}] {addr} -> {len(data)}B: {data[:32]!r}")
        except socket.timeout:
            pass

    sock.close()
    elapsed = time.time() - start
    print(f"\n[RESULTADO] {count} pacotes em {elapsed:.1f}s "
          f"({count/elapsed:.1f} pkt/s)")
    return count > 0


if __name__ == "__main__":
    print("=" * 60)
    print("SIBA UDP Connectivity Test")
    print("=" * 60)

    test_tx(b"PING")
    time.sleep(0.5)
    test_rx()
    test_continuous_rx(10.0)
```

Roda com:
```bash
python3 test_udp.py
```

---

## Como abrir o issue no Zephyr

URL: https://github.com/zephyrproject-rtos/zephyr/issues/new

**Título:**
```
drivers: ethernet: eth_wch: PLL3RDY wait condition inverted causes silent reboot on CH32V307
```

**Corpo:**
```markdown
## Bug description

In `drivers/ethernet/eth_wch.c` line 658, the PLL3 ready wait loop
has an inverted condition that causes a silent watchdog reset on
CH32V307 with internal PHY.

**Buggy (current):**
```c
RCC->CTLR |= RCC_PLL3ON;
while (RCC->CTLR & RCC_PLL3RDY) {
    ;
}
```

**Fix:**
```c
RCC->CTLR |= RCC_PLL3ON;
while (!(RCC->CTLR & RCC_PLL3RDY)) {
    ;
}
```

## Root cause

`RCC_PLL3RDY` is bit 29 of `RCC->CTLR` (defined in hal_wch as
`((uint32_t)(1<<29))`). The bit **sets to 1** when PLL3 locks.

The original loop exits immediately when PLL3RDY == 0 (not yet ready),
proceeding to configure the MAC with an unstable clock. This causes
MDIO to read 0x0000 for PHY ID registers and eventually triggers a
watchdog reset.

The WCH official SDK (`EVT/EXAM/ETH/MAC_RAW/ETH_Driver/eth_driver_10M.c`)
uses the correct pattern:
```c
RCC_PLL3Cmd(ENABLE);
while (RESET == RCC_GetFlagStatus(RCC_FLAG_PLL3RDY));
```

Reverse engineering of `RCC_GetFlagStatus(RCC_FLAG_PLL3RDY)`:
- `RCC_FLAG_PLL3RDY = 0x3D`
- `0x3D >> 5 = 1` -> reads `RCC->CTLR`
- `0x3D & 0x1F = 29` -> checks bit 29
- Returns SET when `RCC->CTLR & (1<<29)` != 0

Therefore it is mathematically equivalent to `while (!(RCC->CTLR & RCC_PLL3RDY))`.

## Symptom

Board prints `PHY mii 0 id 0` then resets silently with no further logs.

## Affected hardware

CH32V307VCT6 with `phy-connection-type = "internal"` in devicetree.

## Introduced by

Commit `27821b8ea53` — the only commit ever touching this file.
The bug has been present since the driver was first added (2026-02-24).
```