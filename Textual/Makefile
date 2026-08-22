# ------------------------------------------------------------
#  Makefile - TCC (abnTeX2 / UDESC)
#
#    make          compila tudo (pdflatex + bibtex + pdflatex x2)
#    make quick    uma passada só de pdflatex (rascunho rápido)
#    make view     compila (se preciso) e abre o PDF
#    make watch    recompila sozinho a cada alteração (precisa de latexmk)
#    make clean    apaga os auxiliares, mantém o PDF
#    make mrproper apaga auxiliares e o PDF
#    make help     lista os alvos
# ------------------------------------------------------------

DOC      := abntex2-model_UDESC_2024
BIB      := abntex2-ref_UDESC_2020.bib
BST      := abntex2-alf_revNBR2023.bst

LATEX    := pdflatex
LATEXOPT := -interaction=nonstopmode -file-line-error -halt-on-error
BIBTEX   := bibtex
LATEXMK  := latexmk
VIEWER   := xdg-open

IMGDIRS  := Textuais/Imgs PreTextuais PosTextuais
IMGEXT   := \( -name '*.png' -o -name '*.jpg' -o -name '*.jpeg' \
              -o -name '*.pdf' -o -name '*.eps' \)

# Fontes que, se mudarem, obrigam a recompilar.
SOURCES  := $(shell find . -name '*.tex' -not -path './.git/*') $(BIB) $(BST)

# As imagens não entram como pré-requisito direto porque alguns arquivos têm
# espaço no nome, e o make não lida bem com isso. Em vez disso procura-se
# alguma imagem mais nova que o PDF; se houver, força-se a recompilação.
IMG_NEWER := $(shell test -f $(DOC).pdf && \
             find $(IMGDIRS) -type f $(IMGEXT) -newer $(DOC).pdf -print -quit 2>/dev/null)

.PHONY: all pdf quick bib view watch clean mrproper help errors warnings FORCE

all: pdf

# ---- compilação completa -----------------------------------
pdf: $(DOC).pdf

ifneq ($(IMG_NEWER),)
$(DOC).pdf: FORCE
endif
FORCE:

$(DOC).pdf: $(SOURCES)
	@echo "==> pdflatex (1/3)"
	@$(LATEX) $(LATEXOPT) $(DOC) > /dev/null || { $(MAKE) --no-print-directory errors; exit 1; }
	@echo "==> bibtex"
	@$(BIBTEX) $(DOC) > /dev/null || true
	@echo "==> pdflatex (2/3)"
	@$(LATEX) $(LATEXOPT) $(DOC) > /dev/null || { $(MAKE) --no-print-directory errors; exit 1; }
	@echo "==> pdflatex (3/3)"
	@$(LATEX) $(LATEXOPT) $(DOC) > /dev/null || { $(MAKE) --no-print-directory errors; exit 1; }
	@$(MAKE) --no-print-directory warnings
	@echo "==> pronto: $(DOC).pdf"

# ---- uma passada só (rascunho) -----------------------------
quick:
	@echo "==> pdflatex (passada única)"
	@$(LATEX) $(LATEXOPT) $(DOC) > /dev/null || { $(MAKE) --no-print-directory errors; exit 1; }
	@$(MAKE) --no-print-directory warnings
	@echo "==> pronto: $(DOC).pdf (referências podem estar desatualizadas)"

# ---- só a bibliografia -------------------------------------
bib:
	@$(BIBTEX) $(DOC)

# ---- abrir o PDF -------------------------------------------
view: pdf
	@$(VIEWER) $(DOC).pdf >/dev/null 2>&1 &

# ---- recompilar continuamente ------------------------------
watch:
	@command -v $(LATEXMK) >/dev/null 2>&1 || \
	    { echo "latexmk não instalado (sudo apt install latexmk)"; exit 1; }
	$(LATEXMK) -pdf -bibtex -pvc -interaction=nonstopmode $(DOC)

# ---- diagnóstico -------------------------------------------
errors:
	@echo "------------------- ERROS -------------------"
	@grep -E "^(\./)?[^ ]*:[0-9]+:|^! " $(DOC).log 2>/dev/null | head -30 || true
	@echo "---------------------------------------------"
	@echo "log completo em $(DOC).log"

warnings:
	@out=$$(grep -E "(Reference|Citation) .* undefined" $(DOC).log 2>/dev/null | sort -u); \
	if [ -n "$$out" ]; then \
	    echo "--- referências/citações não resolvidas ---"; \
	    echo "$$out" | sed 's/^/    /' | head -20; \
	fi

# ---- limpeza -----------------------------------------------
clean:
	@rm -f $(DOC).aux $(DOC).log $(DOC).toc $(DOC).lof $(DOC).lot $(DOC).loq \
	       $(DOC).out $(DOC).bbl $(DOC).blg $(DOC).brf $(DOC).idx $(DOC).ind \
	       $(DOC).ilg $(DOC).glo $(DOC).gls $(DOC).glg $(DOC).acn $(DOC).acr \
	       $(DOC).alg $(DOC).fls $(DOC).fdb_latexmk $(DOC).synctex.gz \
	       $(DOC).run.xml $(DOC).bcf
	@find PreTextuais Textuais PosTextuais -name '*.aux' -delete 2>/dev/null || true
	@echo "==> auxiliares removidos"

mrproper: clean
	@rm -f $(DOC).pdf
	@echo "==> PDF removido"

help:
	@echo "make          - compila tudo (pdflatex + bibtex + pdflatex x2)"
	@echo "make quick    - uma passada só de pdflatex (rascunho)"
	@echo "make view     - compila e abre o PDF"
	@echo "make watch    - recompila a cada alteração (latexmk)"
	@echo "make bib      - roda só o bibtex"
	@echo "make clean    - apaga auxiliares"
	@echo "make mrproper - apaga auxiliares + PDF"
