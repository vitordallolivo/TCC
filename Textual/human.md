# Correção humana — TCC Bancada BLDC

Arquivo de acompanhamento do plano de escrita (16 Ações, Cap. 3 e 4). Atualizado
**depois** de cada ação ser escrita, nunca antes (ver `CLAUDE.md`, regra 4).

Legenda de status: ✅ escrito · ⏳ não iniciado

---

## ✅ Ajustes pontuais pedidos ao vivo (revisão desta rodada)

Cinco pedidos pequenos e independentes, todos aplicados e compilados juntos
no final. Ordem cronológica do pedido, não de importância.

### 1. Algoritmo 7 movido de Cap. 4 para Cap. 3 (inversão deliberada)

O algoritmo de identificação de patamar/coeficiente $k_Q$
(`alg:kq_identificacao`) vivia definido por inteiro dentro de
`Conclusao.tex` (Resultados), e a Metodologia (`02_MotorBLDC.tex`, seção
"Identificação dos Coeficientes Aerodinâmicos da Hélice em Bancada",
Ação 3) só tinha um `\autoref` apontando pra lá — ordem errada, já que o
algoritmo é método ("como se faz"), não resultado. **Invertido**: o bloco
`\begin{algorithm}...\end{algorithm}` completo agora está em
`02_MotorBLDC.tex`, logo após a `fig:fluxo_ensaio_helice`; em
`Conclusao.tex` restou só a frase de citação (\autoref ao algoritmo, sem
redefini-lo). Numeração do algoritmo mudou automaticamente (LaTeX recalcula
sozinho) já que agora `alg:kq_identificacao` aparece antes de
`alg:autotuning` no fluxo do documento — não precisa de ajuste manual.

Também diminuída a seta de retorno ("próximo patamar") da
`fig:fluxo_ensaio_helice` (`looseness=0.5` em vez do arco cheio) — a pedido
do autor, que achou o arco grande demais.

### 2. Frequência do ESC: 16 kHz → 12 kHz

O autor confirmou que a frequência real de chaveamento do ESC é
\SI{12}{kHz}, não \SI{16}{kHz} como o texto assumia em vários pontos de
`08_SAME.tex`. **Todas as 4 ocorrências** de 16~kHz como frequência do ESC
foram trocadas para 12~kHz, conferidas por `grep` até zerar:
- Justificativa da taxa de amostragem do ADC (parágrafo de abertura da
  seção "Medição de Tensão e Corrente de Fase").
- Período de PWM usado no cálculo de folga de $t_s$: era \SI{62,5}{\micro\second}
  a 16~kHz, **recalculado para \SI{83,33}{\micro\second} a 12~kHz** (esse
  segundo era um número derivado que passou despercebido na primeira
  varredura e só foi pego numa segunda checagem — confirmado que não sobrou
  nenhum "62,5" órfão no arquivo).
- Frequência de corte do filtro \textit{antialiasing} (ruído de
  chaveamento e harmônicos).
- A frase que comparava a simulação CA do sensor de corrente (12~kHz) com
  o chaveamento do ESC (antes 16~kHz): como os dois valores agora
  coincidem, a frase foi reescrita para só declarar que a simulação usa a
  mesma frequência do ESC, sem precisar mais distinguir os dois números.

**Revisão do orientador:** nenhuma pendência restante — as 4 ocorrências
foram confirmadas por `grep` (zero resultado para `16.*kHz` em todo
`Textuais/` após o ajuste).

### 3. Conteúdo de resultado movido da Metodologia (3.8.1) pros Resultados

O autor apontou que um bloco inteiro em `08_SAME.tex` (seção do sensor de
corrente) era resultado, não método, e "tava no lugar errado". Movido para
`Conclusao.tex` (`sec:resultado_sensor_corrente`):
- As duas figuras de densidade de fluxo (núcleo isolado e três núcleos),
  antes em `Textuais/Imgs/Metodologia/`, agora fisicamente movidas
  (`mv`, não copiadas) para `Textuais/Imgs/Resultados/`, com labels novos
  (`fig:resultado_single_gap`, `fig:resultado_trifase_geral` — os antigos
  `fig:sensor_single_gap`/`fig:sensor_trifase_geral` não existem mais em
  lugar nenhum).
- Parágrafo do critério de \textit{crosstalk} desprezível (1\%, ancorado no
  erro do ADC) — movido pra logo antes do resultado de crosstalk que já
  existia em Resultados (Ação 15), removendo a referência cruzada que
  antes apontava de volta pra Metodologia.
- Parágrafo da banda passante do sensor (SS49E, $f_{-3dB}\approx117$~kHz).
- Parágrafo da margem de saturação do núcleo (lei de Ampère,
  $B_{gap}\approx54,6$~mT).
- Parágrafo sobre ausência de rejeição de campo de modo comum (núcleo
  único vs. sensoriamento diferencial).

O que **ficou** na Metodologia (3.8.1): justificativa de custo, topologia,
tabela do SS49E, escolha do método de elementos finitos, descrição das
duas condições de simulação (CC isolado / CA três núcleos) — isso
continua sendo "como se faz", não resultado.

### 4. Marcadores Q1/Q2/Q3 e ajustes finos nas 5 figuras de planejamento

- Adicionados losangos pretos Q1/Q2/Q3 com seta na borda esquerda de cada
  uma das 5 figuras de planejamento (CAD, SIBA, Interface, SAME + o
  portão), na altura exata da transição de cor das faixas de fundo — não
  existia nenhum marcador antes desse pedido.
- SIBA: as 5 considerações soltas do topo (Safety, Alimentação, ETH,
  Escolha de micro, Esquemáticos) — que geravam setas cruzando —
  **fundidas num bloco único**. O par "Arquivos BOM"/"Pedir" **substituído
  por "Esquemático" e "Protótipo" em blocos próprios**, com seta tracejada
  de retrabalho ("se reprovado, refatora") saindo de "Validação PCB" —
  mesmo padrão replicado na SAME (removido também o nó "Pedir" de lá,
  Q3 da SAME ficou só com "Validações").
- Interface: bloco Q3 novo ("Interface Funcional") reunindo funcionalidades
  reais já descritas no texto (configuração de sensores, calibração de
  células de carga, \textit{scripts} de estimação SMO/hélice, registro em
  CSV) — também fundido num bloco único pelo mesmo motivo de cruzamento de
  setas; corrigido um bug visual em que uma seta cruzava por cima do texto
  de "TinyTerminal" (linha reta entre dois nós que ficavam nos dois lados
  do TinyTerminal).
- SAME: BLDC/Parâmetros/Simulações/Observador/Teste movidos mais pra
  direita pra parar de sobrepor "Observador" e "Simulações".
- Figura do fluxo YAML (`fig:yaml_example_config`): caixa central "Config"
  aumentada de 6,6~cm pra 10,4~cm de altura — as setas de cima
  (\textit{TinyTerminal}) e de baixo (Diálogos) apontavam pra fora da
  caixa antes desse ajuste.

### 5. Listas de siglas e símbolos (`PreTextuais/Listas.tex`) auditadas

Processo: `grep` de cada sigla/símbolo contra todo `Textuais/*.tex` e
`Textuais/**/*.tex`, excluindo a própria `Listas.tex`, pra achar o que
nunca aparece no corpo do texto e o que aparece mas não tá listado. Nenhum
item removido ou adicionado sem confirmar com `grep` primeiro.

**Siglas removidas (zero ocorrência no corpo do texto):** BMS, CCT, CPU,
DAC, DLL, ECM, ISO, LAN, LLC, PCI, SEI, TCKEC, TLS. `ECM` em particular é
sintoma de uma seção sobre modelo de circuito equivalente de bateria que
nunca foi escrita — os símbolos `$R_0$`/`$R_1$` (resistência ôhmica/de
polarização da bateria) tinham o mesmo problema, ver abaixo.

**Siglas adicionadas (usadas no texto, faltavam na lista):** BOM, CA, CC,
FEMM, GND, LSB, PC, RMS, SPMM.

**Símbolos removidos:** `$R_0$` (zero ocorrência em todo o documento) e
`$R_1$` (a definição na lista era "resistência de polarização da bateria",
mas o único uso real de `$R_1$` no texto é como resistor genérico da ponte
de Wheatstone em `Revisao.tex` — contexto completamente diferente do
descrito; removido em vez de redefinido porque é uma variável local,
autoexplicada no próprio parágrafo onde aparece).

**Não adicionados de propósito (nomes próprios, não siglas técnicas):**
APC (fabricante de hélice), UIUC (universidade), WCH (fabricante do
microcontrolador) — apareceram na varredura de padrão `(XXXX)` mas são
marcas/instituições, não abreviações de termo técnico; convenção ABNT não
lista isso na lista de siglas.

**Revisão do orientador:** nenhuma pendência de conteúdo — é só
higienização de lista, não afeta o corpo do texto.

---

## ✅ Reconstrução das figuras "quadro geral" (metodologia principal + YAML)

A pedido do autor, duas figuras grandes e ilegíveis (importadas como imagem
externa, `.png`/`.drawio.png`) foram **reconstruídas em TikZ nativo**,
substituindo os arquivos de imagem por diagramas versionáveis no próprio
`.tex`. Processo iterativo, com várias rodadas de ajuste ao vivo — só o
estado final é descrito aqui; não repetir o histórico de tentativas em
sessões futuras.

**Figura da metodologia principal** (`Textuais/Metodologia/00_Introducao.tex`),
antes uma única imagem `TCC Planejamento.png` (rotacionada 90°, ilegível em
qualquer zoom razoável). Virou **5 figuras TikZ**:
- `fig:portao_metodologia` — portão simples Q1→Q2→Q3→FIM com a legenda de
  cada marco.
- `fig:fluxo_cad` — fluxo da frente CAD (layout vertical, faixas de fase
  horizontais).
- `fig:fluxo_siba` — fluxo da frente SIBA, duas colunas lado a lado
  (\textit{Hardware}/PCB e \textit{Firmware}/Código) convergindo em "SIBA
  Completa". As considerações iniciais soltas (Safety, Alimentação, ETH,
  Escolha de micro, Esquemáticos) foram **fundidas em um único bloco** — a
  versão com 5 caixinhas separadas gerava setas se cruzando. O par
  "Arquivos BOM"/"Pedir" foi **substituído por "Esquemático" e "Protótipo"
  em blocos próprios** (a pedido do autor: a etapa real não é gerar BOM, é
  fazer esquemático e protótipo), com uma seta tracejada de
  retrabalho ("se reprovado, refatora") voltando de "Validação PCB".
- `fig:fluxo_interface` — fluxo da frente Interface (PC), com um novo bloco
  Q3 ("Interface Funcional") reunindo funcionalidades reais já descritas no
  texto (configuração de sensores, calibração de células de carga,
  \textit{scripts} de estimação SMO/hélice, registro em CSV) — também
  fundidas num bloco único pelo mesmo motivo (setas cruzando quando eram
  4 caixas separadas).
- `fig:fluxo_same` — fluxo da frente SAME, 3 trilhas de entrada (Código,
  PCB da SAME, BLDC→Parâmetros→Simulações↔Observador→Teste simples)
  convergindo numa trilha única. Mesmo ajuste do par BOM/Pedir → Esquemático
  + Protótipo (blocos próprios) + seta de retrabalho, espelhando a SIBA.

Todas as 5 têm marcadores de fase (losango preto Q1/Q2/Q3 com seta) na
borda esquerda, na altura da transição de cor das faixas de fundo —
recurso pedido explicitamente pelo autor e ausente na primeira versão.

**Figura do fluxo YAML** (`fig:yaml_example_config`, `03_Interface.tex`),
antes a imagem `YAML_uso.drawio.png` (desatualizada — nem citava
`plots.yaml` nem o fato de os diálogos gravarem de volta no `SAME.yaml`).
Duas rodadas de redesenho:
1. Layout fonte→consumidor→resultado com os 5 YAMLs como caixas
   individuais — abandonado por gerar cruzamento de setas confuso.
2. **Layout final:** chamador (`TinyTerminal`, `Janela principal`,
   `generate.py`, `Diálogos de calibração/estimação`) → caixa única
   "Config" contendo os 5 arquivos YAML listados dentro → resultado. Cada
   seta chamador→Config tem um rótulo dizendo quais arquivos aquele
   chamador especificamente lê/escreve. A caixa "Config" precisou de altura
   mínima 10,4 cm para cobrir a extensão vertical dos 4 chamadores (bug
   inicial: caixa menor que a faixa dos chamadores fazia as setas de cima e
   de baixo apontarem para fora da caixa).

**Compilação final desta rodada:** `pdflatex` (2×) + `bibtex` + `pdflatex`
(2×), **115 páginas**, sem erros, sem referência ou citação não resolvida.
Todas as figuras renderizadas e conferidas visualmente (`pdftoppm`) antes
de reportar como concluído.

**Revisão do orientador:** nenhuma pendência de conteúdo — só as fotos
reais (não afetadas por esta reconstrução, que era só das duas figuras
esquemáticas). Vale conferir se o layout de duas colunas (SIBA) e três
trilhas (SAME) fica legível impresso em P&B, já que as faixas de fase
dependem de cor para diferenciação (verde/laranja/vermelho ficam em tons de
cinza parecidos se a impressão não for colorida).

---

## ✅ Revisão crítica do documento + reforço do Cap. 4 e escrita do Cap. 5

Depois de todas as 16 Ações escritas, o autor pediu um julgamento de valor
honesto do PDF inteiro (nota 0–5 por parte, comparando com o que se espera
de um TCC). Resumo do parecer dado (não repetir a leitura completa em
sessões futuras, só re-executar se o conteúdo mudar muito):

- **Pré-textuais/Introdução/Revisão/Metodologia:** boas a muito boas (4–4,5).
  Um ponto técnico a conferir: a fórmula da ponte de Wheatstone completa em
  `Revisao.tex` (`eq:wheatstone_simples`) usa $V_{ex}K\epsilon/4$, coeficiente
  típico de quarto-de-ponte, para uma configuração descrita no texto como
  "ponte completa" (que costuma ter ganho $V_{ex}K\epsilon$, sem o /4). Não
  corrigido — é uma decisão de conteúdo técnico do autor, não de redação.
- **Resultados (Cap. 4):** nota 2/5 no estado anterior a esta sessão —
  a modelagem do SMO (autotuning, PLL, identificação de $k_Q$) é forte mas
  é toda simulação; a validação física real só existia para R e L do motor.
  Todo o resto (coeficientes mecânicos, $C_T$/$C_Q$ da hélice em bancada,
  validação da SIBA, calibração de canal da SAME, sensor de corrente medido
  vs. simulado) não tinha nem estrutura no texto.
- **Conclusão (Cap. 5):** nota 0,5/5 — só existiam 2 bullets de trabalhos
  futuros, sem nenhuma conclusão de fato.

**Ação tomada:** reforçar o Cap. 4 com tabelas/figuras placeholder para
todo item de resultado que faltava estrutura (não só imagem), e escrever o
Cap. 5 por completo. Detalhes abaixo.

### Reforço do Capítulo 4

- **Coeficientes Mecânicos** (`Conclusao.tex`, dentro de "Medição de
  Parâmetros"): além da nota `Pendente` e da figura `fig:coastdown` (já
  existentes), acrescentada `tab:resultado_atrito` (tabela de patamares
  $\omega_{m,k}$/$T_{e,k}$, placeholder) e `eq:resultado_b_tauc` (equação
  com valores de $B$/$\tau_c$ marcados `[pendente]`).
- **Nova subseção "Identificação dos Coeficientes $C_T$ e $C_Q$ em
  Bancada"** (`sec:resultado_coef_aero_bancada`), dentro de "Modelagem do
  SMO", logo após "Identificação dos Coeficientes Aerodinâmicos Globais"
  (a versão em simulação). **Esta subseção não existia de forma nenhuma
  antes** — o procedimento da Ação 3 (`sec:coef_aero_bancada`, Cap. 3) não
  tinha nenhuma contrapartida de resultado no Cap. 4. Contém
  `tab:resultado_ct_cq` (placeholder) e `fig:resultado_ct_cq_comparacao`
  (placeholder, comparação com APC/UIUC).
- **SIBA** (`sec:resultado_validacao_siba`): acrescentada
  `tab:resultado_validacao_siba` (placeholder, ganho/offset/erro por
  sensor: bateria, células de carga, Pitot).
- **SAME > Calibração de Canal** (`sec:resultado_calibracao_canal_same`):
  acrescentada `tab:resultado_calibracao_canal_same` (placeholder,
  ganho/offset/erro/ruído por canal de tensão e corrente).

### Capítulo 5 — Conclusão e Trabalhos Futuros (escrito do zero)

Estrutura nova, substituindo os 2 bullets antigos:
- **`\section{Conclusão}`**: retoma os 5 objetivos específicos da
  Introdução um a um, com veredito honesto por objetivo (2 "atingidos" —
  rede de comunicação e software de supervisão; 1 "majoritariamente
  atingido" — modelagem/observador, falta só a parte mecânica; 2
  "parcialmente atingidos" — hardware elétrico e aerodinâmico, projetados
  mas sem validação física fechada), com remissão a seção/tabela/figura
  específica para cada afirmação. Fecha com parágrafo de síntese.
- **`\section{Limitações e Itens Pendentes}`**: lista objetiva dos 6 itens
  que faltam por dependerem de ensaio físico (não por corte de escopo),
  cada um com remissão à seção correspondente, e nota que o `human.md`
  tem o levantamento completo.
- **`\section{Sugestões para Trabalhos Futuros}`**: mantidos os 2 itens
  originais do autor (bancada 6 DOF, curva SOC×OCV da bateria) e
  acrescentado 1 item nascido da própria Revisão Bibliográfica (extensão
  do observador para a região de enfraquecimento de campo,
  `sec:motor_curvas`) — não inventado, é conteúdo já presente no Cap. 2
  que ficou fora do escopo ensaiado.

**Revisão do orientador:** a distinção entre "Limitações e Itens
Pendentes" (o que falta para fechar o que já foi prometido) e "Trabalhos
Futuros" (extensões novas de escopo) foi uma escolha editorial desta
sessão — confirmar se faz sentido manter separado ou se o orientador
prefere fundir em uma seção só.

**Compilação final:** `pdflatex` (2×) + `bibtex` + `pdflatex` (2×), 112
páginas, sem erros, sem citação ou referência não resolvida.

### Marcação `(placeholder)` nos títulos (a pedido do autor)

Acrescentado `(placeholder)` ao título de toda seção/subseção cujo
conteúdo é majoritariamente pendente (não só a figura, a seção inteira),
para ficar visível direto no sumário. Só o texto do título mudou — os
`\label` não foram tocados, então nenhuma referência cruzada quebrou.
Marcadas: `\section{SIBA (placeholder)}`
(`sec:resultado_validacao_siba`), `\subsection{Calibração de Canal e
Formas de Onda Reais (placeholder)}`
(`sec:resultado_calibracao_canal_same`), `\subsection{Identificação dos
Coeficientes $C_T$ e $C_Q$ em Bancada (placeholder)}`
(`sec:resultado_coef_aero_bancada`), `\subsection{Montagem Final do
Conjunto (placeholder)}` (`sec:resultado_montagem_final`) — todas em
`Conclusao.tex`; `\subsection{Coeficientes Mecânicos (placeholder)}` em
`Conclusao.tex` (título pré-existente, não criado nesta sessão, mas 100\%
pendente agora); e `\subsection{Calibração do Tubo de Pitot
(placeholder)}` em `07_SIBA.tex`. **Não marcadas** por terem conteúdo real
predominante (só uma figura ou um fato pontual pendente dentro delas):
"Bancada Provisória de Caracterização", "Identificação dos Coeficientes
Aerodinâmicos da Hélice em Bancada", "Sensor de Corrente" (SAME).

### Fotos finais de montagem/operação (a pedido do autor)

Nova `\subsection{Montagem Final do Conjunto}`
(`sec:resultado_montagem_final`) em `Conclusao.tex`, dentro de "Modelagem
Física", logo após `fig:bancada_fisica`. Três figuras-placeholder novas:
`fig:resultado_motor_helice_montado` (motor com hélice montado na
bancada), `fig:resultado_conexoes_internas` (detalhe da fiação/conectores
internos da placa já integrada) e `fig:resultado_bancada_operacao` (foto
geral da bancada completa em operação durante um ensaio). Todas seguem o
padrão de caixa tracejada (regra 8). Compilação final: **113 páginas**.

---

## ✅ Ação 10 — 3.8.1: sensor de corrente (metodologia)

**Escrito em:** `Textuais/Metodologia/08_SAME.tex`, nova
`\subsubsection{Sensor de Corrente com Núcleo Toroidal}`
(`\label{sec:same_sensor_corrente}`), inserida ao final da subseção 3.8.1,
antes de "Arquitetura de Firmware do Observador".

Conteúdo transplantado de `Simulacoes/Sensor Corrente/Toroide/SensorCorrente.tex`
até a descrição das duas condições de simulação (CC isolado / CA três núcleos
em PCB) — parado antes da varredura de corrente e da equação de transferência
com valores numéricos (`G_núcleo = 5,355 G/A`), que ficam para a Ação 15
(Capítulo 4, resultado).

**Correções aplicadas (as 4 pedidas na Ação 10):**
- 12 kHz (simulação CA) vs 16 kHz (chaveamento do ESC, 3.8.1): **não
  unificado para o mesmo valor** — em vez disso, uma frase objetiva declara
  que 12 kHz corresponde à fundamental da corrente de fase (o sinal a
  caracterizar) e não à portadora PWM do ESC (16 kHz), com base no
  comentário já existente em `run_simulacao.py:17`. Uma primeira versão
  dessa frase incluía uma alegação de "onde se concentra a energia do
  sinal" — o autor apontou que isso é uma afirmação de **resultado**
  disfarçada de metodologia (regra 2 do `CLAUDE.md`) e foi removida; a frase
  atual só declara qual frequência foi usada e o que ela representa, sem
  argumentar por quê. **Revisão do orientador recomendada**: confirmar se
  essa distinção é a interpretação correta pretendida pela nota
  "Uniformizar" do plano, ou se a intenção era de fato re-rodar a simulação
  CA a 16 kHz.
- Limiar de 1% de crosstalk ancorado ao erro do ADC (Tabela 6, `tab:adc_error`):
  ±4 LSB ≈ 3,2 mV sobre fundo de escala de 3,3 V ≈ 0,1% — abaixo do limiar de
  1% adotado.
- Banda passante declarada: SS49E ($t_{ack}=3\mu s$) → $f_{-3dB}\approx117$ kHz,
  abaixo do Nyquist de 333 kSps/2 ≈ 166,5 kHz. Aproximação de primeira ordem,
  não é medição — sinalizar se o orientador preferir uma medição real de
  banda passante em bancada.
- Margem de saturação: estimada por lei de Ampère ($B_{gap}\approx54,6$ mT a
  100 A), comparada por ordem de grandeza com $B_{sat}$ típico de MnZn
  (380–500 mT, McLyman). **Não há extração de campo dentro do núcleo (fora do
  entreferro) pela simulação FEMM** — `run_simulacao.py` só extrai B no gap.
  A conclusão qualitativa (margem folgada) é considerada segura pela ordem de
  grandeza, mas não há valor simulado exato de B no ferrite para citar.

**Imagens inseridas (já resolvidas, copiadas para o repositório):**
- `Textuais/Imgs/Metodologia/sensor_single_gap.pdf` (copiado de
  `Simulacoes/Sensor Corrente/Toroide/single_gap.pdf`)
- `Textuais/Imgs/Metodologia/sensor_trifase_geral_d028mm.pdf` (copiado de
  `.../trifase_geral_d028mm.pdf`)
- Figura de topologia (`fig:topologia_sensor_corrente`) é TikZ inline, não
  precisa de arquivo de imagem.

**Dados pendentes:** nenhum para esta ação — todo o conteúdo usa
dados de datasheet ou já simulados.

**Outras alterações de suporte:**
- `PacotesBasicos.tex`: adicionado `\DeclareSIUnit{\gauss}{G}` (necessário
  para `\SI{...}{\gauss}` nas próximas ações — 14 e 15 vão precisar).
- Compilação verificada: `pdflatex` (2x) + `bibtex` + `pdflatex` (2x) sem
  erros novos. O único aviso de referência não resolvida
  (`fig:bancada_fisica`, `Conclusao.tex:694`) é pré-existente — ver Ação 16.

---

## ✅ Ação 11 — referências do sensor de corrente

**Escrito em:** `abntex2-ref_UDESC_2020.bib`, entradas novas ao final do
arquivo: `ramsden2006`, `ziegler2009`, `mclyman2011`, `femm_meeker2018`,
`pyfemm_meeker`, `lammeraner1966`, `allegro_an296128_2023`.

Todas com edição/ano verificados por busca web (não copiados sem checagem).
`Fitzgerald2014` e `Figliola2019` (linhas da tabela do plano marcadas "já
citado") **não** ganharam entrada nova — serão citados na Ação 15/14 quando
o texto de resultados for escrito.

**Citadas no corpo do texto (3.8.1) nesta sessão:** `ramsden2006`,
`ziegler2009`, `mclyman2011`, `femm_meeker2018`, `pyfemm_meeker`,
`lammeraner1966`, `allegro_an296128_2023` — todas com `\cite` em
`08_SAME.tex`, nenhuma ficou órfã.

**Pendente:** `hall_dc_sensor_2016` (citação incompleta, TODO de autor) que
existia em `Simulacoes/Sensor Corrente/Toroide/referencias.bib` **não foi
importada** para a bibliografia principal — foi substituída por `ramsden2006`
na afirmação sobre efeito Hall/sensor ratiométrico, conforme a tabela da
Ação 11. Não é necessário nenhuma ação adicional, só registro de que a
substituição foi intencional.

**Revisão do orientador:** nenhuma pendência de conteúdo; checar apenas se
as referências escolhidas (em vez das alternativas "ou" listadas no plano,
ex. McLyman em vez de Erickson e Maksimović) são aceitáveis.

---

## ✅ Ação 15 — resultados do sensor de corrente (Cap. 4)

**Escrito em:** `Textuais/Conclusao.tex` (arquivo do Capítulo 4 "Resultados e
Discussões" — apesar do nome do arquivo). **Reestruturada três vezes a
pedido do autor**: (1) veio como seção "Validação da Cadeia de Medição
Elétrica" com SIBA/SAME como subseções dentro dela; (2) o autor rejeitou o
nome da seção-guarda-chuva e pediu SIBA e SAME como `\section` própria; (3)
o autor pediu para mover SIBA/SAME para depois das seções de modelagem
("Modelagem do SMO", "Modelagem Física"), seguindo a mesma ordem do
Capítulo 3 (Metodologia), onde SIBA/SAME também vêm por último, depois do
motor, da interface e das ferramentas. Posição final: entre "Modelagem
Física" e o Capítulo 5 (Conclusão). Estado final:
- `\section{SIBA}` (`sec:resultado_validacao_siba`) — placeholder visível
  (`\textit{Pendente: ...}`), sem dado nenhum ainda (bateria, células de
  carga, Pitot).
- `\section{SAME}` (`sec:resultado_validacao_same`), contendo
  `\subsection{Sensor de Corrente}` (`sec:resultado_sensor_corrente`).

Todo o conteúdo descrito abaixo está dentro de SAME > Sensor de Corrente, e
cobre exatamente o que tinha ficado de fora da Ação 10:
varredura de corrente CC ($G_{núcleo}=5{,}355$ G/A), equação de
transferência com valores numéricos, envelope de tolerância/deriva,
varredura de crosstalk (0,0026% máx.) e tabela de indutância própria/mútua.

**Escopo reduzido em relação à Ação 14 original:** a Ação 14 pedia uma seção
mais ampla ("validação da cadeia de medição elétrica") incluindo ganho/offset
calibrado por canal, erro contra referência, piso de ruído (motor parado e
ESC chaveando) e formas de onda reais — **nenhum desses itens foi escrito**
porque dependem de dados de bancada que não existem no repositório (não há
CSV nem valor algum para essas grandezas). Só a subseção "Sensor de
Corrente" foi criada agora, com dados 100% oriundos da simulação FEMM já
existente. A seção `\section{Validação da Cadeia de Medição Elétrica}` fica
aberta para receber essas outras subseções quando os ensaios forem feitos —
**não criar subseções vazias ou com texto sem valor nesse meio tempo**
(regra 2).

**Imagens inseridas (copiadas para o repositório):**
- `Textuais/Imgs/Resultados/sensor_fig_I_vs_B.pdf`
- `Textuais/Imgs/Resultados/sensor_fig_erro_sensor.pdf`
- `Textuais/Imgs/Resultados/sensor_fig_crosstalk.pdf`
- `Textuais/Imgs/Resultados/sensor_fig_inductancia_mutua.pdf`
- (todas copiadas de `Simulacoes/Sensor Corrente/Toroide/*.pdf`)

**Não usadas (existem no diretório de simulação mas ficaram de fora, por
redundância com a tabela `tab:resultado_inducao_resumo` já usada no texto
original):** `fig_linearidade.pdf`, `fig_inductancia_self.pdf`,
`fig_coef_acoplamento.pdf`. Se o orientador preferir gráfico em vez de
tabela para indutância própria/coeficiente de acoplamento, essas três
existem prontas.

**Dados pendentes:** nenhum para o que foi escrito — usa apenas resultados
de simulação já existentes. A comparação "medido vs. simulado" mencionada
no plano original da Ação 14 (curva real do sensor calibrado contra os
5,355 G/A previstos) continua pendente de ensaio físico.

**Local reservado para dado real (a pedido do autor, revisado):** a versão
inicial usava um bloco `% TODO ... %` comentado. O autor pediu para não
deixar nada comentado — o espaço reservado deve estar ativo no texto, à
vista, no mesmo padrão já usado em `fig:telas_gui` (caixa tracejada com
texto). Substituído por uma figura ativa
`fig:resultado_sensor_medido_vs_simulado` (caixa tracejada via TikZ, sem
depender de arquivo de imagem externo), logo após a Tabela
`tab:resultado_inducao_resumo` e antes de `\section{Modelagem do SMO}`, com
parágrafo introdutório citando-a. Ao preencher com o dado real: substituir
o conteúdo do `\node` tracejado por
`\includegraphics{Textuais/Imgs/Resultados/sensor_medido_vs_simulado.png}`
(gerar essa imagem a partir da curva medida) e escrever no parágrafo o G
medido e o erro frente aos 13,39 mV/A previstos
(Eq.~`eq:ganho_offset_valores`).

**Revisão do orientador:** nenhuma pendência de conteúdo na subseção
"Sensor de Corrente". A seção "SIBA" é um placeholder textual visível
(`\textit{Pendente: ...}`) sem resultado — tecnicamente tensiona a regra 2
do `CLAUDE.md`, mas foi criada assim a pedido explícito do autor, seguindo o
mesmo padrão que já existia em "Coeficientes Mecânicos" (linha com `aaa`
antes desta ação). Substituir por conteúdo real assim que o ensaio da SIBA
for feito, ou remover a subseção se não fizer sentido mantê-la vazia até lá.

---

## ✅ Ação 4 — 3.3 calibração de sensores e configuração default (revisada ao vivo pelo autor)

**Escrito em:** `Textuais/Metodologia/03_Interface.tex`, ao final da seção
3.3, depois da Figura 24 (`fig:yaml_example_config`).

**Versão inicial e correção do autor:** a primeira versão tinha três
`\subsection` novas (Fluxo de Calibração, Células de Carga, YAML como
Configuração Default), com blocos `\begin{verbatim}` mostrando trechos de
`SIBA.yaml` e do header C gerado. O autor pediu, em tempo real, para
remover as subseções "Células de Carga" e "YAML como Configuração Default"
(consideradas texto sem utilidade) e para não criar tantas subseções para
explicar um único fluxo — a explicação deve se apoiar na figura, não
substituí-la por prosa longa. **Estado final:** só restou um parágrafo
corrido (sem `\subsection` própria, sem `\label{sec:fluxo_calibracao}`)
imediatamente antes da Figura 25 (`fig:seq_calibracao`), remetendo a ela
para os detalhes do fluxo. Os blocos de código YAML/header foram removidos
por completo.

**Levantamento de código feito antes de escrever** (para não inventar
comportamento, ainda válido apesar do corte de texto):
`Codigos/Aplicacao_Pi/GUI/SensorConfigDialog.py`,
`Codigos/Aplicacao_Pi/GUI/LoadCellDialog.py`,
`Codigos/SIBA/lib/OS/CMD_Core.c`, `Codigos/SIBA/lib/OS/HAL.c` e
`Codigos/Aplicacao_Pi/Config/SIBA.yaml` + `CMD_Core_prm_template.h.j2`.
Achados que corrigiam premissas do plano original (persistem no parágrafo
final, resumidos): não há campo `unidade`/`taxa de amostragem` no YAML; não
há persistência em flash nem em arquivo no PC — os coeficientes vivem em
RAM e se perdem a cada reset; não há pacote de confirmação/ACK dedicado, só
o próximo pacote de telemetria.

**Figura mantida (TikZ inline, sem imagem faltando):** `fig:seq_calibracao`
— sequência Diálogo GUI → comando textual → CMD binário → firmware → canal
atualizado → telemetria.

**Pendente (não escrito nesta ação):** os 9 espaços reservados da Figura 23
(`fig:telas_gui`, já existente antes desta sessão) continuam vazios —
precisam de sessão de captura de tela com o sistema rodando. Prioridade
sugerida pelo plano: tela principal com dados em execução, TinyTerminal
mostrando o hexadecimal de um comando, e o diálogo de calibração de célula
de carga (`LoadCellDialog`).

**Revisão do orientador:** nenhuma pendência — texto revisado e aprovado ao
vivo pelo autor durante a sessão.

---

## ✅ Ações 1, 2, 3, 5, 6, 7, 8, 9, 12, 13, 16 — pacote final

Escritas em sequência a pedido do autor ("faça todas as ações, não parem").
Onde faltava imagem real (foto, captura de tela, gráfico com dado que ainda
não existe), a regra 8 do `CLAUDE.md` (adicionada nesta sessão, a pedido do
autor) passou a valer: nunca comentar com `%`, sempre inserir a figura
ativa com caixa tracejada (`\node[draw, dashed, ...]`) no lugar da imagem,
mesmo padrão de `fig:telas_gui`. Onde faltava só um dado numérico (não uma
imagem), usa-se o mesmo padrão de nota `\textit{Pendente: ...}` já
validado na Ação 15 (seção "SIBA").

- **Ação 1** — `02_MotorBLDC.tex`, nova `\subsection{Bancada Provisória de
  Caracterização}` (`sec:bancada_provisoria`), antes de "Parâmetros
  Elétricos". Diagrama de blocos real (TikZ) do caminho fonte→ESC→motor→
  instrumentação→PC, mais figura-placeholder para a fotografia
  (`fig:bancada_provisoria`). Dois dados não confirmados em código nem em
  nenhum `.tex`/`.py` do repositório ficaram marcados inline como
  \textit{(a confirmar)}: o instrumento de medição de tensão/corrente usado
  e a taxa de amostragem da bancada provisória — não inventados.
- **Ação 2** — frase no início de "Parâmetros Mecânicos" (`02_MotorBLDC.tex`)
  sobre ensaio sem hélice, e propagação de $D_{prop}$ para as Equações
  `eq:Prop_coeff1`/`eq:Prop_coeff2`, citando a nomenclatura $D\times P$ de
  `sec:helice_aerodinamica` (label novo, adicionado no `\section` de
  Revisão que não tinha nenhum).
- **Ação 3** — nova `\subsection{Identificação dos Coeficientes
  Aerodinâmicos da Hélice em Bancada}` (`sec:coef_aero_bancada`) em
  `02_MotorBLDC.tex`, com figura de fluxo (TikZ, 4 caixas) em vez de texto
  longo, por pedido do autor de preferir figura a prosa quando possível
  (regra 6). Usa as equações já existentes em `Revisao.tex`
  (`eq:Prop_coeff1`, `eq:Prop_coeff2`, `eq:cq_from_cp`,
  `eq:densidade_calculo`) e o critério de patamar do Algoritmo 7
  (`alg:kq_identificacao`, confirmado como o 7º algoritmo do documento por
  contagem de `\begin{algorithm}` na ordem de compilação). Referência nova
  `apc_performance_data` adicionada ao `.bib` para as tabelas de desempenho
  da APC.
- **Ação 5** — `04_Ferramentas.tex`: duas figuras-placeholder
  (`fig:native_sim`, `fig:sniffer_pacote`), citadas em frase antes delas.
- **Ação 6** — `06_Bancada.tex`: frase após a Equação `eq:ppico` e figura-
  placeholder `fig:precarga_osc`.
- **Ação 7** — `06_Bancada.tex`: três figuras-placeholder
  (`fig:cad_caixa`, `fig:montagem_interna`, `fig:painel_externo`), uma por
  subseção existente (Caixa/Organização Interna/Conexões Externas), cada
  uma citada na prosa já existente.
- **Ação 8** — figura-placeholder `fig:pcb_siba` em `07_SIBA.tex` (fim da
  subseção \textit{Hardware}) e `fig:pcb_same` em `08_SAME.tex` (início da
  seção, antes de "Medição de Tensão e Corrente de Fase").
- **Ação 9** — nova `\subsection{Calibração do Tubo de Pitot}`
  (`sec:calibracao_pitot`) em `07_SIBA.tex`. **Correção de premissa do
  plano**, verificada no código (`grep` em `GUI/*.py` e
  `Communication_Data/*.py`): não existe nenhuma entrada manual de pressão
  atmosférica/temperatura combinada pela fórmula de densidade
  (`eq:densidade_calculo`) — o que existe é um campo de densidade do ar
  ($\rho$) já pronto, inserido manualmente em `HeliceDialog.py`
  (`self.sp_rho`, padrão 1,225 kg/m³). O texto escrito reflete isso, não o
  que o plano supunha. O parágrafo sobre o ensaio de túnel de vento em si
  (faixa de velocidade, número de pontos, ajuste obtido) ficou como nota
  `\textit{Pendente: ...}`, por depender de ensaio ainda não feito.
- **Ação 12** — `Conclusao.tex`: figuras-placeholder `fig:setup_kelvin`
  (dentro de "Resistência") e `fig:setup_ressonancia` (dentro de
  "Indutância"), citadas nas frases de abertura de cada subseção já
  existente.
- **Ação 13** — `Conclusao.tex`, "Coeficientes Mecânicos": o texto `aaa`
  foi substituído por nota `\textit{Pendente: ...}` detalhando o que falta
  (tabela de patamares, valores de $B$/$\tau_c$, curva de desaceleração) e
  uma figura-placeholder `fig:coastdown`.
- **Ação 16** — resolvido o `\label{fig:bancada_fisica}` que nunca existia
  (confirmado por `grep` antes desta ação): figura-placeholder inserida em
  `Conclusao.tex`, seção "Modelagem Física", com esse label exato. A
  referência `\autoref{fig:bancada_fisica}` já existente em
  `Conclusao.tex:746` agora resolve para essa figura — não há mais `??` na
  compilação.
- **Restante da Ação 14** (não coberto pela Ação 15): nova
  `\subsection{Calibração de Canal e Formas de Onda Reais}`
  (`sec:resultado_calibracao_canal_same`) dentro de `\section{SAME}`, antes
  de "Sensor de Corrente" — nota `\textit{Pendente: ...}` (ganho/offset por
  canal, erro vs. referência, piso de ruído) e figura-placeholder
  `fig:formas_onda_reais`.

**Reestruturação adicional a pedido do autor:** `\section{SIBA}` e
`\section{SAME}` (Cap. 4) foram movidas para depois de "Modelagem do SMO" e
"Modelagem Física" — ver nota já registrada dentro da Ação 15 acima.

**Compilação final desta sessão:** `pdflatex` (2×) + `bibtex` + `pdflatex`
(2×) sem nenhum erro e **sem nenhuma referência ou citação não resolvida**
em todo o documento (checado com `grep -i undefined` no log da última
passada).

---

## ✅ Sensor de corrente reescrito com dados de bancada (`ensaio_hall_SS49E.csv`)

Substituição da caracterização por simulação FEMM por **caracterização
experimental**, a partir de `~/Downloads/ensaio_hall_SS49E.csv` (30 pontos:
$N = 1$, 4 e 8 espiras) fornecido pelo autor, mais os agregados do ensaio de
resposta em frequência (64 capturas em 1 kHz + 64 em 3 kHz). O autor pediu
três coisas nesta rodada, todas aplicadas: (a) os dados brutos vão para
`PosTextuais/` — pedidos primeiro como **anexo**, corrigido depois pelo próprio
autor para **apêndice**, porque o ensaio é dele (é a distinção da ABNT: anexo é
material de terceiro, apêndice é do autor); (b) o material entra como **uma
coisa só**, sem fragmentar em subseções; (c) **toda a análise em frequência foi
retirada** — o autor a rejeitou por completo. No lugar dela ficou uma única
frase, na metodologia e no resultado, dizendo que a dinâmica é modelada como
primeira ordem (RC) com polo em torno de **56 kHz**, inferido do tempo de
resposta de 3 µs de catálogo, e que esse polo é levado em conta na coerência de
fase.

**Onde foi escrito:**
- `Textuais/Metodologia/08_SAME.tex` — a `\subsubsection{Sensor de Corrente com
  Núcleo Toroidal}` (`sec:same_sensor_corrente`) ganhou, em texto corrido sem
  novas subdivisões: modelo do campo no entreferro via fator de indutância
  $A_l$ de catálogo (Eqs. `eq:mur`, `eq:B_hopkinson`, `eq:dBdI`, `eq:S_teo`),
  faixa de medição, procedimento do ensaio estático (artifício de $N$
  espiras), a frase do polo de 56 kHz e, depois dela, o **estágio de
  condicionamento dimensionado** (ver bloco próprio abaixo).
- `Textuais/Conclusao.tex` — `\subsection{Sensor de Corrente}`
  (`sec:resultado_sensor_corrente`), antes vazia, agora com 3 figuras e 1
  tabela (ver abaixo). Também em texto corrido, uma subseção única.
- `PosTextuais/Apendices.tex` — `\chapter{Dados brutos da caracterização do
  sensor de corrente}` (`apendice:ensaio_hall`), Apêndice A, com a coleta
  completa das 30 linhas (Tabela `tab:hall_dados_brutos`: $N$, $I$, $V_{out}$,
  $\Delta V$, $B$, resíduo). Texto de abertura curto, a pedido do autor —
  apêndice não leva parágrafo explicativo longo. `Anexos.tex` voltou ao estado
  original (`\chapter{TÍTULO}` vazio).

**Figuras e tabelas novas (todas com caption, label, fonte e citação no
corpo — nenhuma é placeholder, todas têm dado real):**

| Label | O que é |
|---|---|
| `fig:hall_vout_i` | $V_{out}(I)$ das três configurações + retas ajustadas (pgfplots) |
| `fig:hall_dv_b` | Curva de transferência $\Delta V \times B$ vs. típico de catálogo |
| `fig:hall_res` | Resíduos do ajuste linear por configuração |
| `tab:hall_result` | $S$, $S_H$, desvio do típico e $R^2$ por configuração |
| `tab:hall_dados_brutos` | Apêndice A — coleta completa |

**Números extraídos (regressão por mínimos quadrados, ponto de $I = 0$
incluído no ajuste):**

| $N$ | $B$ varrido | $S$ (mV/A) | $S_H$ (mV/G) | $R^2$ |
|---|---|---|---|---|
| 1 | 0–78 G | 18,75 ± 0,16 | 1,209 | 0,99939 |
| 4 | 0–330 G | 90,87 ± 0,09 | 1,465 | 0,99999 |
| 8 | 0–516 G | 165,42 ± 0,20 | 1,334 | 0,99999 |

**O achado que o orientador precisa avaliar:** $S_H$ é propriedade de uma única
unidade física de sensor e não pode variar entre ensaios, mas os três valores
divergem ±9,6 % em torno da média (1,336 mV/G), **sem tendência monotônica com
$N$**. O texto atribui a dispersão à hipótese $B \propto N\,I\,(A_l/A_e)$, e
não ao elemento Hall — as duas fontes citadas são a tolerância de ±15 % do
próprio $A_l$ de catálogo e a diferença de acoplamento entre espira única pelo
furo e enrolamento distribuído externamente. O $R^2 \geq 0{,}9994$ dentro de
cada configuração sustenta que é erro sistemático entre montagens, não ruído.
**Consequência assumida no texto:** o ganho usado no firmware é o de $N = 1$
(18,75 mV/A → 42,97 mA/LSB), e o artifício de $N$ espiras vale para verificar
linearidade e faixa, não para calibrar. Se o orientador discordar dessa
leitura, é o parágrafo a rediscutir.

**Análise em frequência retirada a pedido do autor.** Havia sido escrita uma
seção completa (formas de onda médias em 3 kHz, tabela de ganho/defasagem em 1 e
3 kHz, limites inferiores de $f_c$ por critério de fase e de ganho, e tabela
comparando a fase do caminho de corrente com a do caminho de tensão). **Tudo
removido** — o autor considerou o resultado malfeito. Sobrou apenas, em
metodologia e em resultado, a frase de que a dinâmica é modelada como primeira
ordem com polo em torno de **56 kHz** (de catálogo, não medido — declarado como
tal), e que por estar acima do polo de 45,2 kHz do estágio de tensão
(`sec:condicionamento`) o caminho de corrente não é o que limita a coerência de
fase do observador. Nenhuma equação e nenhum dado do ensaio CA sobrou no texto.

Os dados desse ensaio (64 capturas em 1 kHz + 64 em 3 kHz, 96158 amostras a
9,62 MSa/s) **não estão no repositório** — chegaram só como agregados no LaTeX
de rascunho do autor. Se a análise for retomada, refazer a partir das capturas
originais do osciloscópio. Dois problemas conhecidos do que havia: só dois
pontos de frequência, e o *shunt* de 1 Ω tomado pelo valor nominal sem
calibração, o que jogava a sensibilidade absoluta 7–10 % abaixo do valor
estático da mesma configuração de 8 espiras.

A referência `ieee_std_1057` (ajuste de senoide de três parâmetros), adicionada
para essa seção, foi **removida do `.bib`** por ter ficado sem citação.

**Estágio de condicionamento (depois da dinâmica do sensor).** A frase anterior
— "o ganho é 18,75 mV/A, do que resulta 42,97 mA por LSB" — estava errada de
premissa: o sinal do sensor **não pode ir direto ao ADC**, porque o repouso
ratiométrico fica em 2,5 V (alimentação de 5 V) e o microcontrolador converte de
0 a 3,3 V. No resultado (Cap. 4) sobrou apenas o ganho do sensor, sem resolução;
o dimensionamento foi para a metodologia, logo após a frase do polo de 56 kHz.

**Histórico da decisão** (três versões, todas exploradas com o autor no mesmo
dia; vale registrar porque o `.bib` e o texto mudaram junto): (1) amplificador de
diferença dimensionado pela nota SLOA097 da Texas; (2) variante de exatidão da
seção 4 da mesma nota, com segundo operacional como seguidor, escolhida pelo
autor; (3) **versão final, passiva** — divisor resistivo com capacitor, sem
operacional nenhum, proposta pelo autor. As duas primeiras foram descartadas e a
referência `ti_sloa097` foi **removida do `.bib`** por ficar sem citação.

**O que decidiu a favor da versão passiva:** o LM358 (peça que o autor queria
usar, e que já está no caminho de tensão) não serve no estágio de corrente
alimentado a 5 V. O datasheet dá, em todas as variantes, faixa de modo comum de
entrada de $(V^-)$ a $(V^+)-1{,}5$ V, e $(V^+)-2{,}0$ V de −40 a +85 °C — 3,5 V
a 25 °C e 3,0 V sobre temperatura, contra os 3,625 V que a entrada receberia do
sensor em +60 A. Daria +54 A a 25 °C e só +27 A garantidos sobre temperatura.
Alternativas seriam alimentar o LM358 com os 12 V da bancada (existem, ver
`06_Bancada.tex`) ou usar um dual *rail-to-rail* em 5 V — mas a versão passiva
resolve sem nenhuma das duas.

**Dimensionamento final** (na metodologia, com figura em circuitikz desenhada,
não é espaço reservado):

| Grandeza | Valor |
|---|---|
| Divisor | $R_1 = 2{,}0$ kΩ (série) / $R_2 = 18$ kΩ (para o terra), $k = 0{,}900$ |
| Faixa ±60 A na saída | 1,225 V a 3,250 V (nula em 2,238 V) |
| Satura a janela do ADC em | +62,9 A |
| Capacitor | 2,2 nF em paralelo com $R_2$ |
| $R_1 \parallel R_2$ | 1,8 kΩ |
| Frequência de corte | 40,2 kHz |
| Ganho da cadeia | 16,88 mV/A |
| Resolução | 47,7 mA/LSB |
| Equações no texto | `eq:cond_k` ($k = R_2/(R_1+R_2)$) e `eq:cond_fc` ($f_c = 1/[2\pi(R_1\|R_2)C]$) |

**Por que a resolução pior não importa.** Sai de 32,2 mA/LSB (versão com
operacional, janela cheia) para 47,7 mA/LSB (61 % da janela). Mas o resíduo de
linearidade medido no próprio sensor, 1,24 mV, equivale a **66 mA** referidos à
entrada — já maior que o LSB degradado. O ADC não é o elo limitante em nenhum
dos dois casos, então o operacional não comprava nada mensurável.

**O ponto do RC, e por que ele vai nos dois caminhos.** O capacitor é
*antialiasing*, não desacoplamento. Se ficasse só no caminho de corrente,
destruiria a coerência de fase que o observador exige: o descasamento a 12 kHz
saltaria de 2,8° para cerca de 19°. Com o mesmo polo nos dois caminhos o atraso
é comum e se cancela na diferença — descasamento fica nos mesmos 2,8° de antes,
e a atenuação no Nyquist da aquisição sobe de −9,9 dB para −22,5 dB, custando
apenas −0,37 dB em 12 kHz. Para o caminho de tensão (Thévenin de 7,09 kΩ) o
capacitor equivalente é **560 pF**.

**Duas correntes verificadas** (o autor cobrou as duas, e a segunda de fato tinha
ficado de fora da primeira conta):
- *Saída do sensor:* 181 µA estáticos em fundo de escala **+ até 183 µA de pico**
  para carregar o capacitor a 12 kHz = 364 µA, contra ≥1 mA garantidos em
  catálogo (tipo de saída *sourcing*, que é o sentido em que o divisor puxa).
  O pico do capacitor quase dobra a conta — é o que descartou um divisor de
  impedância menor (1,0 k/9,1 k daria 724 µA, apertado demais).
- *Entrada do ADC:* a recarga do capacitor de 8 pF do S&H atravessa
  $R_1 \parallel R_2$ e aparece como erro de **ganho** de
  $(R_1\|R_2)f_S C_{ADC} = 0{,}48$ %. Linear, some na calibração. No caminho de
  tensão o mesmo efeito dá **1,89 %**, porque o Thévenin de lá é 4× maior.

**Consequência para a calibração, já dita no texto:** os 18,75 mV/A foram
medidos com multímetro, ou seja **sem carga**. Com o divisor montado entram a
impedância de saída do sensor e os 0,48 % da injeção de carga. A calibração tem
que ser da cadeia completa — sensor, divisor e ADC juntos —, nunca do sensor
isolado.

**Escolhas minhas, não do autor** (checar se concorda): o par 2,0 k/18 k (a razão
podia ser outra desde que $k \leq 0{,}910$); o polo de 40 kHz para o RC; e os
560 pF do caminho de tensão, que dependem do divisor 130 k/7,5 k que eu sugeri e
que ainda não está no texto (ver abaixo).

**Caminho de tensão escrito** (`sec:same_divisor_tensao`, subsubseção nova antes
da do sensor de corrente), mesma topologia: divisor + capacitor direto ao ADC,
figura `fig:cond_tensao` em circuitikz.

| Grandeza | Valor |
|---|---|
| Divisor | $R_1 = 130$ kΩ / $R_2 = 7{,}5$ kΩ, $k = 0{,}0545$ |
| Fundo de escala | 60,5 V — 2,0× o barramento de 30 V, folga para o sobressinal |
| Resolução | 14,8 mV/LSB |
| Thévenin | 7,09 kΩ — abaixo dos 11,4 kΩ do menor $t_s$ da `tab:rain_max` |
| Capacitor | 560 pF → $f_c = 40{,}1$ kHz (mesmo polo do caminho de corrente) |
| Consumo | 218 µA a 30 V, 6,2 mW em $R_1$ |
| Erro de ganho por injeção de carga | 1,89 % |

As equações `eq:cond_k` e `eq:cond_fc` foram **movidas para essa subsubseção**
(vem antes no documento) e o bloco do sensor de corrente passou a referenciá-las
em vez de repetir — a topologia é a mesma nos dois caminhos.

**Figuras de rebatimento espectral** (`fig:alias_tensao` e `fig:alias_corrente`),
uma por caminho. Mostram as raias das harmônicas do PWM (12 kHz, envelope $1/n$)
já na entrada do ADC, a curva de resposta do filtro, a banda de interesse
sombreada, $f_S/2$ e $f_S$ marcados no próprio eixo, e a anotação do rebatimento.
O caso concreto: a **28ª harmônica do PWM, em 336 kHz, dobra exatamente em
3 kHz** — dentro da faixa da fundamental elétrica. Chega a −47,5 dB no caminho de
tensão e a −63,1 dB no de corrente, onde o polo do sensor se soma ao do RC.

**Três versões foram descartadas antes desta** (o autor reprovou as duas
primeiras por ilegibilidade, com razão): (1) legenda por cima das raias e arco de
anotação atravessando o gráfico; (2) duas séries de raias sobrepostas no mesmo
$x$, indistinguíveis, com $f_S/2$ e $f_S$ em rótulos rotacionados minúsculos.
A versão final: **sem legenda** — os três elementos são rotulados no próprio
gráfico ("banda de interesse", "filtro", e o valor em dB sobre a seta); uma única
série de raias, a que chega ao ADC; e $f_S/2$ e $f_S$ como marcas extras no eixo
$x$, em tamanho normal.

**Regra que o autor reforçou nesta rodada** (vale para o resto do documento):
frase longa encadeada é ilegível, e fórmula com identificador no meio do texto
corrido também. O parágrafo do casamento de polos foi quebrado em três frases
curtas, e a expressão do erro de ganho saiu do texto e virou equação numerada,
uma por caminho (`eq:cond_ganho`, 1,89 %, e `eq:cond_ganho_i`, 0,48 %), **com a
substituição numérica dentro da própria equação** — mesmo padrão de `eq:mur`,
`eq:dBdI` e `eq:S_teo`, e também a pedido do autor: o valor tem que sair na
fórmula, não só no texto que a segue.

**O envelope $1/n$ das harmônicas é ilustrativo**, não medido — serve para
mostrar o mecanismo do rebatimento, não para prever a amplitude real de cada
harmônica. Se o orientador cobrar rigor aí, o caminho é medir o espectro real da
corrente de fase com o ESC chaveando.

**Correções de arraste feitas junto (não pedidas, mas necessárias para o
documento fechar):**
1. `tab:datasheet_ss49e` tinha sensibilidade **2,0 / 2,5 / 3,0 mV/G** e
   linearidade 0,007 % — valores do SS59ET, não do SS49E. Sem a correção o
   Capítulo 4 compararia o medido contra um típico errado. A tabela foi depois
   **substituída inteira** pela tabela de *operating characteristics* que o
   autor colou do datasheet: sensibilidade 1,0 / 1,4 / 1,75 mV/G, alimentação
   2,7–6,5 V, corrente de alimentação 6/10 mA, corrente de saída 1,0/1,5 mA,
   faixa magnética ±650/±1000 G, linearidade −0,7 % do span, tempo de resposta
   3 µs, deriva do repouso ±0,10 %/°C, deriva da sensibilidade separada em
   ≥25 °C (−0,15/0,05) e <25 °C (−0,04/0,185), temperatura de operação
   −40 a 100 °C. Citada como `honeywell_ss49e`. **Legenda:** a primeira versão
   trazia as condições de ensaio ($V_S = 5{,}0$ V, $T_A$ de −40 a 85 °C) na
   `\caption`; o autor pediu para tirar — legenda tem que ser simples.
2. `eq:hall_corrente` e `sec:same_condicionamento` eram **referências sem
   destino** (`\autoref` sem `\label` em lugar nenhum). A equação passou a ser
   definida em 3.8.1.1; o label de condicionamento foi fixado na
   `\subsection{Medição de Tensão e Corrente de Fase}`, que é onde o
   condicionamento dos dois caminhos é de fato descrito.
3. `fig:resultado_sensor_medido_vs_simulado` também era referência sem destino,
   citada duas vezes no Capítulo 5. As duas citações foram reescritas: o
   objetivo específico 1 agora registra o sensor como **caracterizado em
   bancada** (não "por simulação"), e o item de "Limitações e Itens Pendentes"
   passou a ser a calibração na topologia de uso com fonte de alta corrente.
4. `\autoref` em `\chapter` dentro de `apendicesenv`/`anexosenv` não imprime
   "Apêndice"/"Anexo" corretamente — usar `Apêndice~\ref{...}` na mão.

**Referências novas em `abntex2-ref_UDESC_2020.bib`:** `honeywell_ss49e`
(datasheet SS39ET/SS49E/SS59ET), `thornton_nucleos` (catálogo de toroidais —
resolve a nota "datasheet do núcleo não localizado" das Notas gerais, mas é
catálogo web, não PDF do fornecedor), `dewi2016` (fringing field / sensor Hall
de alta corrente) e `ieee_std_1057` (ajuste de senoide de três parâmetros).

**Pendências abertas por esta rodada:**
- Ensaio na topologia de uso ($N = 1$) com fonte capaz de $\pm 60$ A, para
  calibrar o ganho de fato usado. É o único item que a dispersão de ±9,6 %
  deixa em aberto.
- Se o autor quiser resposta em frequência no documento, o ensaio precisa ser
  **refeito** e melhor instrumentado: mais de dois pontos de frequência e
  *shunt* de referência medido, não nominal.
- **Inconsistência não resolvida (decisão do autor):** 3.8.1 diz que o ESC
  chaveia em **12 kHz**; `sec:condicionamento` (Cap. 4) diz **8 kHz**. As duas
  afirmações são sobre a mesma grandeza física. Não foi tocado nesta rodada
  porque não faz parte do sensor de corrente, mas precisa ser unificado.

**Compilação:** `pdflatex` ×2 + `bibtex` + `pdflatex` ×2, sem erro, sem
referência ou citação indefinida. 112 páginas (era 108). Os 12 *overfull hbox*
do log são todos pré-existentes, em `PreTextuais/Listas.tex`.

---

## 📋 Checklist único de pendências reais (todas as ações)

Tudo abaixo é imagem ou dado que só o autor pode fornecer — nenhum foi
inventado. Local = onde está o `\label` da figura ou da nota `Pendente`.

**Imagens (fotos/capturas de tela/gráficos) a produzir e inserir:**

| Label | Arquivo | O que precisa |
|---|---|---|
| `fig:bancada_provisoria` | `02_MotorBLDC.tex` | Foto da montagem provisória |
| `fig:native_sim` | `04_Ferramentas.tex` | Captura de tela: supervisão + native_sim |
| `fig:sniffer_pacote` | `04_Ferramentas.tex` | Captura de tela: pacote de rede |
| `fig:pcb_siba` | `07_SIBA.tex` | Export KiCad: esquemático + roteamento |
| `fig:pcb_same` | `08_SAME.tex` | Export KiCad: esquemático + roteamento |
| `fig:setup_kelvin` | `Conclusao.tex` | Foto do ensaio de resistência (já desmontado, precisa remontar) |
| `fig:setup_ressonancia` | `Conclusao.tex` | Foto do ensaio de indutância (já desmontado, precisa remontar) |
| `fig:coastdown` | `Conclusao.tex` | Gráfico $\omega_m(t)$ do ensaio de desaceleração livre |
| `fig:bancada_fisica` | `Conclusao.tex` | Foto da estrutura mecânica já usada pela equipe |
| `fig:formas_onda_reais` | `Conclusao.tex` | Captura de V/I de fase reais |
| Figura 23 (`fig:telas_gui`) | `03_Interface.tex` | 9 capturas de tela da interface (já existia antes desta sessão) |

**Dados numéricos/textuais a preencher (notas `\textit{Pendente: ...}`):**

| Onde | O que falta |
|---|---|
| `sec:bancada_provisoria` | Instrumento de medição V/I e taxa de amostragem da bancada provisória |
| `sec:calibracao_pitot` | Túnel de vento, faixa de velocidade, nº de pontos, ajuste da calibração |
| "Coeficientes Mecânicos" (Cap. 4) | Tabela de patamares, $B$, $\tau_c$, $J$ |
| `sec:resultado_validacao_siba` | Bateria, células de carga, Pitot — validação completa da cadeia SIBA |
| `sec:resultado_calibracao_canal_same` | Ganho/offset por canal, erro vs. referência, piso de ruído |
| `sec:resultado_sensor_corrente` | Ganho do sensor calibrado na topologia $N = 1$ com fonte de $\pm 60$ A |

---

## Notas gerais

- `PacotesBasicos.tex` ganhou `\DeclareSIUnit{\gauss}{G}` (usado nas Ações
  10 e 15) — próximas ações que usem `\SI{...}{\gauss}` não precisam
  redeclarar.
- Datasheet do núcleo NT-27/16/12-100-IP6 não foi localizado como PDF no
  repositório nem publicamente por busca. A citação passou a usar
  `thornton_nucleos` (catálogo web de toroidais do fabricante) para os
  parâmetros $A_l$, $A_e$ e $L_e$, com McLyman mantido para a teoria de
  concentração de fluxo. Se o autor tiver o PDF do fornecedor (cotação própria
  mencionada no texto), substituir por referência mais específica.
- Os valores de $A_e = 64{,}5$ mm² e $L_e = 64{,}6$ mm usados na Eq. `eq:mur`
  vieram do catálogo, não de medição do núcleo em mãos. A geometria nominal
  (27/16/12 mm) daria $A_e = 66$ mm² e $L_e = 67{,}5$ mm — a diferença é o
  desconto de área efetiva do fabricante, e é a origem admitida da tolerância
  de ±15 % de $A_l$.

---

## ✅ Instrumentação dos ensaios com o motor em rotação (nova subseção)

Escrita a subseção `sec:resultado_setup_scope`, em `Textuais/Conclusao.tex`,
imediatamente antes de "Coeficientes Mecânicos". Descreve o setup
improvisado de aquisição, sem transdutor de torque:

- **Osciloscópio PicoScope Série 5000** do laboratório do PET, com o
  programa de aquisição do próprio fabricante.
- **Quatro canais analógicos + dois digitais**, e essa contagem é que
  definiu o que foi medido: duas tensões de fase e duas correntes de fase
  no analógico; comando do gerador de pulsos e tacômetro óptico no
  digital.
- Correntes lidas pela saída em tensão dos **sensores de efeito Hall da
  própria placa**.
- Terceira tensão e terceira corrente **não medidas** — reconstruídas na
  análise pela soma nula das três (enrolamento equilibrado, sem retorno
  pelo neutro).
- **Alimentação por bateria**: barramento flutuante, sem laço de terra com
  a referência das pontas de prova, e sem a ondulação da retificação
  somada às tensões medidas.
- **Neutro virtual**: o motor é ligado em estrela e não traz o neutro para
  fora, então ele foi refeito com três resistores iguais, um por fase,
  unidos em um nó comum, que serve de referência às pontas de prova.
  Citação nova no `.bib`: `nidec_neutral_point` (glossário Nidec).

### Figuras criadas (as duas em espaço reservado, tracejadas)

| label | falta |
|---|---|
| `fig:setup_picoscope_sw` | captura de tela do programa do PicoScope com os 4 canais analógicos e os 2 digitais |
| `fig:setup_picoscope_fisico` | fotografia da montagem: motor, bateria, os três resistores do neutro virtual, placa de medição e pontas de prova |

### Precisa de revisão do autor

- **Valor dos resistores do neutro virtual não está no texto** — foi escrito
  apenas que é "alto frente à impedância de fase". Se o autor souber o valor
  usado, colocar o número.
- Confirmar que a reconstrução da terceira fase (soma nula) é mesmo o que o
  pipeline de análise faz, e não uma terceira ponta que eu não conheço.
- Confirmar o modelo exato do PicoScope (só "Série 5000" foi informado); se
  houver o número completo (ex.: 5444D MSO), vale trocar.

---

## ✅ Resultados dos ensaios de bancada (três seções escritas)

Tudo em `Textuais/Conclusao.tex`. Compilado limpo (pdflatex + bibtex + 2x
pdflatex): zero referência ou citação indefinida.

### 1. Coeficientes Mecânicos — `sec:resultado_coef_mec`

Saiu do placeholder. O que entrou:

- 19 patamares, de 130 a 705 rad/s, com o motor em vazio na montagem
  improvisada (liga com `sec:resultado_setup_scope`).
- Torque pelo **balanço de potência por fase** — dito explicitamente que não
  há transdutor de torque e que o cálculo não depende de forma de onda, $K_e$
  nem ângulo, logo é independente do observador.
- $B = (32{,}4 \pm 4{,}4)\times10^{-6}$ N·m·s/rad e
  $\tau_c = (15{,}5 \pm 2{,}4)$ mN·m, $R^2 = 0{,}925$ (eq.
  `eq:resultado_b_tauc`, label mantido).
- Parágrafo explicando que o $\tau_c$ alto é **histerese no ferro**, não
  resíduo — $\tau_c$ agrega atrito seco + histerese, $B$ agrega viscoso +
  ventilação + corrente parasita.
- Validação do tacômetro: $-7{,}9\times10^{-4}\,\%$ ± $3{,}7\times10^{-2}\,\%$
  sobre 267 registros → um pulso por volta e sete pares de polos.
- $K_e$ medido equivale a 310 RPM/V, igual à placa.
- **Erro da simulação registrado**: o autotuning partiu do $K_v$ de linha
  tratado como de fase, então usou $K_e$ 3× maior; texto diz que os ganhos
  seguem válidos (otimização é sobre erro de seguimento, não escala de
  torque) e que o valor bom para o firmware é o de bancada.

Figuras: `bancada_te_omega`, `bancada_te_residuo`, `bancada_tacometro`,
`bancada_ke`. Aponta para `tab:bancada_patamares` do apêndice.

**Removido**: o ensaio de momento de inércia. Sumiram a
`\subsubsection{Momento de Inércia}` da metodologia (já tinha saído),
a figura `fig:coastdown`, a tabela placeholder `tab:resultado_atrito`, e as
duas remissões órfãs a `sec:momento_inercia` na Conclusão e na lista de
pendências. $J$ não aparece mais em lugar nenhum.

### 2. Projeto das Frequências de Corte — `sec:resultado_fc`

Nova subseção, logo após a síntese da simulação do SMO.

- Ruído do sensor sem filtro: 719 mA rms, ~15× o degrau do conversor (≈4 bits
  da palavra) → filtrar é condição, não refinamento.
- Varredura mostra o observador **insensível** de 3 kHz para cima, porque ele
  próprio é passa-baixas (estimador de FEM com polo em 1,06 kHz, sincronismo
  em 150 Hz). Logo o corte não se justifica por desempenho do observador.
- O que decide: **rebatimento** no conversor e **faixa dinâmica**.
- Projeto: **20 kHz, um polo, iguais nos dois ramos** — a igualdade é o ponto,
  porque o atraso comum se cancela no erro de fase tensão×corrente.

Figuras: `bancada_espectros`, `bancada_restricoes_fc`. Tabelas do apêndice
citadas: `tab:bancada_varredura_fc`, `tab:bancada_restricoes_fc`.

### 3. Observador nos Registros de Bancada — `sec:resultado_smo_bancada`

- Velocidade: 0,092 % de erro médio.
- Torque: **−7,2 %** com a corrente **medida** projetada no ângulo estimado;
  **+71 %** com a corrente do modelo. Conclusão prática dita no texto:
  projetar a medida, a do modelo serve só à realimentação interna.
- Ângulo dentro de 0,3° de média contra a integral dos pulsos do tacômetro,
  em registro de 0,5 s.
- Limite de baixa rotação: sincronismo a 150 Hz, abaixo de ~1930 RPM o ângulo
  não trava; velocidade continua saindo, torque não.

Figuras: `bancada_smo_alfabeta`, `bancada_smo_rpm_torque`,
`bancada_smo_angulo_tacometro`. Tabela `tab:bancada_smo`.

### Precisa de revisão do autor

- **Apêndice da bancada mantido** — as três seções citam quatro das suas seis
  tabelas (`tab:bancada_patamares`, `tab:bancada_restricoes_fc`,
  `tab:bancada_varredura_fc`, `tab:bancada_smo`). Ficaram sem citação
  `tab:bancada_inventario` e `tab:bancada_parametros`; se o autor quiser
  enxugar o apêndice, são essas duas as candidatas.
- A afirmação de que **os ganhos do autotuning seguem válidos apesar do $K_e$
  3× maior** é interpretação minha (a otimização é sobre erro de seguimento).
  Vale conferir se o autor concorda ou se prefere rodar o autotuning de novo
  com o $K_e$ de bancada.
- A frase de fechamento da simulação foi reescrita: antes dizia "restando a
  validação experimental em bancada como etapa subsequente", agora anuncia as
  duas subseções experimentais que vêm em seguida.
- Não foram escritas seções para as figuras que o autor apagou
  (`bancada_te_vs_fc`, `bancada_comando_rpm`, `bancada_smo_vs_fc`,
  `bancada_smo_rpm`, `bancada_smo_angulo_patamares`) — o conteúdo delas ou
  ficou como frase no texto (o caso da varredura de $f_c$) ou saiu de vez.

---

## ✅ Apêndice B enxugado e renomeado

`PosTextuais/Apendice_Bancada.tex`. Compilado e conferido na página do PDF.

- **Título**: "Dados dos ensaios de bancada do motor" → **"Dados do motor a
  vazio"** (Apêndice B). O parágrafo de abertura acompanhou a troca. O label
  `apendice:bancada` foi mantido, então a remissão em
  `sec:resultado_coef_mec` continua válida.
- **Tabela de inventário das sessões de captura removida** por inteiro
  (`tab:bancada_inventario`). Não era citada em lugar nenhum do texto. As
  tabelas seguintes subiram um número: a de restrições, que era a 20, agora
  é a **19**.
- **Tabela de restrições sobre $f_c$ reformatada** — o cabeçalho de uma
  linha só estourava a margem direita. Agora são duas linhas (grandeza em
  cima, unidade embaixo), com as duas colunas de ruído agrupadas sob um
  `\multicolumn` "Ruído $\sigma_i$" com `\cmidrule`. O que saiu do
  cabeçalho foi para a nota de rodapé da tabela: o atraso é medido em
  $f_{e,\max} = \SI{805}{\hertz}$ e a atenuação em \SI{8}{\kilo\hertz}.
  A tabela agora fecha dentro da margem.

### Precisa de revisão do autor

- **O arquivo é gerado por `p4_apendice.py`** (cabeçalho do próprio `.tex`
  avisa "não editar a mão"). O script não está nesta máquina, então as três
  mudanças foram feitas direto no `.tex`. **Se o script rodar de novo, elas
  se perdem** — as mesmas alterações precisam ser levadas para
  `p4_apendice.py`.
- `tab:bancada_parametros` continua sem citação no corpo do texto; é a única
  que sobrou nessa situação depois da remoção do inventário.

---

## ✅ Notas de rodapé das tabelas do Apêndice B viraram texto

As cinco tabelas do apêndice traziam, entre a tabela e a linha de fonte, uma
nota em corpo menor explicando símbolos, unidades e critérios de cálculo.
Todas foram **removidas dali** e reescritas como **parágrafo corrido antes de
cada tabela**, em norma culta e citando a tabela por `\autoref`. Abaixo da
tabela ficou só `Fonte: Autor (2026)`.

O que virou texto, tabela por tabela:

| tabela | conteúdo movido para o parágrafo de abertura |
|---|---|
| `tab:bancada_patamares` | $E_1$ e $I_1$ como amplitudes de pico da fundamental, a fórmula de $P_{conv}$ e de $T_e$, o significado de $u(T_e)$ e de $N$ |
| `tab:bancada_parametros` | incertezas como intervalos de 95 % da regressão; densidade de ruído como mediana do patamar branco acima de 300 kHz |
| `tab:bancada_restricoes_fc` | filtro de um polo; atraso medido em $f_{e,\max}$; atenuação medida em 8 kHz; definição do ganho de dobramento |
| `tab:bancada_varredura_fc` | cinco registros de 2267 a 6724 RPM, filtros casados, médias sobre número inteiro de períodos elétricos após o travamento do PLL |
| `tab:bancada_smo` | unidades das colunas e a distinção entre $\hat T_e(i)$ (corrente medida) e $\hat T_e(\hat\imath)$ (corrente do modelo); definição de $r_{v_c}$ |

Efeito colateral bom: `tab:bancada_parametros`, que estava sem citação
nenhuma, agora é citada pelo próprio parágrafo que a antecede. Todas as cinco
tabelas do apêndice passam a ser citadas em texto.

Compilado e conferido nas páginas renderizadas do PDF: nenhuma nota sobrou
sob as tabelas, e as remissões continuam resolvidas.

### Precisa de revisão do autor

- Vale a mesma ressalva de antes: o `.tex` é gerado por `p4_apendice.py`, que
  não está nesta máquina. Se o script rodar de novo, as notas voltam para
  baixo das tabelas — a mudança precisa ser levada para o gerador.

---

## ✅ LM358 e atraso de fase do condicionamento removidos

`Textuais/Conclusao.tex`. Compilado limpo, sem referência indefinida.

Na subseção **Condicionamento Analógico e seu Impacto na Reconstrução**
(`sec:condicionamento`) saíram:

- o parágrafo que atribuía ao **LM358** um modelo de polo dominante em
  \SI{45,2}{\kilo\hertz} e \SI{41,5}{\degree} de atraso em
  \SI{40}{\kilo\hertz};
- a **tabela `tab:fase_ampop`** (atraso de fase e atenuação por frequência);
- o parágrafo dos **três cenários de inconsistência de fase** (tensão
  atrasada com corrente ideal, ambos atrasados, ambos com compensação
  proporcional à velocidade).

A subseção ficou com um fator só — a queda da amplitude da FEM com a rotação
e a perda de relação sinal-ruído em baixa velocidade —, e o parágrafo de
abertura foi reescrito de "dois fatores físicos" para "um fator físico".

Fora da subseção, na seção adiante:

- o parágrafo da seção do **sensor de corrente** que comparava o polo de
  \SI{56}{\kilo\hertz} do caminho de corrente com o de
  \SI{45,2}{\kilo\hertz} do estágio de tensão foi removido inteiro;
- a **síntese da simulação** não credita mais ao condicionamento a
  verificação de tolerância do observador (frase do LM358 apagada);
- a **lista de objetivos da Conclusão** não fala mais em "efeito do
  condicionamento analógico sobre a reconstrução".

Zero ocorrência de `LM358`, de `45,2` e de `tab:fase_ampop` no arquivo.

### Precisa de revisão do autor

- O título da subseção continua **"Condicionamento Analógico e seu Impacto na
  Reconstrução"**, mas o conteúdo que sobrou trata da amplitude da FEM, não do
  estágio analógico. Se quiser, dá para renomear para algo como "Limite de
  Baixa Rotação na Reconstrução" — não fiz porque `sec:condicionamento` é
  citada em três pontos do texto e a troca de título não foi pedida.

---

## ✅ Trecho da corrente do modelo apagado e remissões ao Apêndice B

`Textuais/Conclusao.tex`. Compilado limpo.

- **Apagado** o trecho de `sec:resultado_smo_bancada` que comparava o torque
  estimado com a corrente do modelo (o \SI{+71}{\percent}) e concluía que a
  estimação deve projetar a corrente medida. Ficou só o resultado com a
  corrente medida (\SI{-7,2}{\percent}), seguido da remissão à tabela do
  apêndice.
- A coluna $\hat T_e(\hat\imath)$ **continua na tabela do apêndice**
  (`tab:bancada_smo`) como dado bruto, com a definição no parágrafo que a
  antecede — só a análise saiu do capítulo.

### Remissões ao apêndice acrescentadas

Cada afirmação numérica das três seções agora aponta para a tabela de onde o
número saiu:

| onde | passou a citar |
|---|---|
| perdas mecânicas | já citava `tab:bancada_patamares` |
| validação do tacômetro e $K_e$ | `tab:bancada_parametros` |
| ruído de 719 mA rms | `tab:bancada_parametros` |
| insensibilidade do observador ao corte | `tab:bancada_varredura_fc` |
| faixa admissível de $f_c$ | `tab:bancada_restricoes_fc` |
| observador contra medida | `tab:bancada_smo` |

Todas nomeando o `\autoref{apendice:bancada}` junto, para deixar explícito
de onde vêm os dados experimentais.

---

## ✅ 4.5.1 saiu de placeholder: Calibração de Canal

`Textuais/Conclusao.tex`. Compilado e conferido na página renderizada.

A seção era duplicata do que a bancada do PicoScope já mostrava (formas de
onda reais) e o resto estava pendente. Foi reescrita com os dados de
calibração da SAME, que já existiam no arquivo de configuração:

- **Reta afim por canal**: valor físico = escala × tensão no pino do
  conversor + deslocamento de zero, com os dois números vindo do projeto da
  cadeia e vivendo no arquivo de configuração compartilhado entre
  \textit{firmware} e supervisão. Descrito em português, sem nome de campo
  nem de constante (regra 7).
- **Corrente**: \SI{18,750}{\milli\volt\per\ampere} do sensor com uma
  passagem pelo furo, divisor 2,0 k / 18 k atenuando 0,900 (porque o repouso
  em 2,5 V mais 60 A chegaria a 3,625 V, acima dos 3,3 V do conversor),
  cadeia em \SI{16,875}{\milli\volt\per\ampere},
  \SI{47,7}{\milli\ampere} por degrau.
- **Tensão**: divisor 130 k / 7,5 k, atenuação 0,0545455, fundo de escala
  60,5 V (dobro do barramento), \SI{14,8}{\milli\volt} por degrau.
- **Tabela `tab:resultado_calibracao_canal_same` refeita**: era placeholder
  com "Pendente" nas células, agora traz atenuação, escala da cadeia, degrau
  do ADC e faixa válida dos dois caminhos. Um parágrafo explica que a faixa
  válida é critério de diagnóstico (saturado, desconectado, terra aberto),
  não folga.
- **Zero levantado a cada energização**: os três repousos a 0 A do
  \autoref{apendice:ensaio_hall} (2,48570 / 2,48640 / 2,49150 V) dão 5,8 mV
  de espalhamento, ou 0,31 A de corrente inexistente; por ser comum às três
  fases, atravessa a Clarke como sequência zero e vira FEM espúria no
  observador. Daí o firmware medir o repouso real no boot.
- **Ressalva do ensaio improvisado**: as tensões de fase foram medidas direto
  pelas pontas do osciloscópio, **sem o divisor da placa** — então a cadeia
  de corrente foi exercitada com o motor girando, a de tensão continua
  verificada só pelo projeto.
- **Figura de formas de onda apagada** (`fig:formas_onda_reais`, era caixa
  tracejada). O texto passou a apontar para a
  `fig:bancada_smo_alfabeta`, que já mostra tensão e corrente reais.

### Conclusão ajustada

- O objetivo do sistema de medição elétrica passou de "parcialmente atingido"
  para **"majoritariamente atingido"**, já que a escala dos canais fechou.
- A lista de pendências não fala mais em "calibração de canal"; o que sobrou
  é **piso de ruído com o ESC chaveando** e **verificação da cadeia de tensão
  contra instrumento de referência**, com a ressalva do divisor não
  exercitado.

### Precisa de revisão do autor

- Os números da cadeia vieram do arquivo de configuração do projeto, não de
  um ensaio de calibração de canal. O texto diz isso ("saem do projeto da
  cadeia"), mas convém confirmar se o orientador aceita apresentar escala de
  projeto em capítulo de resultado, ou se prefere que isso migre para a
  metodologia e a seção fique só com o que foi medido.
- A escala de tensão foi expressa como \SI{54,5}{\milli\volt\per\volt}
  na tabela (o inverso da atenuação de 0,0545455 é 18,33 V/V). Conferir se
  essa é a forma que o autor prefere ler.

---

## ✅ Conclusão atualizada para o estado real do trabalho

`Textuais/Conclusao.tex`, seções **Conclusão** e **Limitações e Itens
Pendentes**. Compilado limpo.

O texto ainda dizia que a validação física em bancada não tinha sido
executada, o que ficou falso depois dos ensaios com o motor. O que mudou:

- **Parágrafo de síntese reescrito.** Agora afirma que a cadeia de medição
  elétrica foi percorrida com o motor real, na montagem improvisada com
  osciloscópio, e que dali saíram os coeficientes de perdas, a constante de
  FEM, o critério das frequências de corte e a validação do observador contra
  medidas independentes.
- **O que falta ficou nomeado com precisão**: o conjunto completo
  instrumentado, com hélice acoplada, células de carga e tubo de Pitot em
  operação simultânea sob supervisão da SIBA. O texto diz explicitamente que
  essa etapa está prevista para o **segundo semestre de 2026** e que é ela, e
  não a ausência de ensaio com o motor, que separa os objetivos "atingidos"
  dos "parcialmente atingidos".
- **Lista de pendências**: ganhou a frase de que todos os itens estão
  programados para o segundo semestre de 2026, dentro do prazo de fechamento,
  e ganhou um item novo — **montagem final do conjunto na estrutura da
  bancada** (`sec:resultado_montagem_final`), que tinha seção reservada mas
  não aparecia na lista.
- **Remissão a `human.md` apagada do corpo do trabalho.** O parágrafo de
  encerramento das pendências mandava o leitor consultar "`human.md` na raiz
  do repositório" — arquivo de trabalho interno, que não cabe no texto
  entregue.

### Precisa de revisão do autor

- Escrevi "**segundo semestre de 2026**" em dois pontos. Se o prazo for outro
  (defesa em data específica, ou ensaio marcado), é trocar nos dois lugares.
- O objetivo 2 está declarado **atingido**. Ele não menciona o erro do $K_e$
  na simulação, que está descrito em `sec:resultado_coef_mec`. Se o
  orientador achar que a Conclusão deve registrar essa correção, cabe uma
  frase ali.

---

## ✅ Agradecimento ao Alexandre

`PreTextuais/Agradecimentos.tex`, segundo parágrafo. A frase solta que estava
grudada no fim do parágrafo ("também deixo meu agradecimento especial ao
Alexandre que me ajudou a tornar alguns ensaios mais rápidos") virou frase
própria, com o papel dele nomeado:

> Agradeço, em especial, ao Alexandre, membro do setor de elétrica da equipe
> Albatroz, pela ajuda na montagem do sistema de aquisição e por agilizar a
> preparação dos ensaios.

Usei "Agradeço, em especial" para não repetir "Deixo um agradecimento
especial", que já abre o mesmo parágrafo.

### Precisa de revisão do autor

- Ficou só o primeiro nome. Se for para constar o sobrenome, é só dizer qual.

---

## ✅ Apêndice B reduzido a três tabelas

`PosTextuais/Apendice_Bancada.tex`. Compilado e conferido nas páginas
renderizadas.

- **Parágrafo de abertura do apêndice apagado** (o que descrevia os 743
  registros e os canais adquiridos). O capítulo agora começa direto no
  parágrafo da primeira tabela.
- **Tabela de restrições numéricas sobre $f_c$ removida**
  (`tab:bancada_restricoes_fc`), junto com o parágrafo que a apresentava. A
  informação continua no capítulo pela `fig:bancada_restricoes_fc`.
- **Tabela de qualidade do observador contra $f_c$ removida**
  (`tab:bancada_varredura_fc`), junto com o parágrafo dela. A conclusão que
  importava --- observador insensível ao corte de 3 kHz para cima ---
  continua afirmada no texto de `sec:resultado_fc`, agora sem remissão a
  tabela.
- As duas remissões correspondentes em `Textuais/Conclusao.tex` foram
  retiradas; nenhum `\autoref` órfão sobrou.
- **Unidades acrescentadas ao cabeçalho** da tabela do observador
  (`tab:bancada_smo`), em segunda linha: RPM nas rotações,
  \si{\milli\newton\meter} nos torques e \si{\percent} nos erros e no
  resíduo. O parágrafo que antecede a tabela deixou de listar as unidades e
  ficou só com a definição de $\hat T_e(i)$, $\hat T_e(\hat\imath)$ e
  $r_{v_c}$.

O apêndice ficou com três tabelas: pontos experimentais da regressão
(Tabela 16), parâmetros levantados (Tabela 17) e observador contra a medida
direta (Tabela 18).

---

## ✅ Dimensionamento do filtro passa-baixas removido da metodologia

`Textuais/Metodologia/08_SAME.tex`. Compilado limpo, sem referência
indefinida. Fica valendo a divisão: a metodologia mostra a topologia e a
relação entre $C$ e $f_c$; o valor de $f_c$ e sua justificativa são resultado
(`sec:resultado_fc`).

**Caminho de tensão** (`sec:same_divisor_tensao`):

- Capacitor da `fig:cond_tensao` **sem valor** no esquema — só `$C$`. O
  \SI{560}{\pico\farad} saiu.
- Apagada a frase "Com $C = 560$ pF a frequência de corte resulta em
  \SI{40,1}{\kilo\hertz}". No lugar entrou um parágrafo dizendo que o
  capacitor fica em aberto, que ele é o único grau de liberdade restante uma
  vez fixadas as resistências, e que a escolha depende de ruído medido,
  rebatimento e banda útil do observador — remetendo à
  `\autoref{sec:resultado_fc}`.
- **Apagados** o parágrafo do rebatimento da 28ª harmônica (336 kHz → 3 kHz,
  −28,9 dB → −47,5 dB) e a **`fig:alias_tensao`** inteira (o gráfico TikZ das
  harmônicas do PWM).

**Caminho de corrente** (`sec:same_sensor_corrente`):

- Capacitor da `fig:cond_corrente` **sem valor**. O \SI{2,2}{\nano\farad}
  saiu.
- Apagada a frase do $f_c = \SI{40,2}{\kilo\hertz}$. Ficou registrado que
  os dois ramos recebem **a mesma** frequência de corte, para o atraso ser
  comum e se cancelar na comparação tensão×corrente, com o valor justificado
  nos resultados.
- **Apagados** o parágrafo da atenuação em Nyquist (−9,9 → −22,5 dB, −0,37 dB
  em 12 kHz, −63,1 dB na 28ª) e a **`fig:alias_corrente`** inteira.
- O parágrafo das duas correntes perdeu os números que dependiam de $C$ (os
  \SI{183}{\micro\ampere} de pico e o total de
  \SI{364}{\micro\ampere}); ficou o critério: os
  \SI{181}{\micro\ampere} do divisor mais o pico do capacitor têm de somar
  menos que a corrente de saída mínima de catálogo, \SI{1}{\milli\ampere}.
- A frase do polo do sensor (\SI{56}{\kilo\hertz}) não aponta mais para
  `sec:condicionamento` (que perdeu a discussão de polos com a saída do
  LM358); aponta para `sec:resultado_fc`, dizendo que esse polo se soma ao do
  filtro na escolha do corte.

O que **ficou** na metodologia: topologia dos dois divisores, valores de
$R_1$ e $R_2$, atenuação, fundo de escala, resolução por LSB, resistência
equivalente contra o limite da tabela do ADC, erro de ganho de amostragem e
consumo do divisor.

### Precisa de revisão do autor

- Duas figuras a menos na lista de figuras. Se alguma delas fosse citada em
  banca como parte do dimensionamento, o conteúdo está no
  `bancada_espectros` e no `bancada_restricoes_fc` do Capítulo 4.
- A equação de $f_c$ (`eq:cond_fc`) **continua** na metodologia, sem valor
  numérico associado — é ela que liga $C$ ao corte definido nos resultados.

---

## ✅ Bloco do ensaio do sensor Hall quebrado em parágrafos + diagrama

`Textuais/Metodologia/08_SAME.tex`, seção do sensor de corrente com núcleo
toroidal. Era um parágrafo único e enorme descrevendo objetivo, artifício das
$N$ espiras, três configurações, instrumentação, faixa varrida e tratamento da
deriva. Ficou assim:

1. **Objetivo e artifício** — levantar $V_{out}(B)$ e o porquê das $N$
   espiras (a instrumentação não gera corrente da ordem da aplicação).
2. **Montagem e configurações** — remete à figura nova; fonte Minipa em
   limitação de corrente, multímetros de corrente e de tensão, e as três
   configurações ($N = 1$ pelo furo, $N = 4$ e $N = 8$ externas).
3. **Faixa varrida** — 0 a \SI{5,33}{\ampere}, \SI{516}{\gauss} na
   configuração de oito espiras.
4. **Deriva e $\Delta V$** — por que se adota
   $\Delta V = V_{out} - V_O$, com remissão ao Apêndice A.

### Figura nova: `fig:ensaio_hall_setup`

Desenhada em TikZ, sem imagem externa. Duas partes:

- **Cadeia de medida** em blocos: fonte Minipa $\to$ multímetro de corrente
  $\to$ conjunto núcleo--sensor $\to$ multímetro de tensão, com a
  alimentação de \SI{5}{\volt} entrando por cima no conjunto.
- **Três toróides** lado a lado: $N = 1$ com o condutor representado pelo
  símbolo de corrente saindo do plano no furo central, $N = 4$ e $N = 8$ com
  as espiras desenhadas cruzando o anel. Em todos, o elemento Hall aparece
  como retângulo cinza no entreferro. Nota abaixo lembrando que o campo é
  proporcional a $N\,I$.

Citada no corpo do texto, com caption acima e fonte abaixo, no padrão do
capítulo. Conferida na página renderizada.

### Precisa de revisão do autor

- O desenho dos toróides é esquemático: as espiras aparecem como traços
  cruzando o anel, e o número de traços é o $N$ de cada configuração. Se o
  autor preferir uma foto real da montagem no lugar do desenho, é trocar o
  bloco TikZ por `\includegraphics`.

---

## ✅ Duas fotos de ensaio entraram; dois espaços reservados saíram

`Textuais/Conclusao.tex`. Compilado e conferido nas páginas renderizadas.

**Fotos que entraram** (as duas que o autor adicionou em
`Textuais/Imgs/Resultados/`):

| figura | arquivo | onde |
|---|---|---|
| `fig:setup_picoscope_fisico` | `ensaio_motor_vazio.jpg` | substituiu a caixa tracejada da montagem física do ensaio com o motor, em `sec:resultado_setup_scope` |
| `fig:ensaio_hall_foto` | `ensaio_sensor_corrente.jpg` | figura **nova** em `sec:resultado_sensor_corrente`, citada no parágrafo de abertura da seção, antes do gráfico $V_{out}(I)$ |

A foto do sensor é retrato, então entrou com `width=0.5\linewidth`; a do
motor é paisagem, com `0.85\linewidth`. Caption acima e fonte abaixo, no
padrão do capítulo.

**Espaços reservados apagados** (não há foto e não se fala mais neles):

- `fig:setup_kelvin` — montagem do ensaio de resistência por quatro
  terminais. A frase da subseção Resistência perdeu o trecho "na montagem da
  \autoref{...}"; o método continua descrito em texto.
- `fig:setup_ressonancia` — montagem do ensaio de ressonância. Mesma coisa na
  subseção Indutância.

Nenhuma remissão órfã sobrou.

**Captura de tela do PicoScope**: entrou logo em seguida, com
`picoscope_software.png`, substituindo a última caixa tracejada dessa seção
(`fig:setup_picoscope_sw`, `width=0.9\linewidth`). A seção
`sec:resultado_setup_scope` está agora sem nenhum espaço reservado.

---

## ✅ Correção do $K_v$, simulação refeita e reordenação do capítulo

Rodada de correção lógica pedida pelo autor. Compilado limpo (pdflatex +
bibtex + 2× pdflatex): zero referência ou citação indefinida.

### 1. Ordem das seções — simulação antes do ensaio

Criada a `\section{Ensaios de Bancada com o Motor}` (`sec:ensaios_bancada`),
posicionada **depois** de toda a simulação do SMO e antes de "Modelagem
Física". Para lá foram movidas as quatro subseções de bancada:
instrumentação, coeficientes mecânicos, frequências de corte e observador nos
registros. Assim some a remissão para a frente que existia (a seção de
coeficientes mecânicos citava o autotuning, que vinha depois).

### 2. $K_v$ — 310 RPM/V já é de linha

Correção do autor: **não** se divide por $\sqrt{3}$.

- `Scripts/test_coeff_auto.py` (em `Codigos/Aplicacao_Pi/Scripts/`): a linha
  `Kv = 310.0/math.sqrt(3)` virou `Kv = 310.0`. **Foi a única linha alterada
  no código.**
- **Tabela 9** (parâmetros da simulação, `tab:parametros_simulacao`):
  $K_v = \frac{310}{\sqrt{3}}$ → $K_v = 310$ RPM/V.
- O parágrafo que acusava "erro na simulação, constante de FEM 3× maior que a
  medida" foi substituído por outro que registra a convenção correta e diz que
  a simulação foi refeita.
- Varri o resto do documento: as outras menções a 310 são o nome do motor e o
  $K_v$ implícito do apêndice, ambas coerentes. Nenhuma outra conversão por
  $\sqrt{3}$ ligada a $K_e/K_v$ no texto.

### 3. Autotuning rodado de novo e figuras trocadas

`autotune_cost.py --plot`, evolução diferencial, 1278 avaliações. Resultado:

| grandeza | antes (texto) | agora |
|---|---|---|
| custo inicial | 1,47 | **1,99** |
| custo final | 0,32 | **1,16** |
| $\Gamma_{s1}$ | 28,72 | **31,06** |
| $\Gamma_{s2}$ | −8,43 | **−5,94** |
| $\Gamma_t$ | 0,0264 | **0,574** |
| $K_t$ @ 10 µs | 2,64·10³ | **5,74·10⁴** |

Figuras substituídas em `Textuais/Imgs/Desenvolvimento/`:
`Otim_convergencia.png`, `Otim_sensibilidade.png`, `Torque_rpm.png`,
`Eixo_alpha_beta.png`, `PLL_angulos.png`, `PI_controle_Inversor.png` e
`FFT_comparacao.png` (esta última ainda não é usada em nenhum
`\includegraphics`).

O texto do autotuning foi reescrito **porque as figuras novas contam outra
história**:

- A convergência não cai "mais de uma ordem de grandeza": cai de 1,99 para
  1,16 nas primeiras dezenas de avaliações e fica no patamar. O texto agora
  diz isso, e explica os picos de 10³–10⁴ como combinações divergentes
  visitadas pela busca.
- A sensibilidade **não mostra mínimo bem definido** em nenhum dos três
  ganhos: mostra um platô à esquerda e uma parede à direita, com o ótimo no
  fim do platô. O texto passou a descrever isso — a busca devolve o maior
  ganho ainda estável, não um mínimo pontual.
- Saiu a afirmação de que a otimização foi repetida para
  $T_s \in \{2, 10, 20\}\,\mu$s (não foi refeita nesta rodada); ficou só a
  justificativa estrutural de otimizar sobre $\Gamma = K T_s$.

### 4. Duas figuras estavam sem citação no corpo

`fig:desempenho_mecanico` (Torque_rpm) e `fig:eixos_estaticos`
(Eixo_alpha_beta) apareciam sem nenhuma menção no texto, contra a regra 3.
Foi escrito um parágrafo que cita as duas e comenta o que se vê: rotação com
atraso nas transições, torque estimado no nível médio com ondulação grande, e
o \textit{chattering} em torno das grandezas reais.

### 5. Dois ESCs no texto do setup

Acrescentado que o acionamento coube a dois controladores em sessões
diferentes: **Castle Talon 90 A** e **Flycolor 150 A** (são as colunas ESC
A/B do apêndice).

### 6. Afirmações fortes removidas

Oito trechos suavizados nos resultados de bancada: "confirma" → "é compatível
com"; "a explicação é" → "uma explicação plausível"; "é a exigência mais
importante" → "é o ponto de atenção"; "que define onde o corte precisa ficar"
→ "que orienta a escolha do corte"; e afins. Mesma régua aplicada ao texto
novo do autotuning.

### Precisa de revisão do autor

- **O custo final piorou** em relação ao número que estava no texto (1,16
  contra 0,32). Os dois não são comparáveis diretamente: o valor antigo vinha
  de uma configuração anterior da função-custo, e o $K_v$ mudou. Vale decidir
  se quer que eu investigue ou se o número atual serve.
- $K_t$ subiu de 2,64·10³ para 5,74·10⁴ — vinte vezes. É o polo do estimador;
  conferir se faz sentido para o firmware.
- `FFT_comparacao.png` foi regerada mas não é usada em nenhuma figura do
  documento. Se for para entrar, falta escrever a seção.
- O ADC_amostragem/ADC_ideal_vs_amostrado **não** foram regerados (só saem
  rodando `test_coeff_auto.py` direto, não pelo autotune).

---

## ✅ Revisão crítica — cortes e condensações (08/09/2026)

Aplicadas as mudanças não-ambíguas de uma revisão crítica do documento.
Cinco arquivos alterados:

### 1. `Conclusao.tex` — parágrafo histórico do $K_v$ removido

O parágrafo que narrava o erro da simulação ("a simulação inicialmente
convertia dividindo por $\sqrt{3}$...") foi removido por inteiro. O erro
já foi corrigido e o texto publicado está correto; narrar o histórico de
desenvolvimento não acrescenta resultado.

### 2. `Revisao.tex` — seção UDP condensada + subseção ARP removida

A seção "Protocolo de comunicação e telemetria" (antes ~1,5 páginas) foi
condensada para um único parágrafo que retém os fatos técnicos essenciais:
protocolo UDP, *overhead* de 8 bytes, MTU de 1500 bytes com payload
$\leq 1472$, endereçamento estático. As duas figuras (encapsulamento e
cabeçalho UDP) foram removidas junto com o texto que as comentava.
A subseção "Resolução de Endereçamento e Camada Física" (ARP + fig:IP_to_AC)
foi removida inteira — detalhe de camada 2/3 que não é procedimento de ensaio.

### 3. `02_MotorBLDC.tex` — nota de rodapé do $\tau_c$ ampliada

A nota que definia $\tau_c$ apenas como "atrito de Coulomb dos rolamentos"
foi corrigida para: "O parâmetro $\tau_c$ agrega atrito seco dos rolamentos
e perdas por histerese no ferro do estator; a regressão linear não os separa."
Isso antecipa na metodologia o que o capítulo de resultados já discute sobre
o valor alto de $\tau_c$.

### 4. `06_Bancada.tex` — quatro blocos removidos

Removidos por serem documentação de hardware, não procedimento de ensaio:
- Parágrafo de referência + placeholder `fig:precarga_osc` (captura de
  osciloscópio da pré-carga já documentada pelas equações da subseção)
- `\subsection{Caixa de Instrumentação}` + `fig:cad_caixa`
- `\subsection{Organização Interna}` + `fig:montagem_interna`
- `\subsection{Conexões Externas}` + `fig:painel_externo`

As quatro figuras também foram retiradas da tabela de pendências deste arquivo.

### Decisões em aberto (ver listagem abaixo para detalhes)

Seis itens da mesma revisão crítica têm OPÇÃO A/B ou precisam de confirmação
do autor antes de implementar. Ainda não aplicados.

---

## ✅ Sessão 2026-09-08 — decisões aplicadas + modularização dos capítulos

### Decisões 1-A, 2-A, 5, 6 — implementadas

**Decisão 1-A: Curva Característica de Torque removida**
Subseção inteira (`sec:motor_curvas`, `fig:tik_motor_bldc_ideal`) retirada de
`Revisao/00_MotorBLDC.tex`. Referência em `sec:helice_acoplamento` corrigida
(removido o trecho parentético "seu envelope..."). Referência em
`02_TrabFuturos.tex` corrigida (removido `\autoref{sec:motor_curvas}`).

**Decisão 2-A: Wheatstone condensado**
Figura `fig:ponte_wheatstone_paralelo` (circuitikz da ponte) removida de
`Revisao/02_Instrumentacao.tex`. Parágrafo que referenciava a figura
substituído por versão condensada de 3 linhas; equações `eq:gauge_factor_simples`
e `eq:wheatstone_simples` preservadas.

**Decisão 5: parágrafo de pipeline em Modelagem do SMO**
Adicionado após o cabeçalho da seção `\section{Modelagem do SMO}` em
`Resultados/01_ModelagemSMO.tex`. Descreve a sequência simulação → autotuning
→ bancada como motivação da seção.

**Decisão 6: valores de J removidos do autotuning**
Em `Resultados/01_ModelagemSMO.tex`, o trecho "O custo parte de J=1,99 no
ponto inicial e cai para J=1,16 nas primeiras dezenas de avaliações" foi
substituído por formulação qualitativa sem números absolutos do custo.

### Modularização: Revisao.tex e Conclusao.tex

Ambos os arquivos foram divididos no padrão de `Metodologia/` (um arquivo por
`\section`). Os arquivos-pai passaram a ser apenas capítulo + `\input`s.

**`Textuais/Revisao.tex`** → `\input` de:
- `Revisao/00_MotorBLDC.tex`
- `Revisao/01_Helice.tex`
- `Revisao/02_Instrumentacao.tex`
- `Revisao/03_UDP.tex`
- `Revisao/04_Zephyr.tex`

**`Textuais/Conclusao.tex`** → dois capítulos com `\input` de:
- `Resultados/00_MedicaoParametros.tex`
- `Resultados/01_ModelagemSMO.tex`
- `Resultados/02_EnsaiosBancada.tex`
- `Resultados/03_ModelagemFisica.tex`
- `Resultados/04_SIBA.tex`
- `Resultados/05_SAME.tex`
- `Conclusao/00_Conclusao.tex`
- `Conclusao/01_Limitacoes.tex`
- `Conclusao/02_TrabFuturos.tex`

Compilação final: **120 páginas, sem erros, sem referências indefinidas** para
as seções tocadas.

### Decisão 3 — deixada como está
Seção de Zephyr mantida sem corte, conforme instrução do autor.

### Decisão 4 — pendente
Condensação adicional de `sec:same_tensao` em `08_SAME.tex` (remoção das
equações de física magnética). Autor confirmou condensar "mais", mas a instrução
foi cortada antes de especificar até onde. Aguardando retomada.

---

## ✅ Rodada de correções contra o firmware e os dados (26/09/2026)

Oito tarefas independentes pedidas ao vivo, cada uma compilada em separado.
Todas fecharam com **zero referências indefinidas** e sem erro de bibtex.

### 1. Figura da estrutura genérica do SMO: `sign` → `Sat` + figura nova

Em `Revisao/00_MotorBLDC.tex`, o bloco `sign(·)` da `fig:smo_diagrama_bloco`
(Figura 5) e a equação dentro do bloco "Modelo Estimado" passaram a usar
`Sat(·)` — coerente com `eq:smo_modelo`, que já usava `Sat`. O nó TikZ foi
renomeado de `sign` para `sat`.

Criada a **`fig:funcoes_chaveamento`** (Figura 6, pgfplots): sinal, saturação
linear, tangente hiperbólica e sigmoide sobrepostas, com o erro normalizado
pela espessura $\epsilon$ da camada limite. Dois parágrafos acompanham: um
antes da figura (condição de admissibilidade — ímpar, limitada, monotônica) e
um depois (a função sinal é o caso limite e gera *chattering*; as contínuas
trocam a descontinuidade por rampa finita; a escolha desloca o compromisso
convergência × oscilação residual, sem mudar a estrutura do observador).

### 2. Equação 18 e notação de fase

- `eq:eq18_error_dyn`: a quarta linha era $\dot{\hat{e}}_\beta = K_{42}\,
  \text{Sat}(\tilde{i}_\alpha)$ — **corrigido para $\tilde{i}_\beta$**.
- Modelo elétrico por fase: a tensão agora é declarada como tensão de fase
  medida entre terminal e neutro, denotada $v_{an}$, $v_{bn}$, $v_{cn}$, e a
  `eq:bldc_eletrica` passou a escrever $v_{kn}$. Na `fig:bldc_equivalente`
  (Figura 3) os rótulos viraram $v_{an}(t)$/$v_{bn}(t)$/$v_{cn}(t)$ e as
  fontes de FEM, que estavam como $e_1$/$e_2$/$e_3$, viraram $e_a$/$e_b$/$e_c$.

**Escopo deliberadamente contido:** a seção de Clarke continua usando
$v_a$, $v_b$, $v_c$ — ali são tensões de terminal, e a matriz 2/3 elimina o
modo comum, de modo que a transformada vale para as duas convenções. **Se o
orientador quiser a notação `an/bn/cn` propagada até o Clarke, é uma decisão
a tomar** — não foi feita.

### 3. SMO promovido de subseção para seção

`\subsubsection{Observador por Modos Deslizantes (SMO)}` virou
`\section{...}`. Hoje é a **seção 2.2** (antes 2.1.1.1). Não houve órfão: o
conteúdo do SMO vai até o fim de `00_MotorBLDC.tex`. As demais seções do
capítulo 2 deslocaram-se para 2.3–2.6.

### 4. Figura da malha de estimação (simulação): `tanh` → `Sat`

`fig:diagrama_smo` em `Resultados/01_ModelagemSMO.tex`: o bloco "Função de
Chaveamento" mostrava $\tanh(e_i/8{,}0)$, agora mostra
$\text{Sat}(e_i/8{,}0)$. No `alg:smo`, o comentário que citava o nome da
constante de compilação da função de chaveamento foi trocado por uma remissão
à `fig:funcoes_chaveamento` (regra 7 do `CLAUDE.md`).

**Nota factual:** o código realmente roda com a tangente hiperbólica. O texto
agora usa `Sat` como notação genérica da família, o que é consistente — mas
**vale o orientador confirmar** se prefere nomear a forma efetivamente
implementada.

### 5. Seção 3.8.1 realinhada ao firmware

Trocado o parágrafo de abertura e o parágrafo de tempo de conversão em
`Metodologia/08_SAME.tex`. O que mudou, item a item:

| Estava | Está |
|---|---|
| ADCCLK de 14 MHz | divisor só vai a /8 sobre 144 MHz → 18 MHz; ponto de projeto de **12 MHz**, com o barramento de periféricos reduzido à metade |
| "1 MSps por ADC", "333 kSps por canal" | quadro de $(28{,}5+11)\times3 = 118{,}5$ ciclos = **9,875 µs → 101 kHz de taxa de quadro**, que é o $T_s$ do observador |
| ADC1 tensões / ADC2 correntes "de forma entrelançada" | **modo dual simultâneo**: cada par tensão–corrente da mesma fase é amostrado no mesmo instante; só o primeiro conversor tem linha de DMA, então os dois saem numa transferência de 32 bits |
| ESC a 12 kHz | **8 kHz** (bate com a Tabela de parâmetros do Apêndice B) |
| "folga para adotar $t_s$ elevado" | $t_s$ é o que **fixa** a taxa de quadro; 28,5 ciclos é o maior valor tabelado que mantém o quadro em 10 µs; o assentamento fica a cargo do capacitor do filtro *antialiasing* |
| `eq:cond_ganho`: 7,09 kΩ × 333 kHz × 8 pF = 1,89 % | 7,09 kΩ × **101 kHz** × 8 pF = **0,57 %** |
| `eq:cond_ganho_i`: 1,8 kΩ × 333 kHz × 8 pF = 0,48 % | 1,8 kΩ × **101 kHz** × 8 pF = **0,15 %** |

Ajustado junto o comentário do divisor de tensão, que dizia que "a escolha de
$t_s$ fica livre": agora aponta o limite de 25,2 kΩ correspondente aos 28,5
ciclos adotados.

**Precisa de revisão do orientador:** a `tab:rain_max` (impedância máxima de
entrada × tempo de amostragem) continua com os números do *datasheet*,
calculados a 14 MHz. A 12 MHz os limites mudam. Como os dois divisores
(7,09 kΩ e 1,8 kΩ) ficam folgados em qualquer dos dois cenários, a conclusão
não muda — **mas a tabela deveria ser recalculada** se o orientador quiser
rigor numérico.

### 6. Conclusão, objetivo 1

Em `Conclusao/00_Conclusao.tex`, "condicionamento de corrente por seguidor de
tensão" estava errado: **não há amplificador em nenhum dos dois caminhos**.
Reescrito para divisores resistivos com filtro *antialiasing*, com os valores
e as remissões às `fig:cond_tensao` e `fig:cond_corrente`.

### 7. Tabela de pinagem da SIBA refeita pelo devicetree

`tab:pinagem_siba` em `Metodologia/07_SIBA.tex` foi reconstruída a partir de
`Codigos/SIBA/boards/wch/siba_board/siba_board.dts`. Praticamente **toda linha
estava desatualizada**:

- ADC1 (mestre) é PA0/PA1/PA3 — Pitot, termistor da bateria, termistor do
  motor. Não é "tensão, corrente e temperatura da bateria" em PA0/PA1/PA2.
- ADC2 (escravo) existe e não estava na tabela: PC3/PC4/PC2 — tensão e
  corrente da bateria, mais um canal de reserva.
- O barramento I²C é o **I2C2 em PB10/PB11**, não I2C1 em PB8/PB9, e serve a
  **EEPROM de calibração** (FT24C32A, 4 kB), não um "barramento de uso geral".
- Relés: **PB12 (potência) e PB13 (pré-carga)** — não PB15 sozinho.
- Acrescentados: PE15 + PD2 (as duas entradas da porta lógica que habilita o
  contator de estado sólido), PB4/PA15 (LEDs verde e vermelho), PA5–PA7/PA2
  (flash serial prevista em placa, não habilitada).
- Células de carga: **PE0–PE5 (clock) e PE6–PE11 (dado)**, não PE2–PE13.

O parágrafo de abertura também foi reescrito: a restrição do primeiro
conversor aos canais 0, 1 e 3 vem dos pinos tomados pela flash serial, não de
um limite PA0–PA7 do multiplexador.

### 8. Módulo de persistência (EEPROM) nas camadas

A camada *Vcore - OS* passou de quatro para **cinco módulos** em
`Metodologia/05_Placas.tex`: acrescentado o NVM, com um parágrafo curto
(restaura na partida; grava quando o operador confirma uma calibração pela
aplicação de supervisão; na SAME guarda também os parâmetros do observador).
A `fig:Metodologia_Vcore` ganhou o bloco, e as figuras de camadas das duas
placas (`fig:blocos_siba` e `fig:blocos_same`) ganharam o quinto módulo na
fileira do Vcore — a largura das camadas foi de 9,2 cm para 11,2 cm para
acomodá-lo.

Conforme instrução do autor, **não se detalhou o módulo** (layout do EEPROM,
CRC, contagem de gravações ficaram de fora). A metodologia da SAME será
revista em conversa própria.

### Figuras removidas (pedido do autor)

`fig:native_sim` e `fig:sniffer_pacote` (eram as Figuras 37 e 38, ambas em
espaço reservado) foram apagadas de `Metodologia/04_Ferramentas.tex`,
**junto com o parágrafo que as apresentava** — regra do autor: figura que sai
leva o texto dela junto. A `tab:modos_conexao`, logo acima, continua e ainda
cobre o *native\_sim* como modo de validação.

### Referências acrescentadas

Cinco entradas novas em `abntex2-ref_UDESC_2020.bib`, todas com dados
conferidos:

| Chave | Referência | Onde entra |
|---|---|---|
| `Utkin1992` (já existia, nunca citada) | Utkin, *Sliding Modes in Control and Optimization*, Springer, 1992 | teoria de estrutura variável; função sinal como caso limite |
| `SlotineLi1991` | Slotine & Li, *Applied Nonlinear Control*, Prentice Hall, 1991 | camada limite / saturação |
| `EdwardsSpurgeon1998` | Edwards & Spurgeon, *Sliding Mode Control: Theory and Applications*, Taylor & Francis, 1998 | família de funções admissíveis |
| `Young1999` | Young, Utkin & Özgüner, IEEE Trans. Control Syst. Technol., 7(3):328–342, 1999, DOI 10.1109/87.761053 | *chattering* |
| `Kim2011` | Kim, Son & Lee, IEEE Trans. Ind. Electron., 58(9):4069–4077, 2011, DOI 10.1109/TIE.2010.2098357 | sigmoide/tanh em SMO de máquina de ímã permanente |
| `Qiao2013` | Qiao et al., IEEE Trans. Ind. Electron., 60(2):710–719, 2013, DOI 10.1109/TIE.2012.2206359 | idem |

`OkLee2022` (que já estava na bib sem uso) também passou a ser citada, junto
de `AbKaf2024`, como exemplo de aplicação em BLDC.

**Defeito corrigido de passagem:** a bib tinha **três chaves duplicadas**
(`Utkin1992`, `Chiasson2005`, `Perera2003`), cada uma definida duas vezes com
conteúdo diferente. O bibtex acusava `Repeated entry` assim que `Utkin1992`
passou a ser citada. As segundas cópias — nenhuma delas referenciada em lugar
nenhum — foram removidas. A segunda `Utkin1992` era, na verdade, outro livro
(*Sliding Mode Control in Electro-Mechanical Systems*, que é de 1999 e de
Utkin, Guldner & Shi): **se o orientador quiser esse livro na bibliografia,
ele precisa entrar com chave própria e metadados corretos.**

### Precisa de revisão do orientador

1. Propagar (ou não) a notação `an/bn/cn` até a seção de Clarke — item 2.
2. Recalcular a `tab:rain_max` para ADCCLK de 12 MHz — item 5.
3. Confirmar se o texto deve nomear a tangente hiperbólica (o que o código
   roda) em vez da notação genérica `Sat` — item 4.
4. Decidir se o livro de Utkin/Guldner/Shi entra na bibliografia.

---

## ✅ Botão de emergência exercitado no firmware da SIBA (26/09/2026)

Origem: ao conferir a tabela de pinagem contra o devicetree, PE15 e PD2
apareceram declarados mas **nunca usados pelo firmware** — `ready_pin` e
`gpio_pd2_and_out` só existiam no `.dts`. O autor confirmou a função
pretendida e pediu a implementação.

### Contradição resolvida

Três fontes discordavam sobre a direção do PD2: o commit original e o
`SIBA/Readme.md` diziam **entrada**; o comentário mais recente do `.dts`
dizia "saída da porta AND" e ao mesmo tempo "PD2 é a outra entrada" — não
fechava. **Decisão do autor: PD2 é entrada, é o botão de parada de
emergência, com lógica invertida (0 = apertado, 1 = solto). PE15 é saída e
acende um LED externo.**

### Firmware (repositório `Codigos/`, não este)

- `boards/wch/siba_board/siba_board.dts` — nó `gpio_pd2_and_out` virou
  `emergency_button`, com `GPIO_ACTIVE_LOW`: a inversão fica declarada no
  devicetree, e a API lógica do Zephyr já devolve "apertado" em lógica
  positiva. Comentário do PE15 reescrito (LED externo de pronto).
- `lib/OS/HAL.c` / `HAL.h` — novo bloco com `HAL_Emergency_Init`,
  `HAL_Emergency_Is_Pressed` e `HAL_Ready_Led_Set`, com o par
  real/`native_sim` no mesmo padrão dos relés. PD2 entra como entrada com
  pull-up interno. Falha de leitura do pino é tratada como botão apertado.
  `HAL_Init` passou a chamar `HAL_Emergency_Init`.
- `lib/SYS/Security_Manager.c` — `Security_Manager_Check` lê o botão a cada
  despacho de sensor, **antes** do filtro por `sensor_id` (senão só rodaria
  para os três sensores críticos), e aciona o LED externo.
  `Security_Manager_Trigger_Emergency` passou a **abrir os dois relés**
  (potência e depois pré-carga) além de desarmar o ESC, via
  `Battery_Manager_Set_Relay(false)` — não direto no HAL, para que o estado
  publicado na telemetria acompanhe o hardware.
  Acrescentado `Security_Manager_Clear_Emergency`: **soltar o botão encerra a
  emergência sem reiniciar a placa**. Não reenergiza nada — o ESC segue
  desarmado e os relés abertos; reenergizar e rearmar continuam sendo ato
  explícito do operador pela aplicação de supervisão. Um sinalizador interno
  registra que foi o botão quem disparou, de modo que soltá-lo **não** encerra
  uma emergência levantada por temperatura ou por faixa de sensor.

**Compilação:** `make build` (siba_board, ROM 90,6 %) e `make native_sim`
passam. O `native_sim` **já estava quebrado no HEAD** — `Security_Manager.c`
e `NCI.c` referenciam `led_red`/`led_green` sem guarda de POSIX e o overlay
não declarava os nós. Corrigido em `boards/native_sim.overlay` com os dois
LEDs sobre o GPIO emulado. Boot do simulado conferido, sem emergência falsa.

### Texto do TCC ajustado junto

- `tab:pinagem_siba`: a linha "PE15 e PD2 | GPIO saída | duas entradas da
  porta lógica" virou duas linhas — PD2 entrada (botão, nível baixo =
  acionado) e PE15 saída (LED externo de sistema pronto).
- Descrição do *Security Manager* em `07_SIBA.tex`: acrescentado o botão, o
  fato de o desarme ficar retido e o acionamento do LED externo.

### Precisa de revisão do orientador

1. **Falha segura do botão.** Com pull-up interno e contato contra o terra,
   **cabo rompido lê 1 (solto)** — a bancada continuaria liberada. Um botão
   de emergência normalmente fechado (pull-down + contato para 3,3 V)
   inverteria isso: fio partido cairia para 0 e desarmaria. A convenção
   atual foi a informada pelo autor; **vale confirmar se o botão físico é
   NA ou NF** antes do ensaio.
2. **Sem bit de erro próprio.** O acionamento reusa
   `ERROR_SEC_EMERGENCY_ACTIVE`, que é o mesmo do corte por comando e por
   limite de sensor — a GUI não distingue a origem. Um bit novo exige mexer
   no `SIBA.yaml` (o `Error_Codes.h` é gerado), o que mexe também na
   aplicação de supervisão. Não foi feito.
3. **Leitura por varredura, não por interrupção.** O botão é lido junto do
   despacho de sensores (centenas de vezes por segundo). Uma interrupção de
   borda no PD2 seria mais imediata, mas exigiria publicar o evento pelo
   OSSI, e a taxa atual já é folgada para um ensaio de bancada.
4. **Resolvido pelo autor:** não existe SSR nem porta AND — o projeto mudou.
   PD2 é só a entrada do botão de emergência, e a potência é chaveada pelos
   **dois relés** (pré-carga em PB13 e potência em PB12) que o Security
   Manager agora abre na emergência. Os comentários antigos do `.dts` que
   citavam o intertravamento externo foram apagados.
5. **Comentário errado no `HAL.c` da SIBA:** o cabeçalho do arquivo diz
   "ADC2 slave PC0/PC1/PC2", que é a pinagem da **SAME**; na SIBA o ADC2 é
   PC3/PC4/PC2. Só comentário, não afeta o código — mas confunde quem ler.

### Correção da mesma rodada — sem retenção, relés na emergência

O autor descartou duas coisas da primeira versão:

1. **Nada de reiniciar a placa.** A emergência deixou de ser retida; ver o
   item do `Security_Manager.c` acima.
2. **Ownership dos relés — a decidir.** Hoje quem é dono do estado do relé é
   o `Battery_Manager` (`Battery_Manager_Set_Relay`, chamado pelo `CMD_Core`
   no `FUNC_BATTERY`); o Security Manager comanda **através** dele na
   emergência, para não deixar o estado publicado na telemetria mentindo.
   Mover a propriedade dos relés de vez para o Security Manager mexeria no
   `CMD_Core` e no protocolo com a aplicação de supervisão — **não foi feito,
   e é decisão do autor.**

**Texto da camada revertido:** a descrição do *Security Manager* na lista de
módulos da camada *System* (`07_SIBA.tex`) voltou ao original, a pedido do
autor. O capítulo 3 não menciona botão de emergência, LED externo nem relés
na emergência — só a tabela de pinagem registra PD2 e PE15.

### Correção — alarme de contato colado durante a energização

O autor apontou que a checagem de contato colado (`ERROR_BATT_RELAY_MISMATCH`)
estava rígida demais: **enquanto qualquer um dos dois contatos estiver
fechado, corrente é esperada** — inclusive durante a pré-carga, ou seja,
enquanto a bancada está sendo armada.

- `HAL.c`/`HAL.h` — o HAL passou a acompanhar cada linha separadamente
  (`hal_relay_precharge_closed` e `hal_relay_power_closed`), além do
  `hal_relay_energized` que já existia e só vira verdadeiro com a sequência
  inteira concluída. Novo `HAL_Relay_Any_Closed()`.
- `Battery_Manager.c` — a checagem passou a exigir os **dois** contatos
  abertos (`!HAL_Relay_Any_Closed()`) em vez de olhar o `relay_state`, que
  só reflete o último comando completo e ficava falso durante os segundos de
  pré-carga.
- `HAL.c` (trecho de `native_sim`) — o estímulo sintético injetava corrente de
  bateria desde o boot, independentemente do relé, e o simulado acusava
  contato colado em **toda** execução. Agora a corrente sintética só existe
  com algum contato fechado.

**Verificado no simulado:** no boot, dois contatos abertos e sem corrente, o
alarme não aparece; com `batt relay on`, a corrente passa a circular e o
alarme continua sem aparecer. `make build` (ROM 90,75 %) e `make native_sim`
passam.

**Precisa de revisão do orientador:** logo após uma parada de emergência os
dois contatos abrem, mas os capacitores do ESC ainda descarregam — se essa
corrente residual passar de \SI{1}{\ampere} (`BATTERY_RELAY_MISMATCH_CURRENT_A`)
por tempo suficiente, o alarme de contato colado vai disparar sem que haja
falha. Uma janela de carência de alguns décimos de segundo após a abertura
resolveria; **não foi implementada porque o valor da carência é decisão de
bancada.**

### Tempo de pré-carga virou parâmetro de Kconfig

A sequência de energização já tinha o intervalo entre fechar a pré-carga
(PB13) e fechar a potência (PB12) — era `HAL_RELAY_PRECHARGE_MS`, fixo em
\SI{3}{\second} dentro do `HAL.h`. A pedido do autor, o valor saiu do
cabeçalho e virou parâmetro de projeto:

- `Kconfig` — novo `SIBA_RELAY_PRECHARGE_MS`, inteiro em milissegundos,
  padrão \num{3000}, faixa \numrange{0}{60000}, com texto de ajuda explicando
  que deve cobrir alguns múltiplos da constante RC do barramento do ESC e que
  a sequência bloqueia a fila de trabalho durante todo o intervalo.
- `prj.conf` — a linha `CONFIG_SIBA_RELAY_PRECHARGE_MS=3000` deixa o ajuste
  visível junto dos demais parâmetros de bancada; também dá para mexer por
  `make menuconfig`.
- `HAL.h` — `HAL_RELAY_PRECHARGE_MS` virou apenas um apelido do símbolo do
  Kconfig.
- `HAL.c` — o log de fechamento da pré-carga passou a imprimir o tempo que
  vai esperar, para conferência em bancada.

Propagação verificada: trocando o valor no `prj.conf` para \num{1500} o
`autoconf.h` gerado acompanha, e voltando para \num{3000} também. Os dois
alvos compilam (ROM 90,76 %).

**DIVERGÊNCIA ENCONTRADA — precisa de decisão do autor.** O firmware usa
\SI{3}{\second}, mas a metodologia **já especifica outro valor**. Na subseção
"Dimensionamento do Resistor de Pré-carga" (`06_Bancada.tex`), o critério
adotado é o carregamento atingir regime permanente em \SI{1}{\second}:

- $t_{pc} = 5\tau = \SI{1}{\second}$ (`eq:tau_valor`), logo
  $\tau = \SI{0,2}{\second}$;
- com $C_{ESC} = \SI{2000}{\micro\farad}$, isso fixa
  $R_{pc} = \SI{100}{\ohm}$ (`eq:rpc_valor`).

Ou seja, **o projeto diz \SI{1}{\second} e o firmware espera \SI{3}{\second}**
--- \num{15} constantes de tempo em vez de \num{5}. Conservador, mas
incoerente com o texto. Duas saídas, e a escolha é do autor:

1. **Alinhar o firmware ao projeto:** `CONFIG_SIBA_RELAY_PRECHARGE_MS=1000`.
   É o que torna o Capítulo 3 verdadeiro sem reescrever nada.
2. **Manter \SI{3}{\second} e justificar no texto** a margem sobre o critério
   de cinco constantes de tempo.

**Não alterei o padrão por conta própria** --- \SI{3}{\second} é o que a placa
vem rodando, e baixar para \SI{1}{\second} muda o comportamento do hardware.

### HAL sem poder de decisão, `arm`/`disarm` de bancada e shell opcional

Três pedidos do autor na sequência.

**1. O tempo de pré-carga não é decisão do HAL.**
`HAL_Relay_Energize()` passou a receber `precharge_ms` por argumento — o HAL
só garante a ordem dos contatos. O `HAL_RELAY_PRECHARGE_MS` saiu do `HAL.h`.
Quem é dono do valor é o `Battery_Manager` (camada *System*), que o lê do
Kconfig e o passa a cada energização.

**Valor revisto para \SI{5}{\second}** (era \num{3000}, padrão herdado).
O autor confirmou que o resistor de pré-carga continua em \SI{100}{\ohm} ---
ou seja, a \autoref{eq:rpc_valor} do Capítulo 3 segue válida --- e que o tempo
maior é margem deliberada, a refinar em bancada. Com $\tau = \SI{0,2}{\second}$,
\SI{5}{\second} são \num{25} constantes de tempo contra as \num{5} do critério
de projeto.

**2. `arm` e `disarm` viraram a sequência de bancada inteira.**
Antes chamavam só `HAL_ESC_Arm`/`Disarm`. Agora:

- `arm`: recusa se o botão de emergência estiver apertado; encerra uma
  emergência já em vigor se o botão estiver solto; energiza (pré-carga →
  espera → potência) se a potência ainda não estiver fechada; arma o ESC;
  imprime o estado final dos três.
- `disarm`: desarma o ESC, depois abre os relés (potência → pré-carga).

O `arm` encerrar a emergência cobre um furo que só apareceu no teste: uma
parada vinda de `estop` ou de `FUNC_STOP` **não** é do botão, então não se
encerrava sozinha e exigia reiniciar a placa --- justamente o que o autor
não queria.

**3. Estado da cadeia de potência no `tele`.** Duas linhas novas:

```
  reles: pre-carga=FECHADO  potencia=FECHADO  (espera de 5000 ms)
  emergencia: botao=solto  parada=livre
```

Para isso foram expostos `HAL_Relay_Precharge_Closed`,
`HAL_Relay_Power_Closed`, `Battery_Manager_Get_Precharge_Ms` e
`Security_Manager_Is_Emergency_Active`.

**4. Shell fora do produto final.** Novo `SIBA_TEST_SHELL` no `Kconfig`
(`depends on SHELL`, padrão `y`), e o `CMakeLists.txt` passou a compilar
`Test_Shell.c` por esse símbolo em vez de por `CONFIG_SHELL`. O texto de ajuda
diz o que o autor definiu: é ferramenta de bancada, dá acesso direto ao ESC e
aos relés pela UART sem passar pela aplicação de supervisão nem por
autenticação, e não faz parte do produto.

**Medido:** com `CONFIG_SHELL=n`, `CONFIG_SHELL_BACKEND_SERIAL=n` e
`CONFIG_SIBA_TEST_SHELL=n`, a imagem cai de **\SI{91,29}{\percent} para
\SI{67,57}{\percent} da ROM** (\SI{185}{\kilo\byte} → \SI{137}{\kilo\byte}) e a
RAM de \SI{71,8}{\percent} para \SI{68,0}{\percent}. Os três ficaram em `y` no
`prj.conf`, com comentário dizendo para zerar os três no produto.

**Verificado no simulado** (`-uart_stdinout`), ciclo completo:
`arm` → energiza e arma → `estop` → relés abrem, ESC desarma, parada ATIVA →
`arm` → encerra a emergência, reenergiza e arma → estado limpo. Sem
reinicialização. Os dois alvos compilam.

### Precisa de revisão do orientador

1. **O Capítulo 3 não menciona nada disso.** Botão de emergência, LED externo,
   comando dos relés pelo Security Manager, tempo de pré-carga de
   \SI{5}{\second} e o shell como ferramenta descartável no produto --- nada
   foi escrito no texto, a pedido do autor (o texto da camada foi revertido ao
   original). Só a tabela de pinagem registra PD2 e PE15. **Se algo disso deve
   entrar na metodologia, é decisão a tomar.**
2. **\SI{5}{\second} contra o critério de \SI{1}{\second}.** A
   \autoref{eq:tau_valor} declara $t_{pc} = 5\tau = \SI{1}{\second}$ e é dela
   que sai o $R_{pc} = \SI{100}{\ohm}$. O firmware espera \SI{5}{\second}. O
   resistor não muda, mas o texto e o firmware declaram tempos diferentes ---
   convém uma frase no Capítulo 3 dizendo que a espera implementada é
   conservadora frente ao critério.
3. **Armar não passa pelo Propeller Manager.** O `arm` chama `HAL_ESC_Arm`
   direto, como o comando antigo já fazia; o limite de corrente do
   `Propeller_Manager` só atua sobre pedidos de \textit{throttle}, não sobre a
   armação. Mantido como estava.

### Armar pela rede: duas condições + sobrescrita do botão

Pedido do autor depois de constatar que **o único caminho para armar o ESC em
todo o firmware era o comando `arm` do shell** --- que ele acabara de mandar
tirar do produto final. Os dois pedidos se anulavam.

Dois comentários do código mentiam sobre isso e foram a origem da confusão:
`CMD_Core.c` dizia que o `Propeller_Manager` "arma o ESC" (não arma --- recusa
o \textit{throttle} se não estiver armado) e o aviso do `Propeller_Manager`
mandava "armar via CMD de rede", que não existia.

**Protocolo (`Aplicacao_Pi/Config/SIBA.yaml`).** A pedido do autor, em vez de
sobrecarregar `default_action`, os comandos novos viraram valores de
`status_type` --- **acrescentados no fim da lista**, para não deslocar os
índices que a GUI já usa:

| `action_or_status` | Efeito |
|---|---|
| `STATUS_OK` | "tudo certo, pode armar": pré-carga → espera → potência → arma o ESC |
| `STATUS_ERROR` | desligar: desarma o ESC e abre os dois relés |
| `STATUS_EMERGENCY_OVERRIDE` | passa a ignorar o botão físico |
| `STATUS_EMERGENCY_RESTORE` | volta a respeitar o botão |

Todos como `FUNC_START` / `INSTR_WRITE` / `TARGET_DEFAULT`. `generate.py`
rodado: só `SIBA/lib/OS/Common_Types.h` mudou (SAME, SPMM e a aplicação
Python saíram idênticos).

**Firmware.** A sequência saiu do shell e foi para a camada *System*:
`Security_Manager_Arm_Bench()` e `Security_Manager_Shutdown_Bench()`, mais
`Security_Manager_Set_Emergency_Override()` e
`Security_Manager_Button_Pressed()` (estado do botão já descontada a
sobrescrita --- é este que a varredura de segurança usa). Armar exige as
**duas** condições: o supervisório ter mandado `STATUS_OK` **e** o botão
liberado, ou explicitamente sobrescrito. O `CMD_Core` roteia; o `arm`/`disarm`
do shell viraram cascas finas sobre as mesmas funções, de modo que tirar o
shell não tira mais nenhuma capacidade.

A sobrescrita vive **só em RAM** --- reiniciar a placa devolve o botão ao
comando.

**Gancho de teste (só `native_sim`).** O stub do botão reportava "solto"
sempre, o que tornava a recusa impossível de exercitar. Foi acrescentado
`HAL_Emergency_Sim_Set_Pressed()` e o comando de shell `btn on|off`, ambos
sob `#ifdef CONFIG_ARCH_POSIX` --- conferido que não entram no binário da
placa (ausentes do `zephyr.map`).

**Verificado no simulado**, os quatro cenários:

1. Botão apertado → emergência, ESC desarmado, relés abertos; `arm` recusado.
2. Botão solto → emergência encerra sozinha; `arm` arma.
3. Rede: `STATUS_OK` → arma; `STATUS_ERROR` → desliga.
4. Botão apertado + `STATUS_OK` sozinho → **recusado**; depois
   `EMERGENCY_OVERRIDE` + `STATUS_OK` → **arma**, com o estado reportando
   `botao=APERTADO parada=livre sobrescrita=SIM`.

Os dois alvos compilam (ROM 91,52 %).

### Precisa de revisão do orientador

1. **Nada disso está no Capítulo 3.** A metodologia não descreve o botão, o
   LED externo, a sobrescrita por comando, as duas condições para armar, nem o
   shell como ferramenta fora do produto. Foi decisão do autor manter o texto
   da camada como estava; **se algo disso tem de entrar, é escrita a fazer.**
2. **A GUI ainda não manda esses comandos.** O protocolo os define e o
   firmware os atende, mas a aplicação Python não tem botão para eles ---
   sem isso, na prática só o shell arma.
3. **A sobrescrita é um desvio de segurança sem trava.** Qualquer pacote UDP
   `FUNC_START/INSTR_WRITE/TARGET_DEFAULT/STATUS_EMERGENCY_OVERRIDE` desabilita
   o botão de emergência físico, sem autenticação (o protocolo não tem
   nenhuma). Fica registrado em log, e some ao reiniciar. **Convém decidir se
   isso é aceitável para ensaio com hélice acoplada.**
4. **Os dois comentários errados** (`CMD_Core.c` e `Propeller_Manager.c`)
   descrevem um caminho de rede que agora existe de verdade --- mas o texto
   deles continua impreciso e merece revisão.

### Interface: botões de bancada, reset no gerenciador e banco de teste CLI

**Gerenciador de Placas** (`GUI/BoardManagerDialog.py`) ganhou um botão
**⟳ Reiniciar** por linha, que manda `FUNC_RESET`. Habilitado para SIBA e
SAME; desabilitado para a SPMM, que fala texto por Telnet e tem caminho
próprio. Pede confirmação, avisando que o ESC e os relés voltam ao estado de
boot.

**Janela principal** (`GUI/mainAlbatrozGUI.py`): menu **Bancada → SIBA** com
"Armar bancada" e "Desligar bancada", e os mesmos dois como **botões** num
grupo no fim do painel central. O armar pede confirmação. O helper
`_send_board_cmd` passou a aceitar `status`, que faltava para carregar o
`action_or_status`.

**A sobrescrita do botão de emergência ficou fora da tela de propósito**, a
pedido do autor: é comando de terminal, `siba force on|off`, em
`GUI/TinyTerminal.py` e documentado no `Config/commands.yaml` como
`admin_only`. Desvio de segurança não vira clique de menu.

**`native_cli.py`** (novo, raiz da `Aplicacao_Pi`): banco de teste por linha
de comando. Sobe o `zephyr.exe` da SIBA ou da SAME e conversa por UDP em
loopback usando `registry`/`codecs`/`native_sim` — mesmo caminho da GUI, sem
interface gráfica. Comandos: `arm`, `off`, `force on|off`, `estop`, `esc`,
`reset`, `tele`, `watch`, `shell`, `raw`, `sleep`. Como sobe o firmware com
`-uart_stdinout`, o `shell` alcança o shell do próprio firmware.

### `native_sim` da SAME estava quebrado no HEAD

Mesma classe de problema já encontrada na SIBA nesta rodada, mais uma:

- `boards/native_sim.overlay` não declarava `led_red`, `led_green` nem
  `i2c2` --- usados sem guarda de POSIX por `Security_Manager.c`, `NCI.c` e
  `src/main.c`. Acrescentados sobre os emuladores (`gpio_emul`,
  `zephyr,i2c-emul-controller`).
- `boards/native_sim.conf` trazia `CONFIG_GPIO=n` com o comentário "não há
  periférico GPIO na SAME" --- desatualizado, a placa tem os LEDs de status.
  Religado, com `CONFIG_EMUL`/`CONFIG_I2C_EMUL` junto.

Agora a SAME compila e sobe em `native_sim`; a placa real segue compilando
(ROM 66,81 %).

**Verificado pelo CLI**, roteiro completo na SIBA: botão apertado pelo
`shell btn on` → emergência e relés abertos; `arm` → **recusado**;
`force on` → sobrescrita; `arm` → **energiza e arma**; `tele` → 41 pacotes de
telemetria decodificados; `off` → desliga. E na SAME: telemetria decodificada
e `reset` chegando ao firmware (`CMD RESET: reiniciando sistema...`).

**Respondendo ao autor:** o reinício da SAME **já existia** --- `FUNC_RESET`
está no `SAME.yaml` e o `handle_func_reset` do `CMD_Core.c` chama
`sys_reboot(0)`, ignorando o resto do comando. O que faltava era caminho na
interface, que agora é o botão do Gerenciador de Placas.

### Precisa de revisão do orientador

1. **BUG ABERTO na aplicação:** conectar, desconectar e conectar de novo
   deixa os gráficos sem voltar ao normal. Registrado em
   `Aplicacao_Pi/CLAUDE.md` (Observações em aberto). Ainda não investigado ---
   a suspeita é o buffer/curva do \textit{pyqtgraph} não ser rearmado no
   segundo `start_link`, ou o timer de replot ficar preso ao worker antigo.
   Reiniciar a aplicação contorna.
2. **Posição dos botões de bancada na tela.** Eles entram numa linha nova do
   \textit{grid} do painel central, abaixo do que já existia --- escolha
   segura, que não disputa célula com nenhum widget do `.ui`. **Vale o autor
   olhar se é onde ele quer.**
3. **Nada disso está no Capítulo 3** --- continua valendo o que já ficou
   registrado nas rodadas anteriores.
