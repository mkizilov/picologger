#ifndef TEST_LOGGER_SYNC_H
#define TEST_LOGGER_SYNC_H
void fake_memory_barrier(void);
#define __dmb() fake_memory_barrier()
#endif
