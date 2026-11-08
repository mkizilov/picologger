#ifndef TEST_SD_FF_H
#define TEST_SD_FF_H
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } FATFS;
typedef struct { int unused; } FIL;
typedef unsigned int UINT;
typedef enum { FR_OK, FR_EXIST, FR_DISK_ERR } FRESULT;
enum { FA_WRITE = 2, FA_CREATE_NEW = 4 };
FRESULT f_mount(FATFS *fs, const char *path, unsigned char immediate);
FRESULT f_open(FIL *file, const char *path, unsigned char flags);
FRESULT f_write(FIL *file, const void *data, UINT size, UINT *written);
FRESULT f_sync(FIL *file);
FRESULT f_close(FIL *file);
#endif
