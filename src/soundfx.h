#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioEngine AudioEngine;

typedef struct {
    AudioEngine* eng;
    char bell_path[256];
    int enabled; /* 0/1 */
} SoundFX;

void soundfx_init(SoundFX* sfx, AudioEngine* eng);
void soundfx_set_bell(SoundFX* sfx, const char* path);
void soundfx_set_enabled(SoundFX* sfx, bool enabled);

/* Plays the configured bell if enabled and the file exists. */
void soundfx_play_bell(SoundFX* sfx);

#ifdef __cplusplus
}
#endif
