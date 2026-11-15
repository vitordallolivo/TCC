# Roteiro da apresentação

Texto de apoio para a fala, um bloco por slide. Quando aparecer **⏭**, avance para a próxima página. O número entre colchetes é a página do PDF. Os blocos marcados com 🔶 tratam de resultados ilustrativos, a substituir pelos resultados reais antes da defesa.

---

*[1] Capa*

Bom dia a todos. Meu nome é Vitor Dall'Olivo Gonçalves e apresento o Trabalho de Conclusão de Curso intitulado Bancada de Medição para Sistemas Propulsivos Elétricos com Motor BLDC, orientado pela professora Ana Teruko Yokomizo Watanabe e coorientado por Aureo Guilherme Dobrikopf, desenvolvido em parceria com a equipe Albatroz Aerodesign.

**⏭**

*[2] Albatroz Aerodesign*

A Albatroz é um projeto de extensão da UDESC Joinville que projeta e fabrica aeronaves radiocontroladas para a competição SAE Brasil Aerodesign. Nessa competição, a pontuação depende diretamente da carga útil transportada, e quem define essa carga é o conjunto motopropulsor: motor, hélice e bateria. Desde 2022, o regulamento permite apenas motores elétricos sem escovas, os motores BLDC.

**⏭**

*[3] Ensaios descentralizados*

O problema identificado é a descentralização dos ensaios. À esquerda está o conjunto utilizado pela equipe, o motor Scorpion A-5025 com bateria LiPo de seis células; à direita, a bancada de desempenho existente, que mede tração e velocidade do ar. Cada grandeza é levantada em um ensaio separado, sem registro conjunto.

Falta, sobretudo, a medida no eixo. Rotação e torque não são medidos diretamente, pois instalar tacômetro ou transdutor de torque no eixo é inconveniente e, no caso do transdutor, caro. Recorre-se então a estimativas de catálogo, obtidas em condições diferentes das da equipe, o que pode levar a erros de projeto. A proposta deste trabalho é medir esse último braço, rotação e torque no eixo, sem sensor mecânico.

**⏭**

*[4] Conjugado da hélice*

O conjugado exigido pela hélice depende da densidade do ar, do diâmetro, do quadrado da rotação e do coeficiente de torque, próprio de cada hélice. O torque cresce, portanto, de forma quadrática com a rotação, e o coeficiente define essa parábola.

Os gráficos apresentam os coeficientes de tração e de potência de hélices bipás em função da razão de avanço. Na bancada estática não há vento incidente, a razão de avanço é praticamente nula e os coeficientes assumem seu valor máximo, condição equivalente à decolagem. Os fabricantes, em geral, não fornecem o coeficiente de torque diretamente, apenas o de potência.

**⏭**

*[5] Torque define o envelope*

O ponto de operação do conjunto é a intersecção entre a curva de torque do motor e a curva de torque da hélice. O torque, variável não medida nos ensaios atuais, é o que determina o envelope de operação do motor: a rotação de trabalho, a corrente consumida e a tração obtida.

Foram adotadas duas hélices de referência, APC 15x6E e APC 17x10E, utilizadas primeiro em simulação, com os dados de desempenho publicados pelo fabricante, e depois em ensaio de bancada.

**⏭**

*[6] Objetivos*

O objetivo geral é desenvolver um sistema integrado de aquisição de dados, adaptando a bancada de ensaios motopropulsores existente na equipe.

Os objetivos específicos são: projetar e fabricar o sistema de medição de tensão e corrente do motor; modelar o motor e projetar um observador de estados para estimar força contraeletromotriz, rotação e torque; integrar os sensores aerodinâmicos da bancada; interligar os nós de aquisição em rede Ethernet; e desenvolver um software de supervisão em Python.

**⏭**

*[7] Planejamento*

O desenvolvimento foi organizado em três janelas de decisão. Em Q1 são definidas as restrições e escolhas de projeto; em Q2, cada parte é validada isoladamente; em Q3, o sistema é integrado e finalizado.

Quatro frentes percorrem essas janelas em paralelo: CAD, SIBA, interface de usuário e SAME. Como premissa, tudo o que fosse possível foi simulado antes da disponibilidade do hardware, permitindo desenvolver firmware, interface e observador enquanto as placas eram fabricadas.

**⏭**

*[8] Planejamento: CAD*

A frente de CAD parte do diagrama de alimentação e da definição dos equipamentos principais. Em seguida, define-se a localização e as restrições físicas de cada placa, elabora-se o esboço da bancada e, por fim, realiza-se a montagem no gabinete.

**⏭**

*[9] Planejamento: SIBA*

A SIBA, sistema da bancada aerodinâmica, divide-se em duas trilhas. No hardware, parte-se dos requisitos de segurança, alimentação, Ethernet e escolha do microcontrolador até o protótipo, com retorno ao esquemático em caso de reprovação. No firmware, o desenvolvimento começa em placas de desenvolvimento, sobre o sistema operacional de tempo real Zephyr, com validação camada a camada. As duas trilhas convergem na SIBA completa.

**⏭**

*[10] Planejamento: Interface*

A interface foi desenvolvida em Python, com PyQt e Qt Designer. Arquivos de configuração e interface gráfica convergem nos arquivos YAML. Um terminal embutido, o TinyTerminal, permitiu testar parâmetros e comunicação antes das telas definitivas, e o retorno dos membros da equipe orientou as funcionalidades finais de calibração, estimação e registro em CSV.

**⏭**

*[11] Planejamento: SAME*

A SAME, sistema de aquisição do motor, reúne três trilhas: código, placa e motor. Do lado do motor, os parâmetros foram levantados, as simulações e o observador evoluíram em conjunto e tudo foi testado em placa de desenvolvimento. Do lado da placa, seguiu-se o mesmo fluxo de alimentação, condicionamento, esquemático e protótipo, convergindo nas validações.

**⏭**

*[12] Arquitetura*

A SIBA monitora a bancada aerodinâmica e a bateria; a SAME monitora o motor. As duas placas se conectam por um switch de rede ao computador de supervisão.

Ambas utilizam o microcontrolador CH32V307, RISC-V com unidade de ponto flutuante e conversor analógico-digital rápido. O fator decisivo na escolha foi a Ethernet integrada, que atende a uma demanda da própria equipe por comunicação padronizada, sem conversores intermediários. A premissa da arquitetura é que qualquer parâmetro de calibração possa ser alterado pela interface, sem regravar o firmware; os coeficientes ficam armazenados em memória não volátil na placa.

**⏭**

*[13] Supervisório*

À esquerda, a tela principal, com os gráficos em tempo real. À direita, a configuração de ganho e offset dos sensores: o usuário ajusta a calibração, a placa grava o novo valor e devolve a confirmação pela telemetria.

**⏭**

*[14] Camadas da interface*

A interface opera com o hardware real, com um simulador ou com o native_sim, recurso do Zephyr que executa o próprio firmware no computador, alterando apenas a última camada, a de transporte. O restante da aplicação permanece o mesmo nos três casos.

**⏭**

*[15] Configuração por YAML*

Os arquivos YAML são a fonte única de verdade do sistema. Na compilação, um script gera a partir deles os cabeçalhos do firmware das duas placas; na execução, a interface lê os mesmos arquivos. Firmware e interface interpretam os pacotes sempre da mesma forma, e a inclusão de um sensor ou comando exige alterar um único arquivo.

**⏭**

*[16] Observador de estados*

Para obter rotação e torque sem sensor no eixo, emprega-se um observador de estados: um modelo do motor executado no microcontrolador, em paralelo com o motor real. O erro entre a corrente medida e a estimada corrige o modelo continuamente, e dele se extraem a força contraeletromotriz, a posição, a rotação e o torque.

Optou-se pelo observador por modos deslizantes pela robustez a incertezas nos parâmetros do motor. Sua contrapartida é o chattering, oscilação de alta frequência inerente à técnica, cujo tratamento será apresentado nos resultados. Os parâmetros do modelo foram medidos com o rotor bloqueado: a resistência pelo método de quatro fios e a indutância pelo método da ressonância.

**⏭**

*[17] Sensor de corrente customizado*

O observador exige a corrente das três fases, com isolação e na faixa de dezenas de ampères. Sensores comerciais com essas características têm custo elevado, multiplicado por três. Optou-se, então, por um sensor próprio: um núcleo toroidal de ferrite envolvendo o condutor, com um sensor Hall no entreferro. A escolha dessa topologia será detalhada na seção das placas.

**⏭**

*[18] Estimação em partes*

A planta gera as correntes de fase; a transformada de Clarke as leva ao referencial alfa-beta; o observador estima a força contraeletromotriz bruta; o PLL extrai dela ângulo e velocidade; a força contraeletromotriz é reconstruída analiticamente e, com ela, calcula-se o torque.

A reconstrução analítica substitui a abordagem clássica, de arco tangente seguido de filtro passa-baixas, cujo atraso de fase causava perda de sincronismo nas desacelerações. Cada bloco será apresentado a seguir.

**⏭**

*[19] Inversor simulado*

Para gerar as grandezas de referência, foi montada uma planta completa: um controlador PI de velocidade gera a tensão de comando, um inversor de seis passos, como em um ESC comercial, a aplica nas fases, e os modelos elétrico e mecânico do motor, com a carga da hélice, fecham a malha. As grandezas dessa planta são tratadas como os valores reais na comparação com o observador.

**⏭**

*[20] Inversor simulado: resposta*

A velocidade segue os patamares de referência, com a hélice 17x10E. O perfil inclui subidas e descidas propositalmente, já que as desacelerações eram o ponto crítico das versões anteriores do observador.

**⏭**

*[21] Dinâmica do motor*

Cada fase é modelada por resistência, indutância e força contraeletromotriz, em ligação estrela. Por se tratar de um motor com ímãs na superfície do rotor, a indutância pode ser considerada igual nos dois eixos.

À direita, o torque da planta. Observa-se a ondulação imposta pelo acionamento em seis passos, muito maior que o torque médio da hélice, que é a grandeza de interesse.

**⏭**

*[22] Observador por modos deslizantes*

O erro de corrente passa por uma função de chaveamento suavizada, uma saturação no lugar da função sinal, para reduzir o chattering. Os ganhos deslizantes corrigem a estimativa de corrente e a de força contraeletromotriz, que segue ao PLL. O observador é discretizado e executado amostra a amostra.

**⏭**

*[23] Observador: correntes e FEM*

As correntes e a força contraeletromotriz estimadas acompanham as reais nos dois eixos, com o chattering característico do modo deslizante. Esse ruído impede o uso direto da força bruta no cálculo do torque, o que motiva o PLL.

**⏭**

*[24] PLL e reconstrução da FEM*

O PLL trava na posição do rotor: o erro de fase é obtido projetando a força bruta sobre o ângulo estimado e se anula quando há sincronismo. Um filtro PI fornece a velocidade e um integrador, o ângulo. A formulação vale tanto para força contraeletromotriz senoidal quanto trapezoidal. Com ângulo e velocidade, a força é reconstruída sem chattering.

**⏭**

*[25] PLL: ângulo estimado*

O ângulo estimado acompanha o real, com erro de poucos graus. O erro é maior no patamar de menor velocidade, pois a amplitude da força contraeletromotriz é proporcional à rotação e o sinal se aproxima do ruído. Esse limite reaparece no ensaio de bancada.

**⏭**

*[26] Função custo*

Ganhos altos aceleram a convergência, mas ampliam o chattering; ganhos baixos suavizam, mas atrasam a resposta. Para tornar a escolha repetível, definiu-se a função custo J, que mede o erro de torque e de rotação em cada patamar, com peso maior no torque. No termo de torque usa-se a perda de Huber, para que as quedas momentâneas nas comutações do inversor não dominem o custo.

A minimização emprega evolução diferencial, adequada a uma função não convexa. A otimização é feita sobre os ganhos multiplicados pelo passo de amostragem, de modo que o resultado não dependa do passo da simulação e possa ser transferido ao firmware.

**⏭**

*[27] Sensibilidade dos ganhos*

Cada curva mostra o custo ao variar um ganho em torno do ótimo. O primeiro está no fundo de um vale; o segundo, em uma região plana próxima a uma subida abrupta, onde o observador diverge; o terceiro praticamente não altera o custo.

A busca convergiu sem ajuste manual, e os ganhos praticamente coincidiram para as duas hélices. Como o procedimento depende apenas de resistência, indutância e perfil de velocidade, pode ser repetido para outros motores.

**⏭**

*[28] Rotação e torque simulados*

A rotação estimada acompanha os patamares, com pequeno atraso nas transições. O torque estimado oscila em torno do real, oscilação herdada do acionamento em seis passos. Como o torque médio da hélice é cerca de vinte vezes menor que essa ondulação, a identificação da hélice utiliza a corrente medida projetada no ângulo estimado, e não a corrente estimada.

**⏭**

*[29] Ensaio sem as placas de aquisição*

Antes da conclusão das placas, realizou-se um ensaio com osciloscópio PicoScope, com o motor girando em vazio. Os quatro canais analógicos registraram duas tensões e duas correntes de fase; as terceiras foram reconstruídas, pois as três somam zero. Os canais digitais registraram o comando do ESC e um tacômetro óptico.

A alimentação foi feita por bateria, para evitar laço de terra com as pontas do osciloscópio. Como o neutro do motor não é acessível, ele foi reconstruído com três resistores iguais em um nó comum.

**⏭**

*[30] Formas de onda do ensaio*

Correntes, tensões e força contraeletromotriz estimada no referencial alfa-beta. As componentes da força aparecem em quadratura, com amplitude crescente com a rotação, comportamento esperado.

Os mesmos registros validaram o tacômetro contra a frequência elétrica e forneceram a constante de força contraeletromotriz, próxima à de placa do motor. O espectro mostrou que o ruído do sensor sem filtro ocupava parte significativa do conversor, o que definiu por medida o filtro das placas, igual nos ramos de tensão e corrente para preservar a fase entre eles.

**⏭**

*[31] Perdas mecânicas*

Em vazio, todo o torque é consumido pelas perdas. Sem transdutor de torque, ele foi obtido pelo balanço de potência por fase, cálculo independente do observador. A regressão separa uma parcela proporcional à rotação, o atrito viscoso, e uma constante, o atrito seco. A parcela constante elevada sugere contribuição da histerese no ferro do estator. Esse modelo é usado para descontar as perdas no ensaio com hélice.

**⏭**

*[32] Observador nos dados de bancada*

Aplicado aos dados reais, o observador acompanha o tacômetro em todos os patamares, e o torque estimado se mantém próximo ao obtido pelo balanço de potência.

Há um limite inferior de operação: em baixa rotação, a força contraeletromotriz se torna pequena e o PLL perde o travamento. A velocidade continua disponível, mas o torque não. A faixa de interesse, de tração na decolagem, está acima desse limite.

**⏭**

*[33] Coeficiente de torque da hélice*

Para identificar o coeficiente, o motor é levado a um patamar de rotação; após a estabilização, registram-se rotação, torque e densidade do ar, e calcula-se o coeficiente do patamar, repetindo o processo para o patamar seguinte.

Na simulação, o coeficiente estimado convergiu para o nominal nas duas hélices, mesmo com a ondulação muito superior ao torque médio. Em bancada, a comparação com o fabricante é feita patamar a patamar, pois o coeficiente varia com a rotação.

**⏭**

*[34] Escolha do sensor de corrente*

Inicialmente considerou-se um shunt com amplificador isolado. O shunt, porém, precisa ser dimensionado conforme a carga, e para a corrente esperada, de pelo menos 50 A por fase, esse dimensionamento encarece a solução. O transformador de corrente e a bobina de Rogowski medem apenas corrente alternada. Restou o sensor Hall, escolhido pelo custo: um sensor Hall simples com um toroide suporta essa corrente sem dificuldade. A única desvantagem é o tamanho do conjunto.

**⏭**

*[35] Placas de aquisição*

A AQ1 é a placa de condicionamento da SAME: as três fases do motor atravessam os toroides e a placa entrega ao microcontrolador as três correntes e as três tensões condicionadas. O condicionamento foi simulado no LTspice antes da fabricação.

No gabinete, a bateria alimenta o ESC por dois relés: o de pré-carga, com resistor em série para limitar o pico de corrente dos capacitores do ESC, e o de potência. O botão de emergência interrompe os relés fisicamente, sem depender do microcontrolador.

**⏭**

*[36] Sensor de corrente: caracterização*

A fonte disponível não alcançava a corrente da aplicação. Como o campo produzido por N espiras equivale ao de uma corrente N vezes maior, o sensor foi ensaiado com uma, quatro e oito espiras. A saída foi linear nas três configurações, sem saturação do núcleo, e a sensibilidade ficou dentro da faixa de catálogo.

A tensão de repouso do sensor varia entre unidades e com a temperatura. Por ser comum às três fases, esse erro atravessa a transformada de Clarke e contamina a força contraeletromotriz estimada. Por isso, o firmware mede o repouso a cada energização, com o ESC parado.

**⏭**

*[37] Lógica de aquisição*

Os dois conversores operam em modo simultâneo, um nas correntes e outro nas tensões, de modo que tensão e corrente da mesma fase são amostradas no mesmo instante, condição para o observador. A cada conversão, o observador é executado diretamente, com tempo garantido, e o resultado segue como telemetria pela rede até o computador.

**⏭**

*[38] Validação com breakpoints* 🔶

O firmware foi validado na placa por depuração com breakpoints, conferindo os valores em cada etapa, da leitura do conversor à montagem do pacote. Como o código é o mesmo já testado no native_sim, restou validar o que depende do hardware: temporização dos periféricos, DMA e pinagem.

**⏭**

*[39] Ensaio integrado* 🔶

Com SIBA e SAME operando em conjunto, tração, torque e rotação passam a constar do mesmo registro, sincronizados e exportados em CSV. O torque vem do observador, sem sensor no eixo, e a tração, das células de carga. Curvas que antes exigiam ensaios separados e estimativas de catálogo passam a ser obtidas em um único ensaio.

**⏭**

*[40] Conclusão* 🔶

A bancada foi integrada e está operacional. Rotação e torque no eixo passaram a ser obtidos sem sensor mecânico, por um observador validado em simulação e em dados reais. As grandezas elétricas e aerodinâmicas constam do mesmo registro, e toda a calibração é feita pela interface.

O procedimento é repetível para outros motores e hélices, permitindo que a escolha do conjunto motopropulsor se apoie em ensaio próprio, e não apenas em estimativas.

**⏭**

*[41] Trabalhos futuros*

Como trabalhos futuros, sugerem-se: uso de dinamômetro como referência direta de torque; levantamento da curva de carga da bateria; estudo do efeito da temperatura da bateria na tração; extensão do observador para a região de enfraquecimento de campo; e evolução para uma bancada com seis graus de liberdade.

**⏭**

*[42] Referências*

Estas são as principais referências utilizadas.

**⏭**

*[43] Obrigado*

Agradeço a atenção e fico à disposição para as perguntas.
