import struct
import socket

# Configuração de Empacotamento C (Little-Endian, Packed)
# < : Little-Endian (Padrão RISC-V)
# B : uint8_t
# f : float
# b : int8_t
# Formato CMD: func(B), instr(B), target(B), value(f), action(b) = 8 bytes
CMD_STRUCT_FORMAT = '<BBBfb'

def pack_same_cmd(func, instr, target, value, action_status):
    """Transforma os dados em um buffer de 8 bytes compatível com C."""
    return struct.pack(CMD_STRUCT_FORMAT, func, instr, target, value, action_status)

def unpack_same_cmd(data):
    """Traduz o buffer binário de volta para leitura humana (MOCK Receptor)."""
    if len(data) != 8:
        return "Erro: Tamanho de pacote inválido"
    return struct.unpack(CMD_STRUCT_FORMAT, data)

class SAME_Board:
    def __init__(self, ip="127.0.0.1", port=5005):
        self.target_addr = (ip, port)

    def send_cmd(self, cmd_bytes):
        """Simula o envio e imprime a tradução do que foi enviado."""
        unpacked = unpack_same_cmd(cmd_bytes)
        print(f"\n[ETH -> SAME @ {self.target_addr[0]}]")
        print(f"  HEX: {cmd_bytes.hex(sep=' ').upper()}")
        print(f"  TRADUÇÃO: Func={unpacked[0]}, Instr={unpacked[1]}, "
              f"Target={unpacked[2]}, Value={unpacked[3]:.4f}, Action={unpacked[4]}")
        
        # Aqui você poderia usar socket.sendto() se tivesse um listener UDP ligado

class SIBA_Board:
    def __init__(self, ip="127.0.0.1", port=5006):
        self.target_addr = (ip, port)

    def send_cmd(self, cmd_bytes):
        """Placeholder para implementação da board SIBA."""
        print(f"[ETH -> SIBA] Enviando {len(cmd_bytes)} bytes...")

class EthernetManager:
    """Objeto central que será instanciado na sua Main GUI."""
    def __init__(self):
        self.SAME = SAME_Board()
        self.SIBA = SIBA_Board()