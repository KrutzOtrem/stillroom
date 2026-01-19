#include "audio_engine.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

/* We keep the device in a fixed, friendly format and use SDL_AudioStream to convert.
   Note: many MP3s are 44.1kHz. If the device supports it, opening at 44.1kHz avoids
   resampling and usually sounds better. We still keep a fallback rate for devices
   that prefer 48kHz. */
#define OUT_SAMPLE_RATE 48000
#define OUT_CHANNELS    2
#define OUT_FORMAT      AUDIO_S16SYS

/* -------- Visualizer tap (post-mix PCM) -------- */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define VIS_ANALYZER_CAP 4096u   /* mono samples, circular buffer */
#define VIS_WAVE_HZ 90u          /* UI waveform sample rate (points/sec) */
#define VIS_WAVE_CAP 256u        /* waveform points ring capacity */
#define VIS_FFT_N        512u    /* must be power of two */
#define VIS_MAX_BINS     64

static void fft_radix2(float* real, float* imag, uint32_t n) {
    /* In-place iterative Cooley–Tukey radix-2 FFT. */
    uint32_t j = 0;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
    }

    for (uint32_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * (float)M_PI / (float)len;
        const float wlen_r = cosf(ang);
        const float wlen_i = sinf(ang);

        for (uint32_t i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            const uint32_t half = len >> 1;
            for (uint32_t k = 0; k < half; k++) {
                const uint32_t u = i + k;
                const uint32_t v = i + k + half;

                const float vr = real[v] * wr - imag[v] * wi;
                const float vi = real[v] * wi + imag[v] * wr;

                const float ur = real[u];
                const float ui = imag[u];

                real[u] = ur + vr;
                imag[u] = ui + vi;
                real[v] = ur - vr;
                imag[v] = ui - vi;

                const float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
}

typedef struct PcmBuffer {
    int16_t* data;        /* interleaved s16 */
    uint32_t frames;      /* number of frames (not samples) */
    uint32_t pos;         /* current frame cursor */
    int channels;
    int sample_rate;
} PcmBuffer;

/* Ring buffer for already-converted output PCM (S16, OUT_CHANNELS). */
typedef struct PcmRing {
    int16_t* data;           /* interleaved s16, size = capacity_frames * channels */
    uint32_t capacity_frames;
    uint32_t read_pos;       /* frame index */
    uint32_t write_pos;      /* frame index */
    uint32_t frames_queued;  /* how many frames currently stored */
} PcmRing;

typedef enum {
    MUSIC_DEC_NONE = 0,
    MUSIC_DEC_MP3,
    MUSIC_DEC_WAV,
} MusicDecType;

typedef struct MusicDecoder {
    MusicDecType type;
    bool inited;
    bool eof;

    /* Source decode (s16) */
    drmp3 mp3;
    drwav wav;
    uint32_t src_rate;
    uint32_t src_channels;

    /* Converter into out_spec. */
    SDL_AudioStream* conv;

    /* Scratch decode buffer (source frames). */
    int16_t* src_tmp;
    uint32_t src_tmp_frames;
} MusicDecoder;

struct AudioEngine {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec out_spec;

    SDL_mutex* lock;
    SDL_cond*  music_cond;
    SDL_Thread* music_thread;

    SDL_cond*  ambience_cond;
    SDL_Thread* ambience_thread;

    /* async music load request */
    char pending_music_path[512];
    int  pending_music_gen;
    int  active_music_gen;
    bool music_loading;
    bool music_thread_quit;

    /* Streaming music pipeline */
    MusicDecoder dec;
    PcmRing      music_rb;
    bool         music_ended_latched;
    bool         music_paused;
    /* Simple pop/click prevention: keep music muted until we have some buffered frames. */
    bool         music_wait_prefill;

    /* async ambience load request */
    char pending_ambience_path[512];
    int  pending_ambience_gen;
    int  active_ambience_gen;
    bool ambience_loading;
    bool ambience_thread_quit;

    /* Streaming ambience pipeline (looped). */
    MusicDecoder amb_dec;
    PcmRing      ambience_rb;
    bool         ambience_paused;
    bool         ambience_wait_prefill;
    char         ambience_path[512];

    PcmBuffer sfx;

    int master_vol; /* 0..128 */
    int music_vol;  /* 0..128 */
    int ambience_vol;  /* 0..128 */
    int sfx_vol;    /* 0..128 */

    char music_path[512];

    /* Visualizer analyzer ring (mono, post-mix). Written in audio callback, read on UI thread. */
    float    vis_rb[VIS_ANALYZER_CAP];
    uint32_t vis_wpos;
    bool     vis_filled;

    /* Music-only waveform envelope (RMS) sampled at VIS_WAVE_HZ. */
    float    vis_music_wave_rb[VIS_WAVE_CAP];
    uint32_t vis_music_wave_wpos;
    bool     vis_music_wave_filled;
    float    vis_music_wave_sumsq;
    uint32_t vis_music_wave_count;
    uint32_t vis_music_wave_frames;
};

static void pcm_free(PcmBuffer* p) {
    if (!p) return;
    free(p->data);
    memset(p, 0, sizeof(*p));
}

static void ring_free(PcmRing* r) {
    if (!r) return;
    free(r->data);
    memset(r, 0, sizeof(*r));
}

static bool ring_init(PcmRing* r, uint32_t capacity_frames, int channels) {
    if (!r || capacity_frames == 0 || channels <= 0) return false;
    memset(r, 0, sizeof(*r));
    r->capacity_frames = capacity_frames;
    r->data = (int16_t*)calloc((size_t)capacity_frames * (size_t)channels, sizeof(int16_t));
    return r->data != NULL;
}

static void ring_clear(PcmRing* r) {
    if (!r) return;
    r->read_pos = 0;
    r->write_pos = 0;
    r->frames_queued = 0;
}

static uint32_t ring_space_frames(const PcmRing* r) {
    if (!r || r->capacity_frames == 0) return 0;
    return r->capacity_frames - r->frames_queued;
}

/* Writes up to frames into the ring. Returns frames written. */
static uint32_t ring_write_frames(PcmRing* r, const int16_t* src, uint32_t frames, int channels) {
    if (!r || !r->data || !src || frames == 0) return 0;
    uint32_t space = ring_space_frames(r);
    if (space == 0) return 0;
    if (frames > space) frames = space;

    uint32_t first = r->capacity_frames - r->write_pos;
    if (first > frames) first = frames;
    memcpy(&r->data[(size_t)r->write_pos * (size_t)channels], src, (size_t)first * (size_t)channels * sizeof(int16_t));
    r->write_pos = (r->write_pos + first) % r->capacity_frames;
    r->frames_queued += first;

    uint32_t remain = frames - first;
    if (remain) {
        memcpy(&r->data[(size_t)r->write_pos * (size_t)channels],
               src + (size_t)first * (size_t)channels,
               (size_t)remain * (size_t)channels * sizeof(int16_t));
        r->write_pos = (r->write_pos + remain) % r->capacity_frames;
        r->frames_queued += remain;
    }
    return frames;
}

/* Reads up to frames from ring into dst. Returns frames read. */
static uint32_t ring_read_frames(PcmRing* r, int16_t* dst, uint32_t frames, int channels) {
    if (!r || !r->data || !dst || frames == 0) return 0;
    if (r->frames_queued == 0) return 0;
    if (frames > r->frames_queued) frames = r->frames_queued;

    uint32_t first = r->capacity_frames - r->read_pos;
    if (first > frames) first = frames;
    memcpy(dst, &r->data[(size_t)r->read_pos * (size_t)channels], (size_t)first * (size_t)channels * sizeof(int16_t));
    r->read_pos = (r->read_pos + first) % r->capacity_frames;
    r->frames_queued -= first;

    uint32_t remain = frames - first;
    if (remain) {
        memcpy(dst + (size_t)first * (size_t)channels,
               &r->data[(size_t)r->read_pos * (size_t)channels],
               (size_t)remain * (size_t)channels * sizeof(int16_t));
        r->read_pos = (r->read_pos + remain) % r->capacity_frames;
        r->frames_queued -= remain;
    }
    return frames;
}

static int clamp16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static bool ends_with_ci(const char* s, const char* ext) {
    if (!s || !ext) return false;
    size_t ls = strlen(s), le = strlen(ext);
    if (ls < le) return false;
    const char* tail = s + (ls - le);
    for (size_t i = 0; i < le; i++) {
        char a = tail[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static AudioResult convert_to_out_spec(const void* src_bytes,
                                     uint32_t src_len_bytes,
                                     SDL_AudioFormat src_fmt,
                                     int src_channels,
                                     int src_rate,
                                     const SDL_AudioSpec* out_spec,
                                     PcmBuffer* out_pcm)
{
    if (!src_bytes || src_len_bytes == 0) return AUDIO_ERR_DECODE;

    SDL_AudioStream* stream = SDL_NewAudioStream(
        src_fmt, (Uint8)src_channels, src_rate,
        out_spec->format, out_spec->channels, out_spec->freq
    );
    if (!stream) return AUDIO_ERR_STREAM;

    if (SDL_AudioStreamPut(stream, src_bytes, (int)src_len_bytes) != 0) {
        SDL_FreeAudioStream(stream);
        return AUDIO_ERR_STREAM;
    }
    SDL_AudioStreamFlush(stream);

    int avail = SDL_AudioStreamAvailable(stream);
    if (avail <= 0) {
        SDL_FreeAudioStream(stream);
        return AUDIO_ERR_DECODE;
    }

    void* out = malloc((size_t)avail);
    if (!out) {
        SDL_FreeAudioStream(stream);
        return AUDIO_ERR_DECODE;
    }

    int got = SDL_AudioStreamGet(stream, out, avail);
    SDL_FreeAudioStream(stream);

    if (got <= 0) {
        free(out);
        return AUDIO_ERR_DECODE;
    }

    /* got bytes are in out_spec format */
    uint32_t frames = (uint32_t)got / (uint32_t)(sizeof(int16_t) * out_spec->channels);

    out_pcm->data = (int16_t*)out;
    out_pcm->frames = frames;
    out_pcm->pos = 0;
    out_pcm->channels = out_spec->channels;
    out_pcm->sample_rate = out_spec->freq;

    return AUDIO_OK;
}

static AudioResult decode_wav_to_pcm(const char* path, const SDL_AudioSpec* out_spec, PcmBuffer* out_pcm) {
    SDL_AudioSpec wav_spec;
    Uint8* wav_buf = NULL;
    Uint32 wav_len = 0;

    if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
        return AUDIO_ERR_DECODE;
    }

    AudioResult r = convert_to_out_spec(wav_buf, wav_len, wav_spec.format, wav_spec.channels, wav_spec.freq, out_spec, out_pcm);
    SDL_FreeWAV(wav_buf);
    return r;
}

static AudioResult decode_mp3_to_pcm(const char* path, const SDL_AudioSpec* out_spec, PcmBuffer* out_pcm) {
    drmp3_config cfg;
    drmp3_uint64 frames = 0;

    int16_t* pcm = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &frames, NULL);
    if (!pcm || frames == 0) {
        if (pcm) drmp3_free(pcm, NULL);
        return AUDIO_ERR_DECODE;
    }

    uint32_t src_len_bytes = (uint32_t)(frames * cfg.channels * sizeof(int16_t));
    AudioResult r = convert_to_out_spec(pcm, src_len_bytes, AUDIO_S16SYS, (int)cfg.channels, (int)cfg.sampleRate, out_spec, out_pcm);
    drmp3_free(pcm, NULL);
    return r;
}

static AudioResult decode_file_to_pcm(const char* path, const SDL_AudioSpec* out_spec, PcmBuffer* out_pcm) {
    if (!path || !path[0]) return AUDIO_ERR_DECODE;

    /* Prefer WAV, then MP3. */
    if (ends_with_ci(path, ".wav")) return decode_wav_to_pcm(path, out_spec, out_pcm);
    if (ends_with_ci(path, ".mp3")) return decode_mp3_to_pcm(path, out_spec, out_pcm);

    /* Fallback: try WAV loader first, then MP3 (useful if extension is odd). */
    AudioResult r = decode_wav_to_pcm(path, out_spec, out_pcm);
    if (r == AUDIO_OK) return r;
    return decode_mp3_to_pcm(path, out_spec, out_pcm);
}

static void music_decoder_close(MusicDecoder* d) {
    if (!d) return;
    if (d->conv) {
        SDL_FreeAudioStream(d->conv);
        d->conv = NULL;
    }
    if (d->type == MUSIC_DEC_MP3 && d->inited) {
        drmp3_uninit(&d->mp3);
    }
    if (d->type == MUSIC_DEC_WAV && d->inited) {
        drwav_uninit(&d->wav);
    }
    free(d->src_tmp);
    d->src_tmp = NULL;
    d->src_tmp_frames = 0;
    memset(d, 0, sizeof(*d));
}

static AudioResult music_decoder_open(MusicDecoder* d, const char* path, const SDL_AudioSpec* out_spec) {
    if (!d || !path || !path[0] || !out_spec) return AUDIO_ERR_DECODE;
    music_decoder_close(d);

    /* Decide based on extension first. */
    MusicDecType t = MUSIC_DEC_NONE;
    if (ends_with_ci(path, ".mp3")) t = MUSIC_DEC_MP3;
    else if (ends_with_ci(path, ".wav")) t = MUSIC_DEC_WAV;
    else t = MUSIC_DEC_MP3; /* fallback */

    d->type = t;
    d->eof = false;

    if (d->type == MUSIC_DEC_MP3) {
        if (!drmp3_init_file(&d->mp3, path, NULL)) {
            return AUDIO_ERR_DECODE;
        }
        d->inited = true;
        d->src_rate = (uint32_t)d->mp3.sampleRate;
        d->src_channels = (uint32_t)d->mp3.channels;
    } else {
        if (!drwav_init_file(&d->wav, path, NULL)) {
            return AUDIO_ERR_DECODE;
        }
        d->inited = true;
        d->src_rate = (uint32_t)d->wav.sampleRate;
        d->src_channels = (uint32_t)d->wav.channels;
    }

    if (d->src_channels == 0 || d->src_rate == 0) {
        music_decoder_close(d);
        return AUDIO_ERR_DECODE;
    }

    d->conv = SDL_NewAudioStream(
        AUDIO_S16SYS, (Uint8)d->src_channels, (int)d->src_rate,
        out_spec->format, out_spec->channels, out_spec->freq
    );
    if (!d->conv) {
        music_decoder_close(d);
        return AUDIO_ERR_STREAM;
    }

    /* Chunk size: ~4096 source frames per fill iteration. */
    d->src_tmp_frames = 4096;
    d->src_tmp = (int16_t*)malloc((size_t)d->src_tmp_frames * (size_t)d->src_channels * sizeof(int16_t));
    if (!d->src_tmp) {
        music_decoder_close(d);
        return AUDIO_ERR_DECODE;
    }

    return AUDIO_OK;
}

static void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioEngine* a = (AudioEngine*)userdata;
    int16_t* out = (int16_t*)stream;
    int samples = len / (int)sizeof(int16_t);

    memset(out, 0, (size_t)len);

    SDL_LockMutex(a->lock);

    const int ch = a->out_spec.channels;
    const int frames_needed = samples / ch;

    /* Unmute music once we have enough buffered audio to avoid underflow crackle.
       (This is only active right after a track change.) */
    /* Unmute ambience once we have enough buffered audio. */
    if (a->ambience_wait_prefill) {
        const uint32_t prefill = (uint32_t)a->out_spec.freq / 20u; /* ~50ms */
        if (a->ambience_rb.frames_queued >= prefill) {
            a->ambience_wait_prefill = false;
        }
    }
    if (a->music_wait_prefill) {
        const uint32_t prefill = (uint32_t)a->out_spec.freq / 20u; /* ~50ms */
        if (a->music_rb.frames_queued >= prefill) {
            a->music_wait_prefill = false;
        }
    }

    for (int f = 0; f < frames_needed; f++) {
        int16_t music_frame[OUT_CHANNELS] = {0, 0};
        bool have_music = false;
        if (!a->music_paused && !a->music_wait_prefill && a->music_rb.frames_queued > 0) {
            ring_read_frames(&a->music_rb, music_frame, 1, ch);
            have_music = true;
        }
        int16_t amb_frame[OUT_CHANNELS] = {0, 0};
        bool have_amb = false;
        if (!a->ambience_paused && !a->ambience_wait_prefill && a->ambience_rb.frames_queued > 0) {
            ring_read_frames(&a->ambience_rb, amb_frame, 1, ch);
            have_amb = true;
        }

        /* ---- Music-only waveform sampling (low-latency, ignores ambience/SFX) ---- */
        {
            float mono_music = 0.0f;
            if (have_music) {
                if (ch >= 2) {
                    float l = (float)music_frame[0] * ((float)a->music_vol / 128.0f);
                    float r = (float)music_frame[1] * ((float)a->music_vol / 128.0f);
                    mono_music = (l + r) / 65536.0f; /* (l+r)/2 / 32768 */
                } else if (ch == 1) {
                    float m = (float)music_frame[0] * ((float)a->music_vol / 128.0f);
                    mono_music = m / 32768.0f;
                }
            }
            a->vis_music_wave_sumsq += mono_music * mono_music;
            a->vis_music_wave_count += 1u;
            a->vis_music_wave_frames += 1u;

            const uint32_t frames_per_point = (uint32_t)((a->out_spec.freq > 0 ? a->out_spec.freq : 44100) / (int)VIS_WAVE_HZ);
            const uint32_t fpp = frames_per_point > 0u ? frames_per_point : 1u;

            if (a->vis_music_wave_frames >= fpp) {
                float rms = 0.0f;
                if (a->vis_music_wave_count > 0u) {
                    rms = sqrtf(a->vis_music_wave_sumsq / (float)a->vis_music_wave_count);
                }
                /* Normalize into 0..1 range for UI. Gain tuned for lo-fi. */
                float v = rms * 4.5f;
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;

                a->vis_music_wave_rb[a->vis_music_wave_wpos] = v;
                a->vis_music_wave_wpos = (a->vis_music_wave_wpos + 1u) % VIS_WAVE_CAP;
                if (a->vis_music_wave_wpos == 0u) a->vis_music_wave_filled = true;

                a->vis_music_wave_sumsq = 0.0f;
                a->vis_music_wave_count = 0u;
                a->vis_music_wave_frames = 0u;
            }
        }


        for (int c = 0; c < ch; c++) {
            int mix = 0;

            /* Music from ring (silence if underflow or paused). */
            if (have_music) {
                int16_t s = music_frame[c];
                mix += (s * a->music_vol) / 128;
            }

            /* Ambience from ring (silence if underflow or paused). */
            if (have_amb) {
                int16_t s = amb_frame[c];
                mix += (s * a->ambience_vol) / 128;
            }

            /* SFX */
            if (a->sfx.data && a->sfx.pos < a->sfx.frames) {
                int16_t s = a->sfx.data[(a->sfx.pos * ch) + c];
                mix += (s * a->sfx_vol) / 128;
            }

            mix = (mix * a->master_vol) / 128;
            out[f * ch + c] = (int16_t)clamp16(mix);
        }


/* Visualizer tap: store a mono sample of what the user actually hears (post-mix). */
{
    float mono = 0.0f;
    if (ch >= 2) {
        const int l = (int)out[f * ch + 0];
        const int r = (int)out[f * ch + 1];
        mono = (float)(l + r) / 65536.0f; /* (l+r)/2 / 32768 */
    } else if (ch == 1) {
        mono = (float)out[f * ch] / 32768.0f;
    }
    a->vis_rb[a->vis_wpos] = mono;
    a->vis_wpos = (a->vis_wpos + 1u) % VIS_ANALYZER_CAP;
    if (a->vis_wpos == 0u) a->vis_filled = true;
}

/* advance cursors once per frame (rings advance via ring_read_frames above) */
        if (a->sfx.data && a->sfx.pos < a->sfx.frames) {
            a->sfx.pos++;
            if (a->sfx.pos >= a->sfx.frames) {
                pcm_free(&a->sfx);
            }
        }
    }

    /* If decoder has hit EOF and the ring is empty, latch an "ended" event. */
    if (!a->music_paused && a->dec.eof && a->music_rb.frames_queued == 0) {
        a->music_ended_latched = true;
    }

    /* Wake the loader thread when the ring drops below a low watermark. */
    if (!a->music_thread_quit && a->dec.inited) {
        const uint32_t low = a->music_rb.capacity_frames / 2;
        if (a->music_rb.frames_queued < low) {
            SDL_CondSignal(a->music_cond);
        }
    }

    /* Wake ambience loader thread when its ring drops. */
    if (!a->ambience_thread_quit && a->amb_dec.inited) {
        const uint32_t low = a->ambience_rb.capacity_frames / 2;
        if (a->ambience_rb.frames_queued < low) {
            SDL_CondSignal(a->ambience_cond);
        }
    }

    SDL_UnlockMutex(a->lock);
}

static int ambience_loader_thread(void* userdata) {
    AudioEngine* a = (AudioEngine*)userdata;
    if (!a) return 0;

    const uint32_t out_ch = (uint32_t)a->out_spec.channels;
    const uint32_t out_chunk_frames = 4096;
    const uint32_t out_chunk_bytes = out_chunk_frames * out_ch * (uint32_t)sizeof(int16_t);
    int16_t* out_tmp = (int16_t*)malloc(out_chunk_bytes);
    if (!out_tmp) return 0;

    for (;;) {
        SDL_LockMutex(a->lock);
        while (!a->ambience_thread_quit && a->pending_ambience_gen == a->active_ambience_gen) {
            SDL_CondWait(a->ambience_cond, a->lock);
        }
        if (a->ambience_thread_quit) {
            SDL_UnlockMutex(a->lock);
            break;
        }

        char path[512];
        strncpy(path, a->pending_ambience_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        const int job_gen = a->pending_ambience_gen;
        a->active_ambience_gen = job_gen;
        a->ambience_loading = true;
        SDL_UnlockMutex(a->lock);

        /* Open decoder for this ambience path. */
        music_decoder_close(&a->amb_dec);
        AudioResult open_r = music_decoder_open(&a->amb_dec, path, &a->out_spec);
        if (open_r != AUDIO_OK) {
            SDL_LockMutex(a->lock);
            a->ambience_loading = false;
            a->pending_ambience_path[0] = 0;
            SDL_UnlockMutex(a->lock);
            continue;
        }

        SDL_LockMutex(a->lock);
        ring_clear(&a->ambience_rb);
        a->ambience_wait_prefill = true;
        strncpy(a->ambience_path, path, sizeof(a->ambience_path) - 1);
        a->ambience_path[sizeof(a->ambience_path) - 1] = 0;
        SDL_UnlockMutex(a->lock);

        /* Fill loop: keep ring topped up. Loop by seeking to frame 0 at EOF. */
        for (;;) {
            SDL_LockMutex(a->lock);
            const bool quit = a->ambience_thread_quit;
            const bool superseded = (job_gen != a->pending_ambience_gen);
            const uint32_t space = ring_space_frames(&a->ambience_rb);
            SDL_UnlockMutex(a->lock);
            if (quit || superseded) break;

                        /* If ambience is paused, don't burn CPU decoding in the background. */
            SDL_LockMutex(a->lock);
            const bool paused = a->ambience_paused;
            const uint32_t queued = a->ambience_rb.frames_queued;
            const uint32_t high = a->ambience_rb.capacity_frames * 3u / 4u;
            SDL_UnlockMutex(a->lock);

            if (paused) {
                SDL_LockMutex(a->lock);
                SDL_CondWaitTimeout(a->ambience_cond, a->lock, 50);
                SDL_UnlockMutex(a->lock);
                continue;
            }

            if (queued >= high) {
                SDL_LockMutex(a->lock);
                SDL_CondWaitTimeout(a->ambience_cond, a->lock, 50);
                SDL_UnlockMutex(a->lock);
                continue;
            }

            if (space == 0) {
                SDL_LockMutex(a->lock);
                SDL_CondWaitTimeout(a->ambience_cond, a->lock, 20);
                SDL_UnlockMutex(a->lock);
                continue;
            }

            uint32_t got_src = 0;
            if (a->amb_dec.type == MUSIC_DEC_MP3) {
                got_src = (uint32_t)drmp3_read_pcm_frames_s16(&a->amb_dec.mp3, a->amb_dec.src_tmp_frames, a->amb_dec.src_tmp);
            } else if (a->amb_dec.type == MUSIC_DEC_WAV) {
                got_src = (uint32_t)drwav_read_pcm_frames_s16(&a->amb_dec.wav, a->amb_dec.src_tmp_frames, a->amb_dec.src_tmp);
            }

            if (got_src == 0) {
                /* EOF: loop. Clear converter so we don't accumulate stale samples. */
                if (a->amb_dec.conv) SDL_AudioStreamClear(a->amb_dec.conv);
                if (a->amb_dec.type == MUSIC_DEC_MP3) {
                    (void)drmp3_seek_to_pcm_frame(&a->amb_dec.mp3, 0);
                } else if (a->amb_dec.type == MUSIC_DEC_WAV) {
                    (void)drwav_seek_to_pcm_frame(&a->amb_dec.wav, 0);
                }
                continue;
            }

            const uint32_t src_bytes = got_src * a->amb_dec.src_channels * (uint32_t)sizeof(int16_t);
            if (SDL_AudioStreamPut(a->amb_dec.conv, a->amb_dec.src_tmp, (int)src_bytes) != 0) {
                /* Treat converter failure as EOF and attempt to loop. */
                if (a->amb_dec.conv) SDL_AudioStreamClear(a->amb_dec.conv);
                if (a->amb_dec.type == MUSIC_DEC_MP3) {
                    (void)drmp3_seek_to_pcm_frame(&a->amb_dec.mp3, 0);
                } else if (a->amb_dec.type == MUSIC_DEC_WAV) {
                    (void)drwav_seek_to_pcm_frame(&a->amb_dec.wav, 0);
                }
                continue;
            }

            for (;;) {
                int avail = SDL_AudioStreamAvailable(a->amb_dec.conv);
                if (avail <= 0) break;

                SDL_LockMutex(a->lock);
                uint32_t space_frames = ring_space_frames(&a->ambience_rb);
                SDL_UnlockMutex(a->lock);

                if (space_frames == 0) break;

                const uint32_t bytes_per_frame = out_ch * (uint32_t)sizeof(int16_t);
                uint32_t max_bytes = space_frames * bytes_per_frame;

                int want_bytes = avail;
                if ((uint32_t)want_bytes > out_chunk_bytes) want_bytes = (int)out_chunk_bytes;
                if ((uint32_t)want_bytes > max_bytes) want_bytes = (int)max_bytes;
                if (want_bytes <= 0) break;

                int got = SDL_AudioStreamGet(a->amb_dec.conv, out_tmp, want_bytes);
                if (got <= 0) break;

                uint32_t frames = (uint32_t)got / bytes_per_frame;

                SDL_LockMutex(a->lock);
                (void)ring_write_frames(&a->ambience_rb, out_tmp, frames, (int)out_ch);
                SDL_UnlockMutex(a->lock);
            }
        }

        SDL_LockMutex(a->lock);
        a->ambience_loading = false;
        SDL_UnlockMutex(a->lock);
    }

    free(out_tmp);
    return 0;
}

static int music_loader_thread(void* userdata) {
    AudioEngine* a = (AudioEngine*)userdata;
    if (!a) return 0;

    /* Output chunk buffer for pulling converted audio from SDL_AudioStream. */
    const uint32_t out_ch = (uint32_t)a->out_spec.channels;
    const uint32_t out_chunk_frames = 4096;
    const uint32_t out_chunk_bytes = out_chunk_frames * out_ch * (uint32_t)sizeof(int16_t);
    int16_t* out_tmp = (int16_t*)malloc(out_chunk_bytes);
    if (!out_tmp) return 0;

    for (;;) {
        /* Wait for a new play request or quit. */
        SDL_LockMutex(a->lock);
        while (!a->music_thread_quit && a->pending_music_gen == a->active_music_gen) {
            SDL_CondWait(a->music_cond, a->lock);
        }
        if (a->music_thread_quit) {
            SDL_UnlockMutex(a->lock);
            break;
        }

        /* Snapshot request. */
        char path[512];
        strncpy(path, a->pending_music_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        const int job_gen = a->pending_music_gen;

        /* Reset playback state before opening. */
        a->active_music_gen = job_gen;
        a->music_loading = true;
        a->music_ended_latched = false;
        ring_clear(&a->music_rb);
        music_decoder_close(&a->dec);
        a->music_path[0] = 0;
        SDL_UnlockMutex(a->lock);

        /* Open decoder + converter. */
        AudioResult r = music_decoder_open(&a->dec, path, &a->out_spec);

        SDL_LockMutex(a->lock);
        if (job_gen != a->pending_music_gen) {
            /* Superseded immediately. */
            SDL_UnlockMutex(a->lock);
            music_decoder_close(&a->dec);
            continue;
        }
        if (r != AUDIO_OK) {
            a->music_loading = false;
            SDL_UnlockMutex(a->lock);
            music_decoder_close(&a->dec);
            continue;
        }

        strncpy(a->music_path, path, sizeof(a->music_path) - 1);
        a->music_path[sizeof(a->music_path) - 1] = 0;
        a->dec.eof = false;
        a->music_loading = false; /* we'll start filling immediately */
        SDL_UnlockMutex(a->lock);

        /* Fill loop for current track.
           IMPORTANT: when we hit EOF, we must *drain* SDL_AudioStream fully into the
           ring over as many iterations as needed. If we stop immediately, any
           converted audio still queued inside SDL_AudioStream is abandoned, which
           makes tracks end early and the player skip forward.
        */
        bool draining = false;
        bool flushed = false;
        for (;;) {
            SDL_LockMutex(a->lock);
            const bool quit = a->music_thread_quit;
            const bool superseded = (job_gen != a->pending_music_gen);
            const uint32_t space = ring_space_frames(&a->music_rb);
            SDL_UnlockMutex(a->lock);
            if (quit || superseded) break;

                        /* Throttle decode when ring is already mostly full to avoid CPU spikes that
               can stutter rendering on low-power devices. */
            SDL_LockMutex(a->lock);
            const uint32_t queued = a->music_rb.frames_queued;
            const uint32_t high = a->music_rb.capacity_frames * 3u / 4u;
            SDL_UnlockMutex(a->lock);
            if (!draining && queued >= high) {
                SDL_LockMutex(a->lock);
                SDL_CondWaitTimeout(a->music_cond, a->lock, 30);
                SDL_UnlockMutex(a->lock);
                continue;
            }

            /* If ring is full, wait until callback drains it.
               IMPORTANT: don't require a huge contiguous space, or we risk periodic underflows
               (audible as crackle) when the ring hovers below out_chunk_frames. */
            if (space == 0) {
                SDL_LockMutex(a->lock);
                /* Spurious wakeups are fine; we'll re-check conditions. */
                SDL_CondWaitTimeout(a->music_cond, a->lock, 20);
                SDL_UnlockMutex(a->lock);
                continue;
            }

            /* Decode a chunk of source frames (unless we're draining). */
            if (!draining) {
                uint32_t got_src = 0;
                if (a->dec.type == MUSIC_DEC_MP3) {
                    got_src = (uint32_t)drmp3_read_pcm_frames_s16(&a->dec.mp3, a->dec.src_tmp_frames, a->dec.src_tmp);
                } else if (a->dec.type == MUSIC_DEC_WAV) {
                    got_src = (uint32_t)drwav_read_pcm_frames_s16(&a->dec.wav, a->dec.src_tmp_frames, a->dec.src_tmp);
                }

                if (got_src == 0) {
                    SDL_LockMutex(a->lock);
                    a->dec.eof = true;
                    SDL_UnlockMutex(a->lock);
                    draining = true;
                } else {
                    const uint32_t src_bytes = got_src * a->dec.src_channels * (uint32_t)sizeof(int16_t);
                    if (SDL_AudioStreamPut(a->dec.conv, a->dec.src_tmp, (int)src_bytes) != 0) {
                        SDL_LockMutex(a->lock);
                        a->dec.eof = true;
                        SDL_UnlockMutex(a->lock);
                        draining = true;
                    }
                }
            }

            /* When we enter draining mode, flush the converter exactly once. */
            if (draining && !flushed) {
                SDL_AudioStreamFlush(a->dec.conv);
                flushed = true;
            }

            /* Pull converted audio and write into the ring.
               IMPORTANT: never drop samples. If the ring is close to full, only
               pull as much as we can store and leave the rest queued inside
               SDL_AudioStream for the next iteration. Dropping here causes
               audible time-compression ("playing too fast"). */
            for (;;) {
                int avail = SDL_AudioStreamAvailable(a->dec.conv);
                if (avail <= 0) break;

                SDL_LockMutex(a->lock);
                uint32_t space_frames = ring_space_frames(&a->music_rb);
                SDL_UnlockMutex(a->lock);

                if (space_frames == 0) {
                    /* Let the callback drain, then come back. */
                    break;
                }

                const uint32_t bytes_per_frame = out_ch * (uint32_t)sizeof(int16_t);
                uint32_t max_bytes = space_frames * bytes_per_frame;

                int want_bytes = avail;
                if ((uint32_t)want_bytes > out_chunk_bytes) want_bytes = (int)out_chunk_bytes;
                if ((uint32_t)want_bytes > max_bytes) want_bytes = (int)max_bytes;
                if (want_bytes <= 0) break;

                int got = SDL_AudioStreamGet(a->dec.conv, out_tmp, want_bytes);
                if (got <= 0) break;

                uint32_t frames = (uint32_t)got / bytes_per_frame;

                SDL_LockMutex(a->lock);
                (void)ring_write_frames(&a->music_rb, out_tmp, frames, (int)out_ch);
                SDL_UnlockMutex(a->lock);
            }

            if (draining) {
                /* We're done only when the converter has been fully drained. */
                if (SDL_AudioStreamAvailable(a->dec.conv) <= 0) {
                    break;
                }

                /* Converter still has data, but ring may be full. Wait briefly for
                   the callback to drain and then continue draining. */
                SDL_LockMutex(a->lock);
                if (ring_space_frames(&a->music_rb) == 0) {
                    SDL_CondWaitTimeout(a->music_cond, a->lock, 20);
                }
                SDL_UnlockMutex(a->lock);
            }
        }

        /* If superseded, close immediately and loop back to wait for next request. */
        SDL_LockMutex(a->lock);
        const bool superseded2 = (job_gen != a->pending_music_gen);
        SDL_UnlockMutex(a->lock);
        if (superseded2) {
            music_decoder_close(&a->dec);
            continue;
        }

        /* Track has been fully decoded into the ring (or hit EOF). Keep decoder state
           until the next request/stop so the callback can detect EOF + empty ring. */
    }

    free(out_tmp);
    return 0;
}

AudioResult audio_engine_init(AudioEngine** out) {
    if (!out) return AUDIO_ERR_INIT;
    *out = NULL;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return AUDIO_ERR_INIT;
        }
    }

    AudioEngine* a = (AudioEngine*)calloc(1, sizeof(AudioEngine));
    if (!a) return AUDIO_ERR_INIT;

    a->lock = SDL_CreateMutex();
    if (!a->lock) {
        free(a);
        return AUDIO_ERR_INIT;
    }

    a->music_cond = SDL_CreateCond();
    a->ambience_cond = SDL_CreateCond();
    if (!a->music_cond || !a->ambience_cond) {
        if (a->music_cond) SDL_DestroyCond(a->music_cond);
        if (a->ambience_cond) SDL_DestroyCond(a->ambience_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_INIT;
    }

    a->master_vol = 128;
    a->music_vol  = 128;
    a->ambience_vol = 128;
    a->sfx_vol    = 128;
    a->ambience_paused = false;
    a->ambience_wait_prefill = false;
    a->ambience_path[0] = 0;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.format = OUT_FORMAT;
    want.channels = OUT_CHANNELS;
    /* Larger buffer improves stability on embedded hardware; latency doesn't matter here. */
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = a;

    SDL_AudioSpec have;
    SDL_zero(have);

    /* Prefer 44.1kHz (common music sample rate) to avoid resampling; fall back to 48kHz. */
    const int preferred_rates[] = { 44100, OUT_SAMPLE_RATE };
    a->dev = 0;
    for (size_t i = 0; i < sizeof(preferred_rates)/sizeof(preferred_rates[0]); i++) {
        want.freq = preferred_rates[i];
        SDL_zero(have);
        a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (a->dev != 0) break;
    }
    if (a->dev == 0) {
        SDL_DestroyCond(a->music_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_OPEN;
    }

    a->out_spec = have;
    SDL_PauseAudioDevice(a->dev, 0);

    /* Music ring buffer: 2 seconds of output audio. */
    const uint32_t rb_frames = (uint32_t)a->out_spec.freq * 2u;
    if (!ring_init(&a->music_rb, rb_frames, a->out_spec.channels)) {
        SDL_CloseAudioDevice(a->dev);
        SDL_DestroyCond(a->music_cond);
        SDL_DestroyCond(a->ambience_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_INIT;
    }
    if (!ring_init(&a->ambience_rb, rb_frames, a->out_spec.channels)) {
        SDL_CloseAudioDevice(a->dev);
        ring_free(&a->music_rb);
        SDL_DestroyCond(a->music_cond);
        SDL_DestroyCond(a->ambience_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_INIT;
    }
    a->music_paused = false;

    /* Start async music loader. */
    a->pending_music_gen = 0;
    a->active_music_gen = 0;
    a->music_loading = false;
    a->music_thread_quit = false;
    a->pending_music_path[0] = 0;

    a->music_thread = SDL_CreateThread(music_loader_thread, "music_loader", a);
    if (!a->music_thread) {
        SDL_CloseAudioDevice(a->dev);
        SDL_DestroyCond(a->music_cond);
        SDL_DestroyCond(a->ambience_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_INIT;
    }

    /* Start async ambience loader. */
    a->pending_ambience_gen = 0;
    a->active_ambience_gen = 0;
    a->ambience_loading = false;
    a->ambience_thread_quit = false;
    a->pending_ambience_path[0] = 0;

    a->ambience_thread = SDL_CreateThread(ambience_loader_thread, "ambience_loader", a);
    if (!a->ambience_thread) {
        SDL_LockMutex(a->lock);
        a->music_thread_quit = true;
        SDL_CondSignal(a->music_cond);
        SDL_UnlockMutex(a->lock);
        SDL_WaitThread(a->music_thread, NULL);
        a->music_thread = NULL;

        SDL_CloseAudioDevice(a->dev);
        ring_free(&a->music_rb);
        ring_free(&a->ambience_rb);
        SDL_DestroyCond(a->music_cond);
        SDL_DestroyCond(a->ambience_cond);
        SDL_DestroyMutex(a->lock);
        free(a);
        return AUDIO_ERR_INIT;
    }

    *out = a;
    return AUDIO_OK;
}

void audio_engine_quit(AudioEngine** inout) {
    if (!inout || !*inout) return;
    AudioEngine* a = *inout;

    if (a->dev) {
        SDL_PauseAudioDevice(a->dev, 1);
        SDL_CloseAudioDevice(a->dev);
    }

    /* Stop loader thread. */
    if (a->music_thread) {
        SDL_LockMutex(a->lock);
        a->music_thread_quit = true;
        SDL_CondSignal(a->music_cond);
        SDL_UnlockMutex(a->lock);
        SDL_WaitThread(a->music_thread, NULL);
        a->music_thread = NULL;
    }

    if (a->ambience_thread) {
        SDL_LockMutex(a->lock);
        a->ambience_thread_quit = true;
        SDL_CondSignal(a->ambience_cond);
        SDL_UnlockMutex(a->lock);
        SDL_WaitThread(a->ambience_thread, NULL);
        a->ambience_thread = NULL;
    }

    music_decoder_close(&a->dec);
    music_decoder_close(&a->amb_dec);
    ring_free(&a->music_rb);
    ring_free(&a->ambience_rb);
    pcm_free(&a->sfx);

    if (a->music_cond) SDL_DestroyCond(a->music_cond);
    if (a->ambience_cond) SDL_DestroyCond(a->ambience_cond);
    if (a->lock) SDL_DestroyMutex(a->lock);

    free(a);
    *inout = NULL;
}

void audio_engine_set_master_volume(AudioEngine* a, int vol) {
    if (!a) return;
    if (vol < 0) vol = 0;
    if (vol > 128) vol = 128;
    SDL_LockMutex(a->lock);
    a->master_vol = vol;
    SDL_UnlockMutex(a->lock);
}

void audio_engine_set_music_volume(AudioEngine* a, int vol) {
    if (!a) return;
    if (vol < 0) vol = 0;
    if (vol > 128) vol = 128;
    SDL_LockMutex(a->lock);
    a->music_vol = vol;
    SDL_UnlockMutex(a->lock);
}

void audio_engine_set_sfx_volume(AudioEngine* a, int vol) {
    if (!a) return;
    if (vol < 0) vol = 0;
    if (vol > 128) vol = 128;
    SDL_LockMutex(a->lock);
    a->sfx_vol = vol;
    SDL_UnlockMutex(a->lock);
}

void audio_engine_set_ambience_volume(AudioEngine* a, int vol) {
    if (!a) return;
    if (vol < 0) vol = 0;
    if (vol > 128) vol = 128;
    SDL_LockMutex(a->lock);
    a->ambience_vol = vol;
    SDL_UnlockMutex(a->lock);
}

static bool file_exists_local(const char* path) {
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

AudioResult audio_engine_play_ambience(AudioEngine* a, const char* path, bool restart_if_same) {
    if (!a || !path || !path[0]) return AUDIO_ERR_DECODE;
    if (!file_exists_local(path)) return AUDIO_ERR_DECODE;

    SDL_LockMutex(a->lock);

    if (!restart_if_same && a->ambience_path[0] && strcmp(a->ambience_path, path) == 0) {
        /* If something is already buffered/playing, keep it. */
        if (a->amb_dec.inited || a->ambience_rb.frames_queued > 0) {
            SDL_UnlockMutex(a->lock);
            return AUDIO_OK;
        }
    }

    /* Stop current ambience immediately and queue async load. */
    ring_clear(&a->ambience_rb);
    /* decoder lifecycle is owned by ambience loader thread */
    a->ambience_paused = false;
    a->ambience_wait_prefill = true;
    a->ambience_path[0] = 0;

    strncpy(a->pending_ambience_path, path, sizeof(a->pending_ambience_path) - 1);
    a->pending_ambience_path[sizeof(a->pending_ambience_path) - 1] = 0;
    a->pending_ambience_gen++;
    a->ambience_loading = true;

    SDL_CondSignal(a->ambience_cond);
    SDL_UnlockMutex(a->lock);

    return AUDIO_OK;
}

void audio_engine_stop_ambience(AudioEngine* a) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    ring_clear(&a->ambience_rb);
    /* decoder lifecycle is owned by ambience loader thread */
    a->pending_ambience_path[0] = 0;
    a->pending_ambience_gen++;
    a->ambience_path[0] = 0;
    a->ambience_paused = false;
    a->ambience_wait_prefill = false;
    SDL_CondSignal(a->ambience_cond);
    SDL_UnlockMutex(a->lock);
}

void audio_engine_set_ambience_paused(AudioEngine* a, bool paused) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    a->ambience_paused = paused ? true : false;
    SDL_CondSignal(a->ambience_cond);
    SDL_UnlockMutex(a->lock);
}


AudioResult audio_engine_play_music(AudioEngine* a, const char* path, bool restart_if_same) {
    if (!a || !path || !path[0]) return AUDIO_ERR_DECODE;

    SDL_LockMutex(a->lock);

    /* If already playing or already loading the same thing, do nothing unless restart requested. */
    if (!restart_if_same) {
        if (a->music_path[0] && strcmp(a->music_path, path) == 0 &&
            (a->music_rb.frames_queued > 0 || (a->dec.inited && !a->dec.eof))) {
            SDL_UnlockMutex(a->lock);
            return AUDIO_OK;
        }
        if (a->music_loading && a->pending_music_path[0] && strcmp(a->pending_music_path, path) == 0) {
            SDL_UnlockMutex(a->lock);
            return AUDIO_OK;
        }
    }

    /* Stop current playback immediately (ring clear) so UI/input stays responsive. */
    ring_clear(&a->music_rb);
    /* decoder lifecycle is owned by music loader thread */
    a->music_paused = false;
    a->music_ended_latched = false;
    a->music_wait_prefill = true;
    a->music_path[0] = 0;

    /* Queue async load. */
    strncpy(a->pending_music_path, path, sizeof(a->pending_music_path) - 1);
    a->pending_music_path[sizeof(a->pending_music_path) - 1] = 0;
    a->pending_music_gen++;
    a->music_loading = true;

    SDL_CondSignal(a->music_cond);

    SDL_UnlockMutex(a->lock);

    return AUDIO_OK; /* decode happens on the loader thread */
}

void audio_engine_stop_music(AudioEngine* a) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    ring_clear(&a->music_rb);
    /* decoder lifecycle is owned by music loader thread */
    a->music_paused = false;
    a->music_ended_latched = false;
    a->music_wait_prefill = false;
    a->music_path[0] = 0;

    /* Cancel any pending async load. */
    a->pending_music_path[0] = 0;
    a->pending_music_gen++;
    a->music_loading = false;

    SDL_CondSignal(a->music_cond);

    SDL_UnlockMutex(a->lock);
}

void audio_engine_set_music_paused(AudioEngine* a, bool paused) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    a->music_paused = paused;
    SDL_UnlockMutex(a->lock);
}

AudioResult audio_engine_play_sfx(AudioEngine* a, const char* path) {
    if (!a || !path || !path[0]) return AUDIO_ERR_DECODE;

    PcmBuffer tmp = {0};
    AudioResult r = decode_file_to_pcm(path, &a->out_spec, &tmp);
    if (r != AUDIO_OK) {
        pcm_free(&tmp);
        return r;
    }

    SDL_LockMutex(a->lock);
    pcm_free(&a->sfx); /* replace any current SFX */
    a->sfx = tmp;
    SDL_UnlockMutex(a->lock);

    return AUDIO_OK;
}

void audio_engine_update(AudioEngine* a) {
    (void)a;
    /* Nothing needed right now, but kept for future streaming/queue logic. */
}

bool audio_engine_pop_music_ended(AudioEngine* a) {
    if (!a) return false;
    bool ended = false;
    SDL_LockMutex(a->lock);
    if (a->music_ended_latched) {
        ended = true;
        a->music_ended_latched = false;
    }
    SDL_UnlockMutex(a->lock);
    return ended;
}

bool audio_engine_get_spectrum(AudioEngine* a, float* out_bins, int bins_count) {
    if (!a || !out_bins || bins_count <= 0) return false;
    if (bins_count > VIS_MAX_BINS) bins_count = VIS_MAX_BINS;

    float samples[VIS_FFT_N];
    uint32_t wpos = 0;
    bool filled = false;

    SDL_LockMutex(a->lock);
    wpos = a->vis_wpos;
    filled = a->vis_filled;
    const uint32_t available = filled ? VIS_ANALYZER_CAP : wpos;
    if (available < VIS_FFT_N) {
        SDL_UnlockMutex(a->lock);
        return false;
    }

    /* Copy the latest VIS_FFT_N mono samples ending at (wpos-1). */
    uint32_t idx = (wpos + VIS_ANALYZER_CAP - VIS_FFT_N) % VIS_ANALYZER_CAP;
    for (uint32_t i = 0; i < VIS_FFT_N; i++) {
        samples[i] = a->vis_rb[idx];
        idx = (idx + 1u) % VIS_ANALYZER_CAP;
    }
    SDL_UnlockMutex(a->lock);

    float real[VIS_FFT_N];
    float imag[VIS_FFT_N];
    for (uint32_t i = 0; i < VIS_FFT_N; i++) {
        /* Hann window to reduce spectral leakage. */
        const float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(VIS_FFT_N - 1u)));
        real[i] = samples[i] * w;
        imag[i] = 0.0f;
    }

    fft_radix2(real, imag, VIS_FFT_N);

    /* Build magnitude spectrum for k=1..N/2-1 (skip DC). */
    const uint32_t half = VIS_FFT_N / 2u;
    float mags[half];
    mags[0] = 0.0f;
    for (uint32_t k = 1; k < half; k++) {
        const float r = real[k];
        const float im = imag[k];
        mags[k] = sqrtf(r * r + im * im);
    }

    /* Group into bins_count bars linearly (still "true" and fast). */
    const uint32_t k0 = 1u;
    const uint32_t k1 = half; /* exclusive */
    const uint32_t span = k1 - k0;
    float maxv = 1e-6f;
    for (int i = 0; i < bins_count; i++) {
        const uint32_t a0 = k0 + (span * (uint32_t)i) / (uint32_t)bins_count;
        const uint32_t a1 = k0 + (span * (uint32_t)(i + 1)) / (uint32_t)bins_count;
        float acc = 0.0f;
        uint32_t cnt = 0;
        for (uint32_t k = a0; k < a1; k++) { acc += mags[k]; cnt++; }
        float v = (cnt ? (acc / (float)cnt) : 0.0f);

        /* Mild perceptual shaping. */
        v = logf(1.0f + 8.0f * v);

        out_bins[i] = v;
        if (v > maxv) maxv = v;
    }

    /* Normalize to roughly 0..1. */
    const float inv = 1.0f / maxv;
    for (int i = 0; i < bins_count; i++) {
        float v = out_bins[i] * inv;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out_bins[i] = v;
    }

    return true;
}
bool audio_engine_get_music_waveform(AudioEngine* a, float* out, int out_count) {
    if (!a || !out || out_count <= 0) return false;

    SDL_LockMutex(a->lock);

    if (!a->vis_music_wave_filled && a->vis_music_wave_wpos < (uint32_t)out_count) {
        SDL_UnlockMutex(a->lock);
        return false;
    }

    uint32_t wpos = a->vis_music_wave_wpos;
    bool filled = a->vis_music_wave_filled;

    for (int i = 0; i < out_count; i++) {
        uint32_t idx;
        if (filled) {
            idx = (wpos + (uint32_t)i) % VIS_WAVE_CAP;
        } else {
            /* Not filled yet: oldest is 0 */
            uint32_t start_idx = (wpos >= (uint32_t)out_count) ? (wpos - (uint32_t)out_count) : 0u;
            idx = start_idx + (uint32_t)i;
        }
        out[i] = a->vis_music_wave_rb[idx];
    }

    SDL_UnlockMutex(a->lock);
    return true;
}


