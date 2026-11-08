#include "hw_config.h"

static spi_t spi = {
    .hw_inst = spi1,
    .sck_gpio = 10,
    .mosi_gpio = 11,
    .miso_gpio = 12,
    .baud_rate = 12500000
};

static sd_spi_if_t spi_interface = {.spi = &spi, .ss_gpio = 13};
static sd_card_t card = {.type = SD_IF_SPI, .spi_if_p = &spi_interface};

size_t sd_get_num(void) { return 1; }
sd_card_t *sd_get_by_num(size_t number) { return number == 0 ? &card : NULL; }
