/*
 * Escrita serializada na USART1 (a "ponte" para o ESP32).
 *
 * A USART1 e usada por DOIS produtores concorrentes:
 *   - src/uart_cli.c   : respostas do CLI de shell (sob demanda);
 *   - src/telemetry.c  : telemetria periodica (linhas "@V,...").
 *
 * Se os dois escreverem ao mesmo tempo com uart_poll_out(), os bytes se
 * intercalam e corrompem tanto a resposta quanto a telemetria. Esta funcao
 * serializa a escrita com um mutex, garantindo linhas inteiras e atomicas.
 */

#ifndef BRIDGE_UART_H
#define BRIDGE_UART_H

#include <stddef.h>

/* Escreve 'len' bytes na USART1 de forma atomica (thread-safe). */
void bridge_uart_write(const char *s, size_t len);

#endif /* BRIDGE_UART_H */
