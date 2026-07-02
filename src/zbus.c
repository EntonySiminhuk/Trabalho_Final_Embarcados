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

LOG_MODULE_DECLARE(zbus, CONFIG_ZBUS_LOG_LEVEL);

#define FFT_SIZE       256
#define LIMITE_CRITICO 100.0f

/* --------------------------------------------------------------------- */
/* Mensagem e canal Zbus                                                 */
/* --------------------------------------------------------------------- */
struct fft_msg {
	float harmonic_cc;
	float harmonic_1st;
	float harmonic_2nd;
	float harmonic_3rd;
	float harmonic_4th;
};

ZBUS_CHAN_DEFINE(fft_data_chan,
		 struct fft_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(callback_listener, task_sub),
		 ZBUS_MSG_INIT(.harmonic_cc = 0.0f, .harmonic_1st = 0.0f,
			       .harmonic_2nd = 0.0f, .harmonic_3rd = 0.0f,
			       .harmonic_4th = 0.0f));

/* Historico de anomalias (Soft RT). Exportado para o botao poder resetar. */
atomic_t anomaly_count = ATOMIC_INIT(0);

void anomaly_reset(void)
{
	atomic_set(&anomaly_count, 0);
	LOG_INF("Historico de anomalias resetado");
}

/* --------------------------------------------------------------------- */
/* Observadores do canal                                                 */
/* --------------------------------------------------------------------- */
static int cnt_listener;
static void listener_callback_example(const struct zbus_channel *chan)
{
	const struct fft_msg *fft = zbus_chan_const_msg(chan);

	if (++cnt_listener > 10) {
		cnt_listener = 0;
		LOG_INF("FFT: cc=%.2f 1st=%.2f 2nd=%.2f 3rd=%.2f 4th=%.2f",
			(double)fft->harmonic_cc, (double)fft->harmonic_1st,
			(double)fft->harmonic_2nd, (double)fft->harmonic_3rd,
			(double)fft->harmonic_4th);
	}
}
ZBUS_LISTENER_DEFINE(callback_listener, listener_callback_example);

ZBUS_SUBSCRIBER_DEFINE(task_sub, 4);

/* Tarefa Soft RT: reage aos dados e mantem o historico de anomalias. */
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
		send_result_task, NULL, NULL, NULL, 3, 0, 0);

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

	if (!device_is_ready(dev)) {
		LOG_ERR("Sensor MPU6050 nao esta pronto!");
		return;
	}

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

	sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

	shell_print(shell, "RAW: X=%f Y=%f Z=%f",
		    sensor_value_to_double(&accel[0]),
		    sensor_value_to_double(&accel[1]),
		    sensor_value_to_double(&accel[2]));
	return 0;
}
SHELL_CMD_REGISTER(show_raw, NULL, "Mostra valores brutos do sensor", cmd_show_raw);

static int cmd_show_fft(const struct shell *shell, size_t argc, char **argv)
{
	struct fft_msg fft;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (zbus_chan_read(&fft_data_chan, &fft, K_MSEC(100)) == 0) {
		shell_print(shell, "--- Ultimos Dados de FFT ---");
		shell_print(shell, "CC: %f | 1st: %f | 2nd: %f | 3rd: %f | 4th: %f",
			    (double)fft.harmonic_cc, (double)fft.harmonic_1st,
			    (double)fft.harmonic_2nd, (double)fft.harmonic_3rd,
			    (double)fft.harmonic_4th);
	} else {
		shell_error(shell, "Erro ao ler o canal Zbus!");
	}
	return 0;
}
SHELL_CMD_REGISTER(show_fft, NULL, "Mostra os dados atuais do FFT", cmd_show_fft);

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
