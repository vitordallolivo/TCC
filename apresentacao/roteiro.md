# Roteiro da apresentação

Este é o texto para falar durante a apresentação. Cada bloco corresponde a um slide, e quando aparecer **⏭** você passa para a próxima página. O número entre colchetes é a página do PDF, só para você se localizar.

O roteiro vai além do que está escrito no slide: explica o motivo de cada decisão e os problemas que apareceram no caminho. Lendo tudo, passa um pouco dos 20 minutos. Nos slides em que estiver apertado de tempo, fale só o primeiro parágrafo. Os blocos marcados com 🔶 falam de resultados ilustrativos, que devem ser trocados pelo resultado real antes da defesa.

---

*[1] Capa*

Bom dia a todos. Meu nome é Vitor Dall'Olivo Gonçalves e vou apresentar meu Trabalho de Conclusão de Curso em Engenharia Elétrica: uma bancada de medição para sistemas propulsivos elétricos com motor BLDC. O trabalho foi orientado pela professora Ana Teruko Yokomizo Watanabe e coorientado pelo Aureo Guilherme Dobrikopf, e foi desenvolvido junto com a equipe Albatroz Aerodesign.

**⏭**

*[2] Albatroz Aerodesign*

A Albatroz é um projeto de extensão da UDESC Joinville que projeta e fabrica aeronaves radiocontroladas para a competição SAE Brasil Aerodesign. Na competição, a pontuação depende muito da carga útil que o avião consegue levantar, e quem define essa carga é o conjunto motopropulsor: motor, hélice e bateria.

Desde 2022, o regulamento só permite motores elétricos sem escovas, os BLDC. Então todo o desempenho da aeronave passa por entender bem como esse motor e essa hélice se comportam juntos, e é aí que entra este trabalho.

**⏭**

*[3] Ensaios descentralizados*

O problema que identifiquei é que os ensaios da equipe são descentralizados. À esquerda está o conjunto que usamos, o motor Scorpion A-5025 de 310 KV com uma bateria LiPo de seis células, e à direita a bancada de desempenho que a equipe já tinha. Essa bancada mede tração e velocidade do ar, a bateria é ensaiada em outro momento, e nada disso é registrado junto.

O que falta é justamente o eixo. Não há medida direta de rotação nem de torque, porque colocar um tacômetro ou um transdutor de torque no eixo de um motor desses é inconveniente: ocupa espaço, exige acoplamento mecânico e, no caso do transdutor de torque, é caro. Na prática, o que se faz é estimar a partir do catálogo do motor e da hélice. Só que o catálogo é feito em condições diferentes das nossas, e estimativa em cima de estimativa acaba levando a erro de projeto: hélice que não entrega a tração esperada, motor que esquenta além da conta, bateria dimensionada errado.

A ideia do trabalho é conseguir medir esse último braço que faltava, a rotação e o torque no eixo, sem colocar nenhum sensor mecânico no motor.

**⏭**

*[4] Conjugado da hélice*

Para entender por que o torque é tão importante, vale olhar para a hélice. O conjugado que a hélice exige do motor segue esta relação: depende da densidade do ar, do diâmetro elevado à quinta potência, do quadrado da rotação e de um coeficiente adimensional, o coeficiente de torque C_Q, que é próprio de cada hélice. Ou seja, o torque cresce de forma quadrática com a rotação, e a forma dessa parábola é ditada pelo coeficiente.

Os gráficos mostram dados experimentais de duas hélices bipás, com o coeficiente de tração à esquerda e o de potência à direita, em função da razão de avanço J, que relaciona a velocidade do ar com a rotação e o diâmetro. Os dois coeficientes caem conforme o avião ganha velocidade. Na bancada estática não há vento, então J é praticamente zero e ficamos no começo dessas curvas, onde os coeficientes são máximos. Essa é exatamente a condição de decolagem, a mais crítica para a competição.

O detalhe é que o fabricante raramente entrega o coeficiente de torque pronto. Ele entrega o de potência, e o coeficiente ainda varia um pouco com a rotação por efeito do número de Reynolds. Por isso medir o torque de verdade faz diferença.

**⏭**

*[5] Torque define o envelope*

Quando juntamos a curva de torque do motor com a curva de torque da hélice, o ponto onde elas se cruzam é o ponto de operação do conjunto. O motor entrega torque até a rotação nominal, a hélice pede cada vez mais torque conforme gira mais rápido, e o sistema se acomoda onde os dois se equilibram.

Por isso digo que o torque é uma variável invisível que dita o envelope do motor. Ninguém mede o torque nos ensaios atuais, e ele é o que decide em que rotação o conjunto vai trabalhar, quanta corrente vai puxar e quanta tração vai sair.

Ao longo do trabalho usei duas hélices de referência, a APC 15x6E e a APC 17x10E. O sufixo E indica a linha elétrica da APC, com perfil mais fino e mais leve. Primeiro usei as duas na simulação, com os dados de desempenho que a própria APC publica, e depois as ensaiei na bancada para comparar.

**⏭**

*[6] Objetivos*

O objetivo geral foi desenvolver um sistema integrado de aquisição de dados, adaptando a bancada de ensaios motopropulsores que a equipe já tinha, em vez de construir outra do zero.

Para isso, defini cinco objetivos específicos. O primeiro foi projetar e fabricar uma placa para medir tensão e corrente das três fases do motor. O segundo foi modelar o motor, identificando seus parâmetros elétricos e mecânicos, e projetar um observador de estados para estimar a força contraeletromotriz, a rotação e o torque. O terceiro foi integrar os sensores aerodinâmicos que já existiam, tubo de Pitot e células de carga. O quarto foi ligar tudo em uma rede Ethernet com um protocolo padronizado. O quinto foi desenvolver um software de supervisão em Python para monitorar, calibrar e registrar os dados.

**⏭**

*[7] Planejamento*

Antes de construir qualquer coisa, organizei o trabalho em três janelas de decisão. Na primeira, Q1, eu defino as restrições e as escolhas de projeto. Na segunda, Q2, cada parte é validada sozinha. Na terceira, Q3, as partes são integradas e o projeto é fechado.

Quatro frentes atravessam essas janelas em paralelo: o CAD da montagem física, a SIBA, que é a placa da bancada aerodinâmica, a interface de usuário e a SAME, que é a placa de medição do motor. Uma decisão importante foi simular tudo o que fosse possível antes de ter o hardware pronto. As placas demoram para chegar e para montar, então o firmware, a interface e o observador foram desenvolvidos e testados em simulação enquanto isso. Vou mostrar rapidamente cada frente.

**⏭**

*[8] Planejamento: CAD*

A frente de CAD começa com um diagrama simples de alimentação e com a definição dos equipamentos principais: fonte, conversores, relés, switch de rede e as placas. A partir disso decidi onde cada placa ficaria e quais restrições físicas ela teria, como tamanho, ventilação e acesso aos conectores.

Com essas restrições, fiz o esboço da bancada física e, na última janela, montei as placas no gabinete e fiz as modificações necessárias na bancada existente. A ideia era chegar a uma montagem em que cada placa ou relé pudesse ser trocado sem refazer a fiação do resto.

**⏭**

*[9] Planejamento: SIBA*

A SIBA se divide em duas trilhas. No hardware, parti dos requisitos de segurança, alimentação, Ethernet e da escolha do microcontrolador, até chegar aos esquemáticos. Como sempre acontece, tive que readequar componentes conforme a disponibilidade, definir as restrições físicas e só então partir para o protótipo. Se a validação da placa reprova, volta para o esquemático.

No firmware, comprei placas de desenvolvimento para não depender da minha placa estar pronta, criei o projeto no Zephyr, que é o sistema operacional de tempo real que usei, e montei um ambiente de teste. A validação foi por camadas: primeiro a camada base, que chamei de Vcore, depois o sistema e a aplicação. As duas trilhas se encontram na SIBA completa.

**⏭**

*[10] Planejamento: Interface*

A interface foi feita em Python, com PyQt e o Qt Designer para desenhar as telas. Logo no início separei duas coisas: os arquivos de configuração e a interface gráfica. Os dois convergem nos arquivos YAML, que vou explicar mais adiante.

Para testar parâmetros e comunicação sem depender das telas, criei um terminal embutido, o TinyTerminal, que fala com as placas pela mesma rede. Também mostrei a interface para membros da equipe ao longo do desenvolvimento, e esse retorno ajudou a definir as funcionalidades finais: configuração dos sensores, calibração das células de carga, os scripts de estimação do observador e da hélice e o registro em CSV.

**⏭**

*[11] Planejamento: SAME*

A SAME é a frente mais trabalhosa, porque junta três trilhas: o código, a placa de medição e o próprio motor. Do lado do motor, primeiro levantei os parâmetros, depois montei as simulações e o observador, que foram evoluindo juntos, e testei tudo em uma placa de desenvolvimento antes de ter a placa definitiva.

Do lado da placa, defini a alimentação e o condicionamento dos sinais, as restrições físicas e passei para o esquemático e o protótipo. Tudo converge nas validações, com o mesmo laço de refazer o esquemático se algo reprovar, e termina na SAME completa.

**⏭**

*[12] Arquitetura*

Esta é a visão geral do sistema. A SIBA monitora a bancada e a bateria, a SAME monitora o motor, e as duas se ligam a um switch de rede que leva tudo ao computador.

As duas placas usam o mesmo microcontrolador, o CH32V307 da WCH, um RISC-V de 144 MHz com unidade de ponto flutuante e conversor analógico-digital de até 1 milhão de amostras por segundo. O que mais pesou na escolha foi ele ter Ethernet integrada. Em 2025, a própria equipe tinha percebido que precisava de um jeito mais simples de ligar as placas aos computadores de ensaio. Por cabo de rede não precisa de conversor USB-serial nem de equipamento dedicado, porque praticamente todo notebook tem porta de rede. Além disso, cada placa sai por cerca de 30 reais, ou uns 90 com a importação, o que cabia no orçamento.

A ideia central da arquitetura é que qualquer parâmetro de calibração possa ser ajustado pela interface, a qualquer momento, sem precisar regravar o firmware. As placas guardam os coeficientes em uma memória EEPROM e restauram tudo na partida.

**⏭**

*[13] Supervisório*

Esta é a interface funcionando. À esquerda, a tela principal com os gráficos em tempo real. Um seletor de modo de operação decide quais placas se conectam e quais gráficos aparecem. À direita, um exemplo da tela de ganho e offset, onde o usuário ajusta a calibração de cada sensor e confirma. A placa grava na memória e devolve a confirmação pela telemetria.

A barra de menus é organizada por tarefa: perfis do motor, estimativas, que são os scripts do observador e da hélice, bateria, células de carga e as telas de diagnóstico, como o painel de erros e alarmes.

**⏭**

*[14] Camadas da interface*

Como a comunicação é toda por rede e os dados chegam em alta frequência, a interface não podia travar esperando pacote. Por isso dividi a aplicação em quatro camadas, cada uma com uma função só.

O transporte cuida da conexão: abre o socket e recebe os bytes. O domínio tem funções puras que transformam bytes e arquivos YAML em valores físicos, sem rede e sem estado. O serviço orquestra as conexões e roda os scripts de estimação em segundo plano. A apresentação são as telas.

O ganho dessa separação é que quem gerencia a conexão não precisa saber com quem está falando. A mesma interface conversa com três coisas diferentes, escolhidas na hora: o hardware real, um simulador mock que gera dados falsos, ou o native_sim. O native_sim é um recurso do Zephyr que compila o mesmo firmware da placa para rodar como um programa comum no computador, com rede de verdade. Isso salvou o projeto algumas vezes. Por exemplo, foi rodando contra o native_sim que descobri que o compilador do microcontrolador usava enumerações de um byte e o do computador de quatro, o que fazia o tamanho do pacote de comando divergir. Esse erro só teria aparecido na integração física.

**⏭**

*[15] Configuração por YAML*

Com duas placas e um computador trocando pacotes, o maior risco é cada lado interpretar o pacote de um jeito. Para evitar isso, os arquivos YAML são a fonte única de verdade. Neles estão descritos os sensores de cada placa, os comandos e as preferências do usuário.

Na compilação, um script em Python lê esses arquivos e gera, por templates, os cabeçalhos em C que entram no firmware das duas placas. Na execução, a interface lê os mesmos arquivos para montar os diálogos, o terminal e os gráficos. Assim, se eu adiciono um sensor ou um comando, mudo um arquivo só e firmware e interface continuam falando a mesma língua. Os cabeçalhos gerados nunca são editados à mão.

**⏭**

*[16] Observador de estados*

Agora, como medir rotação e torque sem sensor no eixo. A resposta é um observador de estados. A ideia é rodar, dentro do microcontrolador, um modelo matemático do motor em paralelo com o motor real. Os dois recebem a mesma tensão, e comparo a corrente que o modelo prevê com a corrente que foi medida. Esse erro é usado para corrigir o modelo, que vai sendo puxado para o comportamento real. Com o modelo corrigido, consigo ler dele variáveis que não medi, como a força contraeletromotriz, e dela tirar posição, rotação e torque.

Escolhi o observador por modos deslizantes porque ele é robusto a incertezas nos parâmetros do motor, que nunca são exatos. O preço é o chattering, uma oscilação de alta frequência própria da técnica, que tive que tratar, como vou mostrar nos resultados.

Para o modelo funcionar, precisei dos parâmetros do motor. A resistência foi medida com o rotor bloqueado, pelo método de quatro fios. A indutância foi medida por ressonância: como não tinha medidor LCR, coloquei um capacitor conhecido em paralelo com o enrolamento e varri a frequência até achar o pico.

**⏭**

*[17] Sensor de corrente customizado*

O observador depende da corrente de cada fase, então precisei medir três correntes de algumas dezenas de ampères com isolação. Sensores comerciais isolados para essa faixa são caros, e como eu precisava de três, o custo multiplicava.

A solução foi construir um sensor próprio: um núcleo toroidal de ferrite em volta do fio da fase, com um corte, o entreferro, onde coloco um sensor Hall comum, o SS49E. O núcleo concentra o campo magnético da corrente no entreferro, e o sensor Hall converte esse campo em tensão. Vou voltar a esse sensor na parte das placas, explicando como cheguei nele.

**⏭**

*[18] Estimação em partes*

Agora os resultados. Antes de ir para a bancada, validei o observador em simulação, e vou mostrar parte por parte.

A cadeia funciona assim: a planta, que faz o papel do motor, gera as correntes de fase. A transformada de Clarke leva as três fases para dois eixos, alfa e beta, o que simplifica as contas. O observador por modos deslizantes estima a força contraeletromotriz bruta, ainda com chattering. O PLL extrai dela o ângulo e a velocidade. Com ângulo e velocidade, reconstruo a força contraeletromotriz de forma analítica, já limpa, e com ela calculo o torque.

Essa reconstrução foi uma mudança importante. A forma clássica é tirar o ângulo com um arco tangente e filtrar com passa-baixas, mas o atraso do filtro fazia o observador perder o sincronismo nas desacelerações.

**⏭**

*[19] Inversor simulado*

Para simular, eu precisava de algo que fizesse o papel do motor sendo acionado. Montei uma planta completa: uma referência de velocidade entra em um controlador PI, que gera a tensão de comando. Um inversor de seis passos, igual ao que um ESC comercial faz, aplica essa tensão nas fases conforme a posição do rotor. O modelo elétrico do motor gera as correntes e o torque, e a dinâmica mecânica, com inércia, atrito e a carga da hélice, devolve a rotação.

Essa planta existe só na simulação, e as grandezas que ela gera são o que chamo de dados reais, a referência contra a qual comparo o observador. Usei os parâmetros medidos do motor, um barramento de 30 volts e passo de 10 microssegundos.

**⏭**

*[20] Inversor simulado: resposta*

Aqui está a resposta dessa planta com a hélice 17x10E. A velocidade segue os patamares da referência. Montei um perfil com subidas e descidas de propósito, porque as desacelerações eram justamente onde as versões anteriores do observador perdiam o sincronismo. É esse perfil que alimenta todo o resto da simulação.

**⏭**

*[21] Dinâmica do motor*

O modelo elétrico do motor é este circuito por fase: resistência, indutância e a força contraeletromotriz, ligados em estrela. Como o motor tem ímãs na superfície do rotor, posso considerar a indutância igual nos dois eixos, o que simplifica bastante o modelo.

À direita está o torque da planta ao longo da simulação. Esta é a grandeza que o observador precisa reproduzir, e já dá para ver o desafio: o acionamento em seis passos gera uma ondulação de torque enorme, e o torque médio da hélice, que é o que interessa, fica escondido no meio dela.

**⏭**

*[22] Observador por modos deslizantes*

Este é o coração do observador. O erro entre a corrente medida e a estimada passa por uma função de chaveamento. Usei uma saturação em vez do sinal puro, para suavizar o chattering. Esse sinal é multiplicado pelos ganhos deslizantes e corrige duas coisas: a estimativa da corrente e a estimativa da força contraeletromotriz. A força estimada, ainda bruta, segue para o PLL.

O observador foi discretizado para rodar no microcontrolador, então todas as contas são feitas amostra a amostra.

**⏭**

*[23] Observador: correntes e FEM*

Aqui estão correntes e força contraeletromotriz nos eixos alfa e beta, real e estimada. As estimadas seguem as reais, e dá para ver o chattering característico do modo deslizante em volta delas. É esse ruído que não deixa usar a força bruta diretamente para calcular o torque, e por isso o PLL e a reconstrução vêm em seguida.

**⏭**

*[24] PLL e reconstrução da FEM*

O PLL funciona como um filtro que se trava na posição do rotor. Ele calcula o erro de fase projetando a força bruta sobre o ângulo que ele próprio está estimando. Quando está travado, esse erro vai a zero. O erro passa por um filtro de laço PI, que dá a velocidade, e um integrador dá o ângulo. Essa forma de calcular o erro vale tanto para força senoidal quanto para trapezoidal, que é o caso do BLDC.

Com ângulo e velocidade, reconstruo a força contraeletromotriz analiticamente, já sem chattering, e calculo o torque com ela.

**⏭**

*[25] PLL: ângulo estimado*

Este é o desempenho do PLL na simulação: o ângulo estimado acompanha o real ao longo do tempo, com erro de poucos graus dentro da faixa de velocidade simulada. O erro é maior no patamar mais lento, e isso tem explicação física. A amplitude da força contraeletromotriz é proporcional à velocidade, então em rotação baixa o sinal fica pequeno perto do ruído. Esse mesmo limite apareceu depois na bancada.

**⏭**

*[26] Função custo*

Faltava o problema mais chato: escolher os ganhos. Ganho alto converge rápido mas aumenta o chattering, ganho baixo é suave mas lento, e ajustar isso na mão para cada motor não seria repetível.

Então automatizei. Defini uma função custo J que mede o que interessa na saída: o erro de torque e o erro de rotação, em cada patamar, com peso maior no torque, já que é a grandeza principal do trabalho. No torque usei a perda de Huber, que é quadrática para erros pequenos e linear para erros grandes, para que as quedas momentâneas do observador nas comutações do inversor não dominem o custo.

O fluxo é este: uma população de candidatos é transformada em ganhos, simulo planta e observador, calculo o custo e a evolução diferencial gera novos candidatos até convergir. Escolhi evolução diferencial porque a função custo não é convexa por causa do chattering e das saturações, então um método baseado em gradiente se perderia.

Um detalhe importante: otimizo os ganhos já multiplicados pelo passo de amostragem, os gamas. Assim o resultado não fica preso ao passo da simulação, e o firmware só divide pelo seu próprio período de amostragem.

**⏭**

*[27] Sensibilidade dos ganhos*

Este gráfico mostra como o custo varia quando mexo em um ganho de cada vez em torno do ótimo encontrado. O primeiro ganho ficou no fundo de um vale. O segundo ficou numa região plana, mas perto de uma subida abrupta, onde o observador deixa de acompanhar a planta. O terceiro, ligado ao polo do estimador, quase não altera o custo.

A busca levou cerca de mil avaliações, convergiu nas primeiras dezenas e chegou a um custo bem menor que o do ponto de partida, sem nenhum ajuste manual. Os ganhos praticamente coincidiram para as duas hélices, o que indica que a carga muda pouco o ganho ótimo. Como o procedimento só depende de resistência, indutância e perfil de velocidade, ele pode ser repetido para qualquer outro motor.

**⏭**

*[28] Rotação e torque simulados*

Com os ganhos ajustados, este é o resultado final da simulação. A rotação estimada acompanha os patamares, com um pequeno atraso nas transições. O torque estimado oscila em volta do torque real, e essa oscilação vem do acionamento em seis passos, não do observador.

Isso trouxe uma decisão prática para medir a hélice: o torque médio da hélice é cerca de vinte vezes menor que a ondulação. Por isso, na identificação do coeficiente, uso a corrente medida projetada no ângulo estimado, e não a corrente estimada, porque o chattering da estimada não tem média zero e contaminaria o resultado.

**⏭**

*[29] Ensaio sem as placas de aquisição*

Com a simulação funcionando, eu queria dados reais, mas as placas ainda não estavam prontas. Então fiz um ensaio improvisado com um osciloscópio PicoScope da série 5000, do laboratório do PET.

O osciloscópio tem quatro canais analógicos e dois digitais, e isso definiu o que medi: duas tensões e duas correntes de fase nos analógicos, e o comando do ESC e um tacômetro óptico nos digitais. A terceira tensão e a terceira corrente não foram medidas, mas como as três somam zero, reconstruí na análise. As correntes já vieram dos meus sensores Hall.

Alguns cuidados foram necessários. Alimentei pela bateria, e não pela fonte, para o barramento ficar flutuante e não criar laço de terra com as pontas do osciloscópio. O motor não tem o neutro acessível, então reconstruí o neutro com três resistores iguais ligados em um nó comum. Usei dois ESCs em sessões diferentes, um Castle Talon de 90 ampères e um Flycolor de 150. No total foram mais de setecentos registros, e um pouco mais da metade passou na triagem.

**⏭**

*[30] Formas de onda do ensaio*

Estas são as formas de onda reais desse ensaio, já no referencial alfa-beta: correntes, tensões e a força contraeletromotriz estimada pelo observador. As componentes da força aparecem em quadratura e com amplitude crescendo com a rotação, que é o esperado.

Esses registros também serviram para outras coisas. Validei o tacômetro contra a frequência elétrica das tensões, e as duas leituras bateram, confirmando sete pares de polos e um pulso por volta. A constante de força contraeletromotriz medida ficou próxima dos 310 RPM por volt de placa do motor. E, olhando o espectro, vi que o ruído do sensor de corrente sem filtro ocupava uns quatro bits do conversor. Isso me fez definir o filtro das placas por medida: um polo em 20 kHz, igual nos ramos de tensão e de corrente, para que o atraso seja o mesmo nos dois e não estrague a fase que o observador usa.

**⏭**

*[31] Perdas mecânicas*

Para descontar as perdas do motor quando ele estiver com a hélice, eu precisava conhecê-las. Com o motor em vazio, todo o torque vai para as perdas. Como eu não tinha transdutor de torque, calculei o torque pelo balanço de potência de cada fase: potência elétrica que entra, menos a dissipada na resistência, dividida pela velocidade. Esse cálculo não depende do observador, então serve de referência independente.

Fiz uma regressão sobre dezenove patamares de rotação e separei duas parcelas: uma proporcional à rotação, o atrito viscoso, e uma constante, o atrito seco. A parcela constante ficou alta para um motor desse porte. Uma explicação plausível é a histerese no ferro do estator, que aparece na regressão justamente como torque constante. Para o uso que eu queria, que é descontar as perdas, o modelo agregado atende.

**⏭**

*[32] Observador nos dados de bancada*

Com tudo isso, rodei o observador sobre os dados reais do ensaio, com a constante de força medida na bancada. A rotação estimada acompanha o tacômetro em todos os patamares, com erro médio bem abaixo de um por cento. O torque estimado ficou alguns por cento abaixo do torque do balanço de potência. O ângulo também foi conferido contra a integral dos pulsos do tacômetro e ficou dentro de menos de um grau.

Encontrei um limite. Abaixo de cerca de 1900 RPM, o PLL deixa de travar, porque a força contraeletromotriz fica pequena demais. A rotação continua saindo, mas o torque não, porque depende do ângulo. Para a aplicação, que é tração de decolagem, a faixa de interesse está bem acima disso, mas é algo a respeitar na partida.

**⏭**

*[33] Coeficiente de torque da hélice*

Com o observador validado, chego ao que motivou o trabalho: identificar o coeficiente da hélice. O procedimento é este: levo o motor a um patamar de rotação, espero estabilizar, registro rotação, torque e a densidade do ar, calculada pela pressão e pela temperatura do ambiente, e calculo o coeficiente daquele patamar. Depois sigo para o próximo patamar, até no máximo 10 mil RPM.

O gráfico mostra o torque real e o estimado ao longo dos patamares na simulação da 17x10E. Nessa identificação simulada, o coeficiente estimado convergiu para o nominal nas duas hélices, com erro bem pequeno, mesmo com aquela ondulação vinte vezes maior que o torque médio. Na bancada, comparo com a tabela da APC patamar por patamar, e não como um valor único, porque o coeficiente varia com a rotação.

**⏭**

*[34] Escolha do sensor de corrente*

Agora as placas de aquisição, começando pelo sensor de corrente. De início considerei um shunt, que é a forma mais direta de medir corrente: um resistor de valor baixo em série, com um amplificador isolado como o AMC1301 que aparece aqui. O problema é que o shunt precisa ser dimensionado para a carga, e aqui espero pelo menos uns 50 ampères por fase. Um shunt para essa corrente, com a dissipação envolvida, mais o amplificador isolado, multiplicado por três fases, ficava caro.

O transformador de corrente e a bobina de Rogowski estavam descartados desde o começo, porque os dois só medem corrente alternada, e eu preciso da componente contínua.

Sobrou o sensor Hall. Decidi construir o meu pelo preço, que compensava muito: um sensor Hall simples e barato com um toroide de ferrite, que aguenta essa corrente sem problema. O único ponto negativo é o tamanho, porque o toroide ocupa bastante espaço na placa.

**⏭**

*[35] Placas de aquisição*

Esta é a AQ1, a placa de condicionamento da SAME. À esquerda está o projeto, e no meio a placa fabricada, já com os três toroides. As placas foram feitas à mão em fenolite, para baratear e permitir iterar rápido. Antes de fabricar, o condicionamento foi simulado no LTspice, o que reduziu o número de placas refeitas.

As três fases do motor passam por dentro dos toroides, e a placa entrega ao microcontrolador as três correntes e as três tensões já condicionadas. À direita está o gabinete onde tudo foi montado. A bateria só alimenta o ESC, através de dois relés: um de pré-carga, com um resistor em série para limitar o pico de corrente dos capacitores do ESC, e depois o relé de potência. O botão de emergência corta a bobina dos relés fisicamente, sem depender do microcontrolador.

**⏭**

*[36] Sensor de corrente: caracterização*

Para caracterizar o sensor, havia um problema: a fonte do laboratório só chega a uns 5 ampères, longe dos 60 da aplicação. A solução foi enrolar espiras no toroide. Com N espiras, o campo é o mesmo de uma corrente N vezes maior passando uma vez. Ensaiei com uma, quatro e oito espiras, medindo corrente e tensão com multímetros.

A saída ficou linear nas três configurações, sem sinal de saturação do ferrite, e a sensibilidade ficou dentro da faixa do catálogo. Houve uma dispersão entre as configurações, que vem da tolerância do próprio núcleo e da diferença entre uma espira passando pelo furo e um enrolamento por fora. Por isso o ganho usado no projeto é o da configuração real, com uma espira.

Outro problema que apareceu: o nível de repouso do sensor muda de peça para peça e com a temperatura. Parece pouco, mas como esse erro é comum às três fases, ele passa pela transformada de Clarke e aparece como sinal falso na força contraeletromotriz. A solução foi o firmware medir o repouso real toda vez que liga, com o ESC parado, antes de começar a aquisição.

**⏭**

*[37] Lógica de aquisição*

Resumindo a lógica de aquisição da SAME: os dois conversores do microcontrolador trabalham em modo simultâneo, um lendo as três correntes e o outro as três tensões, disparados pelo mesmo evento. Assim, corrente e tensão de uma mesma fase são amostradas no mesmo instante, o que é condição para o observador funcionar. A taxa fica em torno de 100 mil quadros por segundo.

A cada conversão, a camada de hardware chama o observador diretamente, sem passar pela fila de trabalho do sistema, porque o observador precisa rodar em toda amostra, com tempo garantido. O resultado vai para o gerenciador de log, que monta a telemetria e envia pela rede até o computador.

**⏭**

*[38] Validação com breakpoints* 🔶

Para validar o firmware já na placa, usei depuração com breakpoints. Pausava a execução em cada etapa, da leitura do conversor até a montagem do pacote, e conferia os valores. A vantagem é que o código é exatamente o mesmo que já tinha sido testado no native_sim, então o que sobrava para validar era o que depende do hardware de verdade: temporização dos periféricos, DMA e pinagem.

**⏭**

*[39] Ensaio integrado* 🔶

Com SIBA e SAME rodando juntas, chegamos ao objetivo do trabalho: tração, torque e rotação saem no mesmo registro, sincronizados e exportados em CSV. O torque vem do observador, sem nenhum sensor no eixo, e a tração vem das células de carga. Com isso dá para levantar, em um único ensaio, as curvas do conjunto motor, hélice e bateria que antes exigiam vários ensaios separados e estimativas de catálogo.

**⏭**

*[40] Conclusão* 🔶

Para concluir: a bancada ficou integrada e operacional. A rotação e o torque no eixo, que eram o braço que faltava, passaram a ser obtidos sem sensor mecânico, por um observador validado primeiro em simulação e depois em dados reais. As grandezas elétricas e aerodinâmicas passam a sair no mesmo registro, e tudo pode ser calibrado pela interface, sem regravar firmware.

Além disso, o procedimento é repetível. O ajuste dos ganhos depende só de resistência, indutância e perfil de velocidade, então a equipe pode caracterizar outro motor ou outra hélice seguindo o mesmo caminho. Na prática, a escolha de motor e hélice passa a se apoiar em ensaio próprio, e não só em estimativa.

**⏭**

*[41] Trabalhos futuros*

Como próximos passos, o principal seria um dinamômetro, para ter uma referência direta de torque e separar melhor as perdas de atrito e histerese que hoje ficam juntas. Também sugiro levantar a curva de carga da bateria e estudar como a temperatura da bateria afeta a tração ao longo do ensaio. Do lado do observador, dá para estendê-lo para a região de enfraquecimento de campo, que ficou fora do que ensaiei. Por fim, a bancada pode evoluir para uma estrutura com seis graus de liberdade.

**⏭**

*[42] Referências*

Estas são as principais referências usadas no trabalho.

**⏭**

*[43] Obrigado*

Muito obrigado pela atenção. Fico à disposição para as perguntas.
