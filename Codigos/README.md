# Codigos — TCC Equipe Albatroz Aerodesign (UDESC Joinville)

Monorepo do TCC: firmwares das placas embarcadas + aplicação de PC que as
comanda. O objetivo é caracterizar e controlar um motor **BLDC** de aeromodelo
(bancada) e telemetria aerodinâmica.

## Estrutura

| Pasta | O que é |
|-------|---------|
| **`Aplicacao_Pi/`** | **Aplicação PySide6 (GUI)** que roda no PC/Raspberry Pi — cliente unificado das três placas. Ver `Aplicacao_Pi/README.md`. |
| **`SIBA/`** | Firmware da placa de **gerenciamento de energia e telemetria** (CH32V307, Zephyr). Comunicação UDP com protocolo compacto (`CMD` 8 B + `TELEMETRY_LOG`). |
| **`SAME/`** | Firmware do **controle de inversor/motor** (SMO/estimação). Mesmo protocolo UDP da SIBA. |
| **`SPMM/`** | Firmware da **bancada provisória de medição mecânica** do BLDC (atrito B/τc, inércia J). Shell **Telnet (TCP)** + log rápido por **UDP binário**. Ver `SPMM/CLAUDE.md`. |
| **`Equations/`** | Scripts de referência (otimização dos ganhos SMO, simulação, hélice) — os usados pela GUI ficam copiados em `Aplicacao_Pi/Scripts/`. |

## Protocolos (resumo)

- **SIBA/SAME** — UDP. TX (PC→placa) = `CMD` 8 bytes `<BBBfb>` (func, instr,
  target, value, action). RX (placa→PC) = `TELEMETRY_LOG` (struct packed do
  `.yaml`) a 10 Hz. **A fonte de verdade do protocolo são os
  `Aplicacao_Pi/Config/<placa>.yaml`**, dos quais os headers C são gerados
  (`Config/generate.py`).
- **SPMM** — comandos de bancada por **Telnet (TCP 23)** em texto
  (`arm`/`throttle`/`coast`/`log`…) + log por **UDP binário** (cabeçalho `<HH>`
  + amostras `<I11f>`), com base de tempo `t = n·Ts` do micro.

## Rodar

- **GUI:** `cd Aplicacao_Pi && python mainApp.py` (venv Python 3.13 em `.venv`).
- **Firmware (placa):** `make` em cada pasta (Zephyr/west).
- **Firmware no PC (`native_sim`, só Linux):** `make native_sim` gera
  `build_native_sim/zephyr/zephyr.exe` — a GUI sobe esse binário e conversa por
  UDP/loopback (ver "Simulação Nativa" no Gerenciador de Placas). Útil para
  validar o link fim-a-fim sem bancada.

Licença: **MIT** (ver `../LICENSE`).
