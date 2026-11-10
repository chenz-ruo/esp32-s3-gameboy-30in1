#include "myiic.h"
#include "bsp.h"

i2c_master_bus_handle_t bus_handle;

esp_err_t myiic_init(void)
{
    bus_handle = bsp_i2c_bus();
    return bus_handle ? ESP_OK : ESP_FAIL;
}
