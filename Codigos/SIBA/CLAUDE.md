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

- **Relés invertidos** (`GPIO_ACTIVE_LOW`). Ligar: pré-carga → 3 s → potência.
  Desligar: potência → pré-carga. `HAL_Relay_Energize()` bloqueia 3 s.
- **Emergência PD1**: corte físico no 12 V das bobinas; o micro só lê
  (0 V = emergência, pull-down = fio solto vira emergência).
- **PC0/PC1** são LEDs do PHY interno: nunca usar como ADC.
- `CONFIG_ADC_CH32V00X=n` obrigatório: o driver upstream também casa com
  `wch,adc` e anula o driver custom `drivers/adc/adc_ch32v307.c`.
- `&dma1` fica `disabled`: o driver ADC é dono da IRQ do DMA1.
- I2C upstream (`wch_i2c_finish_transfer`) trava se o barramento travar:
  power cycle.
- `CONFIG_FPU_SHARING=y` salva 128 B por preempção: stacks em `prj.conf` foram
  dimensionadas para isso, não reduzir sem medir.

## Rede

IP no `prj.conf` (`169.254.97.51`) diverge do YAML (`.200`). Ver Fase 0 do
plano em `../CLAUDE.md`. LED verde PB4 acompanha `NET_EVENT_IF_UP/DOWN`.

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
