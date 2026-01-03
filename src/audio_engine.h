#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioEngine AudioEngine;

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERR_INIT = -1,
    AUDIO_ERR_OPEN = -2,
    AUDIO_ERR_DECODE = -3,
    AUDIO_ERR_STREAM = -4,
} AudioResult;

/* Initialize the audio device + mixer.
   Safe to call even if SDL audio subsystem isn't initialized yet. */
AudioResult audio_engine_init(AudioEngine** out);

/* Shutdown + free. */
void audio_engine_quit(AudioEngine** inout);

/* Master volume 0..128 (SDL scale). */
void audio_engine_set_master_volume(AudioEngine* a, int vol);

/* Music volume 0..128 (SDL scale). */
void audio_engine_set_music_volume(AudioEngine* a, int vol);

/* SFX volume 0..128 (SDL scale). */
void audio_engine_set_sfx_volume(AudioEngine* a, int vol);

/* Play / stop music (decodes WAV/MP3 fully to PCM and streams via callback). */
AudioResult audio_engine_play_music(AudioEngine* a, const char* path, bool restart_if_same);
void audio_engine_stop_music(AudioEngine* a);

/* Pause/resume music only. */
void audio_engine_set_music_paused(AudioEngine* a, bool paused);

/* Fire-and-forget SFX (bell). It will mix over music. */
AudioResult audio_engine_play_sfx(AudioEngine* a, const char* path);

/* Call once per frame to service “track ended” bookkeeping. */
void audio_engine_update(AudioEngine* a);

/* True if music finished naturally (not stopped). Resets to false after read. */
bool audio_engine_pop_music_ended(AudioEngine* a);

#ifdef __cplusplus
}
#endif
