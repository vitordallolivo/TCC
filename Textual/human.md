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
- Cap. 5: Discussão (hélices) → Conclusões (retoma os cinco objetivos) →
  Trabalhos Futuros.

## Para o autor decidir

- **Pitot e células de carga**: saíram dos objetivos, mas a subseção
  "Calibração do Tubo de Pitot" e menções às células de carga ficam no texto
  (decisão de 15/11: manter por enquanto).
- **`conector_celulas.jpeg`** (Imgs/Resultados) ainda não foi usada.
- **Coeficiente de corrente no firmware**: o texto usa o divisor de 9,1 kΩ
  (0,0809 A/mV), mas a placa ainda está com 0,0784 A/mV (10 kΩ) na EEPROM.
  Gravar o valor novo por CMD antes dos próximos ensaios.
- **Capítulo 3 e segurança**: a metodologia descreve pouco as condições para
  armar a bancada (botão liberado + "pronto" do supervisório), a pré-carga de
  5 s e a sobrescrita do botão por comando.

## Para o orientador revisar

- **Hélices**: torque estimado 16–20 % (17x10E) e 21–41 % (15x6E) acima da
  APC. A discussão (5.1) atribui a três parcelas: teórico simulado (e hélice a
  menos de um diâmetro de um móvel), modelo de perdas de outra sessão, viés do
  observador (+4 a 12 % na simulação). Estimativa independente pela potência
  elétrica: ~+18 % na 15x6E a 2930 rpm.
- **Indutância**: o texto apresenta 6,95 µH (ressonância); o observador e a
  simulação usam 11,3 µH (ajustada nos registros de osciloscópio). Conferir se
  o texto explica a diferença.
- **Aplicação de supervisão**: bug aberto — conectar, desconectar e conectar
  de novo deixa os gráficos sem voltar ao normal. E a bancada arma sozinha ao
  conectar (o "pronto" vai no primeiro pacote e a cada 30 s).
