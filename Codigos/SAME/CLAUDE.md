# SAME — contexto para Claude Code

Contexto comum (protocolo, rede, plano de ação): [`../CLAUDE.md`](../CLAUDE.md).
Descrição completa: [`README.md`](README.md). Notas longas de projeto (mapa de
registradores do ADC, benchmarks do observador, histórico de decisões):
[`docs/notas_tecnicas.md`](docs/notas_tecnicas.md) — ler só quando mexer em
ADC, DMA ou `Observer_Manager`.

Firmware Zephyr 4.4.0 do nó elétrico do motor (CH32V307). Lê V e i das três
fases (ADC1 = correntes, ADC2 = tensões, **dual simultâneo**, um DMA de 32 bits)
e estima ω e torque com **SMO + PLL** (motor Scorpion A-5025-310Kv).
**Só observa**: quem aciona o ESC é a SIBA. Sem shell UART; todo ajuste é por
CMD. UDP: escuta CMD na 4444, envia telemetria para a 4445.

## Arquitetura

Duas threads bloqueantes só: aquisição (espera o ADC e roda `Observer_Step`
colado ao frame) e `nci_rx`. O resto é `k_work` na `same_wq`. CMD que muda
parâmetro marca `reconfig`; a thread do ADC aplica no topo do laço
(`Observer_Apply_Pending`), recalcula constantes e **zera o estado**.

## Invariantes que não podem quebrar

- **Ganhos são Γ, não K**: `Γ = K·Ts` é feito uma vez no Python; o firmware
  usa Γ direto. Logo `OBS_TS_NS` (`Observer_Manager.h`) **tem** de ser igual ao
  `Ts_sim` do `autotune_cost.py`. Mudou um, reotimiza.
- Sinais: `Γ1 > 0`, `Γ2 < 0`, `Γt > 0`.
- `OBS_IMPL = 5` (LUT de seno `static const` em `.rodata` + identidade
  `g_β ≡ −cos θ`). A identidade só vale para **BEMF senoidal**; trapezoidal
  exige LUT com 3 consultas. Não gerar a LUT em RAM com `sinf`.
- Função de comutação default **tanh**. Sigmoide só depois de reotimizar Γ com
  `SMO_SWITCH_FUNC = 2` (sem isso dá ~2 % de erro no torque).
- `BL` não pode ser constante cega: estabilidade pede `BL > Γ1/(1+Φi)`.
- `Te = 1.5·ke_mec·(…)`: `pole_pairs` cancela no torque de propósito; só afeta
  `rpm_same`.
- `TELEMETRY_LOG`/`CMD`/enums vêm do `SAME.yaml`. Enums do `CMD` são
  `packed` para `sizeof(CMD) = 8` em qualquer compilador.

## Aquisição (resumo)

- ADCCLK = 144 MHz / 8 = **18 MHz** (acima dos 14 MHz da folha de dados; não
  existe divisor maior). Baixar PCLK2 quebra o baud da USART: está atrás de
  `CH32_ADC_LOWER_APB2`, desligado.
- `SMPx = 28,5` → frame de 3 canais ≈ 6,58 µs, que **não** é o `OBS_TS` de
  10 µs. O driver avisa no boot (`LOG_WRN`). Caminho: fixar `OBS_TS_NS` no
  período real e rodar o autotune com o mesmo `Ts`.
- Calibração: autocalibração do HW repetida 10× com média aparada (como a SDK
  da WCH), depois offset dos sensores com ESC parado, **após 2 s** de
  estabilização do Hall, Welford em `float`. Observador só liberado depois.
- DMA em ping-pong (HT + TC); `dropped_frames` exposto por
  `adc_ch32v307_get_stats()`.

## Testes

- `tests/observer_golden`: `Observer_Manager.c` real no host (matemática).
- `tests/observer_bench`: mesmo arquivo em rv32imafc no qemu (instret,
  footprint). Nenhum dos dois cobre DMA/registradores: isso só na placa.
- `native_sim` (loopback `127.0.0.100`) **não passa pelo driver de ADC**
  (gerador sintético em `HAL.c`), então não pega bug de aquisição.

## Pendências

- Medir `Observer_Step` com GPIO toggle + osciloscópio para fechar `SMPx`.
- Ligar `dropped_frames` a um bit de `error_bits` no `SAME.yaml`.
- PC0 (V_c) com conexão solta na placa montada: V_c suspeito até o reparo.
- `rom_report`/`ram_report` com pilha Ethernet (antes da Fase 3 do plano de rede).
- Rede: telemetria vai para unicast fixo `169.254.97.139`. Ver plano em
  `../CLAUDE.md`.
