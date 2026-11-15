"""InfoDialog — "Sobre / Ajuda": explica o software do ponto de vista do usuário.

Texto em português, sem jargão de código: o que a placa mede,
como conectar, como calibrar, o que os menus fazem e como ler os erros.
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog, QVBoxLayout, QTextBrowser, QPushButton, QHBoxLayout


_HTML = """
<h2>Aplicação Albatroz — Guia do Usuário</h2>
<p>Este programa é o <b>painel de controle</b> da placa de medição. Ele conecta ao hardware pela rede, mostra a telemetria em gráficos,
envia comandos, calibra sensores e roda as estimativas do motor.</p>

<h3>A placa de medição</h3>
<p>Lê a <b>bateria</b> (tensão e corrente), as <b>tensões e correntes das três
fases</b> do motor e, com elas, <b>estima rotação e torque</b> (observador SMO).
Também <b>aciona o ESC</b> (acelerador do motor) e os relés de potência, e tem
entradas para <b>células de carga</b>.</p>

<h3>Linha de comando</h3>
<p>Os comandos do <b>TinyTerminal</b> também funcionam fora da aplicação, em
qualquer terminal (Linux ou Windows): <code>python placa.py set i_a linear
0.0809</code>, <code>python placa.py tele</code>, ou só
<code>python placa.py</code> para abrir o prompt.</p>

<h3>Como conectar (3 formas)</h3>
<ol>
  <li><b>Hardware real</b> — a placa física ligada no cabo de rede.</li>
  <li><b>Simulador (mock)</b> — um "faz de conta" em Python só para testar a
      interface e os gráficos, sem placa.</li>
  <li><b>Simulação nativa (native_sim)</b> — roda o <i>firmware de verdade</i>
      como um programa no PC. É a forma confiável de testar sem bancada, porque
      é o mesmo código que roda na placa.</li>
</ol>

<h3>Calibração dos sensores</h3>
<ul>
  <li><b>Células de carga (HX711)</b> — modelo padrão do sensor:
      <i>leitura = (bruto − tara) / escala</i>.
      <b>Tarar</b> zera a leitura sem carga; <b>calibrar o ganho</b> é colocar um
      <b>peso conhecido</b> e informar o valor — a placa ajusta a escala para a
      leitura passar a valer aquele peso.</li>
  <li><b>Sensores da bateria e das fases (ADC)</b> — calibração linear
      <i>valor = ganho·leitura + offset</i>.</li>
</ul>
<p>Use <b>Opções → Configuração de Sensores</b> para ajustar ganho, offset e os
<b>limites mínimo/máximo</b> de cada sensor de uma vez.</p>

<h3>Limites e segurança</h3>
<p>Cada sensor tem uma <b>faixa válida (mín/máx)</b>. Se um valor sai da faixa,
a placa acende um <b>erro</b> e, nos casos críticos (tensão/corrente/RPM),
dispara <b>parada de emergência</b> (desarma o motor).</p>

<h3>Painel de Erros</h3>
<p>Em <b>Opções → Painel de Erros e Alarmes</b> (ou clicando no indicador de
erros na barra inferior) você vê, com <b>lâmpadas</b>, todas as condições de
erro e quais estão ativas no momento — em linguagem clara.</p>

<h3>Menus</h3>
<ul>
  <li><b>Motor</b> — carregar/criar perfil do motor.</li>
  <li><b>Estimativas</b> — rodar os scripts (ganhos SMO e coeficiente da
      hélice) e aplicar os resultados.</li>
  <li><b>Bateria</b> — parâmetros da bateria.</li>
  <li><b>Células de Carga</b> — assistente de tara/calibração.</li>
  <li><b>Opções</b> — Configuração de Sensores, Gerenciador de Placas
      (procurar/editar IP), Painel de Erros e este guia.</li>
</ul>
"""


class InfoDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Sobre / Ajuda — Aplicação Albatroz")
        self.setWindowFlags(Qt.Window)
        self.resize(600, 620)

        root = QVBoxLayout(self)
        browser = QTextBrowser()
        browser.setOpenExternalLinks(True)
        browser.setHtml(_HTML)
        root.addWidget(browser, 1)

        bottom = QHBoxLayout()
        bottom.addStretch()
        btn = QPushButton("Fechar")
        btn.clicked.connect(self.close)
        bottom.addWidget(btn)
        root.addLayout(bottom)
