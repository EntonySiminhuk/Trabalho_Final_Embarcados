/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>

#include <string.h>

/* Historico de anomalias vive no zbus.c; o botao apenas o reseta. */
extern void anomaly_reset(void);

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

/*
 * Botoes: sw0 = user button da placa (PC13, gpioc),
 *         sw1 = botao externo definido no overlay (PA10, gpioa).
 */
#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0});

K_SEM_DEFINE(counter_sem, 0, 1);

static void debounce_expired(struct k_work *work)
{
	ARG_UNUSED(work);

	int val0 = gpio_pin_get_dt(&button0);
	int val1 = gpio_pin_get_dt(&button1);

	if (val0) {
		printk("SW0 pressed\n");
	}
	if (val1) {
		printk("SW1 pressed\n");
	}

	/* Qualquer botao reseta o historico de anomalias (requisito do botao). */
	if (val0 || val1) {
		anomaly_reset();
	}

	k_sem_give(&counter_sem);
}

static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_expired);

void button_cb(const struct device *gpiodev, struct gpio_callback *cb, uint32_t pin)
{
	ARG_UNUSED(gpiodev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pin);

	k_work_schedule(&debounce_work, K_MSEC(50));
}

/*
 * button0 e button1 estao em portas GPIO diferentes (gpioc e gpioa), entao
 * cada um precisa da SUA propria struct gpio_callback. Reusar a mesma struct
 * em duas portas corrompe a lista de callbacks.
 */
static struct gpio_callback button0_cb_data;
static struct gpio_callback button1_cb_data;

static int configure_button(const struct gpio_dt_spec *btn, struct gpio_callback *cb)
{
	int ret;

	if (!gpio_is_ready_dt(btn)) {
		printk("Error: button device %s is not ready\n", btn->port->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(btn, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, btn->port->name, btn->pin);
		return ret;
	}

	gpio_init_callback(cb, button_cb, BIT(btn->pin));
	gpio_add_callback(btn->port, cb);

	ret = gpio_pin_interrupt_configure_dt(btn, GPIO_INT_EDGE_FALLING);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
		       ret, btn->port->name, btn->pin);
		return ret;
	}

	return 0;
}

int keyboard_thread(void)
{
	if (configure_button(&button0, &button0_cb_data) != 0) {
		return 0;
	}
	if (configure_button(&button1, &button1_cb_data) != 0) {
		return 0;
	}

	printk("Waiting button being pressed\n");
	while (1) {
		k_sem_take(&counter_sem, K_FOREVER);
		printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	}
}

K_THREAD_DEFINE(keyboard_id, STACKSIZE, keyboard_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
