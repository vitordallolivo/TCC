/**
 * @file log.h (shim freestanding)
 * @brief LOG_* viram no-op. O bench roda bare-metal em qemu: não há stdio, e
 * um printf no caminho de init poluiria a contagem de instret.
 */

#ifndef SHIM_BENCH_LOG_H
#define SHIM_BENCH_LOG_H

#define LOG_LEVEL_INF 3
#define LOG_MODULE_REGISTER(name, level)

#define LOG_INF(...) do { } while (0)
#define LOG_WRN(...) do { } while (0)
#define LOG_ERR(...) do { } while (0)
#define LOG_DBG(...) do { } while (0)

#endif /* SHIM_BENCH_LOG_H */
