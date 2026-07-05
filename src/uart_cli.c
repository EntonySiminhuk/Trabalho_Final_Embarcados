/*
 * CLI externo via ponte do ESP32.
 *
 *   PC --UDP:5000--> ESP32 (Wi-Fi) --Serial2--> STM32 (USART1) --> este modulo
 *   PC <--UDP------- ESP32          <--Serial2-- STM32 (USART1) <-- resposta
 *
 * Cada linha recebida na USART1 e um comando de shell: executamos via
 * shell_execute_cmd() no backend "dummy" (que captura a saida) e escrevemos
 * essa saida de volta na USART1.
 *
 * A RX e feita por INTERRUPCAO + ring buffer. O registrador RX do STM32 guarda
 * so 1 byte, entao ler por polling com sleep perde bytes; a ISR esvazia o FIFO
 * a cada byte e enfileira, e a thread consome sem pressa.
 *
 * Requer: CONFIG_SHELL_BACKEND_DUMMY=y e CONFIG_UART_INTERRUPT_DRIVEN=y
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>

#include <string.h>

#include "bridge_uart.h"

LOG_MODULE_REGISTER(uart_cli, LOG_LEVEL_INF);

/* UART fisica da ponte (definida no overlay: usart1 em PC4/PC5). */
#define CLI_UART_NODE DT_NODELABEL(usart1)
static const struct device *const cli_uart = DEVICE_DT_GET(CLI_UART_NODE);

#define LINE_BUF_SZ      160
#define RING_BUF_SZ      256
#define UART_CLI_STACK   2048
#define UART_CLI_PRIO    6

RING_BUF_DECLARE(rx_rb, RING_BUF_SZ);

/* Serializa a TX da USART1 entre o CLI (respostas) e a telemetria periodica. */
K_MUTEX_DEFINE(tx_mutex);

/* ISR: esvazia o FIFO de RX e joga os bytes no ring buffer. */
static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	while (uart_irq_rx_ready(dev)) {
		uint8_t buf[32];
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		ring_buf_put(&rx_rb, buf, n);
	}
}

/* Escrita atomica na USART1 (thread-safe): ver bridge_uart.h. */
void bridge_uart_write(const char *s, size_t len)
{
	k_mutex_lock(&tx_mutex, K_FOREVER);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(cli_uart, (unsigned char)s[i]);
	}
	k_mutex_unlock(&tx_mutex);
}

static void uart_cli_thread(void)
{
	const struct shell *dummy = shell_backend_dummy_get_ptr();
	char line[LINE_BUF_SZ];
	int pos = 0;

	if (!device_is_ready(cli_uart)) {
		LOG_ERR("UART da ponte (%s) nao esta pronta", cli_uart->name);
		return;
	}
	if (dummy == NULL) {
		LOG_ERR("Backend dummy do shell indisponivel");
		return;
	}

	uart_irq_callback_user_data_set(cli_uart, uart_isr, NULL);
	uart_irq_rx_enable(cli_uart);

	LOG_INF("CLI por UART (ponte ESP32) pronto em %s", cli_uart->name);

	while (1) {
		uint8_t c;

		if (ring_buf_get(&rx_rb, &c, 1) == 0) {
			/* Nada no buffer: dormir e curto e seguro (a ISR guarda os bytes). */
			k_msleep(5);
			continue;
		}

		if (c == '\r') {
			continue; /* ignora CR; fim de linha e o '\n' */
		}

		if (c != '\n') {
			if (pos < (int)sizeof(line) - 1) {
				line[pos++] = (char)c;
			} else {
				LOG_WRN("RX ponte (USART1): linha longa demais - descartando");
				pos = 0;
			}
			continue;
		}

		/* Linha completa: executa como comando de shell. */
		line[pos] = '\0';

		if (pos > 0) {
			LOG_INF("RX ponte (USART1): '%s' (%d bytes)", line, pos);

			shell_backend_dummy_clear_output(dummy);
			shell_execute_cmd(dummy, line);

			size_t out_len = 0;
			const char *out = shell_backend_dummy_get_output(dummy, &out_len);

			if (out != NULL && out_len > 0) {
				bridge_uart_write(out, out_len);
			} else {
				static const char empty[] = "(sem saida)\n";

				bridge_uart_write(empty, strlen(empty));
			}
		}

		pos = 0;
	}
}

K_THREAD_DEFINE(uart_cli_id, UART_CLI_STACK, uart_cli_thread,
		NULL, NULL, NULL, UART_CLI_PRIO, 0, 0);
