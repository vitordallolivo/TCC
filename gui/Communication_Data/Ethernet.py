"""Shim de compatibilidade.

O antigo monólito `Ethernet.py` foi fatiado em camadas (ver CLAUDE.md e o
diretório Communication_Data/): transporte em `links/`, domínio em
`codecs.py`/`registry.py`, serviço em `manager.py`. Este módulo mantém os
nomes históricos (`EthernetManager`, `BoardWorker`, `BoardSimulator`,
`TYPE_MAP`) para que `GUI/mainAlbatrozGUI.py` e `GUI/TinyTerminal.py`
continuem funcionando sem alteração enquanto migram para os nomes novos.
"""
import os

from Communication_Data import codecs
from Communication_Data.manager import LinkManager
from Communication_Data.links.udp_struct_link import UdpStructLink as BoardWorker
from Communication_Data.simulators.board_simulator import BoardSimulator

TYPE_MAP = codecs.TYPE_MAP

__all__ = ["EthernetManager", "LinkManager", "BoardWorker", "BoardSimulator", "TYPE_MAP"]


class EthernetManager(LinkManager):
    """Adapta a assinatura antiga (paths dos YAMLs) para o LinkManager
    dirigido por registry. Deriva o config_dir do caminho do YAML da SIBA."""

    def __init__(self, siba_yaml=None, same_yaml=None, handle_callback=None,
                 config_dir=None, **kwargs):
        if config_dir is None:
            base = siba_yaml or same_yaml
            config_dir = os.path.dirname(base) if base else "."
        super().__init__(config_dir=config_dir, handle_callback=handle_callback, **kwargs)
