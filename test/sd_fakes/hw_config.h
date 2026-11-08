#ifndef TEST_SD_HW_CONFIG_H
#define TEST_SD_HW_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
extern int fake_spi1;
#define spi1 (&fake_spi1)
#define STA_NOINIT 1u
typedef struct { void *hw_inst; unsigned int sck_gpio, mosi_gpio, miso_gpio, baud_rate; } spi_t;
typedef struct { spi_t *spi; unsigned int ss_gpio; } sd_spi_if_t;
typedef enum { SD_IF_SPI = 1 } sd_if_t;
typedef struct sd_card_t sd_card_t;
struct sd_card_t {
    sd_if_t type;
    sd_spi_if_t *spi_if_p;
    struct { uint32_t m_Status; } state;
    bool (*sd_test_com)(sd_card_t *);
};
bool sd_init_driver(void);
size_t sd_get_num(void);
sd_card_t *sd_get_by_num(size_t number);
#endif
