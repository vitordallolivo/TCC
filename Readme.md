# Bancada de Medição para Sistemas Propulsivos Elétricos com BLDC

Trabalho de Conclusão de Curso — Engenharia Elétrica, UDESC Joinville (2026).
Desenvolvido em parceria com a equipe **Albatroz Aerodesign**.

## Introdução

Bem-vindo ao repositório do meu TCC: uma **bancada de ensaios para conjuntos
motopropulsores baseados em motor BLDC**. O sistema faz a aquisição sincronizada
das variáveis elétricas do motor, das forças aerodinâmicas da hélice e da
velocidade do escoamento, servindo como ferramenta de caracterização de
conjuntos motopropulsores.

A arquitetura é organizada em **dois nós de aquisição dedicados**, ambos
construídos em torno do microcontrolador **CH32V307VCT6** (núcleo RISC-V de
144 MHz com FPU e interface Ethernet MAC+PHY integrada), interligados a um
software de supervisão por uma rede **TCP/UDP**:

- **SIBA** (Sistema Integrado da Bancada Aerodinâmica) — instrumentação
  aerodinâmica (Pitot, células de carga) e da bateria.
- **SAME** (Sistema de Aquisição do Motor Elétrico) — medição elétrica de
  tensão e corrente de fase e execução embarcada do observador.

Para estimar velocidade angular e torque no eixo **sem sensores mecânicos**,
implementa-se um **Observador por Modos Deslizantes (SMO)** associado a um
**PLL** e à reconstrução analítica da força contraeletromotriz (BEMF), a partir
das correntes e tensões de fase.

## Objetivos

1. Projetar e fabricar o hardware de medição elétrica do motor BLDC (PCB com
   sensores de corrente por fase, condicionamento de sinal e aquisição
   sincronizada de tensão).
2. Modelar a dinâmica do motor no referencial αβ e projetar o observador de
   estados (SMO + PLL) para estimação de BEMF, velocidade e torque.
3. Integrar e condicionar os sensores aerodinâmicos da bancada (tubo de Pitot e
   células de carga) para medição de tração, momento e velocidade do escoamento.
4. Implementar a infraestrutura de comunicação em rede TCP/UDP entre os nós e a
   estação de supervisão.
5. Desenvolver o software de visualização em Python (monitoramento em tempo real,
   parametrização, registro em CSV e alertas).

## Estrutura do Repositório

```
.
├── Codigos
│   ├── Aplicacao_Pi   # Software de supervisão (Python/PyQt) + geração de código por YAML
│   ├── SIBA           # Firmware Zephyr RTOS do nó aerodinâmico (board custom CH32V307)
│   ├── SAME           # Nó de aquisição do motor (firmware em desenvolvimento)
│   ├── Equations      # Scripts de simulação do SMO, autotuning e identificação da hélice
│   └── Teste          # Scripts de bancada / experimentos pontuais
├── Esquematicos       # Projetos KiCad (SAME, PropellerMeasures/SIBA, InverterBLDC, Ensaio_Bateria)
├── Simulacoes         # Simulações LTspice do front-end de medição (AMC1301, etc.)
└── Referencias Boas   # Bibliografia de apoio
```

### Firmware (Vcore-OS)

O firmware das placas é estruturado em três camadas: **Vcore-OS**, **System** e
**Application**. A camada base encapsula o Zephyr e padroniza a comunicação
interna pelo **OSSI** (barramento de mensagens), com módulos:

- **CMD** — interpreta pacotes binários da rede e roteia comandos.
- **TCKEC** — gerencia o ciclo de vida das threads.
- **NCI** — pilha de rede UDP e socket de comunicação.
- **HAL** — acesso aos periféricos (ADC via DMA, etc.).

A camada **System** (no SIBA) reúne `Propeller_Manager`, `Battery_Manager`,
`Security_Manager`, `LoadCell_Manager` e `Log_Manager`.

### Geração de código por YAML

A pasta `Aplicacao_Pi/Config` adota arquivos YAML (`SAME.yaml`, `SIBA.yaml`,
`commands.yaml`, `user_prefs.yaml`) como **fonte única da verdade**. O script
`generate.py` processa esses descritores via templates Jinja2 e gera os headers
em C (`Common_Types.h`, `CMD.h`, `Log_Manager.h`) — garantindo que firmware e
GUI compartilhem a mesma estrutura de telemetria e comandos. **Esses headers não
devem ser editados manualmente.**

## Hardware Caracterizado

- **Motor:** Scorpion A-5025-310Kv
- **Bateria:** LiPo 6S 3300 mAh 40C
- **Hélice:** `[modelo da hélice — preencher: diâmetro × passo]`
- **MCU:** CH32V307VCT6 (WCH), RISC-V 144 MHz, ADC até 1 Msps, Ethernet integrada

### Parâmetros elétricos medidos

| Parâmetro | Valor (linha) | Valor (fase / estrela) | Método |
|-----------|---------------|------------------------|--------|
| Resistência | R_LL = 17,37 mΩ | R_Y = 8,69 mΩ | 4 fios (Kelvin) |
| Indutância  | L_LL = 13,90 µH | L_Y = 6,95 µH  | Ressonância LC |

## Equacionamento

Modelo de tensão de fase do BLDC em estrela:

```
v_k = R·i_k + L·(di_k/dt) + e_k       (k ∈ {a,b,c})
```

Modelo no referencial estacionário αβ (Clarke):

```
[v_α]     [i_α]        d [i_α]     [e_α]
[v_β]  = R[i_β]  + L_s --[i_β]  +  [e_β]
                       dt
```

Observador (SMO) discretizado por Euler progressivo, com os coeficientes
invariantes ao passo de amostragem:

```
Φ_i = 1 − R_s·T_s/L_s ,  Γ_v = T_s/L_s ,  Γ_s1 = K_s1·T_s ,  Γ_s2 = K_s2·T_s

î_{k+1} = Φ_i·î_k + Γ_v·(v_k − ê_k) + Γ_s1·Sat(i_k − î_k)
ê_{k+1} = ê_k + Γ_s2·Sat(i_k − î_k)
```

Posição e velocidade extraídas via PLL a partir da BEMF estimada; torque
eletromagnético:

```
T̂_e = (3p / 4ω_e)·(ê_α·i_α + ê_β·i_β)
```

Coeficiente de carga da hélice (ensaio estático, V∞ ≈ 0):

```
Q = C_Q·ρ·n²·D⁵   ⇒   T = k_Q·ω_m²,   com  k_Q = C_Q·ρ·D⁵ / (4π²)
```

### Autotuning do observador

Os ganhos (K_s1, K_s2, K_t) são ajustados offline por **Evolução Diferencial**,
otimizando sobre os ganhos discretizados invariantes Γ (transferíveis ao
firmware independentemente de T_s). A função-custo segue **torque + RPM** com
perda de **Huber**, priorizando a fidelidade do torque (w_T = 1,0; w_ω = 0,5).

## Status

| Componente | Estado |
|------------|--------|
| Firmware SIBA (Zephyr) | Board custom + camadas Vcore-OS/System implementadas; Ethernet/UDP validada em simulação |
| Firmware SAME | Em desenvolvimento (hardware e esquemático prontos) |
| Software de supervisão (PyQt) | Funcional |
| Simulação SMO + autotuning | Validada (prova de conceito) |
| Identificação de parâmetros elétricos | Concluída |
| Validação experimental em bancada física | Etapa subsequente |

## Ferramentas

KiCad (esquemáticos/PCB), LTspice (simulação analógica), Zephyr RTOS (firmware),
Python + Numba (simulação do observador), PyQt/Qt Designer (GUI).

## Declaração de uso de IA

Ferramentas de IA generativa (ChatGPT, Claude, Gemini, DeepSeek, Claude Code)
foram usadas como apoio à revisão textual, conferência de cálculos e
desenvolvimento/depuração de scripts. Todo o conteúdo foi revisado e validado
pelo autor, que assume responsabilidade integral pela precisão e originalidade.

## Autor

**Vitor Dall'Olivo Gonçalves** — Engenharia Elétrica, UDESC Joinville
Orientadora: Ana Teruko Yokomizo Watanabe · Coorientador: Aureo Guilherme Dobrikopf