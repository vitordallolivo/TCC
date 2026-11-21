all: apresentacao.pdf roteiro.pdf

apresentacao.pdf: apresentacao.tex beamerthemeUDESC.sty secoes/*.tex figuras/*.tex
	pdflatex -interaction=nonstopmode apresentacao.tex
	pdflatex -interaction=nonstopmode apresentacao.tex

roteiro.pdf: roteiro.tex
	pdflatex -interaction=nonstopmode roteiro.tex

clean:
	rm -f *.aux *.log *.nav *.out *.snm *.toc *.vrb
