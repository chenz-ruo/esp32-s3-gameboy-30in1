/* Shim: the vendor code expects myiic_init() to hand back a shared I2C bus.
 * We already own one in bsp.c, so this just forwards it. */
#ifndef MYIIC_H
#define MYIIC_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#define IIC_SPEED_CLK 400000

extern i2c_master_bus_handle_t bus_handle;

esp_err_t myiic_init(void);

#endif
