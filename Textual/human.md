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

## 📋 Checklist único de pendências reais (todas as ações)

Tudo abaixo é imagem ou dado que só o autor pode fornecer — nenhum foi
inventado. Local = onde está o `\label` da figura ou da nota `Pendente`.

**Imagens (fotos/capturas de tela/gráficos) a produzir e inserir:**

| Label | Arquivo | O que precisa |
|---|---|---|
| `fig:bancada_provisoria` | `02_MotorBLDC.tex` | Foto da montagem provisória |
| `fig:native_sim` | `04_Ferramentas.tex` | Captura de tela: supervisão + native_sim |
| `fig:sniffer_pacote` | `04_Ferramentas.tex` | Captura de tela: pacote de rede |
| `fig:precarga_osc` | `06_Bancada.tex` | Captura de osciloscópio |
| `fig:cad_caixa` | `06_Bancada.tex` | Render do CAD da caixa |
| `fig:montagem_interna` | `06_Bancada.tex` | Foto da caixa aberta |
| `fig:painel_externo` | `06_Bancada.tex` | Foto do painel externo |
| `fig:pcb_siba` | `07_SIBA.tex` | Export KiCad: esquemático + roteamento |
| `fig:pcb_same` | `08_SAME.tex` | Export KiCad: esquemático + roteamento |
| `fig:setup_kelvin` | `Conclusao.tex` | Foto do ensaio de resistência (já desmontado, precisa remontar) |
| `fig:setup_ressonancia` | `Conclusao.tex` | Foto do ensaio de indutância (já desmontado, precisa remontar) |
| `fig:coastdown` | `Conclusao.tex` | Gráfico $\omega_m(t)$ do ensaio de desaceleração livre |
| `fig:bancada_fisica` | `Conclusao.tex` | Foto da estrutura mecânica já usada pela equipe |
| `fig:formas_onda_reais` | `Conclusao.tex` | Captura de V/I de fase reais |
| `fig:resultado_sensor_medido_vs_simulado` | `Conclusao.tex` | Gráfico do sensor de corrente calibrado vs. simulado |
| Figura 23 (`fig:telas_gui`) | `03_Interface.tex` | 9 capturas de tela da interface (já existia antes desta sessão) |

**Dados numéricos/textuais a preencher (notas `\textit{Pendente: ...}`):**

| Onde | O que falta |
|---|---|
| `sec:bancada_provisoria` | Instrumento de medição V/I e taxa de amostragem da bancada provisória |
| `sec:calibracao_pitot` | Túnel de vento, faixa de velocidade, nº de pontos, ajuste da calibração |
| "Coeficientes Mecânicos" (Cap. 4) | Tabela de patamares, $B$, $\tau_c$, $J$ |
| `sec:resultado_validacao_siba` | Bateria, células de carga, Pitot — validação completa da cadeia SIBA |
| `sec:resultado_calibracao_canal_same` | Ganho/offset por canal, erro vs. referência, piso de ruído |
| `fig:resultado_sensor_medido_vs_simulado` (parágrafo) | G medido do sensor de corrente e erro vs. 13,39 mV/A simulado |

---

## Notas gerais

- `PacotesBasicos.tex` ganhou `\DeclareSIUnit{\gauss}{G}` (usado nas Ações
  10 e 15) — próximas ações que usem `\SI{...}{\gauss}` não precisam
  redeclarar.
- Datasheet do núcleo NT-27/16/12-100-IP6 não foi localizado como PDF no
  repositório nem publicamente por busca — a citação de material/permeabilidade
  usa McLyman (handbook genérico) em vez do datasheet do fabricante. Se o
  autor tiver o PDF do fornecedor (cotação própria mencionada no texto),
  adicionar como referência mais específica.
