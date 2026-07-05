/*
 * Telemetria periodica STM32 -> ESP32 -> WebApp (VibraMonitor).
 *
 *   fft_task (Hard RT) --publica--> Zbus (fft_data_chan)
 *                                      |
 *                       telemetry_task (ESTA tarefa, baixa prioridade)
 *                                      | le o ultimo espectro e serializa
 *                                      v
 *   USART1 "@V,<dc>,<h1>,<h2>,<h3>,<h4>,<anomalias>\n"  --> ESP32 --HTTP POST--> API
 *
 * Por que uma tarefa separada e de BAIXA prioridade (numero alto = menos
 * prioritaria no Zephyr):
 *   - Nao pode competir com a fft_task/send_result_task (prio 3, Hard/Soft RT)
 *     nem atrasar a amostragem do sensor. Roda so quando o resto esta ocioso.
 *   - A serializacao e a escrita na UART sao lentas (I/O) e nao devem ficar
 *     no caminho critico do DSP.
 *
 * Periodicidade: 1000 ms. Justificativa:
 *   - O dashboard e um monitor de tendencia; 1 Hz e suficiente e casa com a
 *     taxa do simulador que o webapp ja usava (Simulator:IntervalMs=1000).
 *   - Mantem o trafego Wi-Fi/HTTP baixo e a carga da UART irrisoria.
 *   - A deteccao de anomalia continua sendo Hard/Soft RT no proprio STM
 *     (nao depende desta taxa); aqui so reportamos o estado.
 *
 * A linha comeca com a sentinela "@V," para o ESP distinguir telemetria das
 * respostas do CLI de shell, que compartilham a mesma USART1.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "vibration.h"
#include "bridge_uart.h"

LOG_MODULE_REGISTER(telemetry, LOG_LEVEL_INF);

#define TELEMETRY_PERIOD_MS 1000
#define TELEMETRY_STACK     1024
/* Prioridade alta em numero => baixa prioridade de escalonamento (abaixo do
 * shell/CLI que e 6, e bem abaixo do Hard/Soft RT que e 3). */
#define TELEMETRY_PRIO      10
#define TELEMETRY_LINE_SZ   96

static void telemetry_task(void)
{
	struct fft_msg fft;
	char line[TELEMETRY_LINE_SZ];

	/* Da um tempo para sensor/DSP subirem e publicarem o primeiro espectro. */
	k_msleep(2000);

	LOG_INF("Telemetria STM->ESP ativa (%d ms, sentinela \"@V,\")",
		TELEMETRY_PERIOD_MS);

	while (1) {
		k_msleep(TELEMETRY_PERIOD_MS);

		/* Le o ultimo espectro publicado. Se ainda nao ha dado, pula o ciclo. */
		if (zbus_chan_read(&fft_data_chan, &fft, K_MSEC(100)) != 0) {
			LOG_WRN("Sem espectro no Zbus ainda; ciclo pulado");
			continue;
		}

		long anom = (long)atomic_get(&anomaly_count);

		/* snprintk (do Zephyr) garante suporte a float independente da libc. */
		int n = snprintk(line, sizeof(line),
				 "@V,%.3f,%.3f,%.3f,%.3f,%.3f,%ld\n",
				 (double)fft.harmonic_cc, (double)fft.harmonic_1st,
				 (double)fft.harmonic_2nd, (double)fft.harmonic_3rd,
				 (double)fft.harmonic_4th, anom);

		if (n <= 0) {
			continue;
		}
		if (n >= (int)sizeof(line)) {
			n = (int)sizeof(line) - 1; /* truncou: envia o que coube */
		}

		bridge_uart_write(line, (size_t)n);
	}
}

K_THREAD_DEFINE(telemetry_id, TELEMETRY_STACK, telemetry_task,
		NULL, NULL, NULL, TELEMETRY_PRIO, 0, 0);
