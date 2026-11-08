#!/usr/bin/env python3
"""Testa a lógica do botão de emergência da SIBA pela rede.

Manda o mesmo comando de "pronto" da GUI (FUNC_START/STATUS_OK) e confere o
resultado na telemetria, pelos bits do error_code:

    bit 19  emergência ativa
    bit 25  bancada não energizada (relés abertos)

Os passos esperam o botão mudar de estado, sem tempo fixo. FAÇA SEM BATERIA:
o passo de ligar fecha os relés e arma o ESC de verdade.

    python test_emergencia.py            # placa física, botão manual
    python test_emergencia.py --native   # native_sim, botão simulado
    python test_emergencia.py --loop 600 # religa sempre que o botão é solto
"""
import argparse
import signal
import sys
import time

from native_cli import NativeTester

BIT_EMERGENCIA = 1 << 19
BIT_RELE_ABERTO = 1 << 25
PRECARGA_S = 5.0        # CONFIG_SIBA_RELAY_PRECHARGE_MS
ESPERA_BOTAO_S = 120.0


class Teste:
    def __init__(self, native):
        self.native = native
        self.t = NativeTester("SIBA", spawn=native)
        if not native:
            self.t.ip = self.t.cfg.ip
        self.resultados = []

    # ------------------------------------------------------------- telemetria
    def erro(self):
        return int(self.t.last_tele["error_code"])

    def emergencia(self):
        return bool(self.erro() & BIT_EMERGENCIA)

    def rele_aberto(self):
        return bool(self.erro() & BIT_RELE_ABERTO)

    def espera(self, cond, timeout):
        """Devolve o tempo até `cond` valer em telemetria nova, ou None."""
        t0 = time.monotonic()
        visto = self.t.tele_count
        while time.monotonic() - t0 < timeout:
            if self.t.tele_count != visto:
                visto = self.t.tele_count
                if cond():
                    return time.monotonic() - t0
            time.sleep(0.01)
        return None

    def sempre(self, cond, duracao):
        """True se `cond` valeu em todos os pacotes recebidos em `duracao`."""
        return self.espera(lambda: not cond(), duracao) is None

    def novos_pacotes(self, n=3, timeout=8.0):
        alvo = self.t.tele_count + n
        t0 = time.monotonic()
        while self.t.tele_count < alvo and time.monotonic() - t0 < timeout:
            time.sleep(0.01)
        return self.t.tele_count >= alvo

    # ------------------------------------------------------------------ ações
    def pronto(self):
        self.t.send("FUNC_START", status="STATUS_OK", value=1.0,
                    label="pronto (armar bancada)")

    def desligar(self):
        self.t.send("FUNC_START", status="STATUS_ERROR", value=0.0,
                    label="desligar bancada")

    def botao(self, apertar):
        """Pede a mudança do botão e espera a telemetria refletir."""
        if self.native:
            self.t.shell("btn on" if apertar else "btn off")
        else:
            print(f"\n>>> {'APERTE e mantenha' if apertar else 'SOLTE'} "
                  "o botão de emergência", flush=True)
        dt = self.espera(lambda: self.emergencia() == apertar, ESPERA_BOTAO_S)
        return dt is not None

    def confere(self, nome, ok, detalhe=""):
        self.resultados.append((nome, ok))
        extra = f" ({detalhe})" if detalhe else ""
        print(f"[{'PASSOU' if ok else 'FALHOU'}] {nome}{extra}", flush=True)

    # ------------------------------------------------------------------ passos
    def preparar(self):
        print(f"Escutando telemetria na porta {self.t.pc_rx}; "
              f"comandos para {self.t.ip}:{self.t.board_rx}", flush=True)
        if not self.novos_pacotes(timeout=6.0):
            print("Nenhuma telemetria chegou. Confira a rede e se o firewall "
                  "libera a UDP 5555:\n  sudo ufw allow in on <adaptador> "
                  "proto udp to any port 5555")
            return False
        print(f"Telemetria ok. error_code = 0x{self.erro():08X}", flush=True)
        if not self.rele_aberto():
            print("Relés fechados no início; desligando a bancada.")
            self.desligar()
            self.espera(self.rele_aberto, 3.0)
        if self.emergencia() and not self.botao(False):
            return False
        return self.rele_aberto() and not self.emergencia()

    def passo_recusa(self):
        print("\n--- 1. Botão apertado: tem que recusar ---", flush=True)
        if not self.botao(True):
            return self.confere("emergência detectada", False)
        self.confere("emergência detectada", True)
        self.pronto()
        self.confere("recusou ligar com o botão apertado",
                     self.sempre(self.rele_aberto, PRECARGA_S + 2.0))

    def passo_ligar(self):
        print("\n--- 2. Botão solto: liga em sequência ---", flush=True)
        if not self.botao(False):
            return self.confere("emergência liberada", False)
        self.confere("emergência liberada", True)
        print("Ouça os relés: pré-carga agora e potência depois.", flush=True)
        self.pronto()
        dt = self.espera(lambda: not self.rele_aberto(), PRECARGA_S + 7.0)
        if dt is None:
            return self.confere("energizou", False, "não fechou a tempo")
        if self.native:      # o native_sim não simula a espera da pré-carga
            self.confere("energizou", True, f"{dt:.1f} s")
        else:
            self.confere("energizou depois da pré-carga",
                         dt >= PRECARGA_S - 0.5, f"{dt:.1f} s")

    def passo_parada(self):
        print("\n--- 3. Botão apertado com a bancada ligada ---", flush=True)
        if not self.botao(True):
            return self.confere("emergência detectada", False)
        dt = self.espera(self.rele_aberto, 3.0)
        aberto = dt is not None or self.rele_aberto()
        self.confere("abriu os relés", aberto)

    def passo_soltar(self):
        print("\n--- 4. Soltar não pode religar ---", flush=True)
        if not self.botao(False):
            return self.confere("emergência liberada", False)
        self.confere("emergência liberada", True)
        self.confere("relés continuam abertos",
                     self.sempre(self.rele_aberto, 4.0))

    def passo_durante_precarga(self):
        print("\n--- 5. Botão durante a pré-carga ---", flush=True)
        print("Prepare a mão. Aperte assim que aparecer AGORA, antes de o "
              "relé de potência fechar.", flush=True)
        time.sleep(4.0)
        self.pronto()
        print(">>> AGORA: APERTE e mantenha o botão", flush=True)
        dt = self.espera(self.emergencia, PRECARGA_S + 15.0)
        if dt is None:
            return self.confere("emergência detectada", False)
        self.espera(self.rele_aberto, 3.0)
        self.confere("terminou com relés abertos", self.rele_aberto(),
                     "confirme de ouvido: a potência não pode ter fechado")
        self.botao(False)

    def loop(self, duracao):
        """Religa a bancada sempre que o botão é solto, até `duracao` segundos.

        Sem telemetria (firewall), repete o "pronto": o firmware já recusa com
        o botão apertado e não refaz a sequência com a bancada ligada.
        """
        def termina(*_):
            raise SystemExit(0)
        signal.signal(signal.SIGTERM, termina)

        t0 = time.monotonic()
        agora = lambda: time.monotonic() - t0
        com_tele = self.novos_pacotes(timeout=4.0)
        print(f"Loop por {duracao:.0f} s; telemetria: "
              f"{'sim' if com_tele else 'NÃO (modo cego)'}", flush=True)
        ultimo_envio = -1e9
        anterior = None
        enviado_em = None
        try:
            while agora() < duracao:
                if not com_tele:
                    if agora() - ultimo_envio >= 2.0:
                        self.pronto()
                        ultimo_envio = agora()
                    time.sleep(0.05)
                    continue

                estado = (self.emergencia(), self.rele_aberto())
                if estado != anterior:
                    emg, aberto = estado
                    extra = ""
                    if enviado_em is not None and not aberto:
                        extra = f"  ({agora() - enviado_em:.1f} s após o pronto)"
                        enviado_em = None
                    print(f"t={agora():7.2f}s  botão="
                          f"{'APERTADO' if emg else 'solto'}  relés="
                          f"{'abertos' if aberto else 'FECHADOS'}{extra}",
                          flush=True)
                    if emg:                 # libera religar logo ao soltar
                        ultimo_envio = -1e9
                        enviado_em = None
                    anterior = estado

                if (not estado[0] and estado[1]
                        and agora() - ultimo_envio >= PRECARGA_S + 3.0):
                    self.pronto()
                    ultimo_envio = enviado_em = agora()
                time.sleep(0.02)
        finally:
            self.desligar()
            time.sleep(0.3)
            self.t.close()
        return 0

    def rodar(self):
        try:
            if not self.preparar():
                print("Estado inicial inválido; teste não executado.")
                return 2
            self.passo_recusa()
            self.passo_ligar()
            self.passo_parada()
            self.passo_soltar()
            if not self.native:
                self.passo_durante_precarga()
        finally:
            self.desligar()
            time.sleep(0.3)
            self.t.close()

        falhas = [n for n, ok in self.resultados if not ok]
        print(f"\nRESUMO: {len(self.resultados) - len(falhas)}/"
              f"{len(self.resultados)} verificações passaram", flush=True)
        for nome in falhas:
            print(f"  falhou: {nome}")
        return 1 if falhas else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--native", action="store_true",
                    help="usa o firmware em native_sim com botão simulado")
    ap.add_argument("--loop", type=float, metavar="SEGUNDOS",
                    help="religa sempre que o botão é solto, por SEGUNDOS")
    args = ap.parse_args()
    if args.loop:
        sys.exit(Teste(args.native).loop(args.loop))
    if not args.native:
        print("ATENÇÃO: execute sem bateria. Enter para continuar, Ctrl+C "
              "para sair.")
        input()
    sys.exit(Teste(args.native).rodar())


if __name__ == "__main__":
    main()
