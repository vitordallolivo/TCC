# Configuração e Geração de Protocolo

Este diretório centraliza a **fonte única de verdade** do protocolo de rede
(`CMD`/`TELEMETRY_LOG`) e gera os headers C consumidos pelos firmwares da
SIBA e da SAME.

## Fluxo de automação

1. **`SIBA.yaml`** / **`SAME.yaml`**: definição declarativa de enums e
   structs (bloco `enums`/`structs`) + endereçamento (bloco `link:`). A
   ordem dos campos dita o layout exato de memória.
2. **`generate.py`**: lê os dois YAMLs e renderiza os templates Jinja2 para
   cada placa, de uma vez.
3. **Templates (`.j2`)**: modelos que definem a implementação final dos
   headers C.

`SPMM.yaml` **não** passa por `generate.py` — a SPMM fala um protocolo
diferente (Telnet + log binário), fixo em `../../SPMM/src/spmm.h`; o YAML
aqui só descreve endereçamento/`Ts`/comandos do lado do PC.

## Estrutura de arquivos e outputs

| Fonte (template) | Destino (por placa) | Escopo |
| :--- | :--- | :--- |
| `Common_template.h.j2` | `lib/OS/Common_Types.h` | Enums compartilhados (`func_type`, `target_type`, ...) |
| `CMD_template.h.j2` | `lib/OS/CMD.h` | Struct de comando (PC → placa) |
| `LOG_template.h.j2` | `lib/SYS/Log_Manager.h` | Struct de telemetria (placa → PC) + `SENSOR_ID_*` |

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

Sempre que `SIBA.yaml` ou `SAME.yaml` forem modificados, rode o gerador
antes de compilar o firmware correspondente:

```bash
# no venv (.venv/)
python generate.py
# gera SIBA/lib/OS/{Common_Types,CMD}.h + SIBA/lib/SYS/Log_Manager.h
# e    SAME/lib/OS/{Common_Types,CMD}.h + SAME/lib/SYS/Log_Manager.h
```

Os `.h` gerados nunca são editados à mão.
