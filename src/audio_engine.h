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

/* Ambience volume 0..128 (SDL scale). */
void audio_engine_set_ambience_volume(AudioEngine* a, int vol);


/* Play / stop music (decodes WAV/MP3 fully to PCM and streams via callback). */
AudioResult audio_engine_play_music(AudioEngine* a, const char* path, bool restart_if_same);
void audio_engine_stop_music(AudioEngine* a);

/* Pause/resume music only. */
void audio_engine_set_music_paused(AudioEngine* a, bool paused);

/* Looping ambience (WAV/MP3). Streamed to avoid UI hitches. */
AudioResult audio_engine_play_ambience(AudioEngine* a, const char* path, bool restart_if_same);
void audio_engine_stop_ambience(AudioEngine* a);
void audio_engine_set_ambience_paused(AudioEngine* a, bool paused);

/* Fire-and-forget SFX (bell/notifications). It will mix over music + ambience. */
AudioResult audio_engine_play_sfx(AudioEngine* a, const char* path);

/* Call once per frame to service “track ended” bookkeeping. */
void audio_engine_update(AudioEngine* a);

/* True if music finished naturally (not stopped). Resets to false after read. */
bool audio_engine_pop_music_ended(AudioEngine* a);

/* Get a circular visualizer spectrum (post-mix). Writes bins_count values (suggest 64).
   Returns false if not enough audio history is available yet. */
bool audio_engine_get_spectrum(AudioEngine* a, float* out_bins, int bins_count);

/* Music-only waveform envelope history for UI visualizers.
   Writes up to out_count values (oldest->newest). Returns false if not enough history yet. */
bool audio_engine_get_music_waveform(AudioEngine* a, float* out, int out_count);


#ifdef __cplusplus
}
#endif