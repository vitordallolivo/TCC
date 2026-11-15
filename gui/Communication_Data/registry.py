"""Camada 2 · Domínio — registry de placas dirigido por YAML.

Remove o hardcode de IP/porta/protocolo que estava espalhado em
`EthernetManager` (os `if board == "PLACA"`). Cada placa vira um `BoardConfig`
carregado de `Config/protocolo.yaml` (uma placa só, id "PLACA"). O `LinkManager` itera este registry, sem
conhecer nomes de placa específicos.

Fonte de verdade de endereçamento: bloco `link:` do YAML de cada placa. Se
ausente, cai em defaults retrocompatíveis (o esquema fixo documentado no
CLAUDE.md), para não quebrar YAMLs antigos.
"""
import os
from dataclasses import dataclass, field

import yaml

# Defaults retrocompatíveis (usados só quando o YAML não traz bloco `link:`).
# Espelham o esquema fixo de portas do CLAUDE.md / EthernetManager antigo.
_DEFAULT_LINKS = {
    "PLACA": {"transport": "udp_struct", "ip": "169.254.97.200",
             "ports": {"pc_rx": 5555, "board_rx": 5000}},
}


@dataclass
class BoardConfig:
    """Descrição completa de uma placa para as camadas de transporte/serviço."""
    id: str
    transport: str                       # "udp_struct"
    ip: str
    ports: dict = field(default_factory=dict)
    protocol_cfg: dict = field(default_factory=dict)  # enums/structs do YAML
    raw_yaml: dict = field(default_factory=dict)      # YAML bruto (motors, etc.)

    # Acessos nomeados às portas — cada transporte usa as que precisa.
    @property
    def pc_rx_port(self):
        return self.ports.get("pc_rx")

    @property
    def board_rx_port(self):
        return self.ports.get("board_rx")


def _protocol_cfg(raw, board_id):
    """Extrai o bloco de protocolo (enums/structs), lidando com raiz aninhada."""
    if not isinstance(raw, dict):
        return {}
    key = board_id.lower()
    if key in raw and isinstance(raw[key], dict):
        return raw[key]
    return raw


def board_config_from_yaml(board_id, raw):
    """Constrói um BoardConfig a partir do dict YAML já carregado."""
    board_id = board_id.upper()
    link = raw.get("link") if isinstance(raw, dict) else None
    defaults = _DEFAULT_LINKS.get(board_id, {})

    if not link:
        link = defaults

    transport = link.get("transport", defaults.get("transport", "udp_struct"))
    ip = link.get("ip") or raw.get("DEFAULT_IP_BOARD") or defaults.get("ip", "127.0.0.1")
    ports = dict(defaults.get("ports", {}))
    ports.update(link.get("ports", {}))

    return BoardConfig(
        id=board_id,
        transport=transport,
        ip=ip,
        ports=ports,
        protocol_cfg=_protocol_cfg(raw, board_id),
        raw_yaml=raw if isinstance(raw, dict) else {},
    )


class BoardRegistry:
    """Coleção de BoardConfig indexada por id (maiúsculo)."""

    def __init__(self):
        self._boards = {}

    def register(self, cfg):
        self._boards[cfg.id.upper()] = cfg
        return cfg

    def get(self, board_id):
        return self._boards.get(board_id.upper())

    def ids(self):
        return list(self._boards.keys())

    def all(self):
        return list(self._boards.values())

    def __contains__(self, board_id):
        return board_id.upper() in self._boards

    @classmethod
    def load(cls, config_dir, board_ids=("PLACA",)):
        """Carrega Config/protocolo.yaml para cada id (hoje só "PLACA")."""
        reg = cls()
        for bid in board_ids:
            path = os.path.join(config_dir, "protocolo.yaml")
            raw = {}
            if os.path.exists(path):
                with open(path, "r") as f:
                    raw = yaml.safe_load(f) or {}
            else:
                print(f"[REGISTRY] {path} não encontrado — usando defaults de {bid}.")
            reg.register(board_config_from_yaml(bid, raw))
        return reg
