"""Gráficos de tempo de fala por slide e tabela de prioridade (0 a 10).

Uso: python graficos.py <tempos.json> <prioridade.csv> <roteiro.tex> <saida.pdf>

tempos.json sai do ensaio.py; prioridade.csv é editado à mão.
"""
import json,re,sys
TEMPOS,PRI,ROT,OUT=sys.argv[1:5]
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
d={int(k):v for k,v in json.load(open(TEMPOS))['por_pagina_s'].items()}
t=open(ROT).read()
tit={int(m.group(1)):m.group(3) for m in re.finditer(r'\\slide\{(\d+)( e \d+)?\}\{(.*)\}',t)}
fix=lambda s:s.replace('$\\times$','×').replace('$\\Delta$','Δ').replace('$\\alpha\\beta$','αβ').replace('$k_Q$','kQ')
pri={}
for l in open(PRI,encoding='utf8').read().splitlines()[1:]:
    p,n,_=l.split(';',2); pri[int(p)]=int(n)
rows=[(p,fix(tit[p]),d[p],pri[p]) for p in sorted(pri)]
total=sum(d.values())
INK='#0b0b0b'; INK2='#52514e'; BLUE='#2a78d6'; LIGHT='#a9c8ee'; ORANGE='#eb6834'
plt.rcParams.update({'font.size':8,'axes.edgecolor':'#c9c8c3','text.color':INK,'axes.labelcolor':INK2,'xtick.color':INK2,'ytick.color':INK2})
with PdfPages(OUT) as pdf:
    # página 1: tempos ordenados
    r=sorted(rows,key=lambda x:-x[2])
    fig,ax=plt.subplots(figsize=(8.27,11.69))
    cols=[BLUE if x[2]>=45 else (ORANGE if x[2]<12 else LIGHT) for x in r]
    ax.barh(range(len(r)),[x[2] for x in r],color=cols,height=0.75)
    ax.set_yticks(range(len(r))); ax.set_yticklabels([f'[{x[0]}] {x[1][:52]}' for x in r],fontsize=6.5)
    ax.invert_yaxis(); ax.set_xlabel('Tempo de fala [s]')
    for i,x in enumerate(r): ax.text(x[2]+0.6,i,f'{x[2]:.0f}',va='center',fontsize=6,color=INK2)
    ax.grid(axis='x',color='#e6e5e0',lw=0.6); ax.set_axisbelow(True)
    for s in ('top','right'): ax.spines[s].set_visible(False)
    ax.set_title(f'Tempo de fala por slide, do maior para o menor (total {total/60:.1f} min no vídeo)',loc='left',fontsize=10,fontweight='bold')
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=BLUE,label='45 s ou mais'),Patch(color=LIGHT,label='12 a 45 s'),Patch(color=ORANGE,label='menos de 12 s')],loc='lower right',frameon=False)
    fig.text(0.01,0.005,'Divisórias de seção e referências (3 s cada) ficaram de fora. Tempos medidos com voz sintética.',fontsize=6.5,color=INK2)
    fig.tight_layout(rect=(0,0.01,1,1)); pdf.savefig(fig); plt.close(fig)
    # página 2: tabela por prioridade
    r=sorted(rows,key=lambda x:(-x[3],-x[2]))
    fig,ax=plt.subplots(figsize=(8.27,11.69)); ax.axis('off')
    def sug(p,s):
        if p<=3: return 'cortar ou juntar'
        if p<=5 and s>=25: return 'enxugar fala'
        if p<=5: return 'falar rápido'
        if s>=45: return 'manter, enxugar'
        return 'manter'
    cell=[[f'[{x[0]}]',x[1][:58],f'{x[2]:.0f}',str(x[3]),sug(x[3],x[2])] for x in r]
    tb=ax.table(cellText=cell,colLabels=['Pág.','Slide','Tempo [s]','Prioridade (0–10)','Sugestão'],colWidths=[0.06,0.52,0.09,0.13,0.17],loc='upper center',cellLoc='left')
    tb.auto_set_font_size(False); tb.set_fontsize(6.3); tb.scale(1,0.94)
    seq={10:'#0d366b',9:'#1c5cab',8:'#2a78d6',7:'#5a98e0',6:'#86b4ea',5:'#a9c8ee',4:'#cfe0f5',3:'#e7eff9'}
    for (i,j),c in tb.get_celld().items():
        c.set_edgecolor('#e6e5e0')
        if i==0: c.set_facecolor('#f1f0ec'); c.set_text_props(fontweight='bold')
        elif j==3:
            p=r[i-1][3]; c.set_facecolor(seq[p]); c.set_text_props(color='white' if p>=7 else INK,ha='center')
        elif j==2: c.set_text_props(ha='right')
    cort=sum(x[2] for x in rows if x[3]<=3); enx=sum(x[2] for x in rows if 4<=x[3]<=5)
    ax.set_title(f'Prioridade de cada slide (10 = indispensável)\nPrioridade ≤3 soma {cort:.0f} s; prioridade 4–5 soma {enx/60:.1f} min',loc='left',fontsize=10,fontweight='bold')
    fig.tight_layout(); pdf.savefig(fig); plt.close(fig)

