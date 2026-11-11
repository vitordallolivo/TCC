# Apresentação do TCC

Slides da defesa no padrão visual UDESC (tema `beamerthemeUDESC.sty`).

```
make        # gera apresentacao.pdf (pdflatex, duas passadas)
make clean
```

As figuras vêm direto de `../Textual/Textuais/Imgs/`, então compile a partir desta pasta.

## Estrutura

```
apresentacao.tex          # dados da capa e ordem das seções
beamerthemeUDESC.sty      # tema UDESC
secoes/01_introducao.tex
secoes/02_metodologia.tex
secoes/03_resultados.tex
secoes/04_conclusao.tex
secoes/05_referencias.tex
```

## O que ainda é ilustrativo

Os slides marcados com a etiqueta vermelha **RESULTADO ILUSTRATIVO** trazem
conteúdo provisório, escrito supondo o trabalho concluído com bons resultados:

- **Validação da SIBA**: erros por sensor são valores de exemplo.
- **Ensaio integrado**: espaço reservado para o gráfico de tração e torque.
- **Conclusão**: redigida supondo a validação integrada bem-sucedida.

Os demais resultados (R, L, perdas mecânicas, observador em bancada, torque
da hélice) são os do texto do TCC.
