/*
 * Copyright (c) 2016 Wladimir J. van der Laan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "viv_hook.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

static int frame_counter = 0;
static int max_frames = 0;

static void parse_environment(struct viv_hook_overrides *overrides)
{
    char *value;
    char envkey[60];
    if ((value = getenv("ETNAVIV_CHIP_MODEL")) != NULL) {
        overrides->override_chip_model = true;
        overrides->chip_model = strtoul(value, 0, 0);
        printf("Overriding chip model to 0x%x\n", overrides->chip_model);
    }
    if ((value = getenv("ETNAVIV_CHIP_REVISION")) != NULL) {
        overrides->override_chip_revision = true;
        overrides->chip_revision = strtoul(value, 0, 0);
        printf("Overriding chip revision to 0x%x\n", overrides->chip_revision);
    }
    for (int x=0; x<VIV_NUM_FEATURE_WORDS; ++x) {
        snprintf(envkey, sizeof(envkey), "ETNAVIV_FEATURES%d_SET", x);
        if ((value = getenv(envkey)) != NULL) {
            overrides->features_set[x] = strtoul(value, 0, 0);
            printf("Setting features 0x%08x of word %d\n", overrides->features_set[x], x);
        }
        snprintf(envkey, sizeof(envkey), "ETNAVIV_FEATURES%d_CLEAR", x);
        if ((value = getenv(envkey)) != NULL) {
            overrides->features_clear[x] = strtoul(value, 0, 0);
            printf("Clearing features 0x%08x of word %d\n", overrides->features_clear[x], x);
        }
    }
    if ((value = getenv("ETNAVIV_CHIP_FLAGS_SET")) != NULL) {
        overrides->chip_flags_set = strtoul(value, 0, 0);
        printf("Setting chip flags 0x%08x\n", overrides->chip_flags_set);
    }
    if ((value = getenv("ETNAVIV_CHIP_FLAGS_CLEAR")) != NULL) {
        overrides->chip_flags_clear = strtoul(value, 0, 0);
        printf("Clearing chip flags 0x%08x\n", overrides->chip_flags_clear);
    }
    /* Quit after rendering a certain number of frames */
    if ((value = getenv("ETNAVIV_MAX_FRAMES")) != NULL) {
        max_frames = strtol(value, 0, 0);
    }
}

static void __attribute__((constructor)) constructor()
{
    struct viv_hook_overrides overrides = {0};
    printf("Constructor\n");
    parse_environment(&overrides);
    viv_hook_set_overrides(&overrides);
    the_hook(getenv("ETNAVIV_FDR"));
}

static void __attribute__((destructor)) destructor()
{
    printf("Destructor\n");
    close_hook();
}

unsigned int eglSwapBuffers(void* dpy, void* surface);

unsigned int eglSwapBuffers(void* dpy, void* surface)
{
    static unsigned int (*my_eglSwapBuffers)(void* dpy, void* surface) = NULL;
    if (!my_eglSwapBuffers)
	my_eglSwapBuffers = dlsym(RTLD_NEXT, "eglSwapBuffers");
    unsigned int p = my_eglSwapBuffers(dpy, surface);

    /* Log a frame marker */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t frame_marker[4] = {0x594e4f50, frame_counter, ts.tv_sec, ts.tv_nsec};
    viv_hook_log_marker((void*)frame_marker, sizeof(frame_marker));
    frame_counter += 1;

    /* Stop after ETNAVIV_MAX_FRAMES */
    if (max_frames && frame_counter >= max_frames) {
        printf("Stopping program after ETNAVIV_MAX_FRAMES=%d frames\n", max_frames);
        /* Try to shut down egl properly at least */
        static unsigned int (*my_eglTerminate)(void* dpy) = NULL;
        my_eglTerminate = dlsym(RTLD_DEFAULT, "eglTerminate");
        my_eglTerminate(dpy);
        exit(0);
    }
    return p;
}
