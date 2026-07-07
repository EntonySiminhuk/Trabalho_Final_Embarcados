/*
 * Processamento de vibracao (Hard RT) + distribuicao via Zbus + comandos de shell.
 *
 * Fluxo:
 *   fft_task (prio alta / Hard RT):
 *     coleta janela do acelerometro MPU6050 -> remove offset (DC) ->
 *     filtro IIR (biquad) -> FFT (CMSIS-DSP) -> publica harmonicas no Zbus.
 *   send_result_task (Soft RT):
 *     assina o canal e gera alerta quando a 1a harmonica passa do limite,
 *     acumulando o historico de anomalias (resetavel pelo botao).
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <arm_math.h>
#include <arm_const_structs.h>
#include <math.h>
#include <string.h>

#include "vibration.h"

LOG_MODULE_DECLARE(zbus, CONFIG_ZBUS_LOG_LEVEL);

#define FFT_SIZE       256
#define LIMITE_CRITICO 100.0f

/* --------------------------------------------------------------------- */
/* Canal Zbus (a struct fft_msg vive em vibration.h, compartilhada)      */
/* --------------------------------------------------------------------- */
ZBUS_CHAN_DEFINE(fft_data_chan,
		 struct fft_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(callback_listener, task_sub),
		 ZBUS_MSG_INIT(.harmonic_cc = 0.0f, .harmonic_1st = 0.0f,
			       .harmonic_2nd = 0.0f, .harmonic_3rd = 0.0f,
			       .harmonic_4th = 0.0f));

atomic_t anomaly_count = ATOMIC_INIT(0);

void anomaly_reset(void)
{
	atomic_set(&anomaly_count, 0);
	LOG_INF("Historico de anomalias resetado");
}

/* --------------------------------------------------------------------- */
/* Observadores do canal                                                 */
/* --------------------------------------------------------------------- */
/* Log periodico do monitor (heartbeat do Hard RT). Ligavel via "log_fft on|off". */
static bool log_fft_enabled = true;
static int cnt_listener;

static void listener_callback_example(const struct zbus_channel *chan)
{
	const struct fft_msg *fft = zbus_chan_const_msg(chan);

	if (!log_fft_enabled) {
		return;
	}

	if (++cnt_listener > 10) {
		cnt_listener = 0;
		LOG_INF("monitor: 1a=%.2f 2a=%.2f 3a=%.2f 4a=%.2f (limite %.0f)",
			(double)fft->harmonic_1st, (double)fft->harmonic_2nd,
			(double)fft->harmonic_3rd, (double)fft->harmonic_4th,
			(double)LIMITE_CRITICO);
	}
}
ZBUS_LISTENER_DEFINE(callback_listener, listener_callback_example);

ZBUS_SUBSCRIBER_DEFINE(task_sub, 4);

/*
 * Tarefa Soft RT: reage aos dados e mantem o historico de anomalias.
 * Prioridade 4 = ABAIXO da fft_task (Hard RT, prio 3), deixando a hierarquia
 * hard > soft explicita. Ela bloqueia em zbus_sub_wait, entao mesmo com prio
 * menor nao perde publicacoes (a fila do subscriber tem profundidade 4).
 */
static void send_result_task(void)
{
	const struct zbus_channel *chan;

	while (!zbus_sub_wait(&task_sub, &chan, K_FOREVER)) {
		struct fft_msg fft;

		if (zbus_chan_read(&fft_data_chan, &fft, K_MSEC(100)) != 0) {
			continue;
		}

		if (fft.harmonic_1st > LIMITE_CRITICO) {
			atomic_val_t n = atomic_inc(&anomaly_count) + 1;

			LOG_WRN("ALERTA! 1a harmonica %.2f > limite (anomalia #%ld)",
				(double)fft.harmonic_1st, (long)n);
		}
	}
}
K_THREAD_DEFINE(send_result_task_id, CONFIG_MAIN_STACK_SIZE,
		send_result_task, NULL, NULL, NULL, 4, 0, 0);

/* --------------------------------------------------------------------- */
/* Filtro IIR (biquad, CMSIS-DSP) aplicado antes da FFT                  */
/* --------------------------------------------------------------------- */
#define IIR_ORDER     4
#define IIR_NUMSTAGES (IIR_ORDER / 2)

static float32_t m_biquad_state[IIR_ORDER];
static float32_t m_biquad_coeffs[5 * IIR_NUMSTAGES] = {
	1.0947e-07, 2.1895e-07, 1.0947e-07, 1.9329e+00, -9.3423e-01,
	1.0000e+00, 2.0000e+00, 1.0000e+00, 1.9709e+00, -9.7222e-01,
};

static const arm_biquad_cascade_df2T_instance_f32 iir_inst = {
	IIR_NUMSTAGES,
	m_biquad_state,
	m_biquad_coeffs,
};

/* --------------------------------------------------------------------- */
/* Tarefa Hard RT: amostragem + DSP                                      */
/* --------------------------------------------------------------------- */
static float filter_in[FFT_SIZE];
static float filter_out[FFT_SIZE];
static float ReIm[FFT_SIZE * 2];
static float mod[FFT_SIZE];

static void fft_task(void)
{
	struct fft_msg fft;
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(mpu6050));

	/*
	 * No cold-boot o MPU6050 as vezes nao responde ao I2C a tempo (power-up),
	 * e o init do driver falha ("Failed to read chip ID"). Em vez de desistir,
	 * tentamos reinicializar algumas vezes ate o sensor estabilizar.
	 */
	for (int i = 0; !device_is_ready(dev) && i < 20; i++) {
		LOG_WRN("MPU6050 nao pronto; reinicializando (%d/20)...", i + 1);
		k_msleep(200);
		(void)device_init(dev);
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("Sensor MPU6050 falhou definitivamente!");
		return;
	}
	LOG_INF("MPU6050 pronto");

	while (1) {
		/* 1. Coleta a janela do eixo X, acumulando a media (offset DC). */
		float sum = 0.0f;

		for (int i = 0; i < FFT_SIZE; i++) {
			struct sensor_value accel_x;

			sensor_sample_fetch(dev);
			sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel_x);

			float val = (float)sensor_value_to_double(&accel_x);

			filter_in[i] = val;
			sum += val;

			k_usleep(1000); /* ~1 kHz de amostragem */
		}

		/* 2. Remove o offset (gravidade / componente DC). */
		float mean = sum / (float)FFT_SIZE;

		for (int i = 0; i < FFT_SIZE; i++) {
			filter_in[i] -= mean;
		}

		/* 3. Filtro IIR sobre o sinal (Hard RT: processamento do sinal). */
		arm_biquad_cascade_df2T_f32(&iir_inst, filter_in, filter_out, FFT_SIZE);

		/* 4. Monta o vetor complexo intercalado (Re, Im) e roda a FFT. */
		for (int i = 0; i < FFT_SIZE; i++) {
			ReIm[i * 2] = filter_out[i];
			ReIm[i * 2 + 1] = 0.0f;
		}

		arm_cfft_f32(&arm_cfft_sR_f32_len256, ReIm, 0, 1);
		arm_cmplx_mag_f32(ReIm, mod, FFT_SIZE);

		/* 5. Publica as primeiras harmonicas no Zbus. */
		fft.harmonic_cc = mod[0] / 2.0f;
		fft.harmonic_1st = mod[1];
		fft.harmonic_2nd = mod[2];
		fft.harmonic_3rd = mod[3];
		fft.harmonic_4th = mod[4];

		zbus_chan_pub(&fft_data_chan, &fft, K_NO_WAIT);
	}
}
/* Prioridade alta (3) para caracterizar a tarefa como Hard RT. */
K_THREAD_DEFINE(fft_calculation_task_id, CONFIG_MAIN_STACK_SIZE,
		fft_task, NULL, NULL, NULL, 3, 0, 0);

/* --------------------------------------------------------------------- */
/* Comandos de shell (console local e CLI UDP compartilham estes)        */
/* --------------------------------------------------------------------- */
static int cmd_show_raw(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(mpu6050));
	struct sensor_value accel[3];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (sensor_sample_fetch(dev) != 0) {
		shell_error(shell, "Falha ao ler o MPU6050 (I2C).");
		return -EIO;
	}
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

	double x = sensor_value_to_double(&accel[0]);
	double y = sensor_value_to_double(&accel[1]);
	double z = sensor_value_to_double(&accel[2]);
	double mag = sqrt(x * x + y * y + z * z);

	shell_print(shell, "--- Aceleracao instantanea (MPU6050) ---");
	shell_print(shell, "  X   = %8.3f m/s2", x);
	shell_print(shell, "  Y   = %8.3f m/s2", y);
	shell_print(shell, "  Z   = %8.3f m/s2", z);
	shell_print(shell, "  |a| = %8.3f m/s2  (modulo do vetor)", mag);
	return 0;
}
SHELL_CMD_REGISTER(show_raw, NULL, "Aceleracao X/Y/Z + modulo (m/s2)", cmd_show_raw);

static int cmd_show_fft(const struct shell *shell, size_t argc, char **argv)
{
	struct fft_msg fft;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (zbus_chan_read(&fft_data_chan, &fft, K_MSEC(100)) != 0) {
		shell_error(shell, "Erro ao ler o canal Zbus!");
		return -EIO;
	}

	shell_print(shell, "--- Espectro de vibracao (FFT, eixo X) ---");
	shell_print(shell, "  Componente | Magnitude");
	shell_print(shell, "  DC         | %8.3f", (double)fft.harmonic_cc);
	shell_print(shell, "  1a harm.   | %8.3f", (double)fft.harmonic_1st);
	shell_print(shell, "  2a harm.   | %8.3f", (double)fft.harmonic_2nd);
	shell_print(shell, "  3a harm.   | %8.3f", (double)fft.harmonic_3rd);
	shell_print(shell, "  4a harm.   | %8.3f", (double)fft.harmonic_4th);
	shell_print(shell, "  Limite de anomalia (1a harm.): %.1f  ->  %s",
		    (double)LIMITE_CRITICO,
		    fft.harmonic_1st > LIMITE_CRITICO ? "ANOMALIA!" : "ok");
	return 0;
}
SHELL_CMD_REGISTER(show_fft, NULL, "Espectro de vibracao (harmonicas da FFT)", cmd_show_fft);

static int cmd_show_anomalies(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Anomalias acumuladas: %ld",
		    (long)atomic_get(&anomaly_count));
	return 0;
}
SHELL_CMD_REGISTER(show_anomalies, NULL, "Mostra o total de anomalias detectadas",
		   cmd_show_anomalies);

static int cmd_rt_status(const struct shell *shell, size_t argc, char **argv)
{
	struct fft_msg fft;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "=== Tarefas de Tempo Real ===");
	shell_print(shell, "Hard RT: fft_task (prio 3) - amostra MPU6050 ~1kHz, FFT %d pts",
		    FFT_SIZE);
	shell_print(shell, "Soft RT: send_result_task (prio 4) - alerta/anomalias");
	shell_print(shell, "Limite critico (1a harmonica): %.1f", (double)LIMITE_CRITICO);
	shell_print(shell, "Anomalias acumuladas: %ld", (long)atomic_get(&anomaly_count));

	if (zbus_chan_read(&fft_data_chan, &fft, K_MSEC(100)) == 0) {
		shell_print(shell, "Ultima 1a harmonica: %.3f", (double)fft.harmonic_1st);
	}
	return 0;
}
SHELL_CMD_REGISTER(rt_status, NULL, "Info das tarefas de tempo real", cmd_rt_status);

static int cmd_log_fft(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		log_fft_enabled = true;
		shell_print(shell, "Log periodico do monitor: LIGADO");
	} else if (argc == 2 && strcmp(argv[1], "off") == 0) {
		log_fft_enabled = false;
		shell_print(shell, "Log periodico do monitor: DESLIGADO");
	} else {
		shell_print(shell, "Uso: log_fft on|off   (atual: %s)",
			    log_fft_enabled ? "on" : "off");
	}
	return 0;
}
SHELL_CMD_REGISTER(log_fft, NULL, "Liga/desliga o log periodico do monitor", cmd_log_fft);

static int cmd_reset_anomalies(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	anomaly_reset();
	shell_print(shell, "Historico de anomalias zerado");
	return 0;
}
SHELL_CMD_REGISTER(reset_anomalies, NULL, "Zera o historico de anomalias",
		   cmd_reset_anomalies);
