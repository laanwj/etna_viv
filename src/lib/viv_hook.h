#ifndef H_VIV_HOOK
#define H_VIV_HOOK

#include <sys/mman.h>
#include <stdbool.h>
#include <stdint.h>

#define VIV_NUM_FEATURE_WORDS (8)
struct viv_hook_overrides {
    bool override_chip_model;
    uint32_t chip_model;
    bool override_chip_revision;
    uint32_t chip_revision;
    uint32_t features_clear[VIV_NUM_FEATURE_WORDS];
    uint32_t features_set[VIV_NUM_FEATURE_WORDS];
    uint32_t chip_flags_clear;
    uint32_t chip_flags_set;
};

extern void the_hook(const char *filename);
extern void close_hook(void);
extern void hook_start_logging(const char *filename);
extern void viv_hook_set_overrides(const struct viv_hook_overrides *overrides_in);
extern void viv_hook_log_marker(const char *data, size_t size);

extern int my_open(const char* path, int flags, ...);
extern void *my_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int my_munmap(void *addr, size_t length);
extern int my_ioctl(int d, int request, void *ptr);

#endif

