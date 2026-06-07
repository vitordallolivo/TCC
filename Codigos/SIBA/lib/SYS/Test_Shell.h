#ifndef TEST_SHELL_H
#define TEST_SHELL_H

/* Comandos de teste expostos via Zephyr Shell (CONFIG_SHELL), sobre o mesmo
 * UART do console (zephyr,shell-uart). Permite exercitar manualmente
 * sensores/ESC/relé/bateria/hélice sem depender do link UDP com o PC.
 *
 * Registro é estático (SHELL_CMD_ARG_REGISTER em Test_Shell.c) — nenhuma
 * inicialização em runtime é necessária; basta o arquivo estar linkado.
 * Digite "siba" + TAB no terminal serial (115200 8N1) para ver os comandos. */

#endif /* TEST_SHELL_H */
