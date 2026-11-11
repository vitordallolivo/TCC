"""Camada 3 · Serviço — descoberta/identificação de placas (ping/pong).

Responde "quais placas estão presentes?" por transporte, reaproveitando a
infra do LinkManager (test_io, check_connectivity).
Usado pelo Board Manager (LED verde/vermelho) e pelo "Modo de Operação".
"""
from dataclasses import dataclass


@dataclass
class BoardStatus:
    id: str
    transport: str
    present: bool
    detail: str = ""


class DiscoveryService:
    def __init__(self, manager):
        self.manager = manager

    def probe(self, board_id, timeout=2.0):
        """Sonda uma placa e retorna seu BoardStatus."""
        board_id = board_id.upper()
        cfg = self.manager.registry.get(board_id)
        if cfg is None:
            return BoardStatus(board_id, "?", False, "não registrada")

        # Se o link já está ativo e recebeu algo recentemente, está presente.
        worker = self.manager.workers.get(board_id)
        if worker is not None and worker.is_alive_link():
            return BoardStatus(board_id, cfg.transport, True, "RX ativo")

        if cfg.transport == "udp_struct":
            return self._probe_udp(cfg, timeout)
        return BoardStatus(board_id, cfg.transport, False, "transporte desconhecido")

    def probe_all(self, timeout=2.0):
        """Sonda todas as placas do registry. Retorna {id: BoardStatus}."""
        return {bid: self.probe(bid, timeout) for bid in self.manager.registry.ids()}

    # ------------------------------------------------------------------ #
    def _probe_udp(self, cfg, timeout):
        # test_io cobre mock/native_sim/hardware (CMD neutro -> espera telemetria).
        res = self.manager.test_io(cfg.id, timeout=timeout)
        if res["rx_ok"]:
            return BoardStatus(cfg.id, cfg.transport, True, f"{res['rx_packets']} pkt(s)")
        # Fallback: ping ICMP (hardware presente mas sem telemetria).
        if self.manager.check_connectivity(cfg.ip):
            return BoardStatus(cfg.id, cfg.transport, True, "ping OK (sem telemetria)")
        return BoardStatus(cfg.id, cfg.transport, False, "sem resposta")
