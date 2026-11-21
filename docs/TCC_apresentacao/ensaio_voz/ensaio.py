"""Ensaio da apresentação com voz sintética (Piper, offline).

Uso: venv/bin/python -I ensaio.py <roteiro.tex> <apresentacao.pdf> <saida> [length_scale] [voz.onnx]

Gera um áudio por página a partir do roteiro, mede a duração real e monta um
vídeo com os slides trocando no ritmo da fala.
"""
import re, sys, wave, subprocess, json
from pathlib import Path
from piper import PiperVoice, SynthesisConfig

roteiro, pdf, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
escala = float(sys.argv[4]) if len(sys.argv) > 4 else 1.15   # >1 = mais calmo
PAUSA_TROCA = 1.0      # s de silêncio ao trocar de slide
SEM_FALA = 2.0         # s em divisórias e referências
out.mkdir(parents=True, exist_ok=True)

# ---- roteiro -> texto por página --------------------------------------
txt = roteiro.read_text(encoding="utf8")
txt = txt[txt.index(r"\begin{document}"):]
paginas = {}
for m in re.finditer(r"^\\slide\{(\d+)(?: e (\d+))?\}\{.*\}$", txt, re.M):
    ini = m.end()
    prox = re.search(r"^\\slide\{|^\\parte\{|^\\end\{document\}", txt[ini:], re.M)
    corpo = txt[ini: ini + prox.start()] if prox else txt[ini:]
    corpo = re.sub(r"\\avanca|\\textit\{\(Não ler.*?\)\}|\(Não ler.*?\)", " ", corpo)
    paginas[int(m.group(1))] = corpo.strip()
    if m.group(2):
        paginas[int(m.group(2))] = ""

def falavel(t):
    t = re.sub(r"\\(begin|end)\{(itemize|enumerate)\}", " ", t)
    t = t.replace(r"\item", " ")
    t = re.sub(r"\\textit\{([^}]*)\}", r"\1", t)
    t = t.replace(r"\times", " por ").replace(r"\Delta", "delta ")
    t = t.replace(r"\alpha\beta", "alfa beta").replace(r"\%", "%")
    t = t.replace("``", "").replace("''", "")
    t = re.sub(r"[*_`$\\{}]", "", t)
    t = t.replace("C_Q", "cê quê").replace("CQ", "cê quê").replace("kQ", "cá quê")
    t = re.sub(r"(\d+)x(\d+)E", r"\1 por \2 É", t)
    t = t.replace("%", " por cento").replace("–", " ").replace("×", " por ")
    t = re.sub(r"\s+", " ", t)
    return t.strip()

npag = int(re.search(r"Pages:\s+(\d+)",
           subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True).stdout).group(1))

voz = PiperVoice.load(sys.argv[5] if len(sys.argv) > 5 else str(Path(__file__).parent / "voz/pt_BR-faber-medium.onnx"))
cfg = SynthesisConfig(length_scale=escala)

# ---- áudio por página ---------------------------------------------------
dur = {}
for p in range(1, npag + 1):
    w = out / f"a{p:02d}.wav"
    texto = falavel(paginas.get(p, ""))
    with wave.open(str(w), "wb") as f:
        if texto:
            voz.synthesize_wav(texto, f, syn_config=cfg)
        else:
            f.setnchannels(1); f.setsampwidth(2); f.setframerate(22050)
            f.writeframes(b"\0\0" * int(22050 * SEM_FALA))
    with wave.open(str(w)) as f:
        dur[p] = f.getnframes() / f.getframerate() + PAUSA_TROCA

# ---- vídeo: slide + áudio, página a página -------------------------------
subprocess.run(["pdftoppm", "-r", "96", "-png", str(pdf), str(out / "s")], check=True)
lista = []
for p in range(1, npag + 1):
    img = sorted(out.glob(f"s-*{p:0{len(str(npag))}d}.png"))[0]
    seg = out / f"seg{p:02d}.mp4"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-loop", "1", "-i", str(img),
                    "-i", str(out / f"a{p:02d}.wav"), "-af", f"apad=pad_dur={PAUSA_TROCA}",
                    "-t", f"{dur[p]:.3f}", "-vf", "scale=1280:-2,format=yuv420p",
                    "-c:v", "libx264", "-tune", "stillimage", "-r", "10",
                    "-c:a", "aac", "-ar", "44100", str(seg)], check=True)
    lista.append(f"file '{seg.name}'")
(out / "lista.txt").write_text("\n".join(lista))
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0",
                "-i", str(out / "lista.txt"), "-c", "copy", str(out / "ensaio.mp4")], check=True)

# ---- relatório -----------------------------------------------------------
# faixas de páginas lidas de \parte{nome}{inicial}{final}{tempo} no roteiro
partes = [(m.group(1), int(m.group(2)), int(m.group(3)))
          for m in re.finditer(r"^\\parte\{([^}]+)\}\{(\d+)\}\{(\d+)\}", txt, re.M)]
rel = {"length_scale": escala, "por_pagina_s": {p: round(d, 1) for p, d in dur.items()}}
print(f"length_scale = {escala}")
for nome, a, b in partes:
    t = sum(dur[p] for p in range(a, b + 1))
    rel[nome] = round(t, 1)
    print(f"{nome:26s} {t/60:5.1f} min")
tot = sum(dur.values())
rel["total_s"] = round(tot, 1)
print(f"{'TOTAL':26s} {tot/60:5.1f} min  ({int(tot//60)}:{int(tot%60):02d})")
longos = sorted(dur.items(), key=lambda x: -x[1])[:5]
print("Slides mais longos:", ", ".join(f"[{p}] {d:.0f}s" for p, d in longos))
(out / "tempos.json").write_text(json.dumps(rel, indent=1, ensure_ascii=False))
