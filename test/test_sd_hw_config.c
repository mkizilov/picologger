#include "unity.h"
#include "hw_config.h"

int fake_spi1;
void setUp(void) {}
void tearDown(void) {}

static void uses_requested_spi1_and_pins(void)
{
    TEST_ASSERT_EQUAL_UINT(1, sd_get_num());
    TEST_ASSERT_NULL(sd_get_by_num(1));
    sd_card_t *card = sd_get_by_num(0);
    TEST_ASSERT_NOT_NULL(card);
    TEST_ASSERT_EQUAL_INT(SD_IF_SPI, card->type);
    TEST_ASSERT_EQUAL_UINT(13, card->spi_if_p->ss_gpio);
    TEST_ASSERT_EQUAL_PTR(spi1, card->spi_if_p->spi->hw_inst);
    TEST_ASSERT_EQUAL_UINT(10, card->spi_if_p->spi->sck_gpio);
    TEST_ASSERT_EQUAL_UINT(11, card->spi_if_p->spi->mosi_gpio);
    TEST_ASSERT_EQUAL_UINT(12, card->spi_if_p->spi->miso_gpio);
    TEST_ASSERT_EQUAL_UINT(12500000, card->spi_if_p->spi->baud_rate);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(uses_requested_spi1_and_pins);
    return UNITY_END();
}
