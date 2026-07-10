# SAME — Sistema de Aquisição do Motor Elétrico

Nó de aquisição **elétrica** do motor da bancada de ensaios BLDC (TCC Albatroz
Aerodesign / UDESC). Lê correntes e tensões de fase por **dois ADCs** do
CH32V307 e **estima ω e torque no eixo SEM sensores mecânicos** via Observador
por Modos Deslizantes (SMO) + PLL. Só observa — quem aciona o ESC é a SIBA.

Firmware Zephyr, arquitetura em camadas espelhando a SIBA. Ver `CLAUDE.md` para
a especificação completa (protocolo, equações do SMO, mapa de pinos).

## Build

```bash
make build          # placa real (same_board / CH32V307), gera zephyr.bin
make flash          # grava via WCH-LinkE (OpenOCD)
make native_sim     # binário POSIX no PC (motor virtual synthetic)
make native_sim_run # roda o binário native_sim
make debug          # OpenOCD + GDB
```

## native_sim (sem hardware)

O build `native_sim` roda como um binário no host: os 2 ADCs viram
`zephyr,adc-emul` e a thread de aquisição SINTETIZA um motor girando a
`HAL_SIM_DEMO_RPM` (`lib/OS/HAL_prm.h`). A cadeia ADC→Clarke→SMO→PLL→telemetria
roda inteira no PC e `rpm_same` converge para o valor synthetic; a rede usa os
Offloaded sockets (UDP real no `127.0.0.1`), então a `../Aplicacao_Pi/` troca
CMD/telemetria de verdade com o firmware.

- Telemetria (placa→PC): UDP `127.0.0.1:4445`
- CMD (PC→placa):        UDP `127.0.0.1:4444`

## Fluxo de eventos

Duas threads de I/O bloqueante apenas: **aquisição** (bloqueia na conversão dos
ADCs → roda `Observer_Step` colado a esse evento) e **NCI RX** (bloqueia em
`recvfrom`). Todo o resto (telemetria no log, despacho de CMD, TX UDP) é `k_work`
na `same_wq`. Mudar um parâmetro/ganho por CMD **reinicia** o observador.
