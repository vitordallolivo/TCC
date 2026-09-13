"""
Receptor de telemetria UDP da SIBA — teste simples de conectividade.

Protocolo: SIBA transmite TELEMETRY_LOG packed (little-endian) para a porta 5555.
Board IP: 169.254.97.51  Broadcast: 169.254.255.255:5555

Dentro do WSL2: o WSL2 usa NAT e NÃO tem acesso direto à interface Ethernet física.
Opções:
  1. Rodar este script no Windows (python recv_siba.py) — acesso nativo ao adaptador.
  2. No WSL2 com modo "mirror" (Win11 .wslconfig networkingMode=mirrored): funciona direto.
  3. No WSL1: funciona direto (compartilha a pilha de rede do Windows).

Uso:
  python recv_siba.py          # aguarda e imprime cada pacote recebido
  python recv_siba.py --once   # imprime 1 pacote e sai (para scripts)
"""

import socket
import struct
import sys
import time

UDP_PORT = 5555

# TELEMETRY_LOG __attribute__((packed)) — little-endian
# uint32 timestamp
# float  load_cell_0..5  (6 floats)
# float  v_batt, i_batt, soc, temperature_battery, air_speed, rpm, temperature_motor (7 floats)
# uint32 status      (enum log_status: 0=VALID 1=INVALID 2=OVERFLOW 3=CRITICAL_ALARM 4=DENIED)
# uint32 error_code
STRUCT_FMT = "<I 6f 7f II"
STRUCT_SIZE = struct.calcsize(STRUCT_FMT)  # deve ser 64 bytes

LOG_STATUS = {0: "VALID", 1: "INVALID", 2: "OVERFLOW", 3: "CRITICAL_ALARM", 4: "DENIED_ACTION"}

ERROR_BITS = {
    0:  "BATT_LOW_SOC",
    1:  "BATT_POLY_MISCONFIGURED",
    2:  "BATT_LOW_VOLTAGE",
    3:  "BATT_RELAY_MISMATCH",
    4:  "BATT_OVER_TEMPERATURE",
    8:  "PROP_LOAD_CELL_NOT_CONFIGURED",
    9:  "PROP_LOAD_CELL_NOT_CONNECTED",
    10: "PROP_OVERCURRENT",
    16: "SEC_VOLTAGE_OUT_OF_RANGE",
    17: "SEC_CURRENT_EXCESSIVE",
    18: "SEC_RPM_EXCESSIVE",
    19: "SEC_EMERGENCY_ACTIVE",
    24: "OS_GPIO_INIT_FAILED",
}

def decode_errors(code):
    active = [name for bit, name in ERROR_BITS.items() if code & (1 << bit)]
    return active if active else ["none"]

def parse(data):
    if len(data) < STRUCT_SIZE:
        return None
    fields = struct.unpack_from(STRUCT_FMT, data)
    return {
        "timestamp_ms":        fields[0],
        "load_cell":           list(fields[1:7]),
        "v_batt":              fields[7],
        "i_batt":              fields[8],
        "soc":                 fields[9],
        "temperature_battery": fields[10],
        "air_speed":           fields[11],
        "rpm":                 fields[12],
        "temperature_motor":   fields[13],
        "status":              LOG_STATUS.get(fields[14], f"?{fields[14]}"),
        "error_code":          fields[15],
        "errors":              decode_errors(fields[15]),
    }

def print_packet(p, addr):
    t = p["timestamp_ms"]
    lc = p["load_cell"]
    print(f"[t={t:8d} ms] from {addr[0]}")
    print(f"  cells:  {lc[0]:7.3f}  {lc[1]:7.3f}  {lc[2]:7.3f}  {lc[3]:7.3f}  {lc[4]:7.3f}  {lc[5]:7.3f}  N")
    print(f"  v_batt: {p['v_batt']:6.2f} V   i_batt: {p['i_batt']:6.2f} A   soc: {p['soc']:5.1f}%")
    print(f"  temp_bat: {p['temperature_battery']:5.1f} C   temp_mot: {p['temperature_motor']:5.1f} C")
    print(f"  pitot: {p['air_speed']:6.2f} m/s   rpm: {p['rpm']:7.1f}")
    print(f"  status: {p['status']}   errors: {', '.join(p['errors'])}")
    print()

def main():
    once = "--once" in sys.argv

    print(f"TELEMETRY_LOG struct size: {STRUCT_SIZE} bytes (esperado: 64)")
    assert STRUCT_SIZE == 64, f"Struct size inesperado: {STRUCT_SIZE}"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", UDP_PORT))
    sock.settimeout(5.0)

    print(f"Aguardando telemetria SIBA na porta {UDP_PORT}...")
    print("(Ctrl+C para sair)\n")

    pkts = 0
    t0 = time.monotonic()

    try:
        while True:
            try:
                data, addr = sock.recvfrom(256)
            except socket.timeout:
                elapsed = time.monotonic() - t0
                print(f"  ... nenhum pacote em 5 s (total recebidos: {pkts})")
                continue

            pkts += 1
            p = parse(data)
            if p is None:
                print(f"Pacote inválido ({len(data)} bytes) de {addr}")
                continue

            print_packet(p, addr)

            if once:
                break

    except KeyboardInterrupt:
        print(f"\nTotal: {pkts} pacotes em {time.monotonic()-t0:.1f} s")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
