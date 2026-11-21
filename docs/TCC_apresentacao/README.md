# Apresentação do TCC

> [README principal](../../README.md) · [Texto do TCC](../TCC_documento/README.md)

Slides da defesa no padrão visual UDESC (tema `beamerthemeUDESC.sty`).

```bash
make        # gera apresentacao.pdf e roteiro.pdf
make clean  # apaga os auxiliares
```

As figuras vêm direto de `../TCC_documento/Textuais/Imgs/`, então compile a partir desta pasta.

## Estrutura

```text
apresentacao.tex          # dados da capa e ordem das seções
beamerthemeUDESC.sty      # tema UDESC
figuras/                  # diagramas TikZ extraídos dos .tex do TCC
secoes/01_introducao.tex
secoes/02_fundamentacao.tex
secoes/03_metodologia.tex
secoes/04_resultados.tex
secoes/05_conclusao.tex
secoes/06_referencias.tex
roteiro.tex               # texto falado, slide a slide (gera roteiro.pdf)
ensaio_voz/               # ensaio com voz sintética (Piper) e tempos medidos
apresentacao.pdf          # versão compilada
```

## Conteúdo

O conteúdo segue o texto final do TCC (`../TCC_documento`), que é a fonte de
verdade, e as regras levantadas em `../abnt.md`. Uma figura por slide; as
frases de ligação ficam no `roteiro.tex`, não nos slides. Medida com voz
sintética em ritmo calmo: cerca de 26 minutos.
