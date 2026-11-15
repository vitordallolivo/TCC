# Apresentação do TCC

> [README principal](../../README.md) · [Texto do TCC](../TCC_documento/README.md)

Slides da defesa no padrão visual UDESC (tema `beamerthemeUDESC.sty`).

```bash
make        # gera apresentacao.pdf (pdflatex, duas passadas)
make clean  # apaga os auxiliares
```

As figuras vêm direto de `../TCC_documento/Textuais/Imgs/`, então compile a partir desta pasta.

## Estrutura

```text
apresentacao.tex          # dados da capa e ordem das seções
beamerthemeUDESC.sty      # tema UDESC
figuras/                  # diagramas TikZ extraídos dos .tex do TCC
secoes/01_introducao.tex
secoes/02_planejamento.tex
secoes/03_metodologia.tex
secoes/04_resultados.tex
secoes/05_conclusao.tex
secoes/06_referencias.tex
roteiro.md                # texto falado, slide a slide (⏭ = próxima página)
apresentacao.pdf          # versão compilada
```

## O que ainda é ilustrativo

Os slides marcados com a etiqueta vermelha **RESULTADO ILUSTRATIVO** trazem
conteúdo provisório, escrito supondo o trabalho concluído com bons resultados:

- **Validação com breakpoints**: espaço reservado para a captura da depuração.
- **Ensaio integrado**: espaço reservado para o gráfico de tração e torque.
- **Conclusão**: redigida supondo a validação integrada bem-sucedida.

Os demais resultados vêm do texto do TCC. A apresentação foi pensada para
20 minutos e não traz valores numéricos nos slides.
