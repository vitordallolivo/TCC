# Codigos — contexto comum (SIBA, SAME, Aplicacao_Pi)

Bancada de ensaio de motor BLDC + hélice (TCC, Albatroz Aerodesign / UDESC).
Cada pasta tem seu `CLAUDE.md` curto; detalhe completo fica nos `README.md`.

| Pasta | Papel |
|---|---|
| `SIBA/` | Firmware Zephyr (CH32V307): células de carga, RPM, Pitot, bateria, **aciona o ESC** |
| `SAME/` | Firmware Zephyr (CH32V307): V/I de fase, **estima ω e torque** (SMO+PLL), só observa |
| `Aplicacao_Pi/` | GUI PySide6 no PC, cliente único das duas placas |

## Regras que valem para tudo

- **Protocolo tem fonte única**: `Aplicacao_Pi/Config/SIBA.yaml` e `SAME.yaml`.
  Headers C (`CMD.h`, `Common_Types.h`, `Log_Manager.h`, `CMD_Core_prm.h`,
  `Error_Codes.h`, `Security_prm.h`) são **gerados** por
  `python Aplicacao_Pi/Config/generate.py`. Nunca editar `.h` gerado à mão.
- Remover item do meio de um enum renumera o resto e o `CMD` é byte cru:
  acrescente no fim ou reserve o slot.
- Zephyr v4.4.0, SDK 1.0.1, `make build` / `make flash` / `make native_sim` em
  cada placa. `native_sim` só roda no Linux.
- Commits direto na `master`, sem PR (preferência do autor).

## Rede — estado atual

Transporte UDP, `CMD` de 8 bytes (PC→placa) e `TELEMETRY_LOG` packed LE a
10 Hz (placa→PC).

| | SIBA | SAME |
|---|---|---|
| IP no `prj.conf` | `169.254.97.51/16` ⚠ | `169.254.97.100/24` (máscara default do Zephyr) |
| IP no YAML / app | `169.254.97.200` | `169.254.97.100` |
| Placa escuta CMD | 5000 | 4444 |
| PC escuta telemetria | 5555 | 4445 |
| Destino da telemetria | broadcast `169.254.255.255` | unicast fixo `169.254.97.139` ⚠ |
| MAC (DTS) | `02:00:00:02:03:03` | `02:00:00:02:03:04` |
| `native_sim` | `127.0.0.200` | `127.0.0.100` |

**Não é plug and play hoje.** Motivos, em ordem de gravidade:

1. SAME só fala com um PC que tenha exatamente `169.254.97.139`.
2. IP da SIBA diverge entre firmware (`.51`) e app (`.200`); o
   `UdpStructLink` filtra RX por IP, então descarta a telemetria real.
3. Telemetria sai de um socket sem `bind` (porta de origem efêmera), então o
   firewall do PC não a reconhece como resposta ao CMD. Foi por isso que o
   `Teste/recv_siba.py` precisou criar regra `netsh` no Windows.
4. IP do PC: Windows cai em APIPA `169.254.x.y/16` sozinho (~1 min); Linux
   com NetworkManager **não** cai em link-local quando o DHCP falha e fica sem
   IPv4.
5. WSL2 em modo NAT não enxerga a placa (usar Python no Windows ou
   `networkingMode=mirrored`).

## Suporte da placa (CH32V307) no Zephyr 4.4 — conferido no código-fonte

- Driver Ethernet: o **oficial do Zephyr** (`zephyr/drivers/ethernet/eth_wch.c`,
  `CONFIG_ETH_WCH`, ligado sozinho pelo `&mac` no DTS). Não há driver de
  Ethernet próprio no repositório. O DTS usa `phy-connection-type =
  "internal"`, que significa o **PHY embutido no chip** CH32V307 (não um driver
  nosso); o driver oficial configura o PLL3 dele. Esse PHY embutido é
  **10BASE-T**: o switch precisa aceitar 10 Mbit/s (LED de link aceso é o teste).
- Link detectado pelo `phy_mii` genérico **por polling MDIO** (a IRQ
  `PHYLINK` é só limpa). O commit `ec5f1fb` registra que o `phy_mii` deu
  problema na SIBA e o `4e920ee` diz que o ETH foi resolvido. Testar
  **cabo ligado depois do boot** (IF_DOWN→IF_UP) antes de confiar no hot-plug.
- Filtro de MAC (`MACFFR`): aceita broadcast e, sem
  `CONFIG_ETH_WCH_MULTICAST_FILTER`, todo multicast. DHCP e mDNS passam.
- `CONFIG_NET_DHCPV4_SERVER` existe e serve a placa: pool de
  `NET_DHCPV4_SERVER_ADDR_COUNT` (default 4) endereços consecutivos a partir de
  uma base no mesmo sub-rede, iniciado com `net_dhcpv4_server_start(iface,
  &base)`. **`NET_DHCPV4_SERVER_OPTION_ROUTER` é `y` por default**: desligar,
  senão o PC pode trocar a rota padrão e perder a internet do Wi-Fi.
  `NET_DHCPV4_SERVER_OPTION_DNS_ADDRESS` default vazio (ok).
  Puxa `NET_SOCKETS_SERVICE` (thread extra): medir RAM com `ram_report`.
- `CONFIG_NET_IPV4_AUTO` (link-local na placa) é **EXPERIMENTAL** no 4.4.
  Não usar.
- `native_sim` usa NSOS (sockets do host). DHCP server não se aplica lá:
  habilitar só no conf da placa real (`boards/<placa>.conf`), não no `prj.conf`.

## Plano de ação — rede plug and play

Objetivo: PC com rede em "automático" (padrão de fábrica do Windows e do
Linux), ligado no switch da bancada, fala com SIBA e SAME **sem regra de
firewall, sem IP fixo e sem mexer em driver**. Cada fase é testável sozinha.

### Fase 0 — Consertar o que está errado (pequeno)

- [ ] SIBA `prj.conf`: IP `169.254.97.51` → o mesmo do YAML.
- [ ] SAME `prj.conf`: declarar `CONFIG_NET_CONFIG_MY_IPV4_NETMASK` explícito
      (igual à SIBA).
- [ ] `manager.check_subnet()`: compara 3 octetos (só vale /24). Usar
      `ipaddress.ip_network` com a máscara real.
- [ ] Teste de bancada: cabo plugado após o boot, nas duas placas.

### Fase 1 — Placa responde a quem falou com ela (firmware, `NCI.c` das duas)

Usa só a API de sockets do Zephyr; nada de rede nova.

- [ ] **Um socket só**, com `bind` em `LOCAL_PORT`, para RX e TX. A
      telemetria passa a sair de `IP_placa:LOCAL_PORT`, que é exatamente o
      destino do CMD do PC. Para o firewall stateful (Windows Defender, ufw,
      firewalld) isso é **resposta** e entra sem regra. Validar no
      `native_sim` que `zsock_sendto` (na `*_wq`) convive com o `recvfrom`
      bloqueado na thread `nci_rx`.
- [ ] Em cada CMD válido, guardar `client` (IP+porta) como `peer_addr`
      (protegido por `k_spinlock`). Telemetria vai para o último peer.
- [ ] Sem peer ainda: não transmitir (ou broadcast, só para depuração).
      Remover `CONFIG_NET_CONFIG_PEER_IPV4_ADDR` do caminho de TX.
- [ ] Timeout de peer (ex.: 5 s sem CMD): voltar ao estado "sem peer". Na
      SIBA, opcionalmente, isso vira watchdog de segurança (PC sumiu → ESC em
      1000 µs), ligado ao `Security_Manager`.

### Fase 2 — App acompanha (`Aplicacao_Pi`)

- [ ] `UdpStructLink`: já usa um socket só e envia da porta `pc_rx`. Manter.
      No Windows, trocar `SO_REUSEADDR` por `SO_EXCLUSIVEADDRUSE`.
- [ ] Heartbeat de 1 Hz por placa (CMD neutro do `test_io`, ou um
      `FUNC_HEARTBEAT` **no fim** do enum do YAML + `generate.py`). Mantém o
      estado do firewall e o peer da placa vivos.
- [ ] Aceitar telemetria de IP diferente do configurado quando chegar na porta
      certa com `len == sizeof(TELEMETRY_LOG)`: adotar o IP (`update_ip`) e
      logar. Acaba com IP hardcoded divergente.
- [ ] Apagar a criação de regra `netsh` do `Teste/recv_siba.py`.
- [ ] Mensagens do TinyTerminal: trocar "configure seu IP fixo" por
      diagnóstico real (sem IPv4 na interface, WSL2 NAT, etc.).

Com Fase 0+1+2, **Windows fica plug and play** (APIPA automático + resposta
stateful). Linux ainda depende de o PC ter IP na sub-rede.

### Fase 3 — Placa entrega IP ao PC (DHCP server do Zephyr)

- [ ] Mudar a sub-rede da bancada para privada, ex. `192.168.97.0/24`
      (SIBA `.200`, SAME `.100`). DHCP entregando `169.254/16` viola a RFC
      3927 e o comportamento do cliente não é garantido.
- [ ] `boards/<placa>.conf` (só hardware):
      `CONFIG_NET_DHCPV4_SERVER=y`,
      `CONFIG_NET_DHCPV4_SERVER_OPTION_ROUTER=n`,
      `CONFIG_NET_DHCPV4_SERVER_ADDR_COUNT=4`.
- [ ] Iniciar o servidor no callback de `NET_EVENT_IF_UP` com pools
      **disjuntos** (SIBA `.10–.13`, SAME `.20–.23`). Com as duas ligadas o PC
      aceita a primeira oferta; com uma só, ainda funciona.
- [ ] Atualizar YAML, `prj.conf`, READMEs e `native_sim` (o mapeamento
      `127.0.0.<último octeto>` continua valendo).
- [ ] **Regra de uso**: switch exclusivo da bancada. Ligado na rede do
      laboratório, a placa distribuiria IP para outros PCs.

Com a Fase 3, Linux (NetworkManager padrão) e Windows pegam IP em segundos,
sem esperar o APIPA, e mantêm a internet do Wi-Fi (sem rota padrão).

### Fase 4 — Validação (matriz mínima)

| PC | Firewall | Cabo |
|---|---|---|
| Windows 11 | Defender ligado, perfil Público | switch e direto |
| Ubuntu | `ufw enable` | switch |
| Fedora | `firewalld` padrão | switch |

Em cada um: Wi-Fi ligado ao mesmo tempo, placa ligada antes e depois do PC,
`siba test_io` e `same test_io` verdes, nenhuma regra criada
(`netsh advfirewall firewall show rule name=all` / `ufw status`).

### Descartado

- mDNS (`CONFIG_MDNS_RESPONDER`): IP fixo + aprendizado por porta já resolve
  a descoberta; custaria RAM e resolução `.local` no Windows é irregular.
- Broadcast como caminho principal: Windows aceita resposta unicast a
  broadcast por 3 s, mas o conntrack do Linux não associa, e o Defender
  bloqueia broadcast de entrada no perfil Público.
