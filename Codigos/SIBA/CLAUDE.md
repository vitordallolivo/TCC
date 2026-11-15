# SIBA — contexto para Claude Code

Contexto comum (protocolo, rede, plano de ação): [`../CLAUDE.md`](../CLAUDE.md).
Descrição completa (sensores, shell, protocolo, pinagem): [`README.md`](README.md).

Firmware Zephyr 4.4.0 da bancada (CH32V307, RISC-V 144 MHz). Mede 6 HX711 e,
desde 14/11/2026, **as fases do motor pela AQ1 com o observador SMO+PLL que
era da SAME** (a SAME queimou). **Aciona o ESC** e os relés.
UDP: escuta CMD na 5000, envia telemetria (116 B, 10 Hz) em broadcast para a 5555.

## Fontes de verdade

- Pinagem: `boards/wch/siba_board/siba_board.dts` (manda sobre qualquer doc).
- Protocolo, defaults de sensor, bits de erro, limites `SM_*`:
  `../Aplicacao_Pi/Config/SIBA.yaml` → `generate.py` (gera também
  `lib/SYS/Security_prm.h`). Acrescentar só no FIM das listas.
- Módulos para bissecção: `lib/OS/HAL_prm.h` (`HAL_DISABLE_*`).
  Hoje: ADC **ligado** (fases), RPM por TIM3 **desligado** (`HAL_DISABLE_RPM`),
  HX711 ligado em polling. Pitot/termistores/bateria não são lidos.

## Fases do motor (AQ1) e observador

- ADC dual: ADC1 PA0/PA1/PA3 = i_a/i_b/i_c, ADC2 PC2/PC3/PC4 = V_a/V_b/V_c
  (slot k do ADC1 casado com o slot k do ADC2). i_c derivada
  (`HAL_PH_I_C_DERIVED`). AQ1 J1: 1 GND, 2 +5V, 3 ic, 4 ib, 5 ia, 6 Vc, 7 Vb,
  8 Va. Divisor de tensão 100k/5k6 (0,0188571 V/mV); corrente SS49E +
  4k7/**9k1 no PCB** (yaml usa 10k → 0,0784 A/mV; se 9k1 montado, 0,0809).
- **SMP = 41,5 ciclos** (`CH32_ADC_SMP=4` no CMakeLists) → quadro 9,0 µs, lote de
  256 quadros = 2304 µs. **Ts = `OBS_TS_NS` = 9000** (Observer_Manager.h). Mudou
  SMP, muda `OBS_TS_NS` e refaz o autotune com `--Ts`. Γ default do autotune
  Ts=9 µs, 17x10E: 22,37 / −10 / 0,0591 (`smo_gains_17x10E_Ts9us.json`).
- Pipeline: ISR do DMA copia o lote → fila própria `hal_ph_wq` (**prio 4**, acima
  do HX711 6 e da siba_wq 5) → calibração → `Observer_Step_ABC` por quadro →
  publica a 10 Hz (fases, `rpm_same`, `torque_*`, `t_proc_us`, `t_batch_us`,
  `t_lat_us`, `batch_drops`). Observador e HAL compilam com `-O2
  -fno-math-errno`; tanh por Padé (o `tanhf` da libm era lento).
  Medido pela rede: lote 2304 µs, processamento ~1480–1630 µs, 0 descartes.
- **O que conta é o lote caber no intervalo do DMA** (o observador só "escuta"
  o motor); o Ts só entra nas equações.
- Calibração de offset das correntes no boot (2 s + 4096 quadros, ESC parado).
  Critérios (SIBA.yaml): desvio ≤ `SM_CAL_STD_MAX_MV` (20 mV; ruído real 9–10)
  e média entre `SM_CAL_MEAN_MIN/MAX_MV` (1,2–2,2 V) — sem isso, com cabo
  solto, aceitava fundo de escala como zero. Offsets ajustáveis por
  CMD (`FUNC_SENSOR`, alvos `TARGET_PHASE_*`, ação OFFSET).
- Parâmetros do observador por CMD (`FUNC_SENSOR`, alvos 19–28, qualquer ação ≠
  RESET_DEFAULT grava `value`), salvos no NVM.

## NVM (EEPROM I2C2 0x50) — layout v2

Header 0x000 · calibração 0x020 (19 sensores, 6 páginas) · observador 0x0E0.
A v1 (13 sensores) é migrada sem perder as células. O bloco do observador
guarda o CRC dos defaults do firmware (`def_sig`): firmware com defaults novos
descarta os da EEPROM, senão o Γ/Ts velho venceria.

## Armadilhas de hardware / Zephyr

- **`CONFIG_RISCV_ALWAYS_SWITCH_THROUGH_ECALL=y`** (`siba_board_defconfig`) é
  obrigatório: o PFIC da WCH só libera a IRQ no `mret`; sem isso, troca de
  thread dentro de uma ISR deixava o SysTick "ativo" (IACTR) e a ISR do DMA
  atrasava 6–60 ms.
- **O WCH-Link conectado reprograma o clock** (120 MHz, ADC /2): medição de
  tempo/Ts com o openocd conectado é falsa. Medir pela rede, após power cycle.
  Ler memória sem `halt` funciona, mas já perturba o clock.
- Halt/reset pelo debugger congela o Ethernet → power cycle. `make flash`
  falha às vezes ("error writing to flash"): repetir.
- `k_cycle_get_32()` deste SoC volta ~10 ms (SysTick): o HAL usa TIM2 livre
  a 1 MHz (`hal_us16`). Ferramentas de depuração no HAL: vetor de eventos
  circular que congela no 1º gap (`hal_trace`, `hal_trace_gap_at`), log de
  gaps (`hal_gap`), contador `hal_isr_count`.
- **Relés**: módulo 5 V ativo em baixo; desligar = alta impedância. Ligar:
  pré-carga → `CONFIG_SIBA_RELAY_PRECHARGE_MS` (5 s) → potência.
  `HAL_Relay_Energize()` bloqueia a `siba_wq` → **a telemetria para ~7 s
  durante a pré-carga** (ADC/observador seguem). Corrigir com timer.
- **Emergência PD1** (0 V = emergência). Placa avulsa = emergência ativa;
  `STATUS_EMERGENCY_OVERRIDE` ignora o botão. `FUNC_BATTERY`+`BAT_ACTION_RELAY`
  liga só os relés; `FUNC_START`/`STATUS_OK` arma tudo (relés + ESC);
  `STATUS_ERROR` desliga.
- PWM do ESC (PB6): 50 Hz, 1000–2000 µs, `FUNC_ESC` 0–100 %. O ESC só começou a
  acionar a partir de ~20 % e precisa de ~10 s após os relés.
- HX711 por polling a cada `HAL_HX711_POLL_MS` (50 ms, 2× 10 SPS), sem espera
  bloqueante. Célula solta lê lixo.
- PC0/PC1 = LEDs do PHY, nunca ADC. `CONFIG_ADC_CH32V00X=n` obrigatório.
  `&dma1` `disabled` (o driver ADC é dono da IRQ).

## Estado da bancada (14/11/2026)

- Ensaios: `../Aplicacao_Pi/Ensaios/ensaio_siba.py` (degraus de throttle +
  CSV). Relés, ESC, V_b, V_c e correntes ok.
- V_a (PC2) estava sem ligação elétrica em 14/11 (lia ~13 mV copiando o V_c,
  resíduo do capacitor de amostragem = pino em alta impedância); o autor
  consertou a solda em 15/11.
- **Correntes casadas com as tensões** (`HAL_PH_I_SWAP_BC` + `HAL_PH_I_INVERT`
  em `HAL_prm.h`, 15/11): a sequência das correntes girava ao contrário da das
  tensões → Σv·i ≈ 0 e torque estimado ~25 % do esperado. Diagnóstico pelos
  CSVs: ângulo corrente−tensão por amostra para todas as permutações/sinais;
  a certa concentra o ângulo e dá Σv·i ≈ T·ω. Depois da correção, torque
  estimado com a 15x6E = 1,13–1,23 × (APC J=0 + perdas), crescendo com rpm².
  Os CSVs anteriores a 15/11 10:23 têm `i_a..i_c` na ligação antiga.
- Calibração de boot: desvio máx. 20 mV e média do repouso em 1,2–2,2 V
  (`security_limits` do SIBA.yaml); passou no boot de 15/11.
- `rpm_same` deriva (±1000) com motor parado: zerar a saída quando |ê| < limiar.

## native_sim

`make native_sim && make native_sim_run`. Loopback `127.0.0.200`, NSOS,
HX711/RPM/ADC sintéticos (o pipeline de fases é só da placa). Shell pela stdin
com `-uart_stdinout` (`btn on|off` finge a emergência).
