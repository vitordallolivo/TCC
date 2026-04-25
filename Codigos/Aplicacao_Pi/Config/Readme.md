# Configuração e Geração de Protocolo (SIBA Board)

Este diretório centraliza a **Single Source of Truth** para a interface de comunicação UDP entre a aplicação de controle (Python) e o firmware. A arquitetura utiliza um motor de templates para garantir a paridade binária rigorosa entre as estruturas de dados em ambas as pontas.

## Fluxo de Automação e Sincronização

O pipeline de geração de código mitiga erros de *protocol drift* e garante que alterações na camada de aplicação sejam refletidas instantaneamente na camada de hardware.

1.  **`SIBA.yaml`**: Definição declarativa de Enums e Structs. A ordem dos campos dita o layout exato de memória.
2.  **`generate.py`**: Script de automação que utiliza Jinja2 para parsing e renderização de headers.
3.  **Templates (`.j2`)**: Modelos que definem a implementação final dos cabeçalhos C.

## Estrutura de Arquivos e Outputs

O sistema gera arquivos modulares para respeitar a separação de domínios (OS vs SYS) e evitar redefinições de símbolos em tempo de compilação.

| Fonte Template | Destino (Firmware) | Escopo |
| :--- | :--- | :--- |
| `Common_template.h.j2` | `lib/OS/Common_Types.h` | Definição de tipos base e Enums compartilhadas. |
| `CMD_template.h.j2` | `lib/OS/CMD.h` | Struct de comando (Ground Control -> MCU). |
| `LOG_template.h.j2` | `lib/SYS/Log_Manager.h` | Struct de telemetria/logs (MCU -> Ground Control). |

## Integridade de Memória e Alinhamento

Para garantir que o mapeamento via *cast* direto de ponteiros no microcontrolador funcione sem corrupção de dados:

* **Packing**: Os structs são gerados com `__attribute__((packed))` para forçar o alinhamento de 1 byte, eliminando o preenchimento automático (*padding*) do compilador.
* **Endianness**: O processamento em Python utiliza a biblioteca `struct` com o modificador `<` (Little-Endian) para garantir compatibilidade nativa com a arquitetura de destino.
* **Determinismo**: O uso de tipos de largura fixa (`float`, `int32_t`, `uint32_t`) assegura que o payload seja constante e previsível.

## Procedimento de Build

Sempre que o `SIBA.yaml` for modificado, o gerador deve ser executado para atualizar as dependências do firmware:

```bash
# Certifique-se de estar no ambiente virtual (.venv)
python generate.py