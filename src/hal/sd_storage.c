#include "sd_storage.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "hw_config.h"
#include "pico/stdlib.h"

static FATFS filesystem;
static FIL file;
static FIL dump_file;
static DIR list_directory;
static bool listing, dumping;
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

static bool recovery_size(void *context, uint64_t *size)
{
    FIL *target = context;
    *size = (uint64_t)f_size(target);
    return true;
}

static bool recovery_read(void *context, uint64_t offset, uint8_t *data, size_t length, size_t *read)
{
    FIL *target = context;
    if ((uint64_t)(FSIZE_t)offset != offset || f_lseek(target, (FSIZE_t)offset) != FR_OK ||
        (uint64_t)f_tell(target) != offset) return false;
    UINT actual = 0;
    FRESULT result = f_read(target, data, (UINT)length, &actual);
    *read = actual;
    return result == FR_OK;
}

static bool recovery_truncate(void *context, uint64_t length)
{
    FIL *target = context;
    return (uint64_t)(FSIZE_t)length == length && f_lseek(target, (FSIZE_t)length) == FR_OK &&
           (uint64_t)f_tell(target) == length && f_truncate(target) == FR_OK && f_sync(target) == FR_OK;
}

static char upper_ascii(char value)
{
    return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static unsigned int log_number(const char *name)
{
    if (strlen(name) != 12 || upper_ascii(name[0]) != 'L' || upper_ascii(name[1]) != 'O' ||
        upper_ascii(name[2]) != 'G' || name[3] != '_' || name[8] != '.' ||
        upper_ascii(name[9]) != 'B' || upper_ascii(name[10]) != 'I' || upper_ascii(name[11]) != 'N') return 0;
    unsigned int number = 0;
    for (unsigned int i = 4; i < 8; ++i) {
        if (name[i] < '0' || name[i] > '9') return 0;
        number = number * 10u + (unsigned int)(name[i] - '0');
    }
    return number;
}

static bool recover_newest(void *context, bool *found, log_recovery_result_t *result)
{
    (void)context;
    *found = false;
    DIR directory;
    FILINFO entry;
    if (f_opendir(&directory, "0:/") != FR_OK) return false;
    unsigned int newest = 0;
    bool ok = true;
    for (;;) {
        if (f_readdir(&directory, &entry) != FR_OK) { ok = false; break; }
        if (!entry.fname[0]) break;
        if (!(entry.fattrib & AM_DIR)) {
            unsigned int number = log_number(entry.fname);
            if (number > newest) newest = number;
        }
    }
    if (f_closedir(&directory) != FR_OK) ok = false;
    if (!ok || !newest) return ok;
    char name[20];
    (void)snprintf(name, sizeof(name), "0:/LOG_%04u.BIN", newest);
    if (f_open(&file, name, FA_READ | FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return false;
    log_recovery_file_t recovery = {&file, recovery_size, recovery_read, recovery_truncate};
    log_recovery_status_t status = log_recover(&recovery, result);
    if (f_close(&file) != FR_OK) return false;
    if (status != LOG_RECOVERY_OK) return false;
    *found = true;
    return true;
}

static int create_file(void *context, const char *name, const uint8_t *header, size_t length,
                       uint64_t *write_us, uint64_t *sync_us)
{
    (void)context;
    *write_us = *sync_us = 0;
    if (length != LOG_HEADER_V2_SIZE) return -1;
    FILINFO existing;
    FRESULT result = f_stat(name, &existing);
    if (result == FR_OK) return 1;
    if (result != FR_NO_FILE) return -1;
    char temporary[20];
    size_t name_length = strlen(name);
    if (name_length != 15 || name_length >= sizeof(temporary)) return -1;
    memcpy(temporary, name, name_length + 1);
    memcpy(temporary + name_length - 3, "TMP", 3);
    result = f_open(&file, temporary, FA_WRITE | FA_CREATE_NEW);
    if (result == FR_EXIST) return 1;
    if (result != FR_OK) return -1;
    UINT written = 0;
    uint64_t start = time_us_64();
    result = f_write(&file, header, (UINT)length, &written);
    *write_us = time_us_64() - start;
    if (result != FR_OK || written != length) { (void)f_close(&file); return -1; }
    start = time_us_64();
    result = f_sync(&file);
    *sync_us = time_us_64() - start;
    if (result != FR_OK) { (void)f_close(&file); return -1; }
    if (f_close(&file) != FR_OK) return -1;
    result = f_rename(temporary, name);
    if (result == FR_EXIST) return 1;
    if (result != FR_OK) return -1;
    if (f_open(&file, name, FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return -1;
    if (f_lseek(&file, (FSIZE_t)length) != FR_OK || (uint64_t)f_tell(&file) != length) {
        (void)f_close(&file);
        return -1;
    }
    return 0;
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
    (void)sd_storage_list_close();
    (void)sd_storage_dump_close();
    (void)f_mount(NULL, "0:", 0);
    sd_card_t *card = sd_get_by_num(0);
    if (driver_initialized && card) card->state.m_Status |= STA_NOINIT;
}

bool sd_storage_list_open(void)
{
    if (listing || f_opendir(&list_directory, "0:/") != FR_OK) return false;
    listing = true;
    return true;
}

int sd_storage_list_next(sd_list_entry_t *out)
{
    if (!listing || !out) return -1;
    FILINFO entry;
    if (f_readdir(&list_directory, &entry) != FR_OK) return -1;
    if (!entry.fname[0]) return 0;
    if (!command_filename_valid(entry.fname)) return 2;
    memcpy(out->name, entry.fname, strlen(entry.fname) + 1);
    out->size = (uint64_t)entry.fsize;
    out->directory = (entry.fattrib & AM_DIR) != 0;
    return 1;
}

bool sd_storage_list_close(void)
{
    if (!listing) return true;
    listing = false;
    return f_closedir(&list_directory) == FR_OK;
}

bool sd_storage_dump_open(const char *name)
{
    if (dumping || !command_filename_valid(name)) return false;
    char path[COMMAND_FILENAME_MAX + 4];
    (void)snprintf(path, sizeof(path), "0:/%s", name);
    if (f_open(&dump_file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) return false;
    dumping = true;
    return true;
}

bool sd_storage_dump_read(uint8_t *data, size_t capacity, size_t *read)
{
    if (!dumping || !data || !read || capacity > 32) return false;
    UINT actual = 0;
    FRESULT result = f_read(&dump_file, data, (UINT)capacity, &actual);
    *read = actual;
    return result == FR_OK;
}

bool sd_storage_dump_close(void)
{
    if (!dumping) return true;
    dumping = false;
    return f_close(&dump_file) == FR_OK;
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
    return (log_storage_t){NULL, mount_card, recover_newest, create_file, write_file, sync_file,
                           close_file, unmount_card, card_present, clock_us};
}
