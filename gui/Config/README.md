# Configuração e Geração de Protocolo

> Parte da [aplicação de supervisão](../README.md) · [README principal](../../README.md)

Este diretório centraliza a **fonte única de verdade** do protocolo de rede
(`CMD`/`TELEMETRY_LOG`) e gera os headers C consumidos pelo firmware da
placa (`../../firmware`).

## Fluxo de automação

1. **`protocolo.yaml`**: definição declarativa de enums e structs (blocos
   `enums`/`structs`) + endereçamento (bloco `link:`). A ordem dos campos
   dita o layout exato de memória. A GUI e o `placa.py` leem o mesmo
   arquivo.
2. **`generate.py`**: lê o YAML e renderiza os templates Jinja2.
3. **Templates (`.j2`)**: modelos que definem a implementação final dos
   headers C.

## Estrutura de arquivos e outputs

### Arquivos deste diretório

| Arquivo | Papel |
| :--- | :--- |
| `protocolo.yaml` | Protocolo da placa: `link`, `enums`, `structs`, `error_bits`, `sensor_defaults` e `security_limits` |
| `generate.py` | Renderiza os templates para `firmware/` |
| `*_template.h.j2` | Templates Jinja2 dos headers C |
| `plots.yaml` | Mapeamento telemetria → gráficos da GUI (títulos, eixos, legendas) |
| `commands.yaml` | Árvore de comandos e textos de ajuda do `TinyTerminal` (a gramática fica em `Communication_Data/comandos.py`) |
| `motors/` | Perfis de motor salvos pela GUI (Motor → Criar) |
| `estimativas.yaml` | Resultados gravados pelos diálogos de estimativa (ganhos do SMO, coeficientes da hélice); criado sob demanda |

### Headers gerados

| Fonte (template) | Destino (em `firmware/`) | Gerado quando | Escopo |
| :--- | :--- | :--- | :--- |
| `Common_template.h.j2` | `lib/OS/Common_Types.h` | sempre | Enums compartilhados (`func_type`, `target_type`, ...) |
| `CMD_template.h.j2` | `lib/OS/CMD.h` | sempre | Struct de comando (PC → placa) |
| `LOG_template.h.j2` | `lib/SYS/Log_Manager.h` | sempre | Struct de telemetria (placa → PC) + `SENSOR_ID_*` |
| `Error_Codes_template.h.j2` | `lib/SYS/Error_Codes.h` | yaml tem `error_bits` | Enum `siba_error_bit_t` (mesma tabela que a GUI usa em `errors.py`) |
| `CMD_Core_prm_template.h.j2` | `lib/OS/CMD_Core_prm.h` | yaml tem `sensor_defaults` | Calibração/faixa padrão por sensor, com lei `affine` ou `sqrt` (Pitot) |
| `Security_prm_template.h.j2` | `lib/SYS/Security_prm.h` | yaml tem `security_limits` | Limiares `SM_*` do `Security_Manager` |

## Integridade de memória e alinhamento

- **Packing dos structs**: `CMD` e `TELEMETRY_LOG` são sempre gerados com
  `__attribute__((packed))` (alinhamento de 1 byte, sem padding do
  compilador).
- **Packing seletivo dos enums**: só os enums usados como **campo do
  `CMD`** (`func_type`, `instruct_type`) recebem
  `__attribute__((packed))` — o tamanho de um `enum` em C é
  impl-definido (4 bytes no GCC host/`native_sim`, 1 byte só com
  `-fshort-enums` na placa RISC-V); sem essa marcação seletiva,
  `sizeof(CMD)` diverge entre o firmware real e `native_sim` (bug já
  visto e corrigido — ver `../CLAUDE.md`). Enums de telemetria
  (`log_status`) não são marcados.
- **Endianness**: o lado Python usa o módulo `struct` com `<`
  (little-endian), compatível com a arquitetura RISC-V de destino.
- **Tipos de largura fixa** (`float`, `int32_t`, `uint32_t`) garantem
  payload constante e previsível.

## Procedimento

Sempre que o `protocolo.yaml` for modificado, rode o gerador antes de
compilar o firmware:

```bash
make protocolo        # na raiz do repositório
# firmware/lib/OS/{Common_Types,CMD,CMD_Core_prm}.h
# firmware/lib/SYS/{Log_Manager,Error_Codes,Security_prm}.h
```

Os identificadores gerados mantêm o prefixo histórico do firmware
(`SIBA_SENSOR_TABLE`, `siba_error_bit_t`), definido em `PREFIXO_FW` no
`generate.py`: mudá-lo exige mudar o firmware junto.

Os `.h` gerados nunca são editados à mão.
