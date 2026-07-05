/*
 * Contrato compartilhado do monitor de vibracao.
 *
 * Reune o que precisa ser visto por mais de um modulo:
 *   - struct fft_msg    : mensagem publicada no canal Zbus (harmonicas da FFT);
 *   - fft_data_chan     : o canal Zbus (definido em zbus.c);
 *   - anomaly_count     : historico de anomalias (definido em zbus.c);
 *   - anomaly_reset()   : zera o historico (usado pelo botao).
 *
 * Assim a tarefa de telemetria (src/telemetry.c) le os mesmos dados que o
 * shell, sem duplicar a definicao da struct.
 */

#ifndef VIBRATION_H
#define VIBRATION_H

#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/atomic.h>

/* Harmonicas da FFT do eixo X (magnitudes). */
struct fft_msg {
	float harmonic_cc;   /* componente DC */
	float harmonic_1st;
	float harmonic_2nd;
	float harmonic_3rd;
	float harmonic_4th;
};

ZBUS_CHAN_DECLARE(fft_data_chan);

/* Historico de anomalias (Soft RT). Definido em zbus.c. */
extern atomic_t anomaly_count;

/* Zera o historico de anomalias. */
void anomaly_reset(void);

#endif /* VIBRATION_H */
