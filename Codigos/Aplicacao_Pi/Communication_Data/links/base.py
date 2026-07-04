"""Camada 1 · Transporte — contrato base de um link de placa.

`BoardLink` é a abstração que separa *transporte* de *gerência*. Cada placa
tem UM link (uma thread de I/O). Implementações concretas:
- `UdpStructLink` (SIBA/SAME): CMD 8B em TX, TELEMETRY_LOG em RX.
- `TelnetLink` (SPMM): comandos de texto por Telnet + log binário por UDP.

A camada de serviço (`LinkManager`) só conhece este contrato — nunca os
detalhes de socket/protocolo de cada placa.
"""
import abc
import threading
import time

# Janela (s) sem RX após a qual o link é considerado "sem sinal" — usado pelo
# LED do Board Manager e pela descoberta.
DEFAULT_ALIVE_WINDOW = 2.0


class BoardLink(threading.Thread, abc.ABC):
    """Base de todos os links. Uma thread de I/O por placa.

    Subclasses implementam `run()` (loop de I/O) e `send(raw)`. Contadores
    `rx_count`/`tx_count`/`last_rx_time` são observados por `test_io` e pelos
    LEDs, independente do protocolo.
    """

    def __init__(self, board_id, callback, alive_window=DEFAULT_ALIVE_WINDOW):
        super().__init__(name=f"Link_{board_id}", daemon=True)
        self.board_id = board_id
        self.callback = callback
        self.alive_window = alive_window
        self.running = True
        # Contadores de diagnóstico (ver LinkManager.test_io)
        self.rx_count = 0
        self.tx_count = 0
        self.last_rx_time = 0.0

    @abc.abstractmethod
    def send(self, raw_bytes):
        """Enfileira dados para transmissão (não bloqueante)."""

    def is_alive_link(self):
        """True se recebeu algum pacote dentro da janela `alive_window`.

        (Nome distinto de `Thread.is_alive` de propósito — este reflete o
        *link*, não a thread.)
        """
        return (time.time() - self.last_rx_time) <= self.alive_window and self.rx_count > 0

    def stop(self):
        """Sinaliza o encerramento do loop de I/O. Idempotente."""
        self.running = False
