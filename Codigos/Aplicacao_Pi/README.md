# Aplicacao_Pi — GUI unificada (Albatroz)

Aplicação **PySide6** que roda no PC/Raspberry Pi e é o **cliente único** das
placas do projeto — **SIBA**, **SAME** (UDP `CMD`/`TELEMETRY_LOG`) e **SPMM**
(Telnet + UDP binário). Mostra telemetria em tempo real, comanda o motor e roda
as estimativas de parâmetros.

> Arquitetura, protocolos e decisões de projeto em detalhe: ver `CLAUDE.md`.

## Rodar

```bash
python mainApp.py          # usa o venv Python 3.13 em .venv/
```

## Estrutura em camadas (`Communication_Data/`)

- **Transporte** (`links/`): `BoardLink` (ABC) → `UdpStructLink` (SIBA/SAME),
  `TelnetLink` (SPMM); simuladores e `native_sim`.
- **Domínio** (`codecs.py`, `registry.py`): (des)serialização pura + `BoardConfig`
  dirigido pelos `Config/<placa>.yaml`.
- **Serviço** (`manager.py`, `discovery.py`, `estimation.py`): `LinkManager`
  genérico, descoberta de placas, runner dos scripts de estimação.
- **Apresentação** (`GUI/`): janela principal, TinyTerminal, e popouts
  (Gerenciador de Placas, Estimativas, Calibração de célula, Ensaios SPMM).

## Funcionalidades

- **Modo de Operação** (`SPMM | SIBA | SAME | SIBA+SAME`) — define quais placas
  sobem, para onde vai o throttle (slider) e **reordena os gráficos** relevantes.
- **Gerenciador de Placas** — procurar placas, editar IP/porta em runtime, LED
  verde/vermelho, e **Simulação Nativa** (native_sim, só Linux).
- **Estimativas** (menu próprio): Ganhos do **SMO** (autotuning, R/L, custo ao
  vivo), **Coeficiente da hélice** (CT/CQ/J a partir do CSV exportado) e
  **Ensaios SPMM** (atrito B/τc e inércia J).
- **Calibração de célula de carga (HX711)** — tara/ganho, com diagnóstico de
  desconectada/descalibrada.
- **TinyTerminal** — shell embutido (`siba`/`same`/`spmm`/`mock`/`native`…).

## Três formas de "ligar" uma placa

1. **Hardware real** — cabo Ethernet, IP link-local `169.254.97.x`.
2. **Mock (Python)** — simulador fake para testar a GUI sem placa.
3. **native_sim (só Linux)** — o **firmware real** compilado para POSIX rodando
   como processo, trocando UDP de verdade em loopback.

## Config (`Config/`)

`SIBA.yaml` / `SAME.yaml` / `SPMM.yaml` são a **fonte de verdade** do protocolo
e do endereçamento (bloco `link:`). `commands.yaml` (TinyTerminal), `plots.yaml`
(gráficos), `generate.py` (gera os headers C das placas).

Licença: **MIT** (ver `../../LICENSE`).
