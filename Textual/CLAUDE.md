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
Textual/
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

## Ensaios de bancada do motor — material pronto, falta escrever

Os ensaios de bancada com o motor girando já foram adquiridos e processados.
O pipeline vive em `Dados TCC - Analise/analise/` (fora deste diretório) e
tem `README.md` próprio com o detalhe completo. **O que falta é a redação do
Capítulo 4.**

### O que existe

- **14 figuras** já em `Textuais/Imgs/Resultados/`, prefixo `bancada_`.
- **6 tabelas** em `PosTextuais/Apendice_Bancada.tex`, já incluído em
  `Apendices.tex`. Arquivo **gerado** por `p4_apendice.py` — se algum número
  mudar, rodar o script de novo em vez de editar à mão.
- Dados de origem: \num{743} registros de osciloscópio (duas tensões de fase,
  duas correntes por sensor de efeito Hall, comando do gerador de pulsos e
  tacômetro óptico), dos quais 378 passaram na triagem.

### Achados que precisam entrar no texto

1. **Perdas a vazio (equação do torque de perdas).** Regressão sobre 19
   patamares de rotação, de \SI{130}{\radian\per\second} a
   \SI{705}{\radian\per\second}: atrito viscoso $B = (32{,}4 \pm 4{,}4)
   \times 10^{-6}$ \si{\newton\meter\second\per\radian}, atrito seco
   $\tau_c = (15{,}5 \pm 2{,}4)$ \si{\milli\newton\meter}, $R^2 = 0{,}925$.
   O torque foi medido pelo balanço de potência por fase, que dispensa
   conhecer a forma de onda, a constante de FEM e o ângulo do rotor.
2. **O $\tau_c$ grande tem causa física.** Torque constante significa perda
   proporcional à rotação, que é a assinatura da histerese no ferro. O termo
   de atrito seco da equação agrega atrito mecânico \emph{mais} histerese; o
   viscoso agrega atrito viscoso, ventilação e corrente parasita. Vale dizer
   isso no texto — não é resíduo inexplicado.
3. **O tacômetro fecha com a frequência elétrica em
   $-7{,}9\times10^{-4}\,\%\ \pm\ 3{,}7\times10^{-2}\,\%$** sobre 267
   registros. Confirma um pulso por volta e sete pares de polos.
4. **A constante de FEM medida implica \SI{310}{rpm\per\volt}**, contra os
   \SI{310}{rpm\per\volt} de placa. Ou seja: a caracterização fecha com o
   catálogo do motor.
5. **Erro encontrado na simulação.** O script de autotuning dos ganhos usa
   uma constante de FEM \textbf{três vezes maior} que a medida — a conversão
   partiu do $K_v$ de placa (que é de linha) tratando-o como se fosse de
   fase. Isso escala todo o torque estimado pelo mesmo fator. Precisa ser
   dito no texto e o valor corrigido é o da bancada.
6. **Projeto das frequências de corte.** O ruído do sensor de corrente sem
   filtro é \SI{719}{\milli\ampere} rms, quinze vezes o degrau do conversor.
   A varredura mostra que o observador é **insensível** à frequência de corte
   de \SI{3}{\kilo\hertz} para cima — porque o próprio observador já é um
   passa-baixas (o estimador de força contraeletromotriz tem polo em
   \SI{1,06}{\kilo\hertz} e a malha de sincronismo em \SI{150}{\hertz}).
   Logo a escolha não se justifica por desempenho do observador, e sim pelo
   que ele não consegue desfazer: rebatimento no conversor e faixa dinâmica.
   Projeto: \SI{20}{\kilo\hertz}, um polo, **iguais** nos dois ramos.
7. **Observador nos dados reais.** Velocidade com \SI{0,092}{\percent} de
   erro médio; ângulo elétrico dentro de \SI{0,3}{\degree} de média contra a
   integral dos pulsos do tacômetro, em toda a faixa; torque com
   \SI{-7,2}{\percent} usando a corrente medida projetada no ângulo
   estimado. Usando a corrente do modelo o erro vai a \SI{+71}{\percent} —
   conclusão prática: projetar a corrente **medida**, não a do modelo.
8. **Limite de baixa rotação.** A malha de sincronismo tem frequência natural
   de \SI{150}{\hertz}; abaixo de cerca de \num{1930} rpm o ângulo deixa de
   travar. A velocidade ainda sai, o torque não.

### Figuras disponíveis

| arquivo em `Textuais/Imgs/Resultados/` | conteúdo |
|---|---|
| `bancada_te_omega.png` | torque de perdas contra rotação, com a reta ajustada — **a figura principal** |
| `bancada_te_residuo.png` | resíduos da regressão |
| `bancada_ke.png` | constante de FEM contra rotação |
| `bancada_tacometro.png` | validação do tacômetro (distribuição do erro em cima, correlação embaixo) |
| `bancada_te_vs_fc.png` | robustez do torque medido à banda de medida |
| `bancada_comando_rpm.png` | característica comando do gerador de pulsos $\to$ rotação |
| `bancada_espectros.png` | espectros de corrente e tensão com as bandas marcadas |
| `bancada_restricoes_fc.png` | as três restrições sobre a frequência de corte num gráfico |
| `bancada_smo_vs_fc.png` | qualidade do observador contra a frequência de corte |
| `bancada_smo_rpm_torque.png` | rotação e torque, observador contra medida, ao longo dos patamares |
| `bancada_smo_rpm.png` | só a rotação, patamar a patamar |
| `bancada_smo_alfabeta.png` | correntes, tensões e força contraeletromotriz no referencial estacionário |
| `bancada_smo_angulo_patamares.png` | ângulo elétrico e rotação, patamar a patamar, com o erro por rotação |
| `bancada_smo_angulo_tacometro.png` | ângulo contra a integral dos pulsos, num registro de \SI{0,5}{\second} |

### O que escrever

Montar as seções de resultado **passo a passo**, uma por vez, na ordem:
levantamento das perdas mecânicas $\to$ projeto das frequências de corte
$\to$ observador aplicado aos dados reais. Para cada seção: o que o ensaio
produziu, a figura, o número, e o que o número significa. Nada de repetir
procedimento — isso é do Capítulo 3.

Perguntar ao autor qual seção atacar antes de começar, e seguir as regras de
1 a 8 acima (em especial: toda figura citada no corpo, caption e fonte, e
`human.md` atualizado **depois** de escrever).

## Contexto do plano de escrita

O plano original (16 "Ações", Capítulos 3 e 4) foi recebido do autor e está
sendo executado incrementalmente, uma Ação por vez, via task list. Não
recriar o plano do zero em sessões futuras — perguntar ao autor qual Ação
seguir, ou consultar o estado das tasks já registradas na sessão.
