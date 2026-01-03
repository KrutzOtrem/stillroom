// focus_timer.c - Stillroom (timer + backgrounds only, no audio)
// TrimUI Brick / NextUI SDL2

#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#include "audio_engine.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static uint64_t now_ms(void) { return (uint64_t)SDL_GetTicks(); }

/* -------- Battery (sysfs) --------
   Reads battery percentage from /sys/class/power_supply/<battery>/capacity.
   Cached and refreshed every few seconds to avoid per-frame file I/O.
*/
static int g_batt_percent = -1;
static uint64_t g_batt_last_ms = 0;

static bool read_int_file(const char* path, int* out) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int v = 0;
    bool ok = (fscanf(f, "%d", &v) == 1);
    fclose(f);
    if (!ok) return false;
    *out = v;
    return true;
}

static bool read_str_file(const char* path, char* out, size_t out_sz) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(out, (int)out_sz, f)) { fclose(f); return false; }
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    return true;
}

static int get_battery_percent_sysfs(void) {
    const char* base = "/sys/class/power_supply";
    DIR* d = opendir(base);
    if (!d) return -1;

    struct dirent* de;
    char type_path[512];
    char cap_path[512];
    char type_buf[64];

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        snprintf(type_path, sizeof(type_path), "%s/%s/type", base, de->d_name);
        if (!read_str_file(type_path, type_buf, sizeof(type_buf))) continue;
        if (strcmp(type_buf, "Battery") != 0) continue;

        snprintf(cap_path, sizeof(cap_path), "%s/%s/capacity", base, de->d_name);
        int cap = -1;
        if (read_int_file(cap_path, &cap)) {
            closedir(d);
            if (cap < 0) cap = 0;
            if (cap > 100) cap = 100;
            return cap;
        }
    }

    closedir(d);
    return -1;
}

static void battery_tick_update(void) {
    uint64_t now = now_ms();
    if (now - g_batt_last_ms >= 5000) { /* 5s refresh */
        g_batt_percent = get_battery_percent_sysfs();
        g_batt_last_ms = now;
    }
}

static void chdir_to_exe_dir(void) {
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return;
    path[n] = '\0';
    char* dir = dirname(path);
    if (dir) chdir(dir);
}

/* ------------------------------ Simple bell audio ------------------------------ */
/* WAV only for now. Uses SDL_LoadWAV + SDL_QueueAudio. */

typedef struct BellAudio {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec     spec;
    Uint8*            buf;
    Uint32            len;
    bool              ok;
} BellAudio;

static BellAudio g_bell = {0};

static void bell_shutdown(void) {
    if (g_bell.dev) SDL_CloseAudioDevice(g_bell.dev);
    g_bell.dev = 0;
    if (g_bell.buf) SDL_FreeWAV(g_bell.buf);
    g_bell.buf = NULL;
    g_bell.len = 0;
    g_bell.ok = false;
}

static void bell_init(const char* wav_path) {
    /* audio engine shutdown below */

    SDL_AudioSpec wav_spec;
    Uint8* wav_buf = NULL;
    Uint32 wav_len = 0;

    if (!SDL_LoadWAV(wav_path, &wav_spec, &wav_buf, &wav_len)) {
        fprintf(stderr, "[AUDIO] SDL_LoadWAV failed for %s: %s\n", wav_path, SDL_GetError());
        return;
    }

    /* Open device matching WAV; keep it simple and reliable. */
    SDL_AudioSpec want = wav_spec;
    want.callback = NULL; /* we will queue */
    SDL_AudioSpec have;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        fprintf(stderr, "[AUDIO] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        SDL_FreeWAV(wav_buf);
        return;
    }

    g_bell.dev = dev;
    g_bell.spec = have;
    g_bell.buf = wav_buf;
    g_bell.len = wav_len;
    g_bell.ok  = true;

    SDL_PauseAudioDevice(g_bell.dev, 0);
}

static void bell_play(void) {
    if (!g_bell.ok || !g_bell.dev || !g_bell.buf || g_bell.len == 0) return;
    SDL_ClearQueuedAudio(g_bell.dev);
    SDL_QueueAudio(g_bell.dev, g_bell.buf, g_bell.len);
    SDL_PauseAudioDevice(g_bell.dev, 0);
}

/* ----------------------------- Layout ----------------------------- */
#define UI_MARGIN_X        30
#define UI_MARGIN_TOP      26
#define UI_ROW_GAP         58
#define UI_BOTTOM_MARGIN   22

// Timer placement (upper-left)
#define TIMER_LEFT_X       UI_MARGIN_X
#define TIMER_TOP_PAD      -60
#define UI_INLINE_GAP_PX  3  // tighter than a font-space for inline pairs (can be negative)

// Pixelation strength
#define PIXELATE_FACTOR 14

// Custom picker label padding
#define CUSTOM_LABEL_PAD   12

static void safe_snprintf(char* dst, size_t cap, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(dst, cap, fmt, args);
    dst[cap - 1] = 0;
    va_end(args);
}

static void trim_ascii_inplace(char* s) {
    if (!s) return;
    // trim leading
    size_t len = strlen(s);
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i > 0) memmove(s, s + i, len - i + 1);
    // trim trailing
    len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = 0;
        len--;
    }
}

static void ascii_lower_inplace(char* s) {
    if (!s) return;
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
    }
}

static bool is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

static bool ends_with_icase(const char* s, const char* ext) {
    if (!s || !ext) return false;
    size_t ls = strlen(s), le = strlen(ext);
    if (le > ls) return false;
    const char* a = s + (ls - le);
    for (size_t i = 0; i < le; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)ext[i])) return false;
    }
    return true;
}

/* ----------------------------- StrList ----------------------------- */

typedef struct {
    char** items;
    int count;
    int cap;
} StrList;

static char* str_dup(const char* s) {
    size_t n = strlen(s);
    char* r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static int cmp_cstr(const void* a, const void* b) {
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

static void sl_sort(StrList* l) {
    if (!l || l->count <= 1) return;
    qsort(l->items, (size_t)l->count, sizeof(char*), cmp_cstr);
}

static void sl_free(StrList* l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static void sl_push(StrList* l, const char* s) {
    if (!l || !s) return;
    if (l->count + 1 > l->cap) {
        int ncap = l->cap ? l->cap * 2 : 16;
        char** nitems = (char**)realloc(l->items, sizeof(char*) * ncap);
        if (!nitems) return;
        l->items = nitems;
        l->cap = ncap;
    }
    l->items[l->count++] = str_dup(s);
}

static int sl_find(const StrList* l, const char* s) {
    if (!l || !s) return -1;
    for (int i = 0; i < l->count; i++) {
        if (strcmp(l->items[i], s) == 0) return i;
    }
    return -1;
}

static StrList list_dirs_in(const char* root) {
    StrList out = {0};
    DIR* d = opendir(root);
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        safe_snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        if (is_dir(path)) sl_push(&out, e->d_name);
    }
    closedir(d);
    sl_sort(&out);
    return out;
}

static StrList list_files_png_in(const char* root) {
    StrList out = {0};
    DIR* d = opendir(root);
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (ends_with_icase(e->d_name, ".png") || ends_with_icase(e->d_name, ".jpg")) {
            sl_push(&out, e->d_name);
        }
    }
    closedir(d);
    sl_sort(&out);
    return out;
}

static StrList list_font_files_in(const char* root) {
    StrList out = (StrList){0};
    DIR* d = opendir(root);
    if (!d) return out;

    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (ends_with_icase(e->d_name, ".ttf") || ends_with_icase(e->d_name, ".otf")) {
            sl_push(&out, e->d_name);
        }
    }

    closedir(d);
    sl_sort(&out);
    return out;
}

static StrList list_wav_files_in(const char* root) {
    StrList out = (StrList){0};
    DIR* d = opendir(root);
    if (!d) return out;

    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (ends_with_icase(e->d_name, ".wav")) {
            sl_push(&out, e->d_name);
        }
    }

    closedir(d);
    sl_sort(&out);
    return out;
}

/* ----------------------------- Color system ----------------------------- */

typedef struct {
    const char* name;
    uint8_t r,g,b;
} NamedColor;

static const NamedColor PALETTE[24] = {
    {"Pearl",          245,245,250},
    {"Warm White",     252,247,235},
    {"Paper",          244,239,229},
    {"Graphite",       200,205,215},
    {"Mint",           168,235,214},
    {"Seafoam",        120,214,196},
    {"Teal",            70,180,185},
    {"Sky",            145,200,255},
    {"Denim",           90,135,210},
    {"Lavender",       195,170,255},
    {"Violet",         145,110,240},
    {"Magenta",        230,120,195},
    {"Rose",           255,155,175},
    {"Coral",          255,145,120},
    {"Amber",          255,196, 90},
    {"Lemon",          255,235,130},
    {"Olive",          160,190, 95},
    {"Lime",           140,235,120},
    {"Neon Cyan",       80,255,245},
    {"Neon Blue",       90,160,255},
    {"Neon Purple",    190,110,255},
    {"Neon Pink",      255,110,190},
    {"Retro Green",    120,235,165},
    {"Crimson",        235, 90,105},
};

static SDL_Color color_from_idx(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= 24) idx = 0;
    SDL_Color c = { PALETTE[idx].r, PALETTE[idx].g, PALETTE[idx].b, 255 };
    return c;
}

/* ----------------------------- Config ----------------------------- */

typedef struct {
    char scene[128];
    char weather[128];
    int swap_ab;

    int main_color_idx;
    int accent_color_idx;
    int highlight_color_idx;

    char font_file[256];
    int font_small_pt;
    int font_med_pt;
    int font_big_pt;

    int music_enabled;
    char music_folder[128];

    /* Bells */
    char bell_phase_file[256];
    char bell_done_file[256];

    /* Last-used picker values */
    int last_timer_h;
    int last_timer_m;
    int last_timer_s;
    int last_pomo_session_min;
    int last_pomo_short_break_min;
    int last_pomo_long_break_min;
    int last_pomo_loops;

    /* Tasks (CSV of 32-bit hashes). Kept in config so it survives restarts. */
    char tasks_daily_done[2048];
    char tasks_weekly_done[2048];
} AppConfig;

static void config_defaults(AppConfig* c) {
    memset(c, 0, sizeof(*c));
    strcpy(c->scene, "coffee_shop");
    strcpy(c->weather, "morning");
    c->swap_ab = 1;

    c->main_color_idx = 0;
    c->accent_color_idx = 7;
    c->highlight_color_idx = 14;

    strcpy(c->font_file, "Munro.ttf");
    c->font_small_pt = 42;
    c->font_med_pt   = 50;
    c->font_big_pt   = 100;
    c->music_enabled = 0;
    strcpy(c->music_folder, "Music");
    strcpy(c->bell_phase_file, "bell.wav");
    strcpy(c->bell_done_file, "bell.wav");

    c->last_timer_h = 0;
    c->last_timer_m = 25;
    c->last_timer_s = 0;

    c->last_pomo_session_min = 25;
    c->last_pomo_short_break_min = 5;
    c->last_pomo_long_break_min = 20;
    c->last_pomo_loops = 4;

    c->tasks_daily_done[0] = '\0';
    c->tasks_weekly_done[0] = '\0';
}

static void config_trim(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static bool config_load(AppConfig* c, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        config_trim(line);
        if (!line[0] || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char key[256], val[256];
        safe_snprintf(key, sizeof(key), "%s", line);
        safe_snprintf(val, sizeof(val), "%s", eq + 1);
        config_trim(key);
        config_trim(val);

        if (strcmp(key, "scene") == 0) safe_snprintf(c->scene, sizeof(c->scene), "%s", val);
        else if (strcmp(key, "weather") == 0) safe_snprintf(c->weather, sizeof(c->weather), "%s", val);
        else if (strcmp(key, "timeofday") == 0) safe_snprintf(c->weather, sizeof(c->weather), "%s", val);
        else if (strcmp(key, "swap_ab") == 0) c->swap_ab = atoi(val) ? 1 : 0;
        else if (strcmp(key, "main_color_idx") == 0) c->main_color_idx = atoi(val);
        else if (strcmp(key, "accent_color_idx") == 0) c->accent_color_idx = atoi(val);
        else if (strcmp(key, "highlight_color_idx") == 0) c->highlight_color_idx = atoi(val);
        else if (strcmp(key, "font_file") == 0) safe_snprintf(c->font_file, sizeof(c->font_file), "%s", val);
        else if (strcmp(key, "font_small_pt") == 0) c->font_small_pt = atoi(val);
        else if (strcmp(key, "font_med_pt") == 0) c->font_med_pt = atoi(val);
        else if (strcmp(key, "font_big_pt") == 0) c->font_big_pt = atoi(val);
        else if (strcmp(key, "music_enabled") == 0) c->music_enabled = atoi(val);
        else if (strcmp(key, "music_folder") == 0) safe_snprintf(c->music_folder, sizeof(c->music_folder), "%s", val);
        else if (strcmp(key, "bell_phase_file") == 0) safe_snprintf(c->bell_phase_file, sizeof(c->bell_phase_file), "%s", val);
        else if (strcmp(key, "bell_done_file") == 0) safe_snprintf(c->bell_done_file, sizeof(c->bell_done_file), "%s", val);
        else if (strcmp(key, "last_timer_h") == 0) c->last_timer_h = atoi(val);
        else if (strcmp(key, "last_timer_m") == 0) c->last_timer_m = atoi(val);
        else if (strcmp(key, "last_timer_s") == 0) c->last_timer_s = atoi(val);
        else if (strcmp(key, "last_pomo_session_min") == 0) c->last_pomo_session_min = atoi(val);
        else if (strcmp(key, "last_pomo_short_break_min") == 0) c->last_pomo_short_break_min = atoi(val);
        else if (strcmp(key, "last_pomo_long_break_min") == 0) c->last_pomo_long_break_min = atoi(val);
        else if (strcmp(key, "last_pomo_loops") == 0) c->last_pomo_loops = atoi(val);
        else if (strcmp(key, "tasks_daily_done") == 0) safe_snprintf(c->tasks_daily_done, sizeof(c->tasks_daily_done), "%s", val);
        else if (strcmp(key, "tasks_weekly_done") == 0) safe_snprintf(c->tasks_weekly_done, sizeof(c->tasks_weekly_done), "%s", val);
    }
    fclose(f);

    if (c->main_color_idx < 0 || c->main_color_idx >= 24) c->main_color_idx = 0;
    if (c->accent_color_idx < 0 || c->accent_color_idx >= 24) c->accent_color_idx = 7;
    if (c->highlight_color_idx < 0 || c->highlight_color_idx >= 24) c->highlight_color_idx = 14;

    if (c->font_small_pt < 18) c->font_small_pt = 18;
    if (c->font_med_pt < 18) c->font_med_pt = 18;
    if (c->font_big_pt < 30) c->font_big_pt = 30;

    if (c->font_small_pt > 96) c->font_small_pt = 96;
    if (c->font_med_pt > 120) c->font_med_pt = 120;
    if (c->font_big_pt > 220) c->font_big_pt = 220;

    if (!c->bell_phase_file[0]) strcpy(c->bell_phase_file, "bell.wav");
    if (!c->bell_done_file[0]) strcpy(c->bell_done_file, "bell.wav");

    if (c->last_timer_h < 0) c->last_timer_h = 0;
    if (c->last_timer_h > 24) c->last_timer_h = 24;
    if (c->last_timer_m < 0) c->last_timer_m = 0;
    if (c->last_timer_m > 59) c->last_timer_m = 59;
    if (c->last_timer_s < 0) c->last_timer_s = 0;
    if (c->last_timer_s > 59) c->last_timer_s = 59;

    if (c->last_pomo_session_min < 1) c->last_pomo_session_min = 25;
    if (c->last_pomo_session_min > 180) c->last_pomo_session_min = 180;
    if (c->last_pomo_short_break_min < 1) c->last_pomo_short_break_min = 5;
    if (c->last_pomo_short_break_min > 60) c->last_pomo_short_break_min = 60;
    if (c->last_pomo_long_break_min < 1) c->last_pomo_long_break_min = 20;
    if (c->last_pomo_long_break_min > 120) c->last_pomo_long_break_min = 120;
    if (c->last_pomo_loops < 1) c->last_pomo_loops = 4;
    if (c->last_pomo_loops > 12) c->last_pomo_loops = 12;
    if (!c->font_file[0]) strcpy(c->font_file, "Munro.ttf");
    if (!c->weather[0]) strcpy(c->weather, "morning");
    if (c->music_enabled != 0) c->music_enabled = 1;
    if (!c->music_folder[0]) strcpy(c->music_folder, "Music");

    return true;
}

static void config_save(const AppConfig* c, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "scene=%s\n", c->scene);
    fprintf(f, "weather=%s\n", c->weather);
    fprintf(f, "swap_ab=%d\n", c->swap_ab);

    fprintf(f, "main_color_idx=%d\n", c->main_color_idx);
    fprintf(f, "accent_color_idx=%d\n", c->accent_color_idx);
    fprintf(f, "highlight_color_idx=%d\n", c->highlight_color_idx);

    fprintf(f, "font_file=%s\n", c->font_file);
    fprintf(f, "font_small_pt=%d\n", c->font_small_pt);
    fprintf(f, "font_med_pt=%d\n", c->font_med_pt);
    fprintf(f, "font_big_pt=%d\n", c->font_big_pt);

    fprintf(f, "music_enabled=%d\n", c->music_enabled);
    fprintf(f, "music_folder=%s\n", c->music_folder);

    fprintf(f, "bell_phase_file=%s\n", c->bell_phase_file);
    fprintf(f, "bell_done_file=%s\n", c->bell_done_file);

    fprintf(f, "last_timer_h=%d\n", c->last_timer_h);
    fprintf(f, "last_timer_m=%d\n", c->last_timer_m);
    fprintf(f, "last_timer_s=%d\n", c->last_timer_s);

    fprintf(f, "last_pomo_session_min=%d\n", c->last_pomo_session_min);
    fprintf(f, "last_pomo_short_break_min=%d\n", c->last_pomo_short_break_min);
    fprintf(f, "last_pomo_long_break_min=%d\n", c->last_pomo_long_break_min);
    fprintf(f, "last_pomo_loops=%d\n", c->last_pomo_loops);

    /* Tasks */
    fprintf(f, "tasks_daily_done=%s\n", c->tasks_daily_done);
    fprintf(f, "tasks_weekly_done=%s\n", c->tasks_weekly_done);

    fclose(f);
}

/* ----------------------------- Tasks (.txt driven) ----------------------------- */

static uint32_t fnv1a32(const char* s) {
    /* Stable, fast hash for task identity. */
    uint32_t h = 2166136261u;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        h ^= (uint32_t)(*p++);
        h *= 16777619u;
    }
    return h;
}

static void hashes_free(uint32_t** arr, int* n) {
    if (*arr) free(*arr);
    *arr = NULL;
    *n = 0;
}

static bool hashes_contains(const uint32_t* arr, int n, uint32_t v) {
    for (int i = 0; i < n; i++) if (arr[i] == v) return true;
    return false;
}

static void hashes_remove(uint32_t* arr, int* n, uint32_t v) {
    for (int i = 0; i < *n; i++) {
        if (arr[i] == v) {
            memmove(&arr[i], &arr[i + 1], (size_t)(*n - i - 1) * sizeof(uint32_t));
            (*n)--;
            return;
        }
    }
}

static void hashes_add(uint32_t** arr, int* n, uint32_t v) {
    if (hashes_contains(*arr, *n, v)) return;
    uint32_t* p = (uint32_t*)realloc(*arr, (size_t)(*n + 1) * sizeof(uint32_t));
    if (!p) return;
    p[*n] = v;
    *arr = p;
    (*n)++;
}

static void hashes_from_csv(const char* csv, uint32_t** out, int* out_n) {
    hashes_free(out, out_n);
    if (!csv || !csv[0]) return;

    const char* p = csv;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) break;
        hashes_add(out, out_n, (uint32_t)v);
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') p++;
    }
}

static void hashes_to_csv(const uint32_t* arr, int n, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = 0;
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        char buf[32];
        safe_snprintf(buf, sizeof(buf), "%u", (unsigned)arr[i]);
        size_t need = strlen(buf) + (i ? 1 : 0);
        if (used + need + 1 >= out_sz) break;
        if (i) { out[used++] = ','; out[used] = 0; }
        strcat(out, buf);
        used = strlen(out);
    }
}

/* Forward declaration so Tasks helpers can take an App* before the full app state is defined. */
typedef struct App App;

static StrList tasks_load_txt(const char* path) {
    StrList out = (StrList){0};
    FILE* f = fopen(path, "r");
    if (!f) return out;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Trim ASCII whitespace, ignore blanks. */
        config_trim(line);
        if (!line[0]) continue;
        sl_push(&out, line);
    }
    fclose(f);
    return out;
}

/* ----------------------------- UI ----------------------------- */

typedef struct UI {
    SDL_Window* win;
    SDL_Renderer* ren;
    TTF_Font* font_small;
    TTF_Font* font_med;
    TTF_Font* font_big;
    SDL_Texture* bg_tex;
    int w, h;
} UI;

/* (App forward declaration moved above for Tasks helpers.) */

static SDL_Texture* load_texture(SDL_Renderer* ren, const char* path) {
    if (!is_file(path)) return NULL;
    SDL_Surface* s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    return t;
}

static SDL_Surface* render_text_surface(TTF_Font* font, const char* text, SDL_Color col) {
    return TTF_RenderUTF8_Blended(font, text, col);
}

static void draw_text(UI* ui, TTF_Font* font, int x, int y, const char* text, SDL_Color col, bool right_align) {
    if (!text || !text[0]) return;
    SDL_Surface* s = render_text_surface(font, text, col);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ui->ren, s);
    if (!t) { SDL_FreeSurface(s); return; }
    SDL_Rect dst = { x, y, s->w, s->h };
    if (right_align) dst.x = x - s->w;
    SDL_RenderCopy(ui->ren, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

static void draw_center_text(UI* ui, TTF_Font* font, int cx, int y, const char* text, SDL_Color col) {
    if (!text || !text[0]) return;
    SDL_Surface* s = render_text_surface(font, text, col);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ui->ren, s);
    if (!t) { SDL_FreeSurface(s); return; }
    SDL_Rect dst = { cx - s->w/2, y, s->w, s->h };
    SDL_RenderCopy(ui->ren, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

/* ----------------------------- Pixelation cache ----------------------------- */

typedef struct {
    SDL_Texture* small;
    int sw, sh;
    bool supported;
    bool tested;
} PixelCache;

static void pixel_destroy(PixelCache* pc) {
    if (pc->small) SDL_DestroyTexture(pc->small);
    pc->small = NULL;
    pc->sw = pc->sh = 0;
}

static void pixel_test_support(UI* ui, PixelCache* pc) {
    if (pc->tested) return;
    pc->tested = true;

    SDL_Texture* t = SDL_CreateTexture(ui->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 16, 16);
    if (!t) { pc->supported = false; return; }
    SDL_DestroyTexture(t);
    pc->supported = true;
}

static void pixel_ensure(UI* ui, PixelCache* pc, int w, int h) {
    if (!pc->supported) return;
    if (pc->small && pc->sw == w && pc->sh == h) return;

    pixel_destroy(pc);
    pc->sw = w;
    pc->sh = h;

    pc->small = SDL_CreateTexture(ui->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!pc->small) { pc->supported = false; return; }
    SDL_SetTextureBlendMode(pc->small, SDL_BLENDMODE_NONE);
}

static void draw_bg_normal(UI* ui) {
    if (ui->bg_tex) {
        SDL_Rect dst = {0,0,ui->w,ui->h};
        SDL_RenderCopy(ui->ren, ui->bg_tex, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(ui->ren, 15, 15, 20, 255);
        SDL_RenderClear(ui->ren);
    }
}

static void draw_bg_dimmed(UI* ui, Uint8 overlay_alpha) {
    draw_bg_normal(ui);
    SDL_Rect dst = {0,0,ui->w,ui->h};
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, overlay_alpha);
    SDL_RenderFillRect(ui->ren, &dst);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
}


static void draw_bg_faint(UI* ui, Uint8 bg_alpha) {
    if (!ui->bg_tex) { draw_bg_normal(ui); return; }
    Uint8 old = 255;
    SDL_GetTextureAlphaMod(ui->bg_tex, &old);
    SDL_SetTextureAlphaMod(ui->bg_tex, bg_alpha);
    SDL_Rect dst = {0,0,ui->w,ui->h};
    SDL_RenderCopy(ui->ren, ui->bg_tex, NULL, &dst);
    SDL_SetTextureAlphaMod(ui->bg_tex, old);
}

static void draw_bg_pixelated(UI* ui, PixelCache* pc, int factor) {
    if (!ui->bg_tex) { draw_bg_normal(ui); return; }

    pixel_test_support(ui, pc);
    if (!pc->supported) { draw_bg_normal(ui); return; }

    if (factor < 6) factor = 6;
    if (factor > 30) factor = 30;

    int sw = ui->w / factor;
    int sh = ui->h / factor;
    if (sw < 16) sw = 16;
    if (sh < 16) sh = 16;

    pixel_ensure(ui, pc, sw, sh);
    if (!pc->small) { draw_bg_normal(ui); return; }

    SDL_SetRenderTarget(ui->ren, pc->small);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_RenderClear(ui->ren);

    SDL_Rect src = {0,0,ui->w,ui->h};
    SDL_Rect dstSmall = {0,0,sw,sh};
    SDL_RenderCopy(ui->ren, ui->bg_tex, &src, &dstSmall);

    SDL_SetRenderTarget(ui->ren, NULL);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Rect dstFull = {0,0,ui->w,ui->h};
    SDL_RenderCopy(ui->ren, pc->small, NULL, &dstFull);

    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 70);
    SDL_RenderFillRect(ui->ren, &dstFull);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
}

/* ----------------------------- Bottom hint rendering (mixed colors) ----------------------------- */

static int text_width(TTF_Font* font, const char* s) {
    if (!s || !s[0]) return 0;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, s, &w, &h) != 0) return 0;
    return w;
}
static int text_width_style(TTF_Font* font, int style, const char* s) {
    if (!s || !s[0]) return 0;
    int old = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, style);
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, s, &w, &h) != 0) w = 0;
    TTF_SetFontStyle(font, old);
    return w;
}

static void draw_text_style(UI* ui, TTF_Font* font, int x, int y, const char* text, SDL_Color color, bool right_align, int style) {
    if (!text || !text[0]) return;

    int old = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, style);

    SDL_Surface* surf = render_text_surface(font, text, color);
    if (!surf) { TTF_SetFontStyle(font, old); return; }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ui->ren, surf);
    if (!tex) { SDL_FreeSurface(surf); TTF_SetFontStyle(font, old); return; }

    int draw_x = x;
    if (right_align) draw_x = x - surf->w;

    SDL_Rect dst = { draw_x, y, surf->w, surf->h };
    SDL_RenderCopy(ui->ren, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
    TTF_SetFontStyle(font, old);
}

static void draw_text_baseline(UI* ui, TTF_Font* font, int x, int baseline_y, const char* text, SDL_Color col, bool align_right) {
    if (!text || !text[0]) return;
    int ascent = TTF_FontAscent(font);
    int y = baseline_y - ascent;
    draw_text(ui, font, x, y, text, col, align_right);
}

static void draw_text_style_baseline(UI* ui, TTF_Font* font, int x, int baseline_y, const char* text, SDL_Color col, bool align_right, int style) {
    if (!text || !text[0]) return;
    int ascent = TTF_FontAscent(font);
    int y = baseline_y - ascent;
    draw_text_style(ui, font, x, y, text, col, align_right, style);
}


static void draw_inline_left(UI* ui, TTF_Font* font_main, TTF_Font* font_sec, int x, int y, const char* main_text, const char* secondary_text, SDL_Color main_col, SDL_Color secondary_col) {
    if (!main_text) main_text = "";
    if (!secondary_text) secondary_text = "";

    int asc_main = TTF_FontAscent(font_main);
    int asc_sec  = TTF_FontAscent(font_sec);
    int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);

    draw_text_baseline(ui, font_main, x, baseline_y, main_text, main_col, false);

    if (secondary_text[0]) {
        int wMain = text_width(font_main, main_text);
        int wGap  = (main_text[0] && secondary_text[0]) ? UI_INLINE_GAP_PX : 0;
        int x2 = x + wMain + wGap;
        draw_text_style_baseline(ui, font_sec, x2, baseline_y, secondary_text, secondary_col, false, TTF_STYLE_ITALIC);
    }
}

static void draw_inline_right(UI* ui, TTF_Font* font_main, TTF_Font* font_sec, int xR, int y, const char* italic_text, const char* normal_text, SDL_Color italic_col, SDL_Color normal_col) {
    if (!italic_text) italic_text = "";
    if (!normal_text) normal_text = "";

    int asc_main = TTF_FontAscent(font_main);
    int asc_sec  = TTF_FontAscent(font_sec);
    int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);

    int wItal = text_width_style(font_sec, TTF_STYLE_ITALIC, italic_text);
    int wNorm = text_width(font_main, normal_text);
    int wGap  = (italic_text[0] && normal_text[0]) ? UI_INLINE_GAP_PX : 0;

    int xStart = xR - (wItal + wGap + wNorm);

    if (italic_text[0]) {
        draw_text_style_baseline(ui, font_sec, xStart, baseline_y, italic_text, italic_col, false, TTF_STYLE_ITALIC);
    }
    if (normal_text[0]) {
        int x2 = xStart + wItal + wGap;
        draw_text_baseline(ui, font_main, x2, baseline_y, normal_text, normal_col, false);
    }
}

/* ----------------------------- Clock (stacked words) ----------------------------- */

static const char* word_1_to_19(int n) {
    switch (n) {
        case 1:  return "one";
        case 2:  return "two";
        case 3:  return "three";
        case 4:  return "four";
        case 5:  return "five";
        case 6:  return "six";
        case 7:  return "seven";
        case 8:  return "eight";
        case 9:  return "nine";
        case 10: return "ten";
        case 11: return "eleven";
        case 12: return "twelve";
        case 13: return "thirteen";
        case 14: return "fourteen";
        case 15: return "fifteen";
        case 16: return "sixteen";
        case 17: return "seventeen";
        case 18: return "eighteen";
        case 19: return "nineteen";
        default: return "";
    }
}

static const char* word_tens(int n) {
    switch (n) {
        case 2: return "twenty";
        case 3: return "thirty";
        case 4: return "forty";
        case 5: return "fifty";
        default: return "";
    }
}


static const char* word_tens_2_to_9(int n) {
    switch (n) {
        case 2: return "twenty";
        case 3: return "thirty";
        case 4: return "forty";
        case 5: return "fifty";
        case 6: return "sixty";
        case 7: return "seventy";
        case 8: return "eighty";
        case 9: return "ninety";
        default: return "";
    }
}

static void number_to_words_0_100(char* out, size_t out_sz, int n) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    if (n < 0) n = 0;
    if (n > 100) n = 100;

    if (n <= 19) {
        safe_snprintf(out, out_sz, "%s", word_1_to_19(n));
        return;
    }
    if (n == 100) {
        safe_snprintf(out, out_sz, "one-hundred");
        return;
    }

    int tens = n / 10;
    int ones = n % 10;
    const char* t = word_tens_2_to_9(tens);

    if (ones == 0) {
        safe_snprintf(out, out_sz, "%s", t);
    } else {
        safe_snprintf(out, out_sz, "%s-%s", t, word_1_to_19(ones));
    }
}


static void minute_to_words(char* out, size_t out_sz, int minute) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    if (minute == 0) {
        safe_snprintf(out, out_sz, "zero");
        return;
    }

    if (minute < 10) {
        /* calm spoken form: oh five, oh nine */
        safe_snprintf(out, out_sz, "oh %s", word_1_to_19(minute));
        return;
    }

    if (minute < 20) {
        safe_snprintf(out, out_sz, "%s", word_1_to_19(minute));
        return;
    }

    int tens = minute / 10;
    int ones = minute % 10;
    const char* t = word_tens(tens);
    if (ones == 0) {
        safe_snprintf(out, out_sz, "%s", t);
    } else {
        safe_snprintf(out, out_sz, "%s %s", t, word_1_to_19(ones));
    }
}

static const char* hour_to_word_12h(int hour24) {
    int h = hour24 % 24;
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    return word_1_to_19(h12);
}

static void draw_clock_upper_right_stacked(UI* ui, App* a, int xR, int yTop);
static void draw_battery_upper_right_stacked(UI* ui, App* a, int xR, int yTop);



static void draw_inline_center(UI* ui, TTF_Font* font_main, TTF_Font* font_sec,
                               int xC, int y, const char* italic_text, const char* normal_text,
                               SDL_Color italic_col, SDL_Color normal_col) {
    if (!italic_text) italic_text = "";
    if (!normal_text) normal_text = "";

    int asc_main = TTF_FontAscent(font_main);
    int asc_sec  = TTF_FontAscent(font_sec);
    int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);

    int wItal = text_width_style(font_sec, TTF_STYLE_ITALIC, italic_text);
    int wNorm = text_width(font_main, normal_text);
    int wGap  = (italic_text[0] && normal_text[0]) ? UI_INLINE_GAP_PX : 0;

    int total = wItal + wGap + wNorm;
    int xStart = xC - (total / 2);

    if (italic_text[0]) {
        draw_text_style_baseline(ui, font_sec, xStart, baseline_y, italic_text, italic_col, false, TTF_STYLE_ITALIC);
    }
    if (normal_text[0]) {
        int x2 = xStart + wItal + wGap;
        draw_text_baseline(ui, font_main, x2, baseline_y, normal_text, normal_col, false);
    }
}

static void draw_strikethrough(UI* ui, int x, int y, int w, int text_h, SDL_Color col) {
    if (w <= 0 || text_h <= 0) return;

    int thickness = text_h / 18;
    if (thickness < 1) thickness = 1;
    thickness += 2; /* visible on-device */

    const int STRIKE_Y_OFFSET = 4; /* positive = lower */
    const int STRIKE_X_OFFSET = -8; /* positive = move right */
    const int STRIKE_X_INSET  = 0; /* positive = shorten both ends */

    int line_y = y + (text_h / 2) - (thickness / 2) + STRIKE_Y_OFFSET;

    int line_x = x + STRIKE_X_OFFSET + STRIKE_X_INSET;
    int line_w = w - (STRIKE_X_INSET * 2);
    if (line_w <= 0) return;

    SDL_Rect r = { line_x, line_y, line_w, thickness };

    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, col.r, col.g, col.b, col.a);
    SDL_RenderFillRect(ui->ren, &r);
}



static bool is_nav_hint_label(const char* lab) {
    if (!lab || !lab[0]) return false;
    // Remove bottom D-pad navigation hints only (Up/Down, Left/Right, D-Pad).
    if (strstr(lab, "Up/Down") != NULL) return true;
    if (strstr(lab, "Left/Right") != NULL) return true;
    if (strstr(lab, "D-Pad") != NULL) return true;
    if (strstr(lab, "DPAD") != NULL) return true;
    return false;
}

static void draw_pairs_line(UI* ui, TTF_Font* font, int x, int y,
                            SDL_Color main, SDL_Color accent,
                            bool right_align_total,
                            const char** labels, const char** actions, int count) {
    // Filter out navigation hints (Up/Down, Left/Right, D-Pad) while keeping all other hints.
    const char* f_labels[32];
    const char* f_actions[32];
    int n = 0;
    for (int i = 0; i < count && n < (int)(sizeof(f_labels) / sizeof(f_labels[0])); i++) {
        const char* lab = labels[i] ? labels[i] : "";
        if (is_nav_hint_label(lab)) continue;
        f_labels[n] = labels[i];
        f_actions[n] = actions[i];
        n++;
    }
    if (n <= 0) return;

    const char* sep = "   ";
    int wSep = text_width(font, sep);
    int total = 0;
    for (int i = 0; i < n; i++) {
        char a_with_space[256];
        const char* act = f_actions[i] ? f_actions[i] : "";
        const char* lab2 = f_labels[i] ? f_labels[i] : "";
        if (act[0]) safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
        else safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);
        total += text_width(font, lab2);
        total += text_width(font, a_with_space);
        if (i != n - 1) total += wSep;
    }

    int start_x = x;
    if (right_align_total) start_x = x - total;

    int cx = start_x;
    for (int i = 0; i < n; i++) {
        const char* lab2 = f_labels[i] ? f_labels[i] : "";
        const char* act = f_actions[i] ? f_actions[i] : "";

        char a_with_space[256];
        if (act[0]) safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
        else safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);

        int wL = text_width(font, lab2);
        int wA = text_width(font, a_with_space);

        if (lab2[0]) draw_text(ui, font, cx, y, lab2, main, false);
        if (a_with_space[0]) draw_text(ui, font, cx + wL, y, a_with_space, accent, false);
        cx += wL + wA;

        if (i != n - 1) {
            draw_text(ui, font, cx, y, sep, main, false);
            cx += wSep;
        }
    }
}


static int ui_bottom_baseline_y(UI* ui) {
    /* Anchor all bottom-row UI text to the same baseline, regardless of font sizes. */
    const int descent = TTF_FontDescent(ui->font_small); /* usually negative */
    return (ui->h - UI_BOTTOM_MARGIN) + descent;
}

static void draw_hint_pairs_lr(UI* ui, SDL_Color main, SDL_Color accent,
                               const char** left_labels, const char** left_actions, int left_count,
                               const char** right_labels, const char** right_actions, int right_count) {
    const int baseline_y = ui_bottom_baseline_y(ui);
    const int y = baseline_y - TTF_FontAscent(ui->font_small);

    if (left_count > 0) {
        draw_pairs_line(ui, ui->font_small, UI_MARGIN_X, y, main, accent, false,
                        left_labels, left_actions, left_count);
    }
    if (right_count > 0) {
        draw_pairs_line(ui, ui->font_small, ui->w - UI_MARGIN_X, y, main, accent, true,
                        right_labels, right_actions, right_count);
    }
}

static void draw_hint_pairs_center(UI* ui, SDL_Color main, SDL_Color accent,
                                   const char** labels, const char** actions, int count) {
    const int baseline_y = ui_bottom_baseline_y(ui);
    const int y = baseline_y - TTF_FontAscent(ui->font_small);

    const char* sep = "   ";
    int wSep = text_width(ui->font_small, sep);
    int total = 0;
    for (int i = 0; i < count; i++) {
        const char* lab = labels[i] ? labels[i] : "";
        const char* act = actions[i] ? actions[i] : "";
        char a_with_space[256];
        if (act[0]) safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
        else safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);

        total += text_width(ui->font_small, lab);
        total += text_width(ui->font_small, a_with_space);
        if (i != count - 1) total += wSep;
    }

    int start_x = (ui->w / 2) - (total / 2);

    draw_pairs_line(ui, ui->font_small, start_x, y, main, accent, false, labels, actions, count);
}


/* ----------------------------- App state ----------------------------- */

typedef enum {
    SCREEN_MENU = 0,
    SCREEN_POMO_PICK,
    SCREEN_CUSTOM_PICK,
    SCREEN_TIMER,
    SCREEN_TASKS_PICK,
    SCREEN_TASKS_LIST,
} Screen;

typedef enum {
    MODE_POMODORO = 0,
    MODE_CUSTOM,
    MODE_STOPWATCH,
} RunMode;

typedef struct {
    bool up, down, left, right;
    bool a, b, x, y;
    bool start;
    bool select;
    bool l1, r1;
    bool l2, r2;
    bool l3, r3;
} Buttons;

typedef enum {
    SET_VIEW_MAIN = 0,
    SET_VIEW_SCENE,
    SET_VIEW_APPEARANCE,
    SET_VIEW_FONTS,
    SET_VIEW_COLORS,
    SET_VIEW_MUSIC,
    SET_VIEW_BELL,
    SET_VIEW_BUTTONMAP,
    SET_VIEW_FONT_SIZES,
} SettingsView;

typedef struct App {
    StrList scenes;
    StrList weathers;
    int scene_idx;
    int weather_idx;

    StrList fonts;
    int font_idx;

    char scene_name[128];
    char weather_name[128];

    char music_folder[128];
    char music_song[256];

    /* Music playback */
    AudioEngine* audio;
    StrList music_folders;
    int music_folder_idx;
    int music_sel; /* selection row in music settings */
    int buttonmap_page; /* 0 core, 1 music */
    bool music_user_paused;
    bool music_has_started;
    int trig_l2_down;
    int trig_r2_down;
    struct {
        StrList tracks;
        int idx;
        bool active;
    } musicq;

    /* Bell sounds */
    StrList bell_sounds;
    int bell_sel;           /* selection row in bell settings (0 phase, 1 done) */
    int bell_phase_idx;
    int bell_done_idx;

    AppConfig cfg;

    Screen screen;
    RunMode mode;
    bool running;
    bool paused;
    bool hud_hidden;
    bool session_complete;

    int menu_sel;

    bool timer_menu_open;
    bool quit_requested;

    int pomo_pick_sel;
    int pick_pomo_session_min;
    int pick_pomo_break_min;
    int pick_pomo_long_break_min;
    int pick_pomo_loops;

    uint32_t pomo_session_seconds;
    uint32_t pomo_break_seconds;
    uint32_t pomo_long_break_seconds;

    int  pomo_session_in_pomo; /* 0=first session, 1=second session */
    bool pomo_break_is_long;

    int pick_custom_hours;     // 0..24
    int pick_custom_minutes;   // 0..59
    int pick_custom_seconds;   // 0..59
    int custom_field_sel;      // 0=hh, 1=mm, 2=ss

    int pomo_loops_total;
    int pomo_loops_done;
    bool pomo_is_break;
    uint32_t pomo_remaining_seconds;

    uint32_t custom_total_seconds;
    uint32_t custom_remaining_seconds;

    uint32_t stopwatch_seconds;

    uint64_t last_tick_ms;
    float tick_accum;

    bool settings_open;
    SettingsView settings_view;

    int settings_sel;
    int scene_sel;
    int appearance_sel;
    int fonts_sel;
    int colors_sel;
    int sizes_sel;

    /* Tasks */
    StrList tasks_daily;
    StrList tasks_weekly;
    int tasks_pick_sel;   /* 0=Daily, 1=Weekly, 2=Back */
    int tasks_sel;        /* selected task row within current list */
    int tasks_kind;       /* 0=Daily, 1=Weekly */
    uint32_t* tasks_done_daily;
    int tasks_done_daily_n;
    uint32_t* tasks_done_weekly;
    int tasks_done_weekly_n;
} App;

/* ----------------------------- Tasks (Daily/Weekly) ----------------------------- */

static void tasks_reload(App* a) {
    sl_free(&a->tasks_daily);
    sl_free(&a->tasks_weekly);
    a->tasks_daily = tasks_load_txt("Tasks/Daily.txt");
    a->tasks_weekly = tasks_load_txt("Tasks/Weekly.txt");

    /* completion state from config */
    hashes_from_csv(a->cfg.tasks_daily_done, &a->tasks_done_daily, &a->tasks_done_daily_n);
    hashes_from_csv(a->cfg.tasks_weekly_done, &a->tasks_done_weekly, &a->tasks_done_weekly_n);
}

static bool task_is_done(App* a, int kind, const char* text) {
    uint32_t h = fnv1a32(text);
    if (kind == 0) return hashes_contains(a->tasks_done_daily, a->tasks_done_daily_n, h);
    return hashes_contains(a->tasks_done_weekly, a->tasks_done_weekly_n, h);
}

static void tasks_toggle_done(App* a, int kind, const char* text) {
    uint32_t h = fnv1a32(text);
    if (kind == 0) {
        if (hashes_contains(a->tasks_done_daily, a->tasks_done_daily_n, h)) hashes_remove(a->tasks_done_daily, &a->tasks_done_daily_n, h);
        else hashes_add(&a->tasks_done_daily, &a->tasks_done_daily_n, h);
        hashes_to_csv(a->tasks_done_daily, a->tasks_done_daily_n, a->cfg.tasks_daily_done, sizeof(a->cfg.tasks_daily_done));
    } else {
        if (hashes_contains(a->tasks_done_weekly, a->tasks_done_weekly_n, h)) hashes_remove(a->tasks_done_weekly, &a->tasks_done_weekly_n, h);
        else hashes_add(&a->tasks_done_weekly, &a->tasks_done_weekly_n, h);
        hashes_to_csv(a->tasks_done_weekly, a->tasks_done_weekly_n, a->cfg.tasks_weekly_done, sizeof(a->cfg.tasks_weekly_done));
    }
    config_save(&a->cfg, "config.txt");
}

static void tasks_reset(App* a, int kind) {
    if (kind == 0) {
        hashes_free(&a->tasks_done_daily, &a->tasks_done_daily_n);
        a->cfg.tasks_daily_done[0] = 0;
    } else {
        hashes_free(&a->tasks_done_weekly, &a->tasks_done_weekly_n);
        a->cfg.tasks_weekly_done[0] = 0;
    }
    config_save(&a->cfg, "config.txt");
}

/* ----------------------------- Font loading / reloading ----------------------------- */

static void ui_close_fonts(UI* ui);
static bool ui_open_fonts(UI* ui, const AppConfig* cfg);

static void ui_close_fonts(UI* ui) {
    if (ui->font_small) { TTF_CloseFont(ui->font_small); ui->font_small = NULL; }
    if (ui->font_med)   { TTF_CloseFont(ui->font_med);   ui->font_med = NULL; }
    if (ui->font_big)   { TTF_CloseFont(ui->font_big);   ui->font_big = NULL; }
}

static bool ui_open_fonts(UI* ui, const AppConfig* cfg) {
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "fonts/%s", cfg->font_file);

    TTF_Font* fs = TTF_OpenFont(path, cfg->font_small_pt);
    TTF_Font* fm = TTF_OpenFont(path, cfg->font_med_pt);
    TTF_Font* fb = TTF_OpenFont(path, cfg->font_big_pt);

    if (!fs || !fm || !fb) {
        if (fs) TTF_CloseFont(fs);
        if (fm) TTF_CloseFont(fm);
        if (fb) TTF_CloseFont(fb);
        return false;
    }

  
  ui_close_fonts(ui);
    ui->font_small = fs;
    ui->font_med   = fm;
    ui->font_big   = fb;
    return true;
}


static void draw_clock_upper_right_stacked(UI* ui, App* a, int xR, int yTop) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);

    const char* hour_word = hour_to_word_12h(lt.tm_hour);
    char minute_word[64];
    minute_to_words(minute_word, sizeof(minute_word), lt.tm_min);

    /* Layout: hour (normal, med), minute (small italic accent) */
    int yHour = yTop;

    /* Tight stack: place minute almost touching the hour block */
    int yMin = yHour + TTF_FontHeight(ui->font_med) - 10;
    if (yMin < yHour + 1) yMin = yHour + 1;

    draw_text(ui, ui->font_med, xR, yHour, hour_word, main, true);
    draw_text_style(ui, ui->font_small, xR, yMin, minute_word, accent, true, TTF_STYLE_ITALIC);

}
static void draw_battery_upper_right_stacked(UI* ui, App* a, int xR, int yTop) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    battery_tick_update();

    char top_word[64];
    const char* bottom_word = "percent";

    if (g_batt_percent < 0) {
        safe_snprintf(top_word, sizeof(top_word), "battery");
        bottom_word = "n/a";
    } else {
        number_to_words_0_100(top_word, sizeof(top_word), g_batt_percent);
    }

    int yTopLine = yTop;
    int yBottomLine = yTopLine + TTF_FontHeight(ui->font_med) - 10;
    if (yBottomLine < yTopLine + 1) yBottomLine = yTopLine + 1;

    draw_text(ui, ui->font_med, xR, yTopLine, top_word, main, true);
    draw_text_style(ui, ui->font_small, xR, yBottomLine, bottom_word, accent, true, TTF_STYLE_ITALIC);
}




static void sync_font_list(App* a) {
    sl_free(&a->fonts);
    a->fonts = list_font_files_in("fonts");

    if (a->fonts.count == 0) {
        sl_push(&a->fonts, "Munro.ttf");
        sl_sort(&a->fonts);
    }

    int idx = sl_find(&a->fonts, a->cfg.font_file);
    a->font_idx = (idx >= 0) ? idx : 0;
    if (a->fonts.count > 0) safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s", a->fonts.items[a->font_idx]);
}

static void sync_bell_list(App* a) {
    sl_free(&a->bell_sounds);
    a->bell_sounds = list_wav_files_in("sounds");

    if (a->bell_sounds.count == 0) {
        sl_push(&a->bell_sounds, "bell.wav");
        sl_sort(&a->bell_sounds);
    }

    int p = sl_find(&a->bell_sounds, a->cfg.bell_phase_file);
    int d = sl_find(&a->bell_sounds, a->cfg.bell_done_file);

    if (p < 0) {
        safe_snprintf(a->cfg.bell_phase_file, sizeof(a->cfg.bell_phase_file), "%s", a->bell_sounds.items[0]);
        p = 0;
    }
    if (d < 0) {
        safe_snprintf(a->cfg.bell_done_file, sizeof(a->cfg.bell_done_file), "%s", a->bell_sounds.items[0]);
        d = 0;
    }

    a->bell_phase_idx = p;
    a->bell_done_idx = d;
}

/* ----------------------------- Background refresh ----------------------------- */

static void refresh_weathers_for_scene(App* a) {
    sl_free(&a->weathers);
    if (a->scenes.count == 0) return;

    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "backgrounds/%s", a->scenes.items[a->scene_idx]);
    a->weathers = list_files_png_in(path);

    if (a->weathers.count == 0) {
        sl_push(&a->weathers, "morning.png");
        sl_push(&a->weathers, "dusk.png");
        sl_push(&a->weathers, "night.png");
        sl_sort(&a->weathers);
    }

    int idx = -1;
    if (a->cfg.weather[0]) {
        char want[256];
        safe_snprintf(want, sizeof(want), "%s.png", a->cfg.weather);
        idx = sl_find(&a->weathers, want);
        if (idx < 0) {
            safe_snprintf(want, sizeof(want), "%s.jpg", a->cfg.weather);
            idx = sl_find(&a->weathers, want);
        }
    }
    a->weather_idx = (idx >= 0) ? idx : 0;
}

static void update_bg_texture(UI* ui, App* a) {
    if (ui->bg_tex) { SDL_DestroyTexture(ui->bg_tex); ui->bg_tex = NULL; }
    if (a->scenes.count == 0 || a->weathers.count == 0) return;

    const char* scene = a->scenes.items[a->scene_idx];
    const char* file  = a->weathers.items[a->weather_idx];

    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "backgrounds/%s/%s", scene, file);
    ui->bg_tex = load_texture(ui->ren, path);

    safe_snprintf(a->scene_name, sizeof(a->scene_name), "%s", scene);

    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s", file);
    char* dot = strrchr(tmp, '.');
    if (dot) *dot = 0;
    safe_snprintf(a->weather_name, sizeof(a->weather_name), "%s", tmp);
}

static void persist_scene_weather(App* a) {
    if (a->scenes.count > 0) {
        safe_snprintf(a->cfg.scene, sizeof(a->cfg.scene), "%s", a->scenes.items[a->scene_idx]);
    }
    if (a->weathers.count > 0) {
        char tmp[256];
        safe_snprintf(tmp, sizeof(tmp), "%s", a->weathers.items[a->weather_idx]);
        char* dot = strrchr(tmp, '.');
        if (dot) *dot = 0;
        safe_snprintf(a->cfg.weather, sizeof(a->cfg.weather), "%s", tmp);
    }
    config_save(&a->cfg, "config.txt");
}

static void persist_colors(App* a) { config_save(&a->cfg, "config.txt"); }
static void persist_fonts(App* a)  { config_save(&a->cfg, "config.txt"); }

/* ----------------------------- Input mapping ----------------------------- */

static void buttons_clear(Buttons* b) { memset(b, 0, sizeof(*b)); }

static void map_button(Buttons* b, SDL_ControllerButtonEvent cbe, int swap_ab) {
    SDL_GameControllerButton cb = (SDL_GameControllerButton)cbe.button;

    if (cb == SDL_CONTROLLER_BUTTON_DPAD_UP) b->up = true;
    if (cb == SDL_CONTROLLER_BUTTON_DPAD_DOWN) b->down = true;
    if (cb == SDL_CONTROLLER_BUTTON_DPAD_LEFT) b->left = true;
    if (cb == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) b->right = true;

    if (cb == SDL_CONTROLLER_BUTTON_START) b->start = true;

    if (cb == SDL_CONTROLLER_BUTTON_BACK) b->select = true;
    if (cb == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) b->l1 = true;
    if (cb == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) b->r1 = true;
    if (cb == SDL_CONTROLLER_BUTTON_LEFTSTICK) b->l3 = true;
    if (cb == SDL_CONTROLLER_BUTTON_RIGHTSTICK) b->r3 = true;

    bool a_press = (cb == SDL_CONTROLLER_BUTTON_A);
    bool b_press = (cb == SDL_CONTROLLER_BUTTON_B);
    if (swap_ab) { bool t = a_press; a_press = b_press; b_press = t; }

    if (a_press) b->a = true;
    if (b_press) b->b = true;

    if (cb == SDL_CONTROLLER_BUTTON_X) b->x = true;
    if (cb == SDL_CONTROLLER_BUTTON_Y) b->y = true;
}

static void map_key(Buttons* b, SDL_KeyboardEvent ke, int swap_ab) {
    SDL_Keycode k = ke.keysym.sym;

    if (k == SDLK_UP) b->up = true;
    if (k == SDLK_DOWN) b->down = true;
    if (k == SDLK_LEFT) b->left = true;
    if (k == SDLK_RIGHT) b->right = true;

    if (k == SDLK_TAB) b->start = true;

    bool a_press = (k == SDLK_RETURN);
    bool b_press = (k == SDLK_ESCAPE || k == SDLK_BACKSPACE);
    if (swap_ab) { bool t = a_press; a_press = b_press; b_press = t; }

    if (a_press) b->a = true;
    if (b_press) b->b = true;

    if (k == SDLK_x) b->x = true;
    if (k == SDLK_y) b->y = true;
}




/* ----------------------------- Timing ----------------------------- */

static void fmt_mmss(char* out, size_t cap, uint32_t sec) {
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    safe_snprintf(out, cap, "%02u:%02u", m, s);
}

static void fmt_hhmm(char* out, size_t cap, uint32_t sec) {
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    safe_snprintf(out, cap, "%02u:%02u", h, m);
}

// Custom timer and stopwatch formatting with seconds, hiding hours when it's 0.
static void fmt_hms_opt_hours(char* out, size_t cap, uint32_t sec) {
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;
    if (h == 0) safe_snprintf(out, cap, "%02u:%02u", m, s);
    else safe_snprintf(out, cap, "%02u:%02u:%02u", h, m, s);
}

static void timer_reset(App* a) {
    a->session_complete = false;
    a->paused = false;
    a->tick_accum = 0.0f;
    a->last_tick_ms = now_ms();

    if (a->mode == MODE_POMODORO) {
        a->pomo_loops_done = 0;
        a->pomo_is_break = false;
        a->pomo_session_in_pomo = 0;
        a->pomo_break_is_long = false;
        if (a->pomo_session_seconds == 0) a->pomo_session_seconds = 25 * 60;
        if (a->pomo_break_seconds == 0) a->pomo_break_seconds = 5 * 60;
        if (a->pomo_long_break_seconds == 0) a->pomo_long_break_seconds = 20 * 60;
        a->pomo_remaining_seconds = a->pomo_session_seconds;
    } else if (a->mode == MODE_CUSTOM) {
        a->custom_remaining_seconds = a->custom_total_seconds;
    } else {
        a->stopwatch_seconds = 0;
    }
}

static void timer_reset_keep_paused(App* a) {
    timer_reset(a);
    a->paused = true;
}

static void timer_toggle_pause(App* a) {
    if (!a->running) return;
    a->paused = !a->paused;
}

static void play_bell_phase(App* a);
static void play_bell_done(App* a);

static void tick_one_second(App* a) {
    if (!a->running || a->paused) return;

    if (a->mode == MODE_POMODORO) {
        if (a->pomo_remaining_seconds > 0) a->pomo_remaining_seconds--;

        if (a->pomo_remaining_seconds == 0) {
            /* Phase ended: ring, then transition. */
            if (a->audio) {
                int total = (a->pomo_loops_total <= 0) ? 1 : a->pomo_loops_total;
                bool is_final = (a->pomo_is_break && a->pomo_break_is_long && ((a->pomo_loops_done + 1) >= total));
                if (is_final) play_bell_done(a);
                else play_bell_phase(a);
            }

            if (!a->pomo_is_break) {
                /* Focus session ended -> go to break.
                   Pomodoro = (session + short break) + (session + long break). */
                a->pomo_is_break = true;

                bool is_second_session = (a->pomo_session_in_pomo != 0);
                a->pomo_break_is_long = is_second_session;

                uint32_t brk = a->pomo_break_is_long ? a->pomo_long_break_seconds : a->pomo_break_seconds;
                if (brk == 0) brk = a->pomo_break_is_long ? (20 * 60) : (5 * 60);
                a->pomo_remaining_seconds = brk;
            } else {
                /* Break ended -> either continue the Pomodoro or finish one. */
                a->pomo_is_break = false;

                if (a->pomo_break_is_long) {
                    /* Long break ends a Pomodoro. */
                    a->pomo_session_in_pomo = 0;
                    a->pomo_break_is_long = false;

                    a->pomo_loops_done++;
                    if (a->pomo_loops_total <= 0) a->pomo_loops_total = 1;

                    if (a->pomo_loops_done >= a->pomo_loops_total) {
                        a->running = false;
                        a->session_complete = true;
                        return;
                    }
                } else {
                    /* Short break ends Session 1, go to Session 2. */
                    a->pomo_session_in_pomo = 1;
                    a->pomo_break_is_long = false;
                }

                a->pomo_remaining_seconds = (a->pomo_session_seconds ? a->pomo_session_seconds : 25 * 60);
            }
        }
    } else if (a->mode == MODE_CUSTOM) {
        if (a->custom_remaining_seconds > 0) a->custom_remaining_seconds--;
        if (a->custom_remaining_seconds == 0) {
            if (a->audio) { play_bell_done(a); }
            a->running = false;
            a->session_complete = true;
        }
    } else {
        a->stopwatch_seconds++;
    }
}

static void app_update(App* a) {
    uint64_t t = now_ms();
    uint64_t dt = t - a->last_tick_ms;
    a->last_tick_ms = t;

    a->tick_accum += (float)dt / 1000.0f;
    while (a->tick_accum >= 1.0f) {
        a->tick_accum -= 1.0f;
        tick_one_second(a);
    }
}


/* ----------------------------- Music helpers ----------------------------- */


static bool is_audio_file_name(const char* name) {
    return ends_with_icase(name, ".mp3") || ends_with_icase(name, ".wav");
}

static void strip_ext_inplace(char* s) {
    if (!s) return;
    char* dot = strrchr(s, '.');
    if (dot) *dot = 0;
}

/* 12-char + "..." shortening (user asked to tweak later) */
#define SONG_LABEL_MAX_CHARS 36

static void format_song_label(const char* filename, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!filename) { safe_snprintf(out, cap, "%s", ""); return; }

    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s", filename);
    strip_ext_inplace(tmp);

    size_t n = strlen(tmp);
    if (n > SONG_LABEL_MAX_CHARS) {
        char cut[256];
        memcpy(cut, tmp, SONG_LABEL_MAX_CHARS);
        cut[SONG_LABEL_MAX_CHARS] = 0;
        safe_snprintf(out, cap, "%s...", cut);
    } else {
        safe_snprintf(out, cap, "%s", tmp);
    }
}

static StrList list_audio_files_in_dir(const char* dir) {
    StrList out = {0};
    DIR* d = opendir(dir);
    if (!d) return out;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full[PATH_MAX];
        safe_snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (!is_file(full)) continue;
        if (!is_audio_file_name(ent->d_name)) continue;

        sl_push(&out, ent->d_name);
    }
    closedir(d);

    sl_sort(&out);
    return out;
}

static bool dir_has_audio_files(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return false;
    bool any = false;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!is_audio_file_name(ent->d_name)) continue;

        char full[PATH_MAX];
        safe_snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (is_file(full)) { any = true; break; }
    }
    closedir(d);
    return any;
}

static void music_dir_from_cfg(const AppConfig* cfg, char* out, size_t cap) {
    const char* name = (cfg && cfg->music_folder[0]) ? cfg->music_folder : "Music";
    if (strcmp(name, "Music") == 0) safe_snprintf(out, cap, "music");
    else safe_snprintf(out, cap, "music/%s", name);
    out[cap - 1] = 0;
}

static void sync_music_folder_list(App* a) {
    sl_free(&a->music_folders);

    /*
       Only show the logical root "Music" entry if it actually contains audio files.
       Otherwise it becomes an empty, confusing option alongside real subfolders.

       If nothing has audio anywhere, we keep a single "Music" entry as a safe fallback
       so the UI/settings don't end up with an empty list.
    */
    const bool root_has_audio = dir_has_audio_files("music");

    StrList subs = list_dirs_in("music");
    int subs_with_audio = 0;
    for (int i = 0; i < subs.count; i++) {
        if (!subs.items[i] || !subs.items[i][0]) continue;

        char dir[PATH_MAX];
        safe_snprintf(dir, sizeof(dir), "music/%s", subs.items[i]);
        if (dir_has_audio_files(dir)) { sl_push(&a->music_folders, subs.items[i]); subs_with_audio++; }
    }
    sl_free(&subs);

    if (root_has_audio || subs_with_audio == 0) sl_push(&a->music_folders, "Music");

    sl_sort(&a->music_folders);

    int idx = sl_find(&a->music_folders, a->cfg.music_folder);
    a->music_folder_idx = (idx >= 0) ? idx : 0;

    if (a->music_folders.count > 0) {
        safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s", a->music_folders.items[a->music_folder_idx]);
    }
}

static void app_music_stop(App* a) {
    if (!a || !a->audio) return;

    audio_engine_stop_music(a->audio);

    sl_free(&a->musicq.tracks);
    a->musicq.idx = 0;
    a->musicq.active = false;
    a->music_has_started = false;

    safe_snprintf(a->music_song, sizeof(a->music_song), "%s", "off");
}

static void app_music_start_idx(App* a, int idx) {
    if (!a || !a->audio) return;
    if (!a->cfg.music_enabled) return;

    char dir[PATH_MAX];
    music_dir_from_cfg(&a->cfg, dir, sizeof(dir));

    if (a->musicq.tracks.count <= 0) return;

    if (idx < 0) idx = a->musicq.tracks.count - 1;
    if (idx >= a->musicq.tracks.count) idx = 0;
    a->musicq.idx = idx;

    char full[PATH_MAX];
    safe_snprintf(full, sizeof(full), "%s/%s", dir, a->musicq.tracks.items[a->musicq.idx]);
    (void)audio_engine_play_music(a->audio, full, true);
    a->music_has_started = true;

    format_song_label(a->musicq.tracks.items[a->musicq.idx], a->music_song, sizeof(a->music_song));
}

static void app_music_build_and_start(App* a) {
    if (!a || !a->audio) return;

    if (!a->cfg.music_enabled) {
        app_music_stop(a);
        return;
    }

    char dir[PATH_MAX];
    music_dir_from_cfg(&a->cfg, dir, sizeof(dir));

    sl_free(&a->musicq.tracks);
    a->musicq.tracks = list_audio_files_in_dir(dir);
    a->musicq.idx = 0;

    if (a->musicq.tracks.count <= 0) {
        app_music_stop(a);
        return;
    }

    a->musicq.active = true;
    a->music_user_paused = true;
    a->music_has_started = false;

    /* Start silent: show first track label, but do not auto-play until user unpauses (R3). */
    audio_engine_stop_music(a->audio);
    format_song_label(a->musicq.tracks.items[0], a->music_song, sizeof(a->music_song));
}

static void app_music_next(App* a) {
    if (!a || !a->audio) return;
    if (!a->cfg.music_enabled) return;
    if (!a->musicq.active || a->musicq.tracks.count <= 0) return;
    app_music_start_idx(a, a->musicq.idx + 1);
}

static void app_music_prev(App* a) {
    if (!a || !a->audio) return;
    if (!a->cfg.music_enabled) return;
    if (!a->musicq.active || a->musicq.tracks.count <= 0) return;
    app_music_start_idx(a, a->musicq.idx - 1);
}

static void app_audio_frame_update(App* a) {
    if (!a || !a->audio) return;

    audio_engine_update(a->audio);

    if (audio_engine_pop_music_ended(a->audio)) {
        app_music_next(a);
    }

    /* Music pause is independent of timer pause. */
    if (a->screen == SCREEN_TIMER) {
        bool paused = a->music_user_paused;
        audio_engine_set_music_paused(a->audio, paused);
    }
}

static void app_music_refresh_labels(App* a) {
    if (!a) return;

    safe_snprintf(a->music_folder, sizeof(a->music_folder), "%s", a->cfg.music_folder[0] ? a->cfg.music_folder : "Music");

    if (!a->cfg.music_enabled) {
        safe_snprintf(a->music_song, sizeof(a->music_song), "%s", "off");
        return;
    }

    if (!a->musicq.active) {
        safe_snprintf(a->music_song, sizeof(a->music_song), "%s", "on");
    }
}

static void play_bell_named(App* a, const char* filename) {
    if (!a || !a->audio || !filename || !filename[0]) return;
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "sounds/%s", filename);
    audio_engine_play_sfx(a->audio, path);
}

static void play_bell_phase(App* a) {
    play_bell_named(a, a->cfg.bell_phase_file);
}

static void play_bell_done(App* a) {
    play_bell_named(a, a->cfg.bell_done_file);
}


/* ----------------------------- Drawing helpers ----------------------------- */

static void draw_top_hud(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    int xL = UI_MARGIN_X;
    int xR = ui->w - UI_MARGIN_X;
    int y1 = UI_MARGIN_TOP;

    char left_main[256] = {0};
    char left_sec[256]  = {0};

    if (a->screen == SCREEN_TIMER) {
        if (a->mode == MODE_POMODORO) {
            int total = (a->pomo_loops_total > 0) ? a->pomo_loops_total : 1;
            int disp  = a->session_complete ? a->pomo_loops_done : (a->pomo_loops_done + 1);
            if (disp < 1) disp = 1;
            if (disp > total) disp = total;

            safe_snprintf(left_main, sizeof(left_main), "Pomodoro (%d/%d)", disp, total);

            if (a->session_complete) safe_snprintf(left_sec, sizeof(left_sec), "session complete!");
            else {
                if (!a->pomo_is_break) safe_snprintf(left_sec, sizeof(left_sec), "focusing");
                else safe_snprintf(left_sec, sizeof(left_sec), a->pomo_break_is_long ? "resting longer" : "resting");
            }
        } else if (a->mode == MODE_CUSTOM) {
            safe_snprintf(left_main, sizeof(left_main), "Timer");
            if (a->session_complete) safe_snprintf(left_sec, sizeof(left_sec), "session complete!");
            else safe_snprintf(left_sec, sizeof(left_sec), "session");
        } else {
            safe_snprintf(left_main, sizeof(left_main), "Stopwatch");
            safe_snprintf(left_sec, sizeof(left_sec), "session");
            if (a->paused) safe_snprintf(left_sec, sizeof(left_sec), "stopped");
        }
    } else {
        safe_snprintf(left_main, sizeof(left_main), "Stillroom");
        safe_snprintf(left_sec, sizeof(left_sec), "a place for measured time");
    }

    /* Upper-left: main + italic secondary */
    draw_inline_left(ui, ui->font_med, ui->font_small, xL, y1, left_main, left_sec, main, accent);

    /* Upper-right: background name (italic) + folder name OR clock (menus only) */
const char* folder_src = a->scene_name[0] ? a->scene_name : "location";
const char* bgname_src = a->weather_name[0] ? a->weather_name : "weather";

char folder_buf[128];
char bg_buf[128];
safe_snprintf(folder_buf, sizeof(folder_buf), "%s", folder_src);
safe_snprintf(bg_buf, sizeof(bg_buf), "%s", bgname_src);
trim_ascii_inplace(folder_buf);
trim_ascii_inplace(bg_buf);

    bool show_battery = a->settings_open;
    bool show_clock = a->timer_menu_open;

    if (show_battery) {
        /* Settings: show battery in words. */
        draw_battery_upper_right_stacked(ui, a, xR, y1);
    } else if (show_clock) {
        /* Select menu: show stacked clock. */
        draw_clock_upper_right_stacked(ui, a, xR, y1);
    } else {
        draw_inline_right(ui, ui->font_med, ui->font_small, xR, y1, bg_buf, folder_buf, accent, main);
    }
}


static void draw_big_time_upper_left(UI* ui, App* a, const char* s) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);

    SDL_Surface* surf = render_text_surface(ui->font_big, s, main);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ui->ren, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    int y2 = UI_MARGIN_TOP + UI_ROW_GAP;
    int y = y2 + UI_ROW_GAP + TIMER_TOP_PAD;
    int x = TIMER_LEFT_X;

    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(ui->ren, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}


static void draw_session_complete_upper_left(UI* ui, App* a, const char* msg) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);

    SDL_Surface* surf = render_text_surface(ui->font_med, msg, main);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ui->ren, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    int y2 = UI_MARGIN_TOP + UI_ROW_GAP;
    int y = y2 + UI_ROW_GAP + TIMER_TOP_PAD + 10;
    int x = TIMER_LEFT_X;

    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(ui->ren, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

/* ----------------------------- Settings headers ----------------------------- */

static const char* settings_title(SettingsView v) {
    switch (v) {
        case SET_VIEW_SCENE:        return "Scene Settings";
        case SET_VIEW_APPEARANCE:   return "Appearance Settings";
        case SET_VIEW_FONTS:        return "Font Settings";
        case SET_VIEW_COLORS:       return "Color Settings";
        case SET_VIEW_FONT_SIZES:   return "Font Size Settings";
        case SET_VIEW_BUTTONMAP:    return "Button Map";
        case SET_VIEW_MUSIC:        return "Music Settings";
        case SET_VIEW_BELL:         return "Bell Settings";
        default:                    return "Settings";
    }
}

/* ----------------------------- Settings drawing ----------------------------- */


static void draw_settings_overlay(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    /* Header */
    int x = UI_MARGIN_X;
    int start_y = UI_MARGIN_TOP + UI_ROW_GAP * 2 + 30;
    draw_text(ui, ui->font_med, x, start_y - (UI_ROW_GAP + 18), "Settings", main, false);

    /* Two-page Button Map */
    if (a->settings_view == SET_VIEW_BUTTONMAP) {
        int y = start_y + 10;

        char title[64];
        safe_snprintf(title, sizeof(title), "Button Map (%d/2)", a->buttonmap_page + 1);
        draw_text(ui, ui->font_small, x, y, title, accent, false);
        y += UI_ROW_GAP + 10;

        if (a->buttonmap_page == 0) {
            draw_text(ui, ui->font_small, x, y, "Timer screen", main, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "A: Pause/Resume", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "Select: Menu", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "Start: Settings", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "X: Hide UI", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "Y: Reset (paused)", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "R3: Pause/Resume", accent, false); y += UI_ROW_GAP;
        } else {
            draw_text(ui, ui->font_small, x, y, "Music", main, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "L3: Music On/Off", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "L1/R1: Prev/Next folder", accent, false); y += UI_ROW_GAP;
            draw_text(ui, ui->font_small, x, y, "L2/R2: Prev/Next song", accent, false); y += UI_ROW_GAP;
        }

        const char* labsL[] = {"Left/Right:"};
        const char* actsL[] = {"Page"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
        return;
    }

    /* Music settings */
    if (a->settings_view == SET_VIEW_MUSIC) {
        int y = start_y + 10;

        char line0[256];
        safe_snprintf(line0, sizeof(line0), "Music: %s", a->cfg.music_enabled ? "On" : "Off");
        SDL_Color c0 = (a->music_sel == 0) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y, line0, c0, false);
        y += UI_ROW_GAP + 6;

        char line1[256];
        safe_snprintf(line1, sizeof(line1), "Folder: %s", a->cfg.music_folder[0] ? a->cfg.music_folder : "Music");
        SDL_Color c1 = (a->music_sel == 1) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y, line1, c1, false);
        y += UI_ROW_GAP + 6;

        const char* labsL[] = {"Up/Down:", "Left/Right:"};
        const char* actsL[] = {"Select", "Change"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 1);
        return;
    }

    /* Bell settings */
    if (a->settings_view == SET_VIEW_BELL) {
        int y = start_y + 10;

        char s0[256];
        char s1[256];
        safe_snprintf(s0, sizeof(s0), "Phase Bell:   %s", a->cfg.bell_phase_file[0] ? a->cfg.bell_phase_file : "bell.wav");
        safe_snprintf(s1, sizeof(s1), "Done Bell:    %s", a->cfg.bell_done_file[0] ? a->cfg.bell_done_file : "bell.wav");

        SDL_Color c0 = (a->bell_sel == 0) ? highlight : accent;
        SDL_Color c1 = (a->bell_sel == 1) ? highlight : accent;

        draw_text(ui, ui->font_small, x, y, s0, c0, false); y += UI_ROW_GAP + 6;
        draw_text(ui, ui->font_small, x, y, s1, c1, false); y += UI_ROW_GAP + 6;

        const char* labsL[] = {"Up/Down:", "Left/Right:", "A:"};
        const char* actsL[] = {"Select", "Change", "Preview"};
        const char* labsR[] = {"B:"};
        const char* actsR[] = {"Back"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 3, labsR, actsR, 1);
        return;
    }

    /* Scene settings */
    if (a->settings_view == SET_VIEW_SCENE) {
        int y = start_y + 10;

        char l0[256], l1[256];
        safe_snprintf(l0, sizeof(l0), "Location: %s", a->scene_name[0] ? a->scene_name : ""); 
        safe_snprintf(l1, sizeof(l1), "Weather:   %s", a->weather_name[0] ? a->weather_name : "");

        SDL_Color c0 = (a->scene_sel == 0) ? highlight : accent;
        SDL_Color c1 = (a->scene_sel == 1) ? highlight : accent;

        draw_text(ui, ui->font_small, x, y, l0, c0, false); y += UI_ROW_GAP + 6;
        draw_text(ui, ui->font_small, x, y, l1, c1, false); y += UI_ROW_GAP + 6;

        const char* labsL[] = {"Up/Down:", "Left/Right:"};
        const char* actsL[] = {"Select", "Change"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 1);
        return;
    }

    /* Appearance settings */
    if (a->settings_view == SET_VIEW_APPEARANCE) {
        int y = start_y + 10;
        const char* items[2] = {"Fonts", "Colors"};
        for (int i = 0; i < 2; i++) {
            SDL_Color c = (i == a->appearance_sel) ? highlight : accent;
            draw_text(ui, ui->font_small, x, y, items[i], c, false);
            y += UI_ROW_GAP + 6;
        }

        const char* labsL[] = {"A:"};
        const char* actsL[] = {"Select"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
        return;
    }

    /* Fonts settings */
    if (a->settings_view == SET_VIEW_FONTS) {
        int y = start_y + 10;

        char line0[256];
        safe_snprintf(line0, sizeof(line0), "Font: %s", a->cfg.font_file);
        SDL_Color c0 = (a->fonts_sel == 0) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y, line0, c0, false); y += UI_ROW_GAP + 6;

        SDL_Color c1 = (a->fonts_sel == 1) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y, "Font Sizes", c1, false); y += UI_ROW_GAP + 6;

        const char* labsL[] = {"Up/Down:", "Left/Right:", "A:"};
        const char* actsL[] = {"Select", "Change", "Enter"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 3, labsR, actsR, 1);
        return;
    }

    /* Color settings */
    if (a->settings_view == SET_VIEW_COLORS) {
        int y = start_y + 10;

        const char* titles[3] = {"Main color", "Primary accent", "Highlight"};
        int idxs[3] = {a->cfg.main_color_idx, a->cfg.accent_color_idx, a->cfg.highlight_color_idx};

        for (int i = 0; i < 3; i++) {
            char line[256];
            safe_snprintf(line, sizeof(line), "%s: %s", titles[i], PALETTE[idxs[i]].name);
            SDL_Color c = (i == a->colors_sel) ? highlight : accent;
            draw_text(ui, ui->font_small, x, y, line, c, false);
            y += UI_ROW_GAP + 6;
        }

        const char* labsL[] = {"Left/Right:"};
        const char* actsL[] = {"Change"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
        return;
    }

    /* Font size settings */
    if (a->settings_view == SET_VIEW_FONT_SIZES) {
        int y = start_y + 10;

        char s0[128], s1[128], s2[128];
        safe_snprintf(s0, sizeof(s0), "Small:  %d", a->cfg.font_small_pt);
        safe_snprintf(s1, sizeof(s1), "Medium: %d", a->cfg.font_med_pt);
        safe_snprintf(s2, sizeof(s2), "Big:    %d", a->cfg.font_big_pt);

        SDL_Color c0 = (a->sizes_sel == 0) ? highlight : accent;
        SDL_Color c1 = (a->sizes_sel == 1) ? highlight : accent;
        SDL_Color c2 = (a->sizes_sel == 2) ? highlight : accent;

        draw_text(ui, ui->font_small, x, y, s0, c0, false); y += UI_ROW_GAP + 6;
        draw_text(ui, ui->font_small, x, y, s1, c1, false); y += UI_ROW_GAP + 6;
        draw_text(ui, ui->font_small, x, y, s2, c2, false); y += UI_ROW_GAP + 6;

        const char* labsL[] = {"Left/Right:"};
        const char* actsL[] = {"Change"};
        const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
        return;
    }

    /* Main settings */
    const char* items[5] = {"Scene", "Appearance", "Music", "Bells", "Button Map"};
    int y = start_y + 10;
    for (int i = 0; i < 5; i++) {
        SDL_Color c = (i == a->settings_sel) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y, items[i], c, false);
        y += UI_ROW_GAP + 6;
    }

    const char* labsL[] = {"A:"};
    const char* actsL[] = {"Select"};
    const char* labsR[] = {"B/Start:"};
        const char* actsR[] = {"Close"};
        draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
}


static void draw_menu(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    draw_top_hud(ui, a);

    const char* items[] = { "Timer", "Stopwatch", "Pomodoro", "Tasks", "Exit" };
    int n = 5;

    /* Match Settings placement (left-aligned). */
    int x = UI_MARGIN_X;
    int start_y = UI_MARGIN_TOP + UI_ROW_GAP * 2 + 30;
    int y = start_y + 10;

    draw_text(ui, ui->font_med, x, start_y - (UI_ROW_GAP + 18), "Menu", main, false);

    for (int i = 0; i < n; i++) {
        SDL_Color col = (i == a->menu_sel) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y + i*(UI_ROW_GAP + 8), items[i], col, false);
    }

    /* Root menu: Exit is only via selecting "Exit" + A, so don't show a B quit hint. */
    const char* labsL[] = {"D-Pad:", "A:"};
    const char* actsL[] = {"Navigate", "Select"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, NULL, NULL, 0);
}

static void draw_tasks_pick(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    draw_top_hud(ui, a);

    int x = UI_MARGIN_X;
    int start_y = UI_MARGIN_TOP + UI_ROW_GAP * 2 + 30;
    int y = start_y + 10;

    draw_text(ui, ui->font_med, x, start_y - (UI_ROW_GAP + 18), "Tasks", main, false);

    const char* items[] = { "Daily", "Weekly", "Back" };
    for (int i = 0; i < 3; i++) {
        SDL_Color col = (i == a->tasks_pick_sel) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y + i*(UI_ROW_GAP + 8), items[i], col, false);
    }

    const char* labsL[] = {"D-Pad:", "A:"};
    const char* actsL[] = {"Navigate", "Select"};
    const char* labsR[] = {"B:"};
    const char* actsR[] = {"Back"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 1);
}

static void draw_tasks_list(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

    draw_top_hud(ui, a);

    const StrList* list = (a->tasks_kind == 0) ? &a->tasks_daily : &a->tasks_weekly;
    const char* title = (a->tasks_kind == 0) ? "Daily" : "Weekly";

    int x = UI_MARGIN_X;
    int header_y = UI_MARGIN_TOP + 40;
    draw_text(ui, ui->font_med, x, header_y, title, main, false);

    int y0 = header_y + 80;
    int row_h = UI_ROW_GAP - 10;

    if (list->count == 0) {
        draw_text_style(ui, ui->font_small, x, y0, "No tasks", accent, false, TTF_STYLE_ITALIC);
    } else {
        for (int i = 0; i < list->count; i++) {
            const char* t = list->items[i];
            bool done = task_is_done(a, a->tasks_kind, t);
            SDL_Color col = (i == a->tasks_sel) ? highlight : accent;
            int style = 0;
            if (done) {
                col = accent;
                style |= TTF_STYLE_ITALIC;
            }

            int y = y0 + i * row_h;

            /* Selection highlight bar */
            if (i == a->tasks_sel) {
                int tw = text_width(ui->font_small, t);
                SDL_Color hl = highlight;
                SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ui->ren, hl.r, hl.g, hl.b, 55);
                SDL_Rect r = { x - 10, y - 6, tw + 20, row_h };
                SDL_RenderFillRect(ui->ren, &r);
                SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
            }

            draw_text_style(ui, ui->font_small, x, y, t, col, false, style);

            if (done) {
                /* Strikethrough, baseline-based so it doesn't drift with font size. */
                int asc = TTF_FontAscent(ui->font_small);
                int strike_y = y + (asc * 55) / 100; /* ~0.55 of ascent from top */
                int tw = text_width(ui->font_small, t);
                SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ui->ren, col.r, col.g, col.b, 220);
                SDL_RenderDrawLine(ui->ren, x, strike_y, x + tw, strike_y);
                SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
            }
        }
    }

    const char* labsL[] = {"A:", "X:"};
    const char* actsL[] = {"Done", "Reset"};
    const char* labsR[] = {"B:"};
    const char* actsR[] = {"Back"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 1);
}

static void draw_pomo_picker(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);

    draw_top_hud(ui, a);

    draw_text(ui, ui->font_med, UI_MARGIN_X, ui->h/2 - 210, "Pomodoro", main, false);

    int y = ui->h/2 - 120;

    char l0[128], l1[128], l2[128], l3[128];
    safe_snprintf(l0, sizeof(l0), "Session:      %d min", a->pick_pomo_session_min);
    safe_snprintf(l1, sizeof(l1), "Short Break:  %d min", a->pick_pomo_break_min);
    safe_snprintf(l2, sizeof(l2), "Long Break:   %d min", a->pick_pomo_long_break_min);
    safe_snprintf(l3, sizeof(l3), "Pomodoro:     %d", a->pick_pomo_loops);

    SDL_Color c0 = (a->pomo_pick_sel == 0) ? highlight : accent;
    SDL_Color c1 = (a->pomo_pick_sel == 1) ? highlight : accent;
    SDL_Color c2 = (a->pomo_pick_sel == 2) ? highlight : accent;
    SDL_Color c3 = (a->pomo_pick_sel == 3) ? highlight : accent;

    draw_text(ui, ui->font_small, UI_MARGIN_X, y, l0, c0, false); y += UI_ROW_GAP + 6;
    draw_text(ui, ui->font_small, UI_MARGIN_X, y, l1, c1, false); y += UI_ROW_GAP + 6;
    draw_text(ui, ui->font_small, UI_MARGIN_X, y, l2, c2, false); y += UI_ROW_GAP + 6;
    draw_text(ui, ui->font_small, UI_MARGIN_X, y, l3, c3, false);

    /* Minimal summary line (italic) */
    int focus_total_min = 2 * a->pick_pomo_session_min * a->pick_pomo_loops;
    int rest_total_min  = (a->pick_pomo_break_min + a->pick_pomo_long_break_min) * a->pick_pomo_loops;
    char summary[128];
    safe_snprintf(summary, sizeof(summary), "focusing for %d minutes and resting for %d.", focus_total_min, rest_total_min);
    draw_text_style(ui, ui->font_small, UI_MARGIN_X, y + UI_ROW_GAP + 48, summary, accent, false, TTF_STYLE_ITALIC);


    const char* labsL[] = {"A:"};
    const char* actsL[] = {"Start"};
    const char* labsR[] = {"B:"};
    const char* actsR[] = {"Back"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);

    const char* labsC[] = {"Up/Down:"};
    const char* actsC[] = {"Select"};
    draw_hint_pairs_center(ui, main, accent, labsC, actsC, 1);
}

static void draw_custom_picker(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);

    draw_top_hud(ui, a);

    int header_y = ui->h/2 - 210;
    int big_y    = ui->h/2 - 80;
    int label_y  = big_y - 44;

    draw_text(ui, ui->font_med, UI_MARGIN_X, header_y, "Timer", main, false);

    char hh[8], mm[8], ss[8];
    safe_snprintf(hh, sizeof(hh), "%02d", a->pick_custom_hours);
    safe_snprintf(mm, sizeof(mm), "%02d", a->pick_custom_minutes);
    safe_snprintf(ss, sizeof(ss), "%02d", a->pick_custom_seconds);

    SDL_Color cHH = (a->custom_field_sel == 0) ? highlight : accent;
    SDL_Color cMM = (a->custom_field_sel == 1) ? highlight : accent;
    SDL_Color cSS = (a->custom_field_sel == 2) ? highlight : accent;

    char full[32];
    safe_snprintf(full, sizeof(full), "%s:%s:%s", hh, mm, ss);

    
    int big_left  = UI_MARGIN_X;

    draw_text(ui, ui->font_big, big_left, big_y, full, main, false);

int wHH = 0, hHH = 0;
int wColon = 0, hColon = 0;
int wMM = 0, hMM = 0;
int wSS = 0, hSS = 0;
TTF_SizeUTF8(ui->font_big, hh, &wHH, &hHH);
TTF_SizeUTF8(ui->font_big, ":", &wColon, &hColon);
TTF_SizeUTF8(ui->font_big, mm, &wMM, &hMM);
TTF_SizeUTF8(ui->font_big, ss, &wSS, &hSS);

int cxHH = big_left + (wHH / 2);
int cxMM = big_left + wHH + wColon + (wMM / 2);
int cxSS = big_left + wHH + wColon + wMM + wColon + (wSS / 2);

int wLabHH = text_width(ui->font_small, "hh");
int wLabMM = text_width(ui->font_small, "mm");
int wLabSS = text_width(ui->font_small, "ss");

draw_text(ui, ui->font_small, cxHH - (wLabHH / 2), label_y, "hh", cHH, false);
draw_text(ui, ui->font_small, cxMM - (wLabMM / 2), label_y, "mm", cMM, false);
draw_text(ui, ui->font_small, cxSS - (wLabSS / 2), label_y, "ss", cSS, false);

    const char* labs[] = {"A:", "B:"};
    const char* acts[] = {"Start", "Back"};
    draw_hint_pairs_center(ui, main, accent, labs, acts, 2);
}

static void draw_timer_quick_menu(UI* ui, App* a);
static void draw_timer(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
    /* Top HUD: normally shown, but hidden when Y-toggle is active (unless settings overlay is open). */
    if (!a->hud_hidden || a->settings_open) {
        draw_top_hud(ui, a);
    }

    /* When settings are open, hide only the big time section (and other main-screen
       elements like music label), then draw the settings overlay on top. */
    if (a->settings_open) {
        draw_settings_overlay(ui, a);
        return;
    }

    /* If the Timer quick menu is open, behave like Settings: don't draw the main
       timer content behind it (prevents "Menu" and big time from overlapping). */
    if (a->timer_menu_open) {
        draw_timer_quick_menu(ui, a);
        return;
    }

    char t[64] = {0};
    if (a->mode == MODE_POMODORO) fmt_mmss(t, sizeof(t), a->pomo_remaining_seconds);
    else if (a->mode == MODE_CUSTOM) fmt_hms_opt_hours(t, sizeof(t), a->custom_remaining_seconds);
    else fmt_hms_opt_hours(t, sizeof(t), a->stopwatch_seconds);

    /* Big time / completion state.
       When a session is complete, the big timer should disappear and we show a
       completion message instead. Starting a new session clears session_complete
       in the relevant handlers (custom/pomodoro), so the timer won't stay hidden
       for the next run. */
    if (a->session_complete && (a->mode == MODE_CUSTOM || a->mode == MODE_POMODORO)) {
        /* Big timer hidden on completion; top HUD shows "session complete!". */
    } else {
        draw_big_time_upper_left(ui, a, t);
    }


    if (!a->hud_hidden) {
        const char* folder_src = a->music_folder[0] ? a->music_folder : "Music";
        const char* song_src   = a->music_song[0]   ? a->music_song   : "off";

        char folder_buf[128];
        char song_buf[128];
        safe_snprintf(folder_buf, sizeof(folder_buf), "%s", folder_src);
        safe_snprintf(song_buf,   sizeof(song_buf),   "%s", song_src);

        trim_ascii_inplace(folder_buf);
        trim_ascii_inplace(song_buf);
        ascii_lower_inplace(song_buf);

        const int baseline_y = ui_bottom_baseline_y(ui);

        int asc_med = TTF_FontAscent(ui->font_med);
        int asc_small = TTF_FontAscent(ui->font_small);
        int y_music = baseline_y - (asc_med > asc_small ? asc_med : asc_small);

/* Bottom-right: song (italic, small, accent) + folder (med, main). */
        int xR = ui->w - UI_MARGIN_X;
        draw_inline_right(ui, ui->font_med, ui->font_small, xR, y_music, song_buf, folder_buf, accent, main);
        if (a->music_user_paused && a->cfg.music_enabled) {
            int w_song = 0, h_song = 0;
            int w_folder = 0, h_folder = 0;
            TTF_SizeUTF8(ui->font_small, song_buf, &w_song, &h_song);
            TTF_SizeUTF8(ui->font_med,   folder_buf, &w_folder, &h_folder);

            int gap = (song_buf[0] && folder_buf[0]) ? UI_INLINE_GAP_PX : 0;
            int total_w = w_song + gap + w_folder;
            int x_song = xR - total_w;

            int asc_med  = TTF_FontAscent(ui->font_med);


            int asc_song = TTF_FontAscent(ui->font_small);


            int baseline = baseline_y;


            int y_song_top = baseline - asc_song;



            draw_strikethrough(ui, x_song, y_song_top, w_song, h_song, accent);
        }
    }

    if (!a->hud_hidden) {
        if (a->paused) {
            /* Text-only swap request: show Reset on Y (text only), without changing behavior. */
            const char* labsL[] = {"A:", "Y:"};
            const char* actsL[] = {"Resume", "Reset"};
            draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, NULL, NULL, 0);
        } else {
            const char* labsL[] = {"A:"};
            const char* actsL[] = {"Pause"};
            /* Timer screen: keep hints minimal. Menu is still available on Select,
               but we intentionally do not display it here. */
            draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, NULL, NULL, 0);
        }
    }
}

/* ----------------------------- Scene/weather/font cycling ----------------------------- */

static void cycle_location(UI* ui, App* a, int dir) {
    if (a->scenes.count <= 0) return;

    a->scene_idx += dir;
    if (a->scene_idx < 0) a->scene_idx = a->scenes.count - 1;
    if (a->scene_idx >= a->scenes.count) a->scene_idx = 0;

    refresh_weathers_for_scene(a);
    update_bg_texture(ui, a);
    persist_scene_weather(a);
}

static void cycle_weather(UI* ui, App* a, int dir) {
    if (a->weathers.count <= 0) return;

    a->weather_idx += dir;
    if (a->weather_idx < 0) a->weather_idx = a->weathers.count - 1;
    if (a->weather_idx >= a->weathers.count) a->weather_idx = 0;

    update_bg_texture(ui, a);
    persist_scene_weather(a);
}

static void cycle_palette_idx(int* idx, int dir) {
    int v = *idx + dir;
    if (v < 0) v = 23;
    if (v > 23) v = 0;
    *idx = v;
}

static void cycle_font(UI* ui, App* a, int dir) {
    if (a->fonts.count <= 0) return;

    int prev = a->font_idx;

    a->font_idx += dir;
    if (a->font_idx < 0) a->font_idx = a->fonts.count - 1;
    if (a->font_idx >= a->fonts.count) a->font_idx = 0;

    safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s", a->fonts.items[a->font_idx]);

    AppConfig backup = a->cfg;
    if (!ui_open_fonts(ui, &a->cfg)) {
        a->cfg = backup;
        a->font_idx = prev;
        safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s", a->fonts.items[a->font_idx]);
        ui_open_fonts(ui, &a->cfg);
        return;
    }

    persist_fonts(a);
}

static void clamp_sizes(App* a) {
    if (a->cfg.font_small_pt < 18) a->cfg.font_small_pt = 18;
    if (a->cfg.font_med_pt   < 18) a->cfg.font_med_pt   = 18;
    if (a->cfg.font_big_pt   < 30) a->cfg.font_big_pt   = 30;

    if (a->cfg.font_small_pt > 96)  a->cfg.font_small_pt = 96;
    if (a->cfg.font_med_pt   > 120) a->cfg.font_med_pt   = 120;
    if (a->cfg.font_big_pt   > 220) a->cfg.font_big_pt   = 220;

    if (a->cfg.font_small_pt > a->cfg.font_med_pt) a->cfg.font_med_pt = a->cfg.font_small_pt;
    if (a->cfg.font_med_pt > a->cfg.font_big_pt) a->cfg.font_big_pt = a->cfg.font_med_pt;
}

static void change_size(UI* ui, App* a, int which, int delta) {
    int prevS = a->cfg.font_small_pt;
    int prevM = a->cfg.font_med_pt;
    int prevB = a->cfg.font_big_pt;

    if (which == 0) a->cfg.font_small_pt += delta;
    else if (which == 1) a->cfg.font_med_pt += delta;
    else a->cfg.font_big_pt += delta;

    clamp_sizes(a);

    if (!ui_open_fonts(ui, &a->cfg)) {
        a->cfg.font_small_pt = prevS;
        a->cfg.font_med_pt   = prevM;
        a->cfg.font_big_pt   = prevB;
        ui_open_fonts(ui, &a->cfg);
        return;
    }

    persist_fonts(a);
}

/* ----------------------------- Screen input ----------------------------- */


static void handle_timer_quick_menu(UI* ui, App* a, Buttons* b) {
    (void)ui;
    if (b->b || b->select) { a->timer_menu_open = false; return; }

    if (b->up) a->menu_sel = (a->menu_sel + 3) % 4;
    if (b->down) a->menu_sel = (a->menu_sel + 1) % 4;

    if (b->a) {
        a->timer_menu_open = false;
        if (a->menu_sel == 0) {
            /* Timer */
            a->screen = SCREEN_CUSTOM_PICK;
            a->pick_custom_hours = a->cfg.last_timer_h;
            a->pick_custom_minutes = a->cfg.last_timer_m;
            a->pick_custom_seconds = a->cfg.last_timer_s;
            a->custom_field_sel = 0;
            /* leaving timer screen: stop music */
            app_music_stop(a);
        } else if (a->menu_sel == 1) {
            /* Stopwatch */
            a->mode = MODE_STOPWATCH;
            a->screen = SCREEN_TIMER;
            a->running = true;
            a->session_complete = false;
            a->paused = false;
            a->tick_accum = 0.0f;
            a->last_tick_ms = now_ms();
            a->stopwatch_seconds = 0;
        } else if (a->menu_sel == 2) {
            /* Pomodoro setup */
            a->screen = SCREEN_POMO_PICK;
            a->pomo_pick_sel = 0;
            a->pick_pomo_session_min = a->cfg.last_pomo_session_min;
            a->pick_pomo_break_min = a->cfg.last_pomo_short_break_min;
            a->pick_pomo_long_break_min = a->cfg.last_pomo_long_break_min;
            a->pick_pomo_loops = a->cfg.last_pomo_loops;
            app_music_stop(a);
        } else if (a->menu_sel == 3) {
            /* Exit: quit the app immediately.
               NOTE: App->running controls timer run state (countdown/stopwatch), so do NOT touch it here. */
            a->quit_requested = true;
            a->timer_menu_open = false;
            app_music_stop(a);
        }
    }
}

static void draw_timer_quick_menu(UI* ui, App* a) {
    SDL_Color main = color_from_idx(a->cfg.main_color_idx);
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
    SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);

    const char* items[] = {"Timer", "Stopwatch", "Pomodoro", "Exit"};
    int n = 4;

    /* Match Settings placement (left-aligned). */
    int x = UI_MARGIN_X;
    int start_y = UI_MARGIN_TOP + UI_ROW_GAP * 2 + 30;
    int y = start_y + 10;

    draw_text(ui, ui->font_med, x, start_y - (UI_ROW_GAP + 18), "Menu", main, false);

    for (int i = 0; i < n; i++) {
        SDL_Color c = (i == a->menu_sel) ? highlight : accent;
        draw_text(ui, ui->font_small, x, y + i*(UI_ROW_GAP + 8), items[i], c, false);
    }

    const char* labsL[] = {"A:"};
    const char* actsL[] = {"Select"};
    const char* labsR[] = {"B/Select:"};
    const char* actsR[] = {"Close"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
}
static void handle_menu(App* a, Buttons* b) {
    if (b->up) a->menu_sel = (a->menu_sel + 4) % 5;
    if (b->down) a->menu_sel = (a->menu_sel + 1) % 5;

    if (b->a) {
        if (a->menu_sel == 0) {
            /* Timer */
            a->screen = SCREEN_CUSTOM_PICK;
            a->pick_custom_hours = a->cfg.last_timer_h;
            a->pick_custom_minutes = a->cfg.last_timer_m;
            a->pick_custom_seconds = a->cfg.last_timer_s;
            a->custom_field_sel = 0;
        } else if (a->menu_sel == 1) {
            /* Stopwatch */
            a->mode = MODE_STOPWATCH;
            a->screen = SCREEN_TIMER;
            a->running = true;
            a->session_complete = false;
            a->paused = false;
            a->tick_accum = 0.0f;
            a->last_tick_ms = now_ms();
            a->stopwatch_seconds = 0;
        } else if (a->menu_sel == 2) {
            /* Pomodoro setup */
            a->screen = SCREEN_POMO_PICK;
            a->pomo_pick_sel = 0;
            a->pick_pomo_session_min = a->cfg.last_pomo_session_min;
            a->pick_pomo_break_min = a->cfg.last_pomo_short_break_min;
            a->pick_pomo_long_break_min = a->cfg.last_pomo_long_break_min;
            a->pick_pomo_loops = a->cfg.last_pomo_loops;
        } else if (a->menu_sel == 3) {
            /* Tasks */
            tasks_reload(a);
            a->screen = SCREEN_TASKS_PICK;
            a->tasks_pick_sel = 0;
            a->tasks_sel = 0;
            a->tasks_kind = 0;
        } else if (a->menu_sel == 4) {
            /* Exit */
            a->quit_requested = true;
        }
    }
}

static void handle_tasks_pick(App* a, Buttons* b) {
    if (b->b) { a->screen = SCREEN_MENU; return; }

    if (b->up)   a->tasks_pick_sel = (a->tasks_pick_sel + 2) % 3;
    if (b->down) a->tasks_pick_sel = (a->tasks_pick_sel + 1) % 3;

    if (b->a) {
        if (a->tasks_pick_sel == 0) {
            a->tasks_kind = 0;
            a->tasks_sel = 0;
            a->screen = SCREEN_TASKS_LIST;
        } else if (a->tasks_pick_sel == 1) {
            a->tasks_kind = 1;
            a->tasks_sel = 0;
            a->screen = SCREEN_TASKS_LIST;
        } else {
            a->screen = SCREEN_MENU;
        }
    }
}

static void handle_tasks_list(App* a, Buttons* b) {
    if (b->b) { a->screen = SCREEN_TASKS_PICK; return; }

    StrList* list = (a->tasks_kind == 0) ? &a->tasks_daily : &a->tasks_weekly;
    if (list->count > 0) {
        if (b->up)   a->tasks_sel = (a->tasks_sel + list->count - 1) % list->count;
        if (b->down) a->tasks_sel = (a->tasks_sel + 1) % list->count;

        if (b->a) {
            tasks_toggle_done(a, a->tasks_kind, list->items[a->tasks_sel]);
        }
    }

    if (b->x) {
        tasks_reset(a, a->tasks_kind);
    }
}

static void handle_pomo_pick(App* a, Buttons* b) {
    if (b->up) a->pomo_pick_sel = (a->pomo_pick_sel + 3) % 4;
    if (b->down) a->pomo_pick_sel = (a->pomo_pick_sel + 1) % 4;

    int dir = 0;
    if (b->right) dir = 1;
    if (b->left) dir = -1;

    if (dir != 0) {
        if (a->pomo_pick_sel == 0) {
            a->pick_pomo_session_min += dir;
            if (a->pick_pomo_session_min < 1) a->pick_pomo_session_min = 1;
            if (a->pick_pomo_session_min > 180) a->pick_pomo_session_min = 180;
        } else if (a->pomo_pick_sel == 1) {
            a->pick_pomo_break_min += dir;
            if (a->pick_pomo_break_min < 1) a->pick_pomo_break_min = 1;
            if (a->pick_pomo_break_min > 60) a->pick_pomo_break_min = 60;
        } else if (a->pomo_pick_sel == 2) {
            a->pick_pomo_long_break_min += dir;
            if (a->pick_pomo_long_break_min < 1) a->pick_pomo_long_break_min = 1;
            if (a->pick_pomo_long_break_min > 120) a->pick_pomo_long_break_min = 120;
        } else {
            a->pick_pomo_loops += dir;
            if (a->pick_pomo_loops < 1) a->pick_pomo_loops = 1;
            if (a->pick_pomo_loops > 12) a->pick_pomo_loops = 12;
        }
    }

    if (b->a) {
        a->mode = MODE_POMODORO;
        a->cfg.last_pomo_session_min = a->pick_pomo_session_min;
        a->cfg.last_pomo_short_break_min = a->pick_pomo_break_min;
        a->cfg.last_pomo_long_break_min = a->pick_pomo_long_break_min;
        a->cfg.last_pomo_loops = a->pick_pomo_loops;
        config_save(&a->cfg, "config.txt");

        a->pomo_loops_total = a->pick_pomo_loops;
        a->pomo_loops_done = 0;
        a->pomo_is_break = false;
        a->pomo_session_in_pomo = 0;
        a->pomo_break_is_long = false;

        a->pomo_session_seconds = (uint32_t)(a->pick_pomo_session_min * 60);
        a->pomo_break_seconds = (uint32_t)(a->pick_pomo_break_min * 60);
        a->pomo_long_break_seconds = (uint32_t)(a->pick_pomo_long_break_min * 60);
        a->pomo_session_in_pomo = 0;
        a->pomo_break_is_long = false;
        if (a->pomo_session_seconds == 0) a->pomo_session_seconds = 60;
        if (a->pomo_break_seconds == 0) a->pomo_break_seconds = 60;

        a->pomo_remaining_seconds = a->pomo_session_seconds;

        a->screen = SCREEN_TIMER;
        a->running = true;
        a->session_complete = false;
        a->paused = false;
        a->tick_accum = 0.0f;
        a->last_tick_ms = now_ms();
    }

    if (b->b) a->screen = SCREEN_MENU;
}

static void cycle_custom_value(App* a, int dir) {
    if (a->custom_field_sel == 0) {
        int v = a->pick_custom_hours + dir;
        if (v < 0) v = 24;
        if (v > 24) v = 0;
        a->pick_custom_hours = v;
    } else if (a->custom_field_sel == 1) {
        int v = a->pick_custom_minutes + dir;
        if (v < 0) v = 59;
        if (v > 59) v = 0;
        a->pick_custom_minutes = v;
    } else {
        int v = a->pick_custom_seconds + dir;
        if (v < 0) v = 59;
        if (v > 59) v = 0;
        a->pick_custom_seconds = v;
    }
}

static void handle_custom_pick(App* a, Buttons* b) {
    if (b->left) a->custom_field_sel = (a->custom_field_sel + 2) % 3;
    if (b->right) a->custom_field_sel = (a->custom_field_sel + 1) % 3;

    if (b->up) cycle_custom_value(a, +1);
    if (b->down) cycle_custom_value(a, -1);

    if (b->a) {
        a->mode = MODE_CUSTOM;
        a->cfg.last_timer_h = a->pick_custom_hours;
        a->cfg.last_timer_m = a->pick_custom_minutes;
        a->cfg.last_timer_s = a->pick_custom_seconds;
        config_save(&a->cfg, "config.txt");


        /* Starting a new timer run should always clear the completion flag. */
        a->session_complete = false;

        uint32_t total = (uint32_t)(a->pick_custom_hours * 3600 + a->pick_custom_minutes * 60 + a->pick_custom_seconds);
        a->custom_total_seconds = total;

        a->screen = SCREEN_TIMER;

        a->running = true;
        a->paused = false;
        a->tick_accum = 0.0f;
        a->last_tick_ms = now_ms();

        a->custom_remaining_seconds = a->custom_total_seconds;
        if (a->custom_total_seconds == 0) {
            a->running = false;
            a->session_complete = true;
        }
    }
    if (b->b) a->screen = SCREEN_MENU;
}

static void handle_settings(UI* ui, App* a, Buttons* b) {
    if (b->start) {
        a->settings_open = false;
        a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_BUTTONMAP) {
        if (b->left || b->right) {
            a->buttonmap_page = 1 - a->buttonmap_page;
        }
        if (b->b) a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_MUSIC) {
        if (b->up || b->down) a->music_sel = (a->music_sel + 1) % 2;

        if (b->left || b->right) {
            int dir = b->right ? 1 : -1;

            if (a->music_sel == 0) {
                /* Toggle enable */
                a->cfg.music_enabled = a->cfg.music_enabled ? 0 : 1;
                if (a->cfg.music_enabled) app_music_build_and_start(a);
                else app_music_stop(a);
            } else {
                /* Cycle folder */
                if (a->music_folders.count > 0) {
                    a->music_folder_idx += dir;
                    if (a->music_folder_idx < 0) a->music_folder_idx = a->music_folders.count - 1;
                    if (a->music_folder_idx >= a->music_folders.count) a->music_folder_idx = 0;

                    safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s", a->music_folders.items[a->music_folder_idx]);
                    app_music_build_and_start(a);
                }
            }
            app_music_refresh_labels(a);
        }

        if (b->b) a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_BELL) {
        if (b->up) a->bell_sel = (a->bell_sel + 1) % 2;
        if (b->down) a->bell_sel = (a->bell_sel + 1) % 2;

        if (b->left || b->right) {
            int dir = b->right ? 1 : -1;
            if (a->bell_sounds.count > 0) {
                if (a->bell_sel == 0) {
                    a->bell_phase_idx += dir;
                    if (a->bell_phase_idx < 0) a->bell_phase_idx = a->bell_sounds.count - 1;
                    if (a->bell_phase_idx >= a->bell_sounds.count) a->bell_phase_idx = 0;
                    safe_snprintf(a->cfg.bell_phase_file, sizeof(a->cfg.bell_phase_file), "%s", a->bell_sounds.items[a->bell_phase_idx]);
                } else {
                    a->bell_done_idx += dir;
                    if (a->bell_done_idx < 0) a->bell_done_idx = a->bell_sounds.count - 1;
                    if (a->bell_done_idx >= a->bell_sounds.count) a->bell_done_idx = 0;
                    safe_snprintf(a->cfg.bell_done_file, sizeof(a->cfg.bell_done_file), "%s", a->bell_sounds.items[a->bell_done_idx]);
                }
                config_save(&a->cfg, "config.txt");
            }
        }

        if (b->a) {
            if (a->bell_sel == 0) play_bell_phase(a);
            else play_bell_done(a);
        }

        if (b->b) a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_SCENE) {
        if (b->up) a->scene_sel = (a->scene_sel + 1) % 2;
        if (b->down) a->scene_sel = (a->scene_sel + 1) % 2;

        if (b->left || b->right) {
            int dir = b->right ? 1 : -1;
            if (a->scene_sel == 0) cycle_location(ui, a, dir);
            else cycle_weather(ui, a, dir);
        }

        if (b->b) a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_APPEARANCE) {
        if (b->up) a->appearance_sel = (a->appearance_sel + 1) % 2;
        if (b->down) a->appearance_sel = (a->appearance_sel + 1) % 2;

        if (b->a) {
            if (a->appearance_sel == 0) {
                a->settings_view = SET_VIEW_FONTS;
                a->fonts_sel = 0;
            }
            else a->settings_view = SET_VIEW_COLORS;
        }

        if (b->b) a->settings_view = SET_VIEW_MAIN;
        return;
    }

    if (a->settings_view == SET_VIEW_FONTS) {
        /* Up/Down selects which row is active:
           0 = Font (Left/Right changes font)
           1 = Font sizes (A enters size editor) */
        if (b->up || b->down) a->fonts_sel = (a->fonts_sel + 1) % 2;

        if (a->fonts_sel == 0) {
            if (b->left)  cycle_font(ui, a, -1);
            if (b->right) cycle_font(ui, a,  1);
        } else {
            if (b->a) {
                a->settings_view = SET_VIEW_FONT_SIZES;
                a->sizes_sel = 0;
            }
        }

        if (b->b) a->settings_view = SET_VIEW_APPEARANCE;
        return;
    }

    if (a->settings_view == SET_VIEW_COLORS) {
        if (b->up) a->colors_sel = (a->colors_sel + 2) % 3;
        if (b->down) a->colors_sel = (a->colors_sel + 1) % 3;

        if (b->left || b->right) {
            int dir = b->right ? 1 : -1;
            if (a->colors_sel == 0) cycle_palette_idx(&a->cfg.main_color_idx, dir);
            else if (a->colors_sel == 1) cycle_palette_idx(&a->cfg.accent_color_idx, dir);
            else cycle_palette_idx(&a->cfg.highlight_color_idx, dir);
            persist_colors(a);
        }

        if (b->b) a->settings_view = SET_VIEW_APPEARANCE;
        return;
    }

    if (a->settings_view == SET_VIEW_FONT_SIZES) {
        if (b->up) a->sizes_sel = (a->sizes_sel + 2) % 3;
        if (b->down) a->sizes_sel = (a->sizes_sel + 1) % 3;

        if (b->left)  change_size(ui, a, a->sizes_sel, -2);
        if (b->right) change_size(ui, a, a->sizes_sel,  2);

        if (b->b) a->settings_view = SET_VIEW_FONTS;
        return;
    }

    /* 5 rows in the main Settings list. Up should move to the previous row (wrap).
       (a + 4) % 5 is equivalent to (a - 1) % 5 for non-negative modulo. */
    if (b->up) a->settings_sel = (a->settings_sel + 4) % 5;
    if (b->down) a->settings_sel = (a->settings_sel + 1) % 5;

    if (b->a) {
        if (a->settings_sel == 0) {
            a->settings_view = SET_VIEW_SCENE;
            a->scene_sel = 0;
        } else if (a->settings_sel == 1) {
            a->settings_view = SET_VIEW_APPEARANCE;
            a->appearance_sel = 0;
        } else if (a->settings_sel == 2) {
            a->settings_view = SET_VIEW_MUSIC;
            a->music_sel = 0;
        } else if (a->settings_sel == 3) {
            a->settings_view = SET_VIEW_BELL;
            a->bell_sel = 0;
        } else if (a->settings_sel == 4) {
            a->settings_view = SET_VIEW_BUTTONMAP;
            a->buttonmap_page = 0;
        }
    }

    if (b->b) {
        a->settings_open = false;
        a->settings_view = SET_VIEW_MAIN;
    }
}


static void handle_timer(UI* ui, App* a, Buttons* b) {
    if (a->settings_open) {
        handle_settings(ui, a, b);
        return;
    }

    /* Timer quick menu overlay is now toggled by SELECT. */
    if (a->timer_menu_open) {
        handle_timer_quick_menu(ui, a, b);
        return;
    }

    /* Scene quick-change (D-Pad) while on the timer screen. */
    if (b->up)    cycle_location(ui, a, -1);
    if (b->down)  cycle_location(ui, a, +1);
    if (b->left)  cycle_weather(ui, a, -1);
    if (b->right) cycle_weather(ui, a, +1);

    /* Settings (START) */
    if (b->start) {
        a->settings_open = true;
        a->settings_view = SET_VIEW_MAIN;
        a->settings_sel = 0;
        return;
    }

    /* Timer menu (SELECT) */
    if (b->select) {
        a->timer_menu_open = true;
        a->menu_sel = 0;
        return;
    }

    /* Timer pause/resume */
    if (b->a)  timer_toggle_pause(a);

    /* R3 toggles music pause/resume (independent of timer pause). */
    if (b->r3) {
        /* Toggle music pause/resume. If music never started yet, unpausing should start playback. */
        a->music_user_paused = !a->music_user_paused;

        if (a->audio && a->cfg.music_enabled && a->musicq.active) {
            if (!a->music_user_paused) {
                if (!a->music_has_started) {
                    app_music_start_idx(a, a->musicq.idx);
                }
                audio_engine_set_music_paused(a->audio, false);
            } else {
                audio_engine_set_music_paused(a->audio, true);
            }
        }
    }

    /* L3 toggles music ON/OFF (does not affect timer). */
    if (b->l3) {
        a->cfg.music_enabled = a->cfg.music_enabled ? 0 : 1;
        if (a->cfg.music_enabled) app_music_build_and_start(a);
        else app_music_stop(a);
        app_music_refresh_labels(a);
        config_save(&a->cfg, "config.txt");
    }

    /* Folder/Song navigation (no on-screen hints; see Button Map page 2). */
    if ((b->l1 || b->r1) && a->audio) {
        int dir = b->r1 ? 1 : -1;
        if (a->music_folders.count > 0) {
            a->music_folder_idx += dir;
            if (a->music_folder_idx < 0) a->music_folder_idx = a->music_folders.count - 1;
            if (a->music_folder_idx >= a->music_folders.count) a->music_folder_idx = 0;

            safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s",
                          a->music_folders.items[a->music_folder_idx]);

            if (a->cfg.music_enabled) app_music_build_and_start(a);
            app_music_refresh_labels(a);
            config_save(&a->cfg, "config.txt");
        }
    }

    if (b->l2) app_music_prev(a);
    if (b->r2) app_music_next(a);

    /* Reset (paused only) */
    if (b->x && a->paused) {
        timer_reset_keep_paused(a);
    }

    /* Hide/Show all UI text except the big timer */
    if (!a->settings_open && b->y) {
        a->hud_hidden = !a->hud_hidden;
    }

    /* NOTE: B no longer exits the app or stops the timer on this screen. */
}


/* ----------------------------- Main ----------------------------- */

int main(int argc, char** argv) {
    chdir_to_exe_dir();

    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    /* bell is played via audio_engine_play_sfx */
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }

    UI ui = (UI){0};
    ui.w = 1024;
    ui.h = 768;

    ui.win = SDL_CreateWindow("Lofi Pomodoro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              ui.w, ui.h, SDL_WINDOW_SHOWN);
    if (!ui.win) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.ren) {
        fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    App app;
    memset(&app, 0, sizeof(app));
    config_defaults(&app.cfg);
    config_load(&app.cfg, "config.txt");

    /* Tasks: load lists + completion state early so the Tasks menu is instant. */
    tasks_reload(&app);

    if (audio_engine_init(&app.audio) != AUDIO_OK) {
        app.audio = NULL;
    }

    safe_snprintf(app.music_folder, sizeof(app.music_folder), "%s", "Music");
    safe_snprintf(app.music_song, sizeof(app.music_song), "%s", "off");

    sync_font_list(&app);
    sync_bell_list(&app);
    sync_music_folder_list(&app);
    app_music_refresh_labels(&app);
    if (app.cfg.music_enabled && app.audio) {
        app_music_build_and_start(&app);
    }

    if (!ui_open_fonts(&ui, &app.cfg)) {
        strcpy(app.cfg.font_file, "Munro.ttf");
        app.cfg.font_small_pt = 42;
        app.cfg.font_med_pt   = 50;
        app.cfg.font_big_pt   = 100;
        ui_open_fonts(&ui, &app.cfg);
    }

    app.scenes = list_dirs_in("backgrounds");
    if (app.scenes.count == 0) {
        sl_push(&app.scenes, "coffee_shop");
        sl_push(&app.scenes, "forest");
        sl_push(&app.scenes, "city");
        sl_sort(&app.scenes);
    }

    int sidx = sl_find(&app.scenes, app.cfg.scene);
    app.scene_idx = (sidx >= 0) ? sidx : 0;

    refresh_weathers_for_scene(&app);

    int widx = -1;
    if (app.cfg.weather[0]) {
        char want[256];
        safe_snprintf(want, sizeof(want), "%s.png", app.cfg.weather);
        widx = sl_find(&app.weathers, want);
        if (widx < 0) {
            safe_snprintf(want, sizeof(want), "%s.jpg", app.cfg.weather);
            widx = sl_find(&app.weathers, want);
        }
    }
    if (widx >= 0) app.weather_idx = widx;

    update_bg_texture(&ui, &app);

    app.screen = SCREEN_MENU;
    app.menu_sel = 0;
    app.pomo_pick_sel = 0;
    app.pick_pomo_session_min = 25;
    app.pick_pomo_break_min = 5;
    app.pick_pomo_loops = 4;
    app.pomo_session_seconds = 25 * 60;
    app.pomo_break_seconds = 5 * 60;
    app.last_tick_ms = now_ms();

    SDL_GameController* pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) break;
        }
    }

    PixelCache pix = {0};

    bool quit = false;
    while (!quit) {
        Buttons b;
        buttons_clear(&b);

        SDL_Event e;
        
while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit = true;

    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        map_button(&b, e.cbutton, app.cfg.swap_ab);
    } else if (e.type == SDL_CONTROLLERAXISMOTION) {
        const int TH = 16000;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            if (e.caxis.value > TH && !app.trig_l2_down) { b.l2 = true; app.trig_l2_down = 1; }
            if (e.caxis.value < TH/2) app.trig_l2_down = 0;
        } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            if (e.caxis.value > TH && !app.trig_r2_down) { b.r2 = true; app.trig_r2_down = 1; }
            if (e.caxis.value < TH/2) app.trig_r2_down = 0;
        }
    } else if (e.type == SDL_KEYDOWN) {
        map_key(&b, e.key, app.cfg.swap_ab);
    }
}

        if (app.screen == SCREEN_TIMER) app_update(&app);

        if (app.screen == SCREEN_MENU) {
            /* Root menu: do NOT allow B to exit. Exit is only via selecting "Exit" + A.
               This keeps behavior consistent with the in-timer Select menu. */
            handle_menu(&app, &b);
        } else if (app.screen == SCREEN_POMO_PICK) {
            handle_pomo_pick(&app, &b);
        } else if (app.screen == SCREEN_TASKS_PICK) {
            handle_tasks_pick(&app, &b);
        } else if (app.screen == SCREEN_TASKS_LIST) {
            handle_tasks_list(&app, &b);
        } else if (app.screen == SCREEN_CUSTOM_PICK) {
            handle_custom_pick(&app, &b);
        } else if (app.screen == SCREEN_TIMER) {
            handle_timer(&ui, &app, &b);
        }

        if (app.quit_requested) {
            quit = true;
        }

        app_audio_frame_update(&app);

        SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);

        SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 255);

        SDL_RenderClear(ui.ren);

        /* Dim background whenever any overlay/menu is open, so all menus feel consistent. */
        bool want_dim = (app.screen != SCREEN_TIMER) || app.settings_open || app.timer_menu_open;
        if (want_dim) draw_bg_faint(&ui, 60);
        else draw_bg_normal(&ui);

        if (app.screen == SCREEN_MENU) draw_menu(&ui, &app);
        else if (app.screen == SCREEN_POMO_PICK) draw_pomo_picker(&ui, &app);
        else if (app.screen == SCREEN_CUSTOM_PICK) draw_custom_picker(&ui, &app);
        else if (app.screen == SCREEN_TASKS_PICK) draw_tasks_pick(&ui, &app);
        else if (app.screen == SCREEN_TASKS_LIST) draw_tasks_list(&ui, &app);
        else draw_timer(&ui, &app);

        SDL_RenderPresent(ui.ren);
        SDL_Delay(10);
    }

    config_save(&app.cfg, "config.txt");

    pixel_destroy(&pix);

    if (pad) SDL_GameControllerClose(pad);

    sl_free(&app.scenes);
    sl_free(&app.weathers);
    sl_free(&app.fonts);
    sl_free(&app.bell_sounds);

    if (ui.bg_tex) SDL_DestroyTexture(ui.bg_tex);
    ui_close_fonts(&ui);

    SDL_DestroyRenderer(ui.ren);
    SDL_DestroyWindow(ui.win);

    app_music_stop(&app);
    sl_free(&app.music_folders);
    sl_free(&app.musicq.tracks);
    audio_engine_quit(&app.audio);

    sl_free(&app.tasks_daily);
    sl_free(&app.tasks_weekly);
    hashes_free(&app.tasks_done_daily, &app.tasks_done_daily_n);
    hashes_free(&app.tasks_done_weekly, &app.tasks_done_weekly_n);


    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}