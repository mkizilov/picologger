#ifndef TEST_SD_FF_H
#define TEST_SD_FF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } FATFS;
typedef uint64_t FSIZE_t;
typedef struct { int index; FSIZE_t position; bool open; } FIL;
typedef struct { unsigned int position; } DIR;
typedef struct { char fname[256]; unsigned char fattrib; } FILINFO;
typedef unsigned int UINT;
typedef enum { FR_OK, FR_EXIST, FR_DISK_ERR, FR_NO_FILE } FRESULT;
enum { FA_READ = 1, FA_WRITE = 2, FA_CREATE_NEW = 4, FA_OPEN_EXISTING = 0, AM_DIR = 0x10 };
FRESULT f_mount(FATFS *fs, const char *path, unsigned char immediate);
FRESULT f_open(FIL *file, const char *path, unsigned char flags);
FRESULT f_read(FIL *file, void *data, UINT size, UINT *read);
FRESULT f_write(FIL *file, const void *data, UINT size, UINT *written);
FRESULT f_sync(FIL *file);
FRESULT f_close(FIL *file);
FRESULT f_lseek(FIL *file, FSIZE_t offset);
FRESULT f_truncate(FIL *file);
FRESULT f_stat(const char *path, FILINFO *entry);
FRESULT f_opendir(DIR *directory, const char *path);
FRESULT f_readdir(DIR *directory, FILINFO *entry);
FRESULT f_closedir(DIR *directory);
FRESULT f_rename(const char *old_name, const char *new_name);
FSIZE_t fake_f_size(FIL *file);
#define f_size(file) fake_f_size(file)
#define f_tell(file) ((file)->position)
#endif
