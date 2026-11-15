# Regras de redação — TCC Bancada BLDC

Este projeto está em fase de escrita/revisão de texto. As regras abaixo vêm do
plano de escrita definido com o autor e valem para qualquer sessão futura
que edite `.tex` neste diretório — não precisam ser repetidas a cada pedido.

## Regras

1. É um Trabalho de Conclusão de Curso. Texto direto: descrever o que foi
   feito e o resultado. **Justificativas sem necessidade são desnecessárias**
   — não justificar escolhas triviais nem antecipar objeções que ninguém fez.
2. **Sem pontos em aberto onde eles não cabem.** Na metodologia (Capítulo 3),
   descrever o procedimento — como o ensaio é conduzido, quais grandezas são
   registradas, qual o critério de cálculo — é o próprio conteúdo da seção e
   não conta como pendência. O que não pode ficar em aberto é: referência a
   algo já resolvido deixada solta (`??`, "a definir", remissão sem destino);
   e resultado sem valor, isto é, seção do Capítulo 4 que descreve o ensaio
   mas não apresenta o que aconteceu. Regra prática: **o Capítulo 3 responde
   como se faz; o Capítulo 4 responde quanto deu.**
3. Toda figura precisa de: `\caption`, `\label`, `\includegraphics`, fonte,
   **e citação no corpo do texto** (`\autoref` ou `Figura~\ref{...}`). Figura
   que não é citada não entra.
4. **Fluxo de trabalho: escrever primeiro, depois atualizar `human.md`.**
   Nunca o inverso. `human.md` (raiz deste diretório) registra o que foi
   escrito, o que ficou faltando de imagem ou dado, e o que precisa de
   revisão do orientador. Atualizar esse arquivo é o último passo de cada
   ação, não o primeiro.
5. **Navegar pelo repositório, não varrê-lo.** Usar a estrutura de pastas
   para localizar o que se precisa e abrir apenas o arquivo que interessa.
   Os únicos `.md` a abrir atrás de contexto são os `README.md` — nenhum
   outro `.md` do repositório (deste ou de `Codigos/`) deve ser lido em busca
   de contexto para a escrita.
6. **Se dá para explicar com uma figura, usar a figura** em vez de prosa
   longa — um fluxo, uma sequência de passos ou uma estrutura de dados vira
   diagrama (TikZ ou espaço reservado, ver regra de placeholder abaixo)
   antes de virar parágrafo. Texto e figura não devem duplicar a mesma
   explicação lado a lado; a figura carrega a lógica, o texto só a
   apresenta e comenta o que não é óbvio nela.
7. **Nada de identificador de código solto no meio do texto** (nomes de
   `struct`, campos, `enum`, função, variável — ex.: `target_id`,
   `CMD_Core`, `DEFAULT_ACTION_SENSE_LINEAR_COEF`). Descrever a lógica em
   português, para leitor que não programa. Exceção: nomes próprios do
   projeto já consolidados no texto corrido (SIBA, SAME, CMD, YAML,
   TinyTerminal) — esses não são "código", são como as peças já foram
   batizadas ao longo do documento.
8. **Nunca deixar figura pendente como comentário `%`.** Se falta imagem
   real (foto, captura de tela, gráfico com dado que ainda não existe),
   inserir a figura ativa mesmo assim, com uma caixa tracejada no lugar da
   imagem (mesmo padrão de `fig:telas_gui`: `\begin{tikzpicture}` com um
   `\node[draw, dashed, ...]` descrevendo o que vai entrar ali). O documento
   tem que compilar e mostrar visivelmente "aqui falta isto", nunca esconder
   a pendência atrás de `%`.

## Formato dos blocos de figura

Caption acima, fonte abaixo (padrão já usado no documento):

```latex
\begin{figure}[H]
    \centering
    \caption{Título da figura}
    \label{fig:rotulo}
    \includegraphics[width=0.85\textwidth]{Textuais/Imgs/<Capítulo>/arquivo.png}
    \fonte{Autor (2026)}
\end{figure}
```

Se o bloco específico da seção usa `{\small Fonte: ...}` solto em vez de
`\fonte{...}`, seguir o que já está naquele arquivo — não misturar os dois
estilos no mesmo capítulo.

## Estrutura relevante para navegação

```
docs/TCC_documento/
├── abntex2-model_UDESC_2024.tex   # documento principal, compilar este
├── abntex2-ref_UDESC_2020.bib     # bibliografia (bibtex, não biber)
├── PacotesBasicos.tex             # pacotes/config compartilhada (siunitx, tikz, etc.)
├── Textuais/
│   ├── Metodologia.tex            # \input dos 00..08_*.tex (Capítulo 3)
│   ├── Metodologia/00_Introducao.tex .. 08_SAME.tex
│   ├── Sys.tex, Revisao.tex, Introducao.tex, Conclusao.tex
│   └── Imgs/<Capítulo>/           # imagens usadas nos \includegraphics
└── human.md                       # arquivo de correção humana (ver regra 4)
```

Compilar com `pdflatex` (2x) + `bibtex` + `pdflatex` (2x), todos a partir
desta pasta — ver `README.md` para o passo a passo completo.

## Ensaios de bancada do motor — já escritos, em fase de revisão

Os ensaios de bancada com o motor girando foram adquiridos, processados e
redigidos. O pipeline de análise vive em `Dados TCC - Analise/analise/` (fora
deste diretório) e tem `README.md` próprio com o detalhe completo. Os dados de
origem são \num{743} registros de osciloscópio, dos quais 378 passaram na
triagem.

### Estado atual — as seções já estão escritas

As seções de resultado de bancada estão redigidas em `Textuais/Conclusao.tex`,
dentro de `\section{Ensaios de Bancada com o Motor}` (`sec:ensaios_bancada`),
que vem **depois** da simulação do SMO — ordem fixada pelo autor: simulação
antes do ensaio. São quatro subseções: `sec:resultado_setup_scope`
(instrumentação), `sec:resultado_coef_mec` (coeficientes mecânicos),
`sec:resultado_fc` (frequências de corte) e `sec:resultado_smo_bancada`
(observador nos registros). O que o autor pedir daqui em diante é revisão
desse texto, não redação do zero.

Regra de tom acertada com o autor: **nada de afirmação forte em resultado**.
Escrever "é compatível com", "sugere", "aponta para"; não "confirma",
"comprova", "atesta", "demonstra a eficácia".

### A montagem (ensaio improvisado)

- Osciloscópio **PicoScope Série 5000** do laboratório do PET, com o programa
  do próprio fabricante. **4 canais analógicos + 2 digitais** — é essa
  contagem que define o que foi medido: duas tensões de fase e duas correntes
  de fase no analógico; comando do gerador de pulsos e tacômetro óptico no
  digital.
- Correntes lidas pela saída em tensão dos sensores de efeito Hall da própria
  placa.
- Terceira tensão e terceira corrente **não são medidas** — reconstruídas na
  análise pela soma nula das três.
- Alimentação por **bateria** (barramento flutuante, sem laço de terra e sem
  ondulação de retificação).
- **Dois ESCs** ao longo das sessões: **Castle Talon 90 A** e **Flycolor
  150 A** (colunas "ESC A/B" do apêndice).
- **Neutro virtual**: motor em estrela sem neutro acessível, refeito com três
  resistores iguais unidos num nó comum. Citação: `nidec_neutral_point`.

### Números levantados

1. **Perdas a vazio.** Regressão sobre 19 patamares, de
   \SI{130}{\radian\per\second} a \SI{705}{\radian\per\second}: $B = (32{,}4
   \pm 4{,}4)\times 10^{-6}$ \si{\newton\meter\second\per\radian}, $\tau_c =
   (15{,}5 \pm 2{,}4)$ \si{\milli\newton\meter}, $R^2 = 0{,}925$. Torque pelo
   balanço de potência por fase — dispensa forma de onda, constante de FEM e
   ângulo do rotor.
2. **$\tau_c$ alto tem leitura física.** Torque constante = perda
   proporcional à rotação = assinatura de histerese no ferro. O termo de
   atrito seco agrega mecânico + histerese; o viscoso agrega viscoso,
   ventilação e corrente parasita. No texto isso aparece como hipótese
   plausível, não como conclusão.
3. **Tacômetro contra frequência elétrica:** $-7{,}9\times10^{-4}\,\%\ \pm\
   3{,}7\times10^{-2}\,\%$ sobre 267 registros — compatível com um pulso por
   volta e sete pares de polos.
4. **Constante de FEM medida** equivale a 310 RPM/V, próximo da placa.
5. **Frequências de corte.** Ruído do sensor sem filtro:
   \SI{719}{\milli\ampere} rms, ~15× o degrau do conversor (~4 bits). O
   observador é insensível ao corte de \SI{3}{\kilo\hertz} para cima, porque
   ele próprio é passa-baixas (estimador de FEM com polo em
   \SI{1,06}{\kilo\hertz}, sincronismo em \SI{150}{\hertz}). O que decide é
   rebatimento no conversor e faixa dinâmica. Projeto: \SI{20}{\kilo\hertz},
   um polo, **iguais** nos dois ramos.
6. **Observador nos dados reais.** Velocidade com \SI{0,092}{\percent} de
   erro médio; ângulo dentro de \SI{0,3}{\degree} de média contra a integral
   dos pulsos; torque \SI{-7,2}{\percent} com a corrente **medida** projetada
   no ângulo estimado.
7. **Limite de baixa rotação.** Sincronismo a \SI{150}{\hertz}; abaixo de
   ~\num{1930} RPM o ângulo não trava. Velocidade continua saindo, torque
   não.

### Convenção do $K_v$ — corrigida em 22/08/2026

O $K_v$ de placa (310 RPM/V) **já é grandeza de linha**: não se divide por
$\sqrt{3}$. O `test_coeff_auto.py` fazia `Kv = 310.0/math.sqrt(3)`; foi
corrigido para `310.0`, o autotuning foi rodado de novo e as figuras da
simulação foram regeradas. **Não existe mais** o achado antigo de "constante
de FEM da simulação 3× maior que a medida" — se aparecer em algum texto ou
anotação, está desatualizado.

### Onde vive o código da simulação

`/home/vitor/Documentos/Facul/TCC/gui/Scripts/`, com a venv
em `gui/.venv` (Python 3.13). `autotune_cost.py --plot` roda o
autotuning (differential evolution sobre $\Gamma = K T_s$), grava
`smo_gains.json` e gera `Otim_convergencia.png`, `Otim_sensibilidade.png` e
`FFT_comparacao.png`, depois chama `plot_pi.py`, que gera `PLL_angulos.png`,
`Torque_rpm.png`, `Eixo_alpha_beta.png` e `PI_controle_Inversor.png`. Os
parâmetros do motor ficam no topo de `test_coeff_auto.py`. **Os arquivos são
CRLF** — preservar as quebras de linha ao editar (gravar em modo binário ou
com `newline=''`), senão o diff vira o arquivo inteiro.

### Figuras de bancada existentes

| arquivo em `Textuais/Imgs/Resultados/` | conteúdo |
|---|---|
| `bancada_te_omega.png` | torque de perdas contra rotação, com a reta ajustada |
| `bancada_te_residuo.png` | resíduos da regressão |
| `bancada_ke.png` | constante de FEM contra rotação |
| `bancada_tacometro.png` | validação do tacômetro |
| `bancada_espectros.png` | espectros de corrente e tensão com as bandas marcadas |
| `bancada_restricoes_fc.png` | as três restrições sobre a frequência de corte |
| `bancada_smo_rpm_torque.png` | rotação e torque, observador contra medida |
| `bancada_smo_alfabeta.png` | correntes, tensões e FEM no referencial estacionário |
| `bancada_smo_angulo_tacometro.png` | ângulo contra a integral dos pulsos |
| `picoscope_software.png` | tela do programa do PicoScope |
| `ensaio_motor_vazio.jpg` | foto da montagem física |

O autor apagou as figuras que achou inúteis (`bancada_te_vs_fc`,
`bancada_comando_rpm`, `bancada_smo_vs_fc`, `bancada_smo_rpm`,
`bancada_smo_angulo_patamares`). Regra que ele deu: **figura que não existe
mais leva junto o texto que vinha com ela**.

### Apêndice da bancada

`PosTextuais/Apendice_Bancada.tex`, gerado por `p4_apendice.py` (script não
está nesta máquina). Tinha 6 tabelas; a de inventário das sessões foi
removida a pedido do autor, restam 5. Se algum número mudar, rodar o script
de novo — mas as alterações de formatação feitas à mão (cabeçalho da tabela
de restrições em duas linhas) precisam ser reproduzidas no script.

## Contexto do plano de escrita

O plano original (16 "Ações", Capítulos 3 e 4) foi recebido do autor e está
sendo executado incrementalmente, uma Ação por vez, via task list. Não
recriar o plano do zero em sessões futuras — perguntar ao autor qual Ação
seguir, ou consultar o estado das tasks já registradas na sessão.
