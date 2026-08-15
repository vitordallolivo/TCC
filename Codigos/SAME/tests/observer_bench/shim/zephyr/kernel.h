/**
 * @file kernel.h (shim)
 * @brief Substituto mínimo de <zephyr/kernel.h> para compilar
 * lib/SYS/Observer_Manager.c no host, fora do Zephyr.
 *
 * O harness roda single-thread: mutex e atomic viram no-op / int simples.
 * Isso NÃO testa concorrência (Observer_Apply_Pending vs same_wq) — só a
 * matemática de Observer_Step, que é o objetivo do A/B V0 x V5.
 */

#ifndef SHIM_ZEPHYR_KERNEL_H
#define SHIM_ZEPHYR_KERNEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Mutex (no-op: harness é single-thread) ---- */
struct k_mutex { int dummy; };
#define K_FOREVER 0

static inline void k_mutex_init(struct k_mutex *m)              { (void)m; }
static inline int  k_mutex_lock(struct k_mutex *m, int timeout) { (void)m; (void)timeout; return 0; }
static inline int  k_mutex_unlock(struct k_mutex *m)            { (void)m; return 0; }

/* ---- Atomics (no-op: harness é single-thread) ---- */
typedef long atomic_t;

static inline atomic_t atomic_get(const atomic_t *t)          { return *t; }
static inline atomic_t atomic_set(atomic_t *t, atomic_t v)    { atomic_t o = *t; *t = v; return o; }
static inline atomic_t atomic_clear(atomic_t *t)              { atomic_t o = *t; *t = 0; return o; }

#endif /* SHIM_ZEPHYR_KERNEL_H */
