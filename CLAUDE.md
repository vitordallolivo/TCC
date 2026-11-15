# TCC — sistema de aquisição para ensaios de motores BLDC

Placa de medição (CH32V307, Zephyr) + aplicação de supervisão no PC, uma
placa só. A GUI e o CLI (`gui/placa.py`) não distinguem placas. O firmware
**não foi renomeado** (decisão do autor, 15/11/2026): por dentro ainda usa o
prefixo `SIBA` (`siba_wq`, `SIBA_SENSOR_TABLE`, board `siba_board`,
`CONFIG_SIBA_*`) e o campo de telemetria `rpm_same`. Não renomear sem pedido.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `firmware/` | Firmware Zephyr da placa de medição: fases (V/I) + SMO+PLL, ESC, relés, segurança. Ver `firmware/CLAUDE.md`. |
| `gui/` | Aplicação de supervisão (PySide6), scripts de simulação/autotune (`gui/Scripts`) e ensaios (`gui/Ensaios`). Ver `gui/CLAUDE.md`. |
| `docs/TCC_documento/` | Texto do TCC (LaTeX, abnTeX2). Ver `docs/TCC_documento/CLAUDE.md`. |
| `docs/TCC_apresentacao/` | Apresentação (Beamer); usa as figuras de `docs/TCC_documento`. |
| `legado/` | Código antigo fora do build (`Equations/`, `Teste/`). |
| `Simulacoes/`, `Esquematicos/`, `Figuras/` | Simulações de circuito, esquemáticos e figuras avulsas. |

`make help` na raiz lista os atalhos (firmware, protocolo, GUI, documentos).

## Regras que valem para tudo

- **Protocolo tem fonte única**: `gui/Config/protocolo.yaml`. Os headers C
  (`CMD.h`, `Common_Types.h`, `Log_Manager.h`, `CMD_Core_prm.h`,
  `Error_Codes.h`) são **gerados** por `make protocolo`
  (`gui/Config/generate.py`). Nunca editar `.h` gerado à mão.
- Remover item do meio de um enum renumera o resto e o `CMD` é byte cru:
  acrescente no fim ou reserve o slot.
- Zephyr v4.4.0, SDK 1.0.1. `native_sim` só roda no Linux.
- Commits direto na `master`, sem PR (preferência do autor).

## Rede — estado atual

UDP, `CMD` de 8 bytes (PC→placa) e `TELEMETRY_LOG` packed LE a 10 Hz
(placa→PC). IP da placa `169.254.97.200`, CMD na porta 5000, telemetria por
broadcast para a porta 5555; `native_sim` em `127.0.0.200`. Sem DHCP: o PC
precisa de IP link-local na mesma sub-rede e regra de firewall de entrada.

## Suporte da placa (CH32V307) no Zephyr 4.4 — conferido no código-fonte

- Driver Ethernet: o **oficial do Zephyr** (`zephyr/drivers/ethernet/eth_wch.c`,
  `CONFIG_ETH_WCH`, ligado sozinho pelo `&mac` no DTS). Não há driver de
  Ethernet próprio no repositório. O DTS usa `phy-connection-type =
  "internal"`, que significa o **PHY embutido no chip** CH32V307 (não um driver
  nosso); o driver oficial configura o PLL3 dele. Esse PHY embutido é
  **10BASE-T**: o switch precisa aceitar 10 Mbit/s (LED de link aceso é o teste).
- Link detectado pelo `phy_mii` genérico **por polling MDIO** (a IRQ
  `PHYLINK` é só limpa). O commit `ec5f1fb` registra que o `phy_mii` deu
  problema na placa e o `4e920ee` diz que o ETH foi resolvido. Testar
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
