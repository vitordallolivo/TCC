# gui — contexto para Claude Code

Contexto comum (protocolo, rede): [`../CLAUDE.md`](../CLAUDE.md).
Descrição completa (GUI, comandos, TinyTerminal, scripts): [`README.md`](README.md).

GUI PySide6 (`.venv/bin/python mainApp.py`) e CLI (`placa.py`) para **uma
placa só**. Não há mais distinção entre placas: o id interno do link é
`"PLACA"` (chave do `LinkManager`/registry) e o protocolo é
`Config/protocolo.yaml`.

## Camadas

| Camada | Arquivos |
|---|---|
| Transporte | `Communication_Data/links/` (`BoardLink`, `UdpStructLink`) |
| Domínio (puro) | `comandos.py` (gramática), `registry.py`, `codecs.py`, `motor_config.py`, `errors.py` |
| Serviço | `manager.py` (`LinkManager`), `discovery.py`, `estimation.py`, `native_sim.py` |
| Apresentação | `GUI/`; CLI em `placa.py` |

`Ethernet.py` é só shim de compatibilidade (`EthernetManager = LinkManager`).

## Comandos: uma gramática só

`Communication_Data/comandos.py` (`Protocolo.interpretar(linha)`) transforma
`arm`, `off`, `estop`, `esc N`, `reset`, `force on|off`,
`set <sensor> linear|offset|min|max|reset [v]`, `set <parâmetro> v|reset`,
`bat ...`, `raw ...` em `CMD` de 8 bytes. Usada por TinyTerminal
(`enviar_comando`), `placa.py`, `SensorConfigDialog`, `LoadCellDialog` e
`Scripts/cmd_test.py`. Comando novo: acrescentar ali, não em cada cliente.
Alvo desconhecido levanta `ErroComando` (antes caía em `TARGET_DEFAULT` ou
no índice 0 em silêncio).

`Config/commands.yaml` só alimenta help/autocompletar do TinyTerminal.

## Três modos de conexão

1. **Hardware** (`connect [ip]`): IP e portas do bloco `link:` do yaml.
2. **Mock Python** (`mock start`): `BoardSimulator`, **fake**.
3. **native_sim** (`native start`, só Linux): firmware real como processo,
   loopback `127.0.0.200`. Modo confiável para validar protocolo. As fases
   do motor ficam em zero (o pipeline do ADC é só da placa); o NVM não tem
   EEPROM (o `nvm` do shell mostra zeros). `native shell <cmd>` fala com o
   shell do firmware; `placa.py --native` faz o mesmo sem GUI.

`test_io` manda um CMD neutro e conta RX/TX no socket: funciona nos três.

## Armadilhas

- `UdpStructLink` aceita RX só do IP configurado (ou `127.*`).
- `check_subnet()` compara 3 octetos (assume /24).
- `LinkManager.stop()` precisa limpar `self.workers`: thread não reinicia.
- Com a GUI aberta, ninguém mais recebe a telemetria (ela segura a 5555):
  `placa.py tele/watch/errs` e os `Ensaios/` precisam da GUI fechada; os
  comandos que só enviam funcionam com ela aberta.
- O `TinyTerminal` troca o `sys.stdout`: em teste da GUI, imprimir via
  `sys.__stdout__`.
- `Kv` do motor (310 RPM/V) é de placa, por volt de barramento: **não**
  dividir o Kv por √3. O √3 vai no Ke: `Ke1 = 60/(2π·Kv·√3)`, `Kt = 1,5·Ke1`
  (`motor_config.ke_kt_from_kv`, igual ao firmware).
- R/L do motor vêm de ensaio, não do autotune (que só dá Γ1/Γ2/Γt).
- Estimativas (ganhos do SMO, coeficientes da hélice) gravam em
  `Config/estimativas.yaml` só após confirmação.
- Nomes internos do firmware continuam com o prefixo antigo
  (`SIBA_SENSOR_TABLE`, `siba_error_bit_t`, `rpm_same` na telemetria):
  `generate.py` mantém `PREFIXO_FW = "SIBA"` para os headers gerados não
  mudarem. `rpm_same` é também nome de coluna dos CSVs de ensaio.

## Observador e "pronto"

- Parâmetros do motor/observador vão por `FUNC_SENSOR` (alvos
  `TARGET_POLES_PAIRS..TARGET_TS_ADC`), qualquer ação ≠ RESET_DEFAULT grava
  `value`. `ts_adc_us` do perfil **não** vai para a placa
  (`board_skip_keys`): o Ts é o do ADC.
- A GUI manda `FUNC_START`/`STATUS_OK` ("pronto") no 1º pacote e **a cada
  30 s**: fecha os relés e arma o ESC se a emergência estiver liberada.
  "Desligar" suspende.
- `Ensaios/ensaio_degraus.py`: degraus de throttle pela rede, CSV com toda a
  telemetria (GUI fechada). Respeita o botão de emergência; em erro ou
  Ctrl-C manda throttle 0 + desligar.
- `Scripts/plot_pi.py` e `helice_coeff.py` aceitam `SMO_GAINS_JSON`,
  `SMO_PLOT_SUFFIX` (e `SMO_PLOT_ONLY` no plot_pi) junto com `SMO_K_LOAD`
  para gerar as figuras por hélice (15x6E: 1,0e-6; 17x10E: 2,6e-6).
- Arquivos de `Scripts/` podem ser **CRLF**: preservar.
