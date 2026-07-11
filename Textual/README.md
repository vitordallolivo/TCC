# Texto do TCC (LaTeX / abnTeX2)

Fonte LaTeX do trabalho, baseado no modelo [abnTeX2](https://www.abntex.net.br/) customizado pela UDESC/CCT
(`abntex2-model_UDESC_2024.tex`).

## Estrutura

```
abntex2-model_UDESC_2024.tex   # arquivo principal (compilar este)
PacotesBasicos.tex             # pacotes e configurações compartilhadas
abntex2-alf_revNBR2023.bst     # estilo de bibliografia (bibtex) já incluso no repo
abntex2-ref_UDESC_2020.bib     # base de referências bibliográficas
PreTextuais/                   # capa, resumo, listas, etc.
Textuais/                      # introdução, revisão, metodologia, conclusão
PosTextuais/                   # apêndices, anexos, glossário, índice remissivo
```

## Dependências

É necessária uma distribuição LaTeX completa (o documento compila com **pdflatex**, não precisa de
XeLaTeX/LuaLaTeX). Recomenda-se o **TeX Live**.

### Debian / Ubuntu

```bash
sudo apt install \
    texlive-latex-base \
    texlive-latex-recommended \
    texlive-latex-extra \
    texlive-fonts-recommended \
    texlive-lang-portuguese \
    texlive-pictures \
    texlive-science \
    texlive-publishers
```

Esses pacotes cobrem, entre outros: `abntex2` e `abntex2cite` (`texlive-publishers`), `circuitikz` e
`pgfplots` (`texlive-pictures`), `algorithm`/`algpseudocode`/`chemformula`/`siunitx`
(`texlive-science`), e o restante das dependências de formatação (`microtype`, `xstring`, `adjustbox`,
`chngcntr`, `lastpage`, `backref`, `xfrac` etc.).

Se preferir não se preocupar com pacotes individuais, instale a distribuição completa:

```bash
sudo apt install texlive-full
```

Opcional, só é necessário se for automatizar a build com `latexmk`:

```bash
sudo apt install latexmk
```

### Windows

Instale o [MiKTeX](https://miktex.org/download) (instala pacotes faltantes sob demanda ao compilar) ou
o TeX Live para Windows.

### macOS

Instale o [MacTeX](https://www.tug.org/mactex/).

## Como compilar

O documento usa **bibtex** (não biber) para a bibliografia, com o estilo `.bst` incluso no repositório.
A partir desta pasta (`Textual/`):

```bash
pdflatex abntex2-model_UDESC_2024
bibtex   abntex2-model_UDESC_2024
pdflatex abntex2-model_UDESC_2024
pdflatex abntex2-model_UDESC_2024
```

As duas últimas passadas de `pdflatex` são necessárias para resolver referências cruzadas, sumário e
citações (padrão do LaTeX).

Se tiver `latexmk` instalado, uma única chamada resolve as passagens automaticamente:

```bash
latexmk -pdf -bibtex abntex2-model_UDESC_2024
```

O PDF final é gerado como `abntex2-model_UDESC_2024.pdf`.

## Observação

Todos os arquivos gerados pela compilação (`.aux`, `.log`, `.toc`, `.bbl`, `.pdf`, etc.) são ignorados
pelo `.gitignore` desta pasta — não é necessário (nem recomendado) versioná-los.
