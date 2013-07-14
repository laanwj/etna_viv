#ifndef H_VIV_HOOK
#define H_VIV_HOOK

#include <sys/mman.h>

extern void the_hook(const char *filename);
extern void close_hook(void);
extern void hook_start_logging(const char *filename);

extern int my_open(const char* path, int flags, ...);
extern void *my_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int my_munmap(void *addr, size_t length);
extern int my_ioctl(int d, int request, void *ptr);

#endif

