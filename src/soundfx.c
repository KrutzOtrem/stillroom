#include "soundfx.h"
#include "audio_engine.h"
#include <stdio.h>
#include <string.h>

static bool file_exists(const char* path) {
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void soundfx_init(SoundFX* sfx, AudioEngine* eng) {
    if (!sfx) return;
    memset(sfx, 0, sizeof(*sfx));
    sfx->eng = eng;
    sfx->enabled = 1;
    /* Default bell path. You can change later via settings. */
    strncpy(sfx->bell_path, "sounds/bell.wav", sizeof(sfx->bell_path) - 1);
    sfx->bell_path[sizeof(sfx->bell_path) - 1] = 0;
}

void soundfx_set_bell(SoundFX* sfx, const char* path) {
    if (!sfx) return;
    if (!path) path = "";
    strncpy(sfx->bell_path, path, sizeof(sfx->bell_path) - 1);
    sfx->bell_path[sizeof(sfx->bell_path) - 1] = 0;
}

void soundfx_set_enabled(SoundFX* sfx, bool enabled) {
    if (!sfx) return;
    sfx->enabled = enabled ? 1 : 0;
}

void soundfx_play_bell(SoundFX* sfx) {
    if (!sfx || !sfx->eng) return;
    if (!sfx->enabled) return;
    if (!file_exists(sfx->bell_path)) return;

    /* Ignore errors for now. If decoding fails, we just stay silent. */
    audio_engine_play_sfx(sfx->eng, sfx->bell_path);
}
