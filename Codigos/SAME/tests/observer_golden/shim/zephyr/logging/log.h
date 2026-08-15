/**
 * @file log.h (shim)
 * @brief Substituto mínimo de <zephyr/logging/log.h> para o harness de host.
 * Logs vão para stderr, deixando o stdout limpo para o CSV.
 */

#ifndef SHIM_ZEPHYR_LOGGING_LOG_H
#define SHIM_ZEPHYR_LOGGING_LOG_H

#include <stdio.h>

#define LOG_LEVEL_INF 3

#define LOG_MODULE_REGISTER(name, level)  /* nada */

#define LOG_INF(fmt, ...) fprintf(stderr, "[inf] " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) fprintf(stderr, "[wrn] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[err] " fmt "\n", ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) do { } while (0)

#endif /* SHIM_ZEPHYR_LOGGING_LOG_H */
