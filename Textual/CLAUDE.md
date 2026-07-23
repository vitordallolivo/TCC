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

## Contexto do plano de escrita

O plano original (16 "Ações", Capítulos 3 e 4) foi recebido do autor e está
sendo executado incrementalmente, uma Ação por vez, via task list. Não
recriar o plano do zero em sessões futuras — perguntar ao autor qual Ação
seguir, ou consultar o estado das tasks já registradas na sessão.
