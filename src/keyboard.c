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

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7


/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
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
    //enum button_evt evt = val ? BUTTON_EVT_PRESSED : BUTTON_EVT_RELEASED;
	k_sem_give(&counter_sem);
}

static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_expired);

void button_cb(const struct device *gpiodev, struct gpio_callback *cb, uint32_t pin)
{
    k_work_schedule(&debounce_work, K_MSEC(50));
}

static struct gpio_callback button_cb_data;
int keyboard_thread(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button0)) {
		printk("Error: button device %s is not ready\n",
		       button0.port->name);
		return 0;
	}
    
    if (!gpio_is_ready_dt(&button1)) {
		printk("Error: button device %s is not ready\n",
		       button1.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button0, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button0.port->name, button0.pin);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button1, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button1.port->name, button1.pin);
		return 0;
	}

	/* Callback uses pin_mask, so need bit shifting */
	gpio_init_callback(&button_cb_data, button_cb, BIT(button0.pin) | BIT(button1.pin));
	gpio_add_callback(button0.port, &button_cb_data);
    gpio_add_callback(button1.port, &button_cb_data);

	/* Setup input pin for interrupt */
	ret = gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_FALLING);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button0.port->name, button0.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_FALLING);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button1.port->name, button1.pin);
		return 0;
	}

	printk("Waiting button being pressed\n");
	while(1){
		k_sem_take(&counter_sem, K_FOREVER);
		printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	}
}

K_THREAD_DEFINE(keyboard_id, STACKSIZE, keyboard_thread, NULL, NULL, NULL,PRIORITY, 0, 0);