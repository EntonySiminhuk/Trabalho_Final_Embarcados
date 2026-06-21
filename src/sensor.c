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
#define SENSOR0_NODE	DT_ALIAS(sensor0)

#if !DT_NODE_HAS_STATUS(SENSOR0_NODE, okay)
#error "Unsupported board: sensor0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec sensor0 = GPIO_DT_SPEC_GET_OR(SENSOR0_NODE, gpios, {0});

K_SEM_DEFINE(sensor_sem, 0, 1);


void sensor_cb(const struct device *gpiodev, struct gpio_callback *cb, uint32_t pin)
{
    
}

static struct gpio_callback button_cb_data;

int sensor_thread(void)
{
	int ret;

	if (!gpio_is_ready_dt(&sensor0)) {
		printk("Error: button device %s is not ready\n",
		       sensor0.port->name);
		return 0;
	}
    

	ret = gpio_pin_configure_dt(&sensor0, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, sensor0.port->name, sensor0.pin);
		return 0;
	}

	/* Callback uses pin_mask, so need bit shifting */
	gpio_init_callback(&button_cb_data, sensor_cb, BIT(sensor0.pin));
	gpio_add_callback(sensor0.port, &button_cb_data);

	/* Setup input pin for interrupt */
	ret = gpio_pin_interrupt_configure_dt(&sensor0, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, sensor0.port->name, sensor0.pin);
		return 0;
	}

	printk("Waiting button being pressed\n");
	while(1){
		k_sem_take(&sensor_sem, K_FOREVER);
		printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	}
}

//K_THREAD_DEFINE(sensor_id, STACKSIZE, sensor_thread, NULL, NULL, NULL,PRIORITY, 0, 0);