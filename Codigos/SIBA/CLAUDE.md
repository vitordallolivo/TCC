# SIBA — contexto para Claude Code

Contexto comum (protocolo, rede, plano de ação): [`../CLAUDE.md`](../CLAUDE.md).
Descrição completa (sensores, shell, protocolo, pinagem): [`README.md`](README.md).

Firmware Zephyr 4.4.0 da placa aerodinâmica + bateria (CH32V307, RISC-V
144 MHz). Mede 6 HX711, RPM, Pitot, bateria; **aciona o ESC** e os relés.
UDP: escuta CMD na 5000, envia telemetria (64 B, 10 Hz) para a 5555.

## Fontes de verdade

- Pinagem: `boards/wch/siba_board/siba_board.dts` (manda sobre qualquer doc).
- Protocolo, defaults de sensor, bits de erro: `../Aplicacao_Pi/Config/SIBA.yaml`
  → `generate.py`.
- Habilitar/desligar módulo para bissecção: `lib/OS/HAL_prm.h`
  (`HAL_DISABLE_*`). **ADC está desligado nesta versão da placa**
  (`HAL_DISABLE_ADC`): v_batt, i_batt, temperaturas e Pitot saem 0.

## Arquitetura

`main.c` → Managers (Battery, Propeller, Security, Log; sem thread) → OS
(`OSSI` = `siba_wq`, `NCI` = UDP, `HAL`, `CMD_Core`) → drivers.

Threads: `hal_hx711` (pri 6), `nci_rx` (pri 5, `recvfrom` na 5000), `siba_wq`
(pri 5). ADC e RPM são ISR + `k_work`; TX de telemetria é `k_timer` de 100 ms.

Ordem de init: `OSSI → Security → Battery → Propeller → Log → CMD_Core → NCI →
HAL → NVM` (NVM restaura calibração da EEPROM I2C2 0x50).

## Armadilhas de hardware

- **Relés**: módulo de 5 V ativo em nível baixo (`GPIO_ACTIVE_LOW`). 0 V liga;
  para desligar o pino fica em **alta impedância** (`hal_relay_drive`), pois
  3,3 V em push-pull não desliga o módulo. Ligar: pré-carga →
  `CONFIG_SIBA_RELAY_PRECHARGE_MS` (5 s) → confere o botão → potência.
  `HAL_Relay_Energize()` bloqueia a `siba_wq` (e a telemetria) nesse tempo.
- **Emergência PD1**: corte físico no 12 V das bobinas; o micro só lê por um
  divisor (0 V = emergência), sem pull interno.
- **PWM do ESC (PB6)**: `pwms = <&pwm4 0 ...>` (canal começa em 0 = CH1) e
  `output-low` no pinctrl; sem isso o pinctrl deixa o pino analógico. Medido:
  50 Hz, 1000–2000 µs. `FUNC_ESC` recebe **0–100 %** com `INSTR_WRITE`.
- **"Pronto" repetido**: a GUI manda `FUNC_START`/`STATUS_OK` a cada 30 s;
  `Security_Manager_Arm_Bench` não rearma um ESC já armado (não zera o throttle).
- **Shell só no `native_sim`**: `CONFIG_SHELL=n` no `prj.conf` (economiza
  ~48 kB de ROM); `boards/native_sim.conf` religa.
- Pausar o core pelo WCH-Link congela o Ethernet até o reset.
- **PC0/PC1** são LEDs do PHY interno: nunca usar como ADC.
- `CONFIG_ADC_CH32V00X=n` obrigatório: o driver upstream também casa com
  `wch,adc` e anula o driver custom `drivers/adc/adc_ch32v307.c`.
- `&dma1` fica `disabled`: o driver ADC é dono da IRQ do DMA1.
- I2C upstream (`wch_i2c_finish_transfer`) trava se o barramento travar:
  power cycle.
- `CONFIG_FPU_SHARING=y` salva 128 B por preempção: stacks em `prj.conf` foram
  dimensionadas para isso, não reduzir sem medir.

## Rede

IP no `prj.conf` alinhado com o YAML (`169.254.97.200`). Telemetria em
broadcast; no Linux com ufw, liberar a UDP 5555. LED verde PB4 acompanha `NET_EVENT_IF_UP/DOWN`.

## native_sim

`make native_sim && make native_sim_run`. Loopback `127.0.0.200`, NSOS para
UDP, HX711/RPM/ADC sintéticos, I2C emulado. `HAL_SIM_NOISE_SCALE` controla
ruído. Shell pela stdin com `-uart_stdinout` (`btn on|off` finge a emergência).

## Pendências

- Calibrar sensores ADC quando os circuitos forem montados (`siba sensor <id>
  linear|offset`).
- `cell_count_lut[]` com faixas sobrepostas em `Battery_Manager.c`: usar
  `siba batt cells <n>` fixo.
- Propeller_Manager: Ct/Cp não implementado.
- PHY LINK/ACT LEDs (PC0/PC1) sem pinctrl.
- `BAT_ACTION_CELL_COUNT_OVERWRITE` só pelo shell, não por CMD.
- Watchdog (IWDG) e máquina de estados global em `App.h`.
