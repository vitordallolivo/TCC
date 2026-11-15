# human.md — pendências do texto (atualizado em 15/11/2026)

Arquivo enxugado: só o estado atual e o que ainda pede decisão do autor ou
revisão do orientador. O histórico completo das alterações está no git.

## Estado atual

- SIBA e SAME não aparecem mais no texto: tudo é **placa de medição**
  (Cap. 1, 3, 4, figuras, siglas, resumo e *abstract*).
- Cap. 4: hélices APC 15x6E e 17x10E em "Ensaios de Bancada com o Motor"
  (`sec:resultado_coef_aero_bancada`), uma tabela e uma figura por hélice, com
  erro em relação ao teórico e curva ajustada. Dados em
  `Textuais/Imgs/Resultados/helice/` (CSV completo + `medido_*.dat`, já
  corrigidos para o divisor de 9,1 kΩ).
- Cap. 4: "Caracterização do Sensor de Corrente" virou seção própria (4.2),
  logo após Medição de Parâmetros (arquivo `Resultados/001_SensorCorrente.tex`).
- Indutância: ressonância (6,95 µH) + ajuste aos registros de osciloscópio
  (11,3 µH, faixa 8,7–15,2 µH entre sessões), com gráfico de barras do erro;
  método descrito em `sec:metodo_L_ajuste`.
- Revisão: "Transformadas de Clarke e Park" — Clarke de 3 termos (como no
  firmware), Park com θe = ωe·t + φ0 e figura dos referenciais; funções de
  forma trapezoidais por partes e derivadas saíram (o observador usa sen/−cos).
- Sensibilidade do autotune (17x10E) em três subfiguras com eixo ΔΓ; gerada
  por script no scratchpad a partir de `autotune_cost.py` (código intocado).
- Fig. "Camadas lógicas": aplicação no computador, System e Vcore com módulos;
  texto explica que a aplicação fica na GUI para poupar RAM.
- Cap. 5: Discussão (resumo + hélices, indutância, faixa de corrente, montagem
  e mau contato, memória/rede com gráfico de RAM) → Conclusões → Trabalhos
  Futuros (só sugestões genéricas para próximos TCCs).
- Listas de siglas e símbolos revisadas (removidos Pitot/extensometria e siglas
  sem uso; incluídos ΔΓ, dq, DHCP, EEPROM, RAM etc.).

- Metodologia: "Padronização das Placas" virou as subseções "Microcontrolador
  e Comunicação" e "Sistema Operacional --- Vcore" dentro de "Placa de
  Medição" (`07_SIBA.tex`); `05_Placas.tex` e `PosTextuais/Anexos.tex` apagados.
- Cap. 4 termina com "Discussão dos Resultados"; Cap. 5 "Conclusão" segue o
  modelo: objetivos numerados → Contribuições → Limitações → Trabalhos
  Futuros → Considerações Finais.

## Para o autor decidir

- **Células de carga**: saíram dos objetivos, mas as menções ficam no texto.
  Pitot e o anexo de calibração do Pitot foram removidos (15/11).
- **`conector_celulas.jpeg`** (Imgs/Resultados) ainda não foi usada.
- **Coeficiente de corrente no firmware**: o texto usa o divisor de 9,1 kΩ
  (0,0809 A/mV), mas a placa ainda está com 0,0784 A/mV (10 kΩ) na EEPROM.
  Gravar o valor novo por CMD antes dos próximos ensaios.
- **Capítulo 3 e segurança**: a metodologia descreve pouco as condições para
  armar a bancada (botão liberado + "pronto" do supervisório), a pré-carga de
  5 s e a sobrescrita do botão por comando.

## Para o orientador revisar

- **Hélices (4.6.5)**: 1º ensaio +19 a +47 % acima da APC (perdas dos
  registros de osciloscópio); 2º ensaio (perdas a vazio levantadas de novo na
  mesma montagem, B = 101e-6, tau_c = 20,7 mN.m) −2,8 a +0,3 % (15x6E) e
  +5,3 a +8,6 % (17x10E). Afastar a mesa não mudou nada. Pontos < 2500 rpm usam
  perdas extrapoladas (o ESC não sustenta < 2500 rpm a vazio). CSVs em
  `Imgs/Resultados/helice/ensaio_repeticao_*.csv` e `ensaio_vazio_*.csv`.
- **Indutância**: os dois métodos e a diferença já estão no texto (4.1.2 e
  5.1.2, citando Hurley e Wölfle). Conferir se a explicação satisfaz.
- **Gráfico de RAM (5.1.5)**: números do `ram_report` do build atual; se o
  firmware mudar, refazer.
- **Aplicação de supervisão**: bug aberto — conectar, desconectar e conectar
  de novo deixa os gráficos sem voltar ao normal. E a bancada arma sozinha ao
  conectar (o "pronto" vai no primeiro pacote e a cada 30 s).
