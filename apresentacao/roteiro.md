# Roteiro da apresentação

Texto para ler em voz alta durante a apresentação. Cada bloco corresponde a um slide; quando aparecer **⏭**, passe para a próxima página. O número entre colchetes é a página do PDF, só para você se localizar.

Tempo total: cerca de 20 minutos, em média meio minuto por página. Os trechos marcados com 🔶 falam de resultados ilustrativos: troque pelo resultado real antes da defesa.

---

*[1] Capa*

Bom dia a todos. Meu nome é Vitor Dall'Olivo Gonçalves e vou apresentar meu Trabalho de Conclusão de Curso, uma bancada de medição para sistemas propulsivos elétricos com motor BLDC, orientada pela professora Ana Watanabe e coorientada pelo Aureo Dobrikopf.

**⏭**

*[2] Albatroz Aerodesign*

O trabalho nasceu dentro da equipe Albatroz Aerodesign, que projeta e constrói aeronaves para a competição SAE Brasil Aerodesign. Pelo regulamento, o motor precisa ser elétrico sem escovas, e o conjunto propulsor é o que define quanta carga útil a aeronave consegue levantar, que é exatamente o que dá pontos na competição.

**⏭**

*[3] Ensaios descentralizados*

Hoje a equipe ensaia esse conjunto de forma descentralizada. De um lado temos o motor e a bateria, do outro a bancada que mede tração e velocidade do ar. O que falta é a medida direta do que acontece no eixo: rotação e torque. Medir isso diretamente costuma ser inconveniente, então o que se faz são estimativas, e estimativas acabam levando a erros de projeto.

**⏭**

*[4] Conjugado da hélice*

Para entender por que o torque importa tanto, olhem para a hélice. O conjugado que ela exige do motor cresce com o quadrado da rotação e depende de um coeficiente próprio de cada hélice, o coeficiente de torque. Nos gráficos vemos os coeficientes de tração e de potência de hélices bipás, que caem conforme a razão de avanço aumenta. Na bancada estática estamos no início dessas curvas, onde eles são máximos.

**⏭**

*[5] Torque define o envelope*

Juntando a curva do motor com a curva da hélice, o ponto de operação é onde as duas se cruzam. Por isso o torque, que é uma variável invisível nos ensaios atuais, é o que realmente dita o envelope em que o motor vai trabalhar. Ao longo do trabalho usei duas hélices de referência, a APC 15x6E e a APC 17x10E, primeiro na simulação com os dados de catálogo da APC e depois no ensaio em bancada.

**⏭**

*[6] Objetivos*

O objetivo geral foi desenvolver um sistema integrado de aquisição de dados, adaptando a bancada que a equipe já tinha. Para isso, defini cinco objetivos específicos: medir o motor, modelar o motor e projetar um observador de estados, medir a parte aerodinâmica da hélice, ligar tudo em uma rede Ethernet e desenvolver um software de supervisão.

**⏭**

*[7] Planejamento*

Antes de construir qualquer coisa, planejei o trabalho em três janelas de decisão. Na primeira se definem as restrições do projeto, na segunda cada parte é validada sozinha e na terceira o projeto é fechado. Quatro frentes andaram em paralelo, CAD, SIBA, interface e SAME, e a ideia foi simular tudo antes de ter o hardware em mãos.

**⏭**

*[8] Planejamento: CAD*

A frente de CAD começa pelo diagrama de alimentação e pela escolha dos equipamentos principais. Com isso defini onde ficaria cada placa e suas restrições físicas, fiz o esboço da bancada e terminei com a montagem das placas no gabinete.

**⏭**

*[9] Planejamento: SIBA*

A SIBA, que cuida da parte aerodinâmica e da bateria, se dividiu em duas trilhas. No hardware, parti da segurança, da alimentação e da escolha do microcontrolador até chegar ao protótipo validado. No firmware, comecei com placas de desenvolvimento e um ambiente de teste no Zephyr, validando camada por camada. As duas trilhas se encontram na SIBA completa.

**⏭**

*[10] Planejamento: Interface*

A interface foi feita em Python, em paralelo com o resto. Os arquivos de configuração em YAML e a interface gráfica nasceram juntos, e o terminal embutido serviu para testar parâmetros e comunicação. O retorno dos membros da equipe ajudou a fechar as funcionalidades de calibração, estimação e registro.

**⏭**

*[11] Planejamento: SAME*

A SAME, que mede o motor, junta três trilhas: o código, a placa e o próprio motor. Do lado do motor, levantei os parâmetros, montei as simulações e o observador e testei tudo em placa de desenvolvimento antes de partir para o esquemático e o protótipo.

**⏭**

*[12] Arquitetura*

Na arquitetura final, a SIBA monitora a bancada e a bateria, a SAME monitora o motor e as duas se ligam a um switch de rede que leva tudo ao computador. A ideia central é que qualquer parâmetro de calibração possa ser ajustado pela interface, sem regravar firmware.

**⏭**

*[13] Supervisório*

Esta é a interface na prática. À esquerda, a tela principal com os gráficos em tempo real. À direita, um exemplo de ajuste de ganho e offset de cada sensor, feito direto pelo usuário.

**⏭**

*[14] Camadas da interface*

Como a comunicação é por rede, a interface precisou ser separada em camadas: apresentação, serviço, domínio e transporte. Essa separação permite trocar o que está do outro lado do cabo sem mexer no resto. A mesma interface conversa com o hardware real, com um simulador ou com o firmware compilado para o computador, o native_sim.

**⏭**

*[15] Configuração por YAML*

Os arquivos YAML são a fonte única de verdade do sistema. A partir deles são gerados os cabeçalhos do firmware das duas placas e a configuração da interface, então firmware e computador sempre falam a mesma língua.

**⏭**

*[16] Observador de estados*

Para chegar ao torque e à rotação sem sensor mecânico, usei um observador de estados. A ideia é rodar um modelo do motor em paralelo com o motor real e corrigir o modelo com o erro entre a corrente medida e a corrente estimada. O motor usado foi o Scorpion A-5025.

**⏭**

*[17] Sensor de corrente customizado*

Como o observador depende da corrente de cada fase, precisei medir três correntes. Os sensores comerciais isolados são caros, então construí um sensor próprio: um núcleo toroidal em volta do condutor com um sensor Hall no entreferro.

**⏭**

*[18] Estimação em partes*

Agora começam os resultados, e vou mostrar o observador por partes. A planta gera as correntes, a transformada de Clarke leva para o referencial alfa-beta, o observador estima a força contraeletromotriz, o PLL extrai ângulo e velocidade, a força contraeletromotriz é reconstruída e daí sai o torque.

**⏭**

*[19] Inversor simulado*

Para simular, primeiro precisei de um inversor. Ele recebe uma referência de velocidade, um controlador PI gera a tensão de comando, o inversor de seis passos aplica essa tensão no modelo elétrico do motor e a dinâmica mecânica fecha a malha.

**⏭**

*[20] Inversor simulado: resposta*

Aqui está a resposta desse inversor simulado com a hélice 17x10E. A velocidade segue os patamares de referência, e é esse o perfil que alimenta todo o resto da simulação.

**⏭**

*[21] Dinâmica do motor*

O modelo do motor é o circuito em estrela de cada fase, com resistência, indutância e a força contraeletromotriz. Ao lado está o torque da planta ao longo da simulação, a grandeza que o observador precisa reproduzir.

**⏭**

*[22] Observador por modos deslizantes*

Este é o observador por modos deslizantes. O erro de corrente passa por uma função de chaveamento suavizada, os ganhos deslizantes corrigem a estimativa de corrente e a estimativa da força contraeletromotriz, e essa força estimada segue para o PLL.

**⏭**

*[23] Observador: correntes e FEM*

Na simulação, as correntes e a força contraeletromotriz estimadas seguem as reais nos dois eixos, com o chaveamento característico do modo deslizante em volta delas.

**⏭**

*[24] PLL e reconstrução da FEM*

O PLL detecta o erro de fase, filtra, integra e entrega ângulo e velocidade. Com eles, reconstruo a força contraeletromotriz analiticamente, sem o ruído do chaveamento e sem o atraso de um filtro passa-baixas.

**⏭**

*[25] PLL: ângulo estimado*

O ângulo estimado acompanha o ângulo real da planta ao longo do tempo, o que mostra que a malha de sincronismo segura a referência.

**⏭**

*[26] Função custo*

Faltava escolher os ganhos. Para isso defini uma função custo J, que é a média dos erros de torque e de rotação ponderados, com mais peso no torque. Um algoritmo de evolução diferencial gera candidatos, simula planta e observador, calcula o custo e repete até convergir.

**⏭**

*[27] Sensibilidade dos ganhos*

Esta figura mostra como o custo varia em torno do ótimo para cada ganho. Os ganhos ficaram em regiões de custo baixo, e a busca chegou a um conjunto utilizável sem ajuste manual.

**⏭**

*[28] Rotação e torque simulados*

Com esses ganhos, a rotação estimada acompanha os patamares e o torque estimado oscila em torno do torque da hélice. Essa oscilação vem do acionamento em seis passos.

**⏭**

*[29] Ensaio sem as placas de aquisição*

Antes das placas ficarem prontas, fiz um ensaio improvisado com o motor girando em vazio. Usei um osciloscópio de quatro canais, medindo duas tensões e duas correntes de fase, com um tacômetro óptico como referência de rotação.

**⏭**

*[30] Formas de onda do ensaio*

Estas são as formas de onda tiradas desse ensaio, já no referencial alfa-beta. Correntes, tensões e a força contraeletromotriz estimada aparecem em quadratura, como era de se esperar.

**⏭**

*[31] Perdas mecânicas*

Com o motor em vazio, todo o torque vai para as perdas. Pelo balanço de potência de cada fase, levantei o torque de perdas em função da rotação e separei a parcela de atrito viscoso da de atrito seco.

**⏭**

*[32] Observador nos dados de bancada*

Então rodei o observador sobre os dados reais. A rotação estimada acompanha o tacômetro em todos os patamares e o torque estimado fica próximo do torque medido pelo balanço de potência. Há um limite em rotações baixas, onde a malha de sincronismo perde a referência.

**⏭**

*[33] Coeficiente de torque da hélice*

Com o observador funcionando, dá para identificar o coeficiente da hélice. Aplico um patamar de rotação, espero estabilizar, registro rotação e torque e calculo o coeficiente. O gráfico compara o torque real com o estimado nesse procedimento.

**⏭**

*[34] Escolha do sensor de corrente*

Agora as placas de aquisição, começando por como cheguei ao sensor de corrente. A primeira ideia foi um shunt com amplificador isolado, mas o custo por fase pesava. O transformador de corrente e a bobina de Rogowski não medem corrente contínua. O sensor Hall com núcleo toroidal foi o que juntou isolação, resposta em contínua e custo baixo.

**⏭**

*[35] Placas de aquisição*

Esta é a placa de condicionamento AQ1, do projeto até a placa fabricada com os três núcleos, e o gabinete onde tudo foi montado.

**⏭**

*[36] Sensor de corrente: caracterização*

Caracterizei o sensor com uma fonte de corrente e multímetros, em três configurações de espiras. A saída variou de forma linear com a corrente nas três configurações.

**⏭**

*[37] Lógica de aquisição*

De forma resumida, a aquisição funciona assim: o conversor coleta as amostras, o observador processa e o resultado vira telemetria que vai pela rede até o computador.

**⏭**

*[38] Validação com breakpoints* 🔶

Para validar o firmware na placa, usei breakpoints, pausando a execução em cada etapa e conferindo os valores. O código é o mesmo que já tinha sido testado no native_sim.

**⏭**

*[39] Ensaio integrado* 🔶

Com SIBA e SAME sincronizadas, tração, torque e rotação passam a sair no mesmo registro, exportado em CSV, prontos para a equipe analisar.

**⏭**

*[40] Conclusão* 🔶

Em resumo, a bancada ficou integrada e operacional. Torque e rotação passaram a ser obtidos sem sensor mecânico, as grandezas elétricas e aerodinâmicas ficam no mesmo registro e o procedimento pode ser repetido para outros conjuntos. Com isso a equipe passa a escolher motor e hélice apoiada em ensaio próprio, e não só em estimativa.

**⏭**

*[41] Trabalhos futuros*

Como próximos passos, sugiro usar um dinamômetro como referência de torque, levantar a curva de carga da bateria, estudar o efeito da temperatura da bateria na tração, estender o observador para enfraquecimento de campo e evoluir para uma bancada com seis graus de liberdade.

**⏭**

*[42] Referências*

Estas são as referências usadas no trabalho.

**⏭**

*[43] Obrigado*

Muito obrigado pela atenção. Fico à disposição para as perguntas.
