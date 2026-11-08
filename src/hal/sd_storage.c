#include "sd_storage.h"

#include "ff.h"
#include "hw_config.h"
#include "pico/stdlib.h"

static FATFS filesystem;
static FIL file;
static bool driver_initialized;

static bool mount_card(void *context)
{
    (void)context;
    if (!driver_initialized) {
        if (!sd_init_driver()) return false;
        driver_initialized = true;
    }
    sd_card_t *card = sd_get_by_num(0);
    if (!card || !card->sd_test_com || !card->sd_test_com(card)) return false;
    return f_mount(&filesystem, "0:", 1) == FR_OK;
}

static int create_file(void *context, const char *name)
{
    (void)context;
    FRESULT result = f_open(&file, name, FA_WRITE | FA_CREATE_NEW);
    return result == FR_OK ? 0 : result == FR_EXIST ? 1 : -1;
}

static bool write_file(void *context, const uint8_t *data, size_t length, size_t *written)
{
    (void)context;
    UINT actual = 0;
    FRESULT result = f_write(&file, data, (UINT)length, &actual);
    *written = actual;
    return result == FR_OK;
}

static bool sync_file(void *context) { (void)context; return f_sync(&file) == FR_OK; }
static bool close_file(void *context) { (void)context; return f_close(&file) == FR_OK; }

static void unmount_card(void *context)
{
    (void)context;
    (void)f_mount(NULL, "0:", 0);
    sd_card_t *card = sd_get_by_num(0);
    if (driver_initialized && card) card->state.m_Status |= STA_NOINIT;
}

static bool card_present(void *context)
{
    (void)context;
    sd_card_t *card = sd_get_by_num(0);
    return card && card->sd_test_com && card->sd_test_com(card);
}

static uint64_t clock_us(void *context) { (void)context; return time_us_64(); }

log_storage_t sd_storage_interface(void)
{
    return (log_storage_t){NULL, mount_card, create_file, write_file, sync_file,
                           close_file, unmount_card, card_present, clock_us};
}
