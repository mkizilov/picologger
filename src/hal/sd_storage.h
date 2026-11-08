#ifndef PICOLOGGER_SD_STORAGE_H
#define PICOLOGGER_SD_STORAGE_H

#include "log_writer.h"
#include "command.h"

log_storage_t sd_storage_interface(void);
/* Called on the storage core after successful recovery reads. */
void sd_storage_set_progress_hook(void (*hook)(void));

typedef struct {
    char name[COMMAND_FILENAME_MAX + 1];
    uint64_t size;
    bool directory;
} sd_list_entry_t;

/* Storage core only. Each next/read performs at most one FatFs operation. */
bool sd_storage_list_open(void);
/* 1 entry, 2 unsupported name, 0 end, -1 error. */
int sd_storage_list_next(sd_list_entry_t *entry);
bool sd_storage_list_close(void);
bool sd_storage_dump_open(const char *name);
bool sd_storage_dump_read(uint8_t *data, size_t capacity, size_t *read);
bool sd_storage_dump_close(void);

#endif
