# Aplicacao_Pi — contexto para Claude Code

Contexto comum (protocolo, rede, plano de ação): [`../CLAUDE.md`](../CLAUDE.md).
Descrição completa (GUI, TinyTerminal, modos, scripts): [`README.md`](README.md).

GUI PySide6 (`python mainApp.py`), cliente único da SIBA e da SAME por UDP.
Modo de Operação `SIBA | SAME | SIBA+SAME` decide quais links sobem e para
quem vai o throttle (sempre a SIBA; no modo SAME sozinho o slider fica
desabilitado).

## Camadas

| Camada | Arquivos |
|---|---|
| Transporte | `Communication_Data/links/` (`BoardLink`, `UdpStructLink`) |
| Domínio (puro) | `registry.py` (lê `Config/<placa>.yaml`), `codecs.py`, `motor_config.py`, `errors.py` |
| Serviço | `manager.py` (`LinkManager`), `discovery.py`, `estimation.py`, `native_sim.py` |
| Apresentação | `GUI/` |

`Ethernet.py` é só shim de compatibilidade (`EthernetManager = LinkManager`).
`LinkManager` itera o registry: não escrever `if board == "SIBA"`.

## Três modos por placa

1. **Hardware** (`<placa> connect`): IP e portas do bloco `link:` do YAML.
2. **Mock Python** (`<placa> mock start`): `BoardSimulator`, **fake**, pode
   divergir do firmware.
3. **native_sim** (`<placa> native start`, só Linux): firmware real como
   processo, loopback `127.0.0.<último octeto do IP>`. É o modo confiável para
   validar protocolo. `<placa> native shell <cmd>` fala com o shell do
   firmware; `native_cli.py` faz o mesmo sem GUI.

`<placa> test_io` manda um CMD neutro e conta RX/TX no socket: funciona nos
três modos.

## Armadilhas

- `UdpStructLink` aceita RX só do IP configurado (ou `127.*`). Com o IP da SIBA
  divergente do firmware, a telemetria real é descartada. Ver plano de rede.
- `check_subnet()` compara 3 octetos (assume /24).
- `LinkManager.stop()` precisa limpar `self.workers`: thread não reinicia
  (bug já corrigido; não regredir).
- GUI usa um buffer de tempo comum às placas: sample-and-hold evita buracos
  de `NaN` no modo SIBA+SAME.
- Arquivos de `Scripts/` são **CRLF**: preservar (`newline=''`).
- `Kv` do motor (310 RPM/V) é de placa, por volt de barramento: **não**
  dividir o Kv por √3. O √3 vai no Ke: `Ke1 = 60/(2π·Kv·√3)`, `Kt = 1,5·Ke1`
  (`motor_config.ke_kt_from_kv`, igual ao firmware).
- R/L do motor vêm de ensaio de bancada, não do autotune (que só dá Γ1/Γ2/Γt).
- Estimativas que gravam no `SAME.yaml` pedem confirmação antes.
