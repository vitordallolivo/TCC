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
ensaio_voz/ensaio.py      # vídeo com voz sintética (Piper), tempo medido por slide
ensaio_voz/graficos.py    # gera tempos_prioridade.pdf
ensaio_voz/prioridade.csv # prioridade de cada slide, de 0 a 10 (editar à mão)
ensaio_voz/tempos_*.json  # tempos medidos (calmo = voz Faber; 23nov = provisório, refazer com a Faber)
apresentacao.pdf          # versão compilada
tempos_prioridade.pdf     # tempo de fala por slide e tabela de prioridade
```

## Conteúdo

O conteúdo segue o texto final do TCC (`../TCC_documento`), que é a fonte de
verdade, e as regras levantadas em `../abnt.md`. Uma figura por slide; as
frases de ligação ficam no `roteiro.tex`, não nos slides. Cada bloco do
roteiro introduz o conceito antes de chegar ao resultado.

Medida provisória com voz sintética (23/11/2026, voz substituta, refazer com a
Faber): cerca de 25 minutos, acima dos 20 da defesa. `tempos_prioridade.pdf` mostra onde estão os
slides mais longos e a prioridade de cada um, para guiar os cortes.

```bash
python -I ensaio_voz/ensaio.py roteiro.tex apresentacao.pdf saida/ 1.15 voz.onnx
python -I ensaio_voz/graficos.py saida/tempos.json ensaio_voz/prioridade.csv roteiro.tex tempos_prioridade.pdf
```
