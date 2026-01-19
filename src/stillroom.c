// focus_timer.c - Stillroom (timer + backgrounds only, no audio)
// TrimUI Brick / NextUI SDL2
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <strings.h> // strcasecmp
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
/* Branding toggles (compile-time)
   Set to 1 to show the subtitle under the app name.
*/
#ifndef STILLROOM_SHOW_TAGLINE
#define STILLROOM_SHOW_TAGLINE 0
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#include "app.h"
#include "audio_engine.h"
#include "features/booklets/booklets.h"
#include "features/focus_menu/focus_menu.h"
#include "features/meditation/meditation.h"
#include "features/music/music_player.h"
#include "features/quest/quest.h"
#include "features/routines/routines.h"
#include "features/tasks/tasks.h"
#include "features/timer/timer.h"
#include "features/updater/updater.h"
#include "ui/keyboard.h"
#include "ui/palette.h"
#include "ui/ui_shared.h"

#include "utils/file_utils.h"
#include "utils/string_utils.h"
#include <stdio.h>

/*
 *  Data / Globals
 */

typedef enum {
  STZ_P_NONE = 0,
  STZ_P_CUSTOM = 1,
  STZ_P_SEASON = 2,
  STZ_P_LOCATION = 3,
  STZ_P_MOOD = 4,
  STZ_P_PHASE = 5,
  STZ_P_BACKGROUND = 6,
  STZ_P_QMARK = 7,
  STZ_P_EXCL = 8,
  STZ_P_COLON = 9,
  STZ_P_SEMI = 10,
  STZ_P_ELLIPSIS = 11,
  STZ_P_DOT = 12,
  STZ_P_COMMA = 13,
  STZ_P_APOSTROPHE = 14,
  STZ_P_FOR = 15,
  STZ_P_AND = 16,
  STZ_P_NOR = 17,
  STZ_P_BUT = 18,
  STZ_P_OR = 19,
  STZ_P_YET = 20,
  STZ_P_SO = 21,
  STZ_P_BECAUSE = 22,
  STZ_P_SINCE = 23,
  STZ_P_WHILE = 24,
  STZ_P_IF = 25
} StanzaPiece;

#define STZ_P_NONE 0
#define STZ_P_CUSTOM 1

typedef struct {
  const char *background;
  const char *phase;
  const char *location;
  const char *mood;
  const char *season;
  const char *custom;
} StanzaDisplay;

/* Forward declarations for existing static functions to prevent implicit decl
 * changes */
static void draw_poetic_stack_upper_right(UI *ui, App *a, int xR, int y_top,
                                          SDL_Color main, SDL_Color accent);
static void handle_stanza_selector(UI *ui, App *a, Buttons *b);
static void stanza_selector_open(App *a);
static int stz_piece_style(StanzaPiece p);
static SDL_Color stz_piece_color(StanzaPiece p, SDL_Color main,
                                 SDL_Color accent);
static const char *stz_piece_tray_label(StanzaPiece p);
static const char *stz_piece_line_label(const StanzaDisplay *d, StanzaPiece p);
static bool stz_piece_is_suffix(StanzaPiece p);
static bool stz_piece_no_space_after(StanzaPiece p);
static void stz_editor_load_from_work(App *a);
static void stz_draw_piece_line_right(
    UI *ui, const StanzaDisplay *disp, int xR, int y, const int *pieces,
    const char custom_words[][64], int len, int cursor, SDL_Color main,
    SDL_Color accent, SDL_Color hi, bool is_cursor_row, bool has_focus,
    SDL_Color cursor_col, StanzaPiece holding, const char *hold_custom);
static int stz_parse_line_to_pieces(const char *tpl, int *out,
                                    char custom_words[][64], int cap);
static void stz_serialize_line_from_pieces(const int *pcs,
                                           const char custom_words[][64],
                                           int len, char *out, size_t cap);

static const char *stz_custom_label_or_default(const char *c);

/* Forward declarations for existing static functions to prevent implicit decl
 * changes */
static void stanza_custom_kb_clamp(App *a);
static void draw_stanza_custom_keyboard(UI *ui, App *a, SDL_Color main,
                                        SDL_Color accent, SDL_Color hi);
static void handle_stanza_custom_keyboard(App *a, Buttons *b);

/* Forward declarations for local functions */

/* static void update_thread_cancel(App *a); */
static void focus_stats_load(App *a, const char *path);
void music_state_load(App *a);
static void sync_font_list(App *a);
static void sync_bell_list(App *a);
static void sync_meditation_guided_list(App *a);
void sync_music_folder_list(App *a);
static void buttons_clear(Buttons *b);

void app_update(App *a);
static bool timer_main_screen(const App *a);

void app_audio_frame_update(App *a);
static void anim_overlay_update(App *a);
static void help_overlay_tick(UI *ui, App *a, Buttons *b);

static float breath_phase_scale(const App *a);

static void draw_bg_scaled(UI *ui, float scale, int y_shift_up, bool blurred);
static void draw_bg_normal_shifted(UI *ui, int y_shift_up);
static void draw_bg_blurred(UI *ui);
static void draw_bg_faint(UI *ui, int bg_alpha);

static void draw_bg_normal(UI *ui);
static void anim_overlay_draw(UI *ui, App *a);
static void draw_help_overlay(UI *ui, App *a);
static void textcache_free(TextCache *tc);
static void anim_overlay_unload(App *a);
static void help_overlay_close(App *a);
static void ui_close_fonts(UI *ui);

void music_state_save(const App *a);
void app_music_build(App *a, bool was_playing);
void app_music_refresh_labels(App *a);
void app_music_prev(App *a);
void app_music_next(App *a);
void app_music_stop(App *a);
void music_state_set(App *a, const char *folder, const char *tracking_filename);
static void capture_run_activity(App *a);
static void apply_ambience_from_cfg(UI *ui, App *a, bool user_action);

static void refresh_moods_for_location(App *a);
static void seasons_init(App *a);
static void refresh_weathers_for_scene(App *a);
static void ambience_path_from_name(const App *a, char *out, size_t cap);

int hud_draw_tpl_line(UI *ui, int xR, int y, const char *tpl, SDL_Color main,
                      SDL_Color accent, const char *season, const char *bg,
                      const char *phase_str, const char *loc, const char *mood,
                      bool check, bool *out_found);
static void persist_scene_weather(App *a);
static bool ui_open_fonts(UI *ui, const AppConfig *cfg);
static void persist_fonts(App *a);

static void animations_set_enabled(UI *ui, App *a, bool enabled);

static void persist_colors(App *a);
static void apply_detected_season(UI *ui, App *a, bool force);

void focus_history_append(App *a, uint32_t seconds, const char *reason);
static void award_focus_seconds(App *a, uint32_t seconds);

/* Forward declarations for Recovery Stubs (Missing Functions) */
static int draw_text_wrapped_centered(UI *ui, TTF_Font *font, int cx, int y,
                                      const char *text, SDL_Color color,
                                      int max_w, int max_lines);
static bool split_trailing_tag(const char *in, char *base, size_t bcap,
                               char *tag, size_t tcap);

static void draw_icon_heart(UI *ui, int x, int y, int scale, SDL_Color col);
static void draw_clock_upper_left_stacked(UI *ui, App *a, int xL, int y);
static void draw_battery_upper_right_stacked(UI *ui, App *a, int xR, int y);
static void format_focusing_line(App *a, char *buf, size_t cap);
static void draw_text_cached(UI *ui, TextCache *cache, TTF_Font *font, int x,
                             int y, const char *text, SDL_Color color,
                             bool center, int style);

static void fmt_hms_opt_hours(char *buf, size_t cap, uint32_t seconds);

void draw_text_style_baseline(UI *ui, TTF_Font *font, int x, int y,
                              const char *text, SDL_Color color, bool center,
                              int style);
int ui_bottom_stack_top_y(UI *ui);

static void focus_stats_remove_name(App *a, const char *name);
static void focus_stats_ensure_name(App *a, const char *name);
static void format_as_of_today_phrase(char *buf, size_t cap);
static void format_duration_hm(uint64_t seconds, char *buf, size_t cap);
static void draw_inline_left(UI *ui, TTF_Font *f1, TTF_Font *f2, int x, int y,
                             const char *s1, const char *s2, SDL_Color c1,
                             SDL_Color c2);
static void focus_history_load_to_cache(App *a);
static void truncate_to_width(UI *ui, TTF_Font *font, const char *src, int w,
                              char *dst, size_t dst_cap);
static int ui_bottom_baseline_y(UI *ui);
void mood_display_from_folder(const char *folder, char *out, size_t cap);
static const char *breath_phase_label(int phase);
static void fmt_mmss(char *buf, size_t cap, uint32_t seconds);

static SDL_Surface *render_text_surface(TTF_Font *font, const char *text,
                                        SDL_Color color);
static void draw_inline_right(UI *ui, TTF_Font *f1, TTF_Font *f2, int x, int y,
                              const char *s1, const char *s2, SDL_Color c1,
                              SDL_Color c2);

static void resume_clear(App *a);
static int date_int_from_ymd(int y, int m, int d);

static void draw_stanza_selector(UI *ui, App *a, SDL_Color main,
                                 SDL_Color accent);

static void resume_capture(App *a);

static void timer_toggle_pause(App *a);

static bool file_exists_local(const char *path);

#ifndef STILLROOM_VERSION
#define STILLROOM_VERSION "v0.8.1"
#endif
/* ------------------------------------------------------------
   Logging + crash capture
   ------------------------------------------------------------ */
static FILE *g_log_fp = NULL;
static int g_log_fd = -1;
static void log_open(const char *path) {
  /* Open append-only log early. Try to keep both FILE* (convenient) and fd
   * (signal-safe). */
  g_log_fp = fopen(path, "a");
  if (g_log_fp) {
    setvbuf(g_log_fp, NULL, _IOLBF, 0); /* line-buffered */
    g_log_fd = fileno(g_log_fp);
  } else {
    g_log_fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0644);
  }
}
static void log_close(void) {
  if (g_log_fp) {
    fflush(g_log_fp);
    fclose(g_log_fp);
    g_log_fp = NULL;
    g_log_fd = -1;
    return;
  }
  if (g_log_fd >= 0) {
    close(g_log_fd);
    g_log_fd = -1;
  }
}
static void log_printf(const char *fmt, ...) {
  if (!g_log_fp)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log_fp, fmt, ap);
  fputc('\n', g_log_fp);
  fflush(g_log_fp);
  va_end(ap);
}
static void sdl_log_to_file(void *userdata, int category,
                            SDL_LogPriority priority, const char *message) {
  (void)userdata;
  (void)category;
  (void)priority;
  if (g_log_fp) {
    fprintf(g_log_fp, "SDL: %s\n", message);
    fflush(g_log_fp);
  }
}
static void crash_write_str(const char *s) {
  if (g_log_fd < 0 || !s)
    return;
  (void)write(g_log_fd, s, strlen(s));
}
static void crash_write_int(int v) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%d", v);
  if (n > 0)
    crash_write_str(buf);
}
static void crash_handler(int sig) {
  crash_write_str("\n\n===== CRASH =====\nSignal: ");
  crash_write_int(sig);
  crash_write_str("\n");
  crash_write_str("===== END CRASH =====\n");
  signal(sig, SIG_DFL);
  raise(sig);
}
static void crash_install_handlers(void) {
  signal(SIGSEGV, crash_handler);
  signal(SIGABRT, crash_handler);
  signal(SIGFPE, crash_handler);
  signal(SIGILL, crash_handler);
  signal(SIGBUS, crash_handler);
}
static void log_session_header(void) {
  if (!g_log_fp)
    return;
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  char tbuf[64];
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
  fprintf(g_log_fp, "\n\n=== Stillroom session start: %s ===\n", tbuf);
  fprintf(g_log_fp, "Build: %s %s\n", __DATE__, __TIME__);
  fflush(g_log_fp);
}
uint64_t now_ms(void) { return (uint64_t)SDL_GetTicks(); }
/* -------- Battery (sysfs) --------
   Reads battery percentage from /sys/class/power_supply/<battery>/capacity.
   Cached and refreshed every few seconds to avoid per-frame file I/O.
*/
static int g_batt_percent = -1;
static uint64_t g_batt_last_ms = 0;
static bool read_int_file(const char *path, int *out) {
  FILE *f = fopen(path, "r");
  if (!f)
    return false;
  int v = 0;
  bool ok = (fscanf(f, "%d", &v) == 1);
  fclose(f);
  if (!ok)
    return false;
  *out = v;
  return true;
}
static bool read_str_file(const char *path, char *out, size_t out_sz) {
  FILE *f = fopen(path, "r");
  if (!f)
    return false;
  if (!fgets(out, (int)out_sz, f)) {
    fclose(f);
    return false;
  }
  fclose(f);
  size_t n = strlen(out);
  while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
    out[--n] = '\0';
  return true;
}
static int get_battery_percent_sysfs(void) {
  const char *base = "/sys/class/power_supply";
  DIR *d = opendir(base);
  if (!d)
    return -1;
  struct dirent *de;
  char type_path[512];
  char cap_path[512];
  char type_buf[64];
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    snprintf(type_path, sizeof(type_path), "%s/%s/type", base, de->d_name);
    if (!read_str_file(type_path, type_buf, sizeof(type_buf)))
      continue;
    if (strcasecmp(type_buf, "battery") != 0)
      continue;
    snprintf(cap_path, sizeof(cap_path), "%s/%s/capacity", base, de->d_name);
    int cap = -1;
    if (read_int_file(cap_path, &cap)) {
      closedir(d);
      if (cap < 0)
        cap = 0;
      if (cap > 100)
        cap = 100;
      return cap;
    }
  }
  closedir(d);
  return -1;
}
static void battery_tick_update(void) {
  uint64_t now = now_ms();
  /* Update immediately at startup, then every 5 seconds.
     This avoids showing "n/a" for the first few seconds after launch. */
  if (g_batt_percent < 0 || g_batt_last_ms == 0 ||
      (now - g_batt_last_ms) >= 5000) {
    g_batt_percent = get_battery_percent_sysfs();
    g_batt_last_ms = now;
  }
}
static void chdir_to_exe_dir(void) {
  char path[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0)
    return;
  path[n] = '\0';
  char *dir = dirname(path);
  if (dir)
    chdir(dir);
}

/* -------------------------------------------------------------------------- */
/* Panic Logging: write to ./CRASH.txt (current dir, not states/)             */
/* -------------------------------------------------------------------------- */
static int panic_log_first_call = 1;
static void panic_log(const char *fmt, ...) {
  // First call overwrites, subsequent calls append
  FILE *f = fopen("CRASH.txt", panic_log_first_call ? "w" : "a");
  panic_log_first_call = 0;
  if (!f)
    return;

  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fflush(f);
  fclose(f);
}

/* Helper to ensure a directory exists without crashing */
static void ensure_dir_exists(const char *path) {
  struct stat st = {0};
  if (stat(path, &st) == -1) {
    mkdir(path, 0755);
  }
}

/* ----------------------------- Layout ----------------------------- */
/* Layout constants moved to ui_shared.h where appropriate */

// String utilities moved to utils/string_utils.c

/* Forward declarations */
typedef struct App App;

// StrList struct moved to utils/string_utils.h
/* trim_newline moved */
/* File utils moved to utils/file_utils.c */

static void move_legacy_state_path(const char *from, const char *to) {
  if (!from || !to)
    return;
  if (!is_file(from) && !is_dir(from))
    return;
  if (is_file(to) || is_dir(to))
    return;
  rename(from, to);
}
static void migrate_legacy_state(void) {
  ensure_dir(STATES_DIR);
  move_legacy_state_path("activity_log.txt", ACTIVITY_LOG_PATH);
  move_legacy_state_path("focus_history.txt", FOCUS_HISTORY_PATH);
  move_legacy_state_path("focus_stats.txt", FOCUS_STATS_PATH);
  move_legacy_state_path("config.txt", CONFIG_PATH);
  move_legacy_state_path("log.txt", LOG_PATH);
  move_legacy_state_path("booklets_state.txt", BOOKLETS_STATE_PATH);
  move_legacy_state_path("hud_stanzas.txt", HUD_STANZA_OVERRIDES_PATH);
  move_legacy_state_path("music_state.txt", MUSIC_STATE_PATH);

  if (is_dir("tasks") && !is_dir(STATES_TASKS_DIR)) {
    rename("tasks", STATES_TASKS_DIR);
  } else {
    ensure_dir(STATES_TASKS_DIR);
  }
}
/* List utils moved */

/* Booklet/Text list utils moved */

/* ----------------------------- Color system ----------------------------- */
/* Palette moved to src/ui/palette.h */
#define PALETTE_SIZE ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))
SDL_Color color_from_idx(int idx) {

  if (idx < 0)
    idx = 0;
  if (idx >= PALETTE_SIZE)
    idx = PALETTE_SIZE - 1;
  NamedColor nc = PALETTE[idx];
  SDL_Color c = {nc.r, nc.g, nc.b, 255};
  return c;
}

SDL_Color ui_color_from_idx(int idx) {
  SDL_Color p_color = color_from_idx(idx);
  SDL_Color c = {p_color.r, p_color.g, p_color.b, 255};
  return c;
}
/* ----------------------------- Config ----------------------------- */
/* AppConfig definition moved to app.h */
static void config_defaults(AppConfig *c) {
  memset(c, 0, sizeof(*c));
  c->scene[0] = 0;
  c->weather[0] = 0;
  c->season[0] = 0;
  c->detect_time = 0;
  c->swap_ab = 1;
  c->animations = 0;
  c->haiku_difficulty = 0;
  c->quest_anchor_date = 0;
  c->quest_anchor_season_rank = 0;
  c->quest_completed = 0;
  /* palette_version is used to keep old configs visually stable across palette
   * reorders. 3 = expanded ~96 color palette. */
  c->palette_version = 3;
  c->main_color_idx = 0;
  /* keep the default vibe: sky accent + amber highlight, but with the expanded
   * palette */
  c->accent_color_idx = 76;    /* sky */
  c->highlight_color_idx = 43; /* amber */
  safe_snprintf(c->font_file, sizeof(c->font_file), "%s", "munro.ttf");
  c->font_small_pt = 42;
  c->font_med_pt = 50;
  c->font_big_pt = 100;
  c->music_enabled = 0;
  safe_snprintf(c->music_folder, sizeof(c->music_folder), "%s", "music");
  c->ambience_enabled = 0;
  safe_snprintf(c->ambience_name, sizeof(c->ambience_name), "%s", "off");
  c->notifications_enabled = 1;
  c->vol_master = 128;
  c->vol_music = 110;
  c->vol_ambience = 96;
  c->vol_notifications = 128;
  safe_snprintf(c->bell_phase_file, sizeof(c->bell_phase_file), "%s",
                "bell.wav");
  safe_snprintf(c->bell_done_file, sizeof(c->bell_done_file), "%s", "bell.wav");
  safe_snprintf(c->meditation_start_bell_file,
                sizeof(c->meditation_start_bell_file), "%s", "bell.wav");
  safe_snprintf(c->meditation_interval_bell_file,
                sizeof(c->meditation_interval_bell_file), "%s", "bell.wav");
  safe_snprintf(c->meditation_end_bell_file,
                sizeof(c->meditation_end_bell_file), "%s", "bell.wav");
  c->last_timer_h = 0;
  c->last_timer_m = 25;
  c->last_timer_s = 0;
  c->last_meditation_h = 0;
  c->last_meditation_m = 10;
  c->last_meditation_bell_min = 0;
  c->last_meditation_breaths = 0;
  c->last_meditation_guided_file[0] = 0;
  c->last_pomo_session_min = 25;
  c->last_pomo_short_break_min = 5;
  c->last_pomo_long_break_min = 20;
  c->last_pomo_loops = 4;
  c->focus_activity[0] = 0;
  c->timer_counting_up = 0;
  /* Default HUD stanza templates (keep current look/behavior). */
  safe_snprintf(c->hud_stanza1, sizeof(c->hud_stanza1), "%s",
                "<phase> <background> <location>");
  safe_snprintf(c->hud_stanza2, sizeof(c->hud_stanza2), "%s", "...and <mood>.");
  c->hud_stanza3[0] = 0;
  c->update_repo[0] = 0;
  safe_snprintf(c->update_asset, sizeof(c->update_asset), "%s",
                "stillroom.elf");
  c->update_target_path[0] = 0;
  c->user_name[0] = 0;
}

static bool config_validate(AppConfig *c);

void config_load(AppConfig *c, const char *path) {
  panic_log("config_load: opening %s", path);

  FILE *f = fopen(path, "r");
  if (!f) {
    panic_log("config_load: file not found, returning");
    return;
  }
  panic_log("config_load: file opened successfully");

  char legacy_meditation_bell[256] = {0};
  char legacy_meditation_breaths[64] = {0};
  char line[512];
  int line_count = 0;
  while (fgets(line, sizeof(line), f)) {
    line_count++;
    config_trim(line);
    if (!line[0] || line[0] == '#')
      continue;
    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = 0;
    char key[256], val[256];
    safe_snprintf(key, sizeof(key), "%s", line);
    safe_snprintf(val, sizeof(val), "%s", eq + 1);
    config_trim(key);
    config_trim(val);
    if (strcmp(key, "scene") == 0)
      safe_snprintf(c->scene, sizeof(c->scene), "%s", val);
    else if (strcmp(key, "weather") == 0 || strcmp(key, "phase") == 0)
      safe_snprintf(c->weather, sizeof(c->weather), "%s", val);
    else if (strcmp(key, "season") == 0)
      safe_snprintf(c->season, sizeof(c->season), "%s", val);
    else if (strcmp(key, "timeofday") == 0)
      safe_snprintf(c->weather, sizeof(c->weather), "%s", val);
    else if (strcmp(key, "detect_time") == 0)
      c->detect_time = atoi(val) ? 1 : 0;
    else if (strcmp(key, "swap_ab") == 0)
      c->swap_ab = atoi(val) ? 1 : 0;
    else if (strcmp(key, "animations") == 0)
      c->animations = atoi(val) ? 1 : 0;
    else if (strcmp(key, "haiku_difficulty") == 0)
      c->haiku_difficulty = atoi(val);
    else if (strcmp(key, "quest_anchor_date") == 0)
      c->quest_anchor_date = atoi(val);
    else if (strcmp(key, "quest_anchor_season_rank") == 0)
      c->quest_anchor_season_rank = atoi(val);
    else if (strcmp(key, "quest_completed") == 0)
      c->quest_completed = atoi(val) ? 1 : 0;
    else if (strcmp(key, "quest_cycle") == 0)
      c->quest_cycle = atoi(val);
    else if (strcmp(key, "quest_spent_seconds") == 0)
      c->quest_spent_seconds = strtoull(val, NULL, 10);
    else if (strcmp(key, "focus_total_seconds") == 0)
      c->focus_total_seconds = strtoull(val, NULL, 10);
    else if (strcmp(key, "palette_version") == 0)
      c->palette_version = atoi(val);
    else if (strcmp(key, "main_color_idx") == 0)
      c->main_color_idx = atoi(val);
    else if (strcmp(key, "accent_color_idx") == 0)
      c->accent_color_idx = atoi(val);
    else if (strcmp(key, "highlight_color_idx") == 0)
      c->highlight_color_idx = atoi(val);
    else if (strcmp(key, "font_file") == 0)
      safe_snprintf(c->font_file, sizeof(c->font_file), "%s", val);
    else if (strcmp(key, "font_small_pt") == 0)
      c->font_small_pt = atoi(val);
    else if (strcmp(key, "font_med_pt") == 0)
      c->font_med_pt = atoi(val);
    else if (strcmp(key, "font_big_pt") == 0)
      c->font_big_pt = atoi(val);
    else if (strcmp(key, "music_enabled") == 0)
      c->music_enabled = atoi(val);
    else if (strcmp(key, "music_folder") == 0)
      safe_snprintf(c->music_folder, sizeof(c->music_folder), "%s", val);
    else if (strcmp(key, "ambience_enabled") == 0)
      c->ambience_enabled = atoi(val);
    else if (strcmp(key, "ambience_name") == 0)
      safe_snprintf(c->ambience_name, sizeof(c->ambience_name), "%s", val);
    else if (strcmp(key, "notifications_enabled") == 0)
      c->notifications_enabled = atoi(val);
    else if (strcmp(key, "vol_master") == 0)
      c->vol_master = atoi(val);
    else if (strcmp(key, "vol_music") == 0)
      c->vol_music = atoi(val);
    else if (strcmp(key, "vol_ambience") == 0)
      c->vol_ambience = atoi(val);
    else if (strcmp(key, "vol_notifications") == 0)
      c->vol_notifications = atoi(val);
    else if (strcmp(key, "bell_phase_file") == 0)
      safe_snprintf(c->bell_phase_file, sizeof(c->bell_phase_file), "%s", val);
    else if (strcmp(key, "bell_done_file") == 0)
      safe_snprintf(c->bell_done_file, sizeof(c->bell_done_file), "%s", val);
    else if (strcmp(key, "meditation_start_bell_file") == 0)
      safe_snprintf(c->meditation_start_bell_file,
                    sizeof(c->meditation_start_bell_file), "%s", val);
    else if (strcmp(key, "meditation_interval_bell_file") == 0)
      safe_snprintf(c->meditation_interval_bell_file,
                    sizeof(c->meditation_interval_bell_file), "%s", val);
    else if (strcmp(key, "meditation_end_bell_file") == 0)
      safe_snprintf(c->meditation_end_bell_file,
                    sizeof(c->meditation_end_bell_file), "%s", val);
    else if (strcmp(key, "meditation_bell_file") == 0)
      safe_snprintf(legacy_meditation_bell, sizeof(legacy_meditation_bell),
                    "%s", val);
    else if (strcmp(key, "last_timer_h") == 0)
      c->last_timer_h = atoi(val);
    else if (strcmp(key, "last_timer_m") == 0)
      c->last_timer_m = atoi(val);
    else if (strcmp(key, "last_timer_s") == 0)
      c->last_timer_s = atoi(val);
    else if (strcmp(key, "last_meditation_h") == 0)
      c->last_meditation_h = atoi(val);
    else if (strcmp(key, "last_meditation_m") == 0)
      c->last_meditation_m = atoi(val);
    else if (strcmp(key, "last_meditation_bell_min") == 0)
      c->last_meditation_bell_min = atoi(val);
    else if (strcmp(key, "last_meditation_breaths") == 0)
      c->last_meditation_breaths = atoi(val);
    else if (strcmp(key, "last_meditation_guided_file") == 0)
      safe_snprintf(c->last_meditation_guided_file,
                    sizeof(c->last_meditation_guided_file), "%s", val);
    else if (strcmp(key, "last_meditation_guided_repeats") == 0)
      safe_snprintf(legacy_meditation_breaths,
                    sizeof(legacy_meditation_breaths), "%s", val);
    else if (strcmp(key, "last_pomo_session_min") == 0)
      c->last_pomo_session_min = atoi(val);
    else if (strcmp(key, "last_pomo_short_break_min") == 0)
      c->last_pomo_short_break_min = atoi(val);
    else if (strcmp(key, "last_pomo_long_break_min") == 0)
      c->last_pomo_long_break_min = atoi(val);
    else if (strcmp(key, "last_pomo_loops") == 0)
      c->last_pomo_loops = atoi(val);
    else if (strcmp(key, "focus_total_seconds") == 0)
      c->focus_total_seconds = (uint64_t)strtoull(val, NULL, 10);
    else if (strcmp(key, "focus_total_sessions") == 0)
      c->focus_total_sessions = (uint64_t)strtoull(val, NULL, 10);
    else if (strcmp(key, "pomo_total_blocks") == 0)
      c->pomo_total_blocks = (uint64_t)strtoull(val, NULL, 10);
    else if (strcmp(key, "pomo_focus_seconds") == 0)
      c->pomo_focus_seconds = (uint64_t)strtoull(val, NULL, 10);
    else if (strcmp(key, "focus_longest_span_seconds") == 0)
      c->focus_longest_span_seconds = (uint64_t)strtoull(val, NULL, 10);
    else if (strcmp(key, "focus_activity") == 0)
      safe_snprintf(c->focus_activity, sizeof(c->focus_activity), "%s", val);
    else if (strcmp(key, "hud_stanza1") == 0)
      safe_snprintf(c->hud_stanza1, sizeof(c->hud_stanza1), "%s", val);
    else if (strcmp(key, "hud_stanza2") == 0)
      safe_snprintf(c->hud_stanza2, sizeof(c->hud_stanza2), "%s", val);
    else if (strcmp(key, "hud_stanza3") == 0)
      safe_snprintf(c->hud_stanza3, sizeof(c->hud_stanza3), "%s", val);
    else if (strcmp(key, "timer_counting_up") == 0)
      c->timer_counting_up = atoi(val) ? 1 : 0;
    else if (strcmp(key, "update_repo") == 0)
      safe_snprintf(c->update_repo, sizeof(c->update_repo), "%s", val);
    else if (strcmp(key, "update_asset") == 0)
      safe_snprintf(c->update_asset, sizeof(c->update_asset), "%s", val);
    else if (strcmp(key, "update_target_path") == 0)
      safe_snprintf(c->update_target_path, sizeof(c->update_target_path), "%s",
                    val);
    else if (strcmp(key, "user_name") == 0)
      safe_snprintf(c->user_name, sizeof(c->user_name), "%s", val);
  }
  fclose(f);
  if (legacy_meditation_bell[0]) {
    if (!c->meditation_start_bell_file[0])
      safe_snprintf(c->meditation_start_bell_file,
                    sizeof(c->meditation_start_bell_file), "%s",
                    legacy_meditation_bell);
    if (!c->meditation_interval_bell_file[0])
      safe_snprintf(c->meditation_interval_bell_file,
                    sizeof(c->meditation_interval_bell_file), "%s",
                    legacy_meditation_bell);
    if (!c->meditation_end_bell_file[0])
      safe_snprintf(c->meditation_end_bell_file,
                    sizeof(c->meditation_end_bell_file), "%s",
                    legacy_meditation_bell);
  }
  if (legacy_meditation_breaths[0] && c->last_meditation_breaths == 0) {
    c->last_meditation_breaths = atoi(legacy_meditation_breaths);
  }
  /* v1 configs stored indices against the old 24-color palette. v2 reorders and
     expands the palette to 48, so we remap indices to keep the selected colors
     the same. */
  if (c->palette_version < 2) {
    static const int v1_to_v2[24] = {
        0,  /* pearl */
        1,  /* warm white */
        2,  /* paper */
        4,  /* graphite */
        28, /* mint */
        32, /* seafoam */
        34, /* teal */
        38, /* sky */
        40, /* denim */
        44, /* lavender */
        45, /* violet */
        14, /* magenta */
        12, /* rose */
        16, /* coral */
        20, /* amber */
        23, /* lemon */
        25, /* olive */
        26, /* lime */
        36, /* neon cyan */
        42, /* neon blue */
        47, /* neon purple */
        15, /* neon pink */
        29, /* retro green */
        8,  /* crimson */
    };
    if (c->main_color_idx >= 0 && c->main_color_idx < 24)
      c->main_color_idx = v1_to_v2[c->main_color_idx];
    if (c->accent_color_idx >= 0 && c->accent_color_idx < 24)
      c->accent_color_idx = v1_to_v2[c->accent_color_idx];
    if (c->highlight_color_idx >= 0 && c->highlight_color_idx < 24)
      c->highlight_color_idx = v1_to_v2[c->highlight_color_idx];
    c->palette_version = 2;
  }
  if (c->main_color_idx < 0 || c->main_color_idx >= PALETTE_SIZE)
    c->main_color_idx = 0;
  if (c->accent_color_idx < 0 || c->accent_color_idx >= PALETTE_SIZE)
    c->accent_color_idx = 38;
  if (c->highlight_color_idx < 0 || c->highlight_color_idx >= PALETTE_SIZE)
    c->highlight_color_idx = 20;
  if (c->font_small_pt < 18)
    c->font_small_pt = 18;
  if (c->font_med_pt < 18)
    c->font_med_pt = 18;
  if (c->font_big_pt < 30)
    c->font_big_pt = 30;
  if (c->font_small_pt > 96)
    c->font_small_pt = 96;
  if (c->font_med_pt > 120)
    c->font_med_pt = 120;
  if (c->font_big_pt > 220)
    c->font_big_pt = 220;
  if (!c->bell_phase_file[0])
    safe_snprintf(c->bell_phase_file, sizeof(c->bell_phase_file), "%s",
                  "bell.wav");
  if (!c->bell_done_file[0])
    safe_snprintf(c->bell_done_file, sizeof(c->bell_done_file), "%s",
                  "bell.wav");
  if (!c->meditation_start_bell_file[0])
    safe_snprintf(c->meditation_start_bell_file,
                  sizeof(c->meditation_start_bell_file), "%s", "bell.wav");
  if (!c->meditation_interval_bell_file[0])
    safe_snprintf(c->meditation_interval_bell_file,
                  sizeof(c->meditation_interval_bell_file), "%s", "bell.wav");
  if (!c->meditation_end_bell_file[0])
    safe_snprintf(c->meditation_end_bell_file,
                  sizeof(c->meditation_end_bell_file), "%s", "bell.wav");
  if (c->last_timer_h < 0)
    c->last_timer_h = 0;
  if (c->last_timer_h > 24)
    c->last_timer_h = 24;
  if (c->last_timer_m < 0)
    c->last_timer_m = 0;
  if (c->last_timer_m > 59)
    c->last_timer_m = 59;
  if (c->last_timer_s < 0)
    c->last_timer_s = 0;
  if (c->last_timer_s > 59)
    c->last_timer_s = 59;
  if (c->last_meditation_h < 0)
    c->last_meditation_h = 0;
  if (c->last_meditation_h > 4)
    c->last_meditation_h = 4;
  if (c->last_meditation_m < 0)
    c->last_meditation_m = 0;
  if (c->last_meditation_m > 59)
    c->last_meditation_m = 59;
  if (c->last_meditation_bell_min < 0)
    c->last_meditation_bell_min = 0;
  if (c->last_meditation_bell_min > 30)
    c->last_meditation_bell_min = 30;
  if (c->last_meditation_breaths < 0)
    c->last_meditation_breaths = 0;
  if (c->last_meditation_breaths > 48)
    c->last_meditation_breaths = 48;
  if (c->last_pomo_session_min < 1)
    c->last_pomo_session_min = 25;
  if (c->last_pomo_session_min > 180)
    c->last_pomo_session_min = 180;
  if (c->last_pomo_short_break_min < 1)
    c->last_pomo_short_break_min = 5;
  config_validate(c);
  panic_log("config_load complete: %d lines processed", line_count);
}
static bool config_validate(AppConfig *c) {

  if (c->last_pomo_short_break_min > 60)
    c->last_pomo_short_break_min = 60;
  /* Rest (long break) can be 0 minutes to disable it entirely. */
  if (c->last_pomo_long_break_min < 0)
    c->last_pomo_long_break_min = 0;
  if (c->last_pomo_long_break_min > 120)
    c->last_pomo_long_break_min = 120;
  /* Pomodoro count wraps 1..4. Keep config sane even if older configs had
   * larger values. */
  if (c->last_pomo_loops < 1)
    c->last_pomo_loops = 4;
  if (c->last_pomo_loops > 4)
    c->last_pomo_loops = 4;
  if (!c->font_file[0])
    safe_snprintf(c->font_file, sizeof(c->font_file), "%s", "munro.ttf");
  if (!c->weather[0])
    safe_snprintf(c->weather, sizeof(c->weather), "%s", "morning");
  if (!c->season[0])
    c->season[0] = 0;
  /* season is a folder name; leave as-is (may include "(i) " prefix). */
  if (c->detect_time != 0)
    c->detect_time = 1;
  if (c->animations != 0)
    c->animations = 1;
  if (c->haiku_difficulty < 0)
    c->haiku_difficulty = 0;
  if (c->haiku_difficulty > 4)
    c->haiku_difficulty = 4;
  if (c->quest_anchor_date < 0)
    c->quest_anchor_date = 0;
  if (c->quest_anchor_season_rank < 1 || c->quest_anchor_season_rank > 4)
    c->quest_anchor_season_rank = 0;
  if (c->quest_completed != 0)
    c->quest_completed = 1;
  if (c->music_enabled != 0)
    c->music_enabled = 1;
  if (!c->music_folder[0])
    safe_snprintf(c->music_folder, sizeof(c->music_folder), "%s", "music");
  if (c->ambience_enabled != 0)
    c->ambience_enabled = 1;
  if (!c->ambience_name[0])
    safe_snprintf(c->ambience_name, sizeof(c->ambience_name), "%s", "off");
  if (c->notifications_enabled != 0)
    c->notifications_enabled = 1;
  if (c->vol_master < 0)
    c->vol_master = 0;
  if (c->vol_master > 128)
    c->vol_master = 128;
  if (c->vol_music < 0)
    c->vol_music = 0;
  if (c->vol_music > 128)
    c->vol_music = 128;
  if (c->vol_ambience < 0)
    c->vol_ambience = 0;
  if (c->vol_ambience > 128)
    c->vol_ambience = 128;
  if (c->vol_notifications < 0)
    c->vol_notifications = 0;
  if (c->vol_notifications > 128)
    c->vol_notifications = 128;
  if (!c->update_asset[0])
    safe_snprintf(c->update_asset, sizeof(c->update_asset), "%s",
                  "stillroom.elf");
  return true;
}
void config_save(const AppConfig *c, const char *path) {
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "scene=%s\n", c->scene);
  fprintf(f, "phase=%s\n", c->weather);
  /* Back-compat for older builds */
  fprintf(f, "weather=%s\n", c->weather);
  fprintf(f, "season=%s\n", c->season);
  fprintf(f, "detect_time=%d\n", c->detect_time);
  fprintf(f, "swap_ab=%d\n", c->swap_ab);
  fprintf(f, "animations=%d\n", c->animations);
  fprintf(f, "haiku_difficulty=%d\n", c->haiku_difficulty);
  if (c->quest_anchor_date > 0)
    fprintf(f, "quest_anchor_date=%d\n", c->quest_anchor_date);
  if (c->quest_anchor_season_rank > 0)
    fprintf(f, "quest_anchor_season_rank=%d\n", c->quest_anchor_season_rank);
  fprintf(f, "quest_completed=%d\n", c->quest_completed ? 1 : 0);
  fprintf(f, "quest_cycle=%d\n", c->quest_cycle);
  fprintf(f, "quest_spent_seconds=%llu\n",
          (unsigned long long)c->quest_spent_seconds);
  fprintf(f, "palette_version=%d\n", c->palette_version);
  fprintf(f, "main_color_idx=%d\n", c->main_color_idx);
  fprintf(f, "accent_color_idx=%d\n", c->accent_color_idx);
  fprintf(f, "highlight_color_idx=%d\n", c->highlight_color_idx);
  fprintf(f, "font_file=%s\n", c->font_file);
  fprintf(f, "font_small_pt=%d\n", c->font_small_pt);
  fprintf(f, "font_med_pt=%d\n", c->font_med_pt);
  fprintf(f, "font_big_pt=%d\n", c->font_big_pt);
  fprintf(f, "music_enabled=%d\n", c->music_enabled);
  fprintf(f, "music_folder=%s\n", c->music_folder);
  fprintf(f, "ambience_enabled=%d\n", c->ambience_enabled);
  fprintf(f, "ambience_name=%s\n", c->ambience_name);
  fprintf(f, "notifications_enabled=%d\n", c->notifications_enabled);
  fprintf(f, "vol_master=%d\n", c->vol_master);
  fprintf(f, "vol_music=%d\n", c->vol_music);
  fprintf(f, "vol_ambience=%d\n", c->vol_ambience);
  fprintf(f, "vol_notifications=%d\n", c->vol_notifications);
  fprintf(f, "bell_phase_file=%s\n", c->bell_phase_file);
  fprintf(f, "bell_done_file=%s\n", c->bell_done_file);
  fprintf(f, "meditation_start_bell_file=%s\n", c->meditation_start_bell_file);
  fprintf(f, "meditation_interval_bell_file=%s\n",
          c->meditation_interval_bell_file);
  fprintf(f, "meditation_end_bell_file=%s\n", c->meditation_end_bell_file);
  fprintf(f, "last_timer_h=%d\n", c->last_timer_h);
  fprintf(f, "last_timer_m=%d\n", c->last_timer_m);
  fprintf(f, "last_timer_s=%d\n", c->last_timer_s);
  fprintf(f, "last_meditation_h=%d\n", c->last_meditation_h);
  fprintf(f, "last_meditation_m=%d\n", c->last_meditation_m);
  fprintf(f, "last_meditation_bell_min=%d\n", c->last_meditation_bell_min);
  fprintf(f, "last_meditation_breaths=%d\n", c->last_meditation_breaths);
  fprintf(f, "last_meditation_guided_file=%s\n",
          c->last_meditation_guided_file);
  fprintf(f, "last_pomo_session_min=%d\n", c->last_pomo_session_min);
  fprintf(f, "last_pomo_short_break_min=%d\n", c->last_pomo_short_break_min);
  fprintf(f, "last_pomo_long_break_min=%d\n", c->last_pomo_long_break_min);
  fprintf(f, "last_pomo_loops=%d\n", c->last_pomo_loops);
  fprintf(f, "focus_total_seconds=%llu\n",
          (unsigned long long)c->focus_total_seconds);
  fprintf(f, "focus_total_sessions=%llu\n",
          (unsigned long long)c->focus_total_sessions);
  fprintf(f, "pomo_total_blocks=%llu\n",
          (unsigned long long)c->pomo_total_blocks);
  fprintf(f, "pomo_focus_seconds=%llu\n",
          (unsigned long long)c->pomo_focus_seconds);
  fprintf(f, "focus_longest_span_seconds=%llu\n",
          (unsigned long long)c->focus_longest_span_seconds);
  fprintf(f, "focus_activity=%s\n", c->focus_activity);
  fprintf(f, "hud_stanza1=%s\n", c->hud_stanza1);
  fprintf(f, "hud_stanza2=%s\n", c->hud_stanza2);
  fprintf(f, "hud_stanza3=%s\n", c->hud_stanza3);
  fprintf(f, "timer_counting_up=%d\n", c->timer_counting_up ? 1 : 0);
  if (c->update_repo[0])
    fprintf(f, "update_repo=%s\n", c->update_repo);
  fprintf(f, "update_asset=%s\n", c->update_asset);
  if (c->update_target_path[0])
    fprintf(f, "update_target_path=%s\n", c->update_target_path);
  if (c->user_name[0])
    fprintf(f, "user_name=%s\n", c->user_name);
  fclose(f);
}
/* ----------------------------- Tasks (.txt driven)
 * ----------------------------- */

/* ----------------------------- UI ----------------------------- */
// Removed duplicate UI struct definition

/* ----------------------------- Text render cache
 * -----------------------------
 */
// Removed duplicate TextCache struct definition

static void draw_bg_normal(UI *ui) {
  if (ui->bg_tex) {
    SDL_Rect dst = {0, 0, ui->w, ui->h};
    SDL_RenderCopy(ui->ren, ui->bg_tex, NULL, &dst);
  } else {
    SDL_SetRenderDrawColor(ui->ren, 15, 15, 20, 255);
    SDL_RenderClear(ui->ren);
  }
}
static void draw_bg_scaled(UI *ui, float scale, int y_shift_up, bool blurred) {
  SDL_Texture *tex =
      blurred ? (ui->bg_blur_tex ? ui->bg_blur_tex : ui->bg_tex) : ui->bg_tex;
  if (!tex) {
    draw_bg_normal(ui);
    return;
  }
  if (scale < 0.1f)
    scale = 0.1f;
  int w = (int)((float)ui->w * scale + 0.5f);
  int h = (int)((float)ui->h * scale + 0.5f);
  int x = (ui->w - w) / 2;
  int y = (ui->h - h) / 2 - y_shift_up;
  SDL_Rect dst = {x, y, w, h};
  SDL_RenderCopy(ui->ren, tex, NULL, &dst);

  if (blurred) {
    /* 50% Dimming Overlay (Alpha 128) to match standard blurred menu style */
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 128);
    SDL_RenderFillRect(ui->ren, &dst);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
  }
}
static void draw_bg_normal_shifted(UI *ui, int y_shift_up) {
  if (!ui->bg_tex) {
    draw_bg_normal(ui);
    return;
  }
  /* Shift the background up by y_shift_up pixels (negative Y). */
  SDL_Rect dst = {0, -y_shift_up, ui->w, ui->h};
  SDL_RenderCopy(ui->ren, ui->bg_tex, NULL, &dst);
}
static void draw_bg_dimmed(UI *ui, Uint8 overlay_alpha) {
  draw_bg_normal(ui);
  SDL_Rect dst = {0, 0, ui->w, ui->h};
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, overlay_alpha);
  SDL_RenderFillRect(ui->ren, &dst);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
}
static void draw_bg_faint(UI *ui, int bg_alpha) {

  if (!ui->bg_tex) {
    draw_bg_normal(ui);
    return;
  }
  Uint8 old = 255;
  SDL_GetTextureAlphaMod(ui->bg_tex, &old);
  SDL_SetTextureAlphaMod(ui->bg_tex, bg_alpha);
  SDL_Rect dst = {0, 0, ui->w, ui->h};
  SDL_RenderCopy(ui->ren, ui->bg_tex, NULL, &dst);
  SDL_SetTextureAlphaMod(ui->bg_tex, old);
}
static void draw_bg_blurred(UI *ui) {
  if (ui->bg_blur_tex) {
    SDL_Rect dst = {0, 0, ui->w, ui->h};
    SDL_RenderCopy(ui->ren, ui->bg_blur_tex, NULL, &dst);

    /* 50% Dimming Overlay (Alpha 128) for readability */
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 128);
    SDL_RenderFillRect(ui->ren, &dst);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
  } else {
    draw_bg_normal(ui);
  }
}
/* ----------------------------- Bottom hint rendering (mixed colors)
 * ----------------------------- */
/* ----------------------------- Shared UI Functions
 * ----------------------------- */

int text_width(TTF_Font *font, const char *s) {
  if (!s || !s[0])
    return 0;
  int w = 0, h = 0;
  if (TTF_SizeUTF8(font, s, &w, &h) != 0)
    return 0;
  return w;
}

int text_width_style(TTF_Font *font, int style, const char *s) {
  if (!s || !s[0])
    return 0;
  int old = TTF_GetFontStyle(font);
  TTF_SetFontStyle(font, style);
  int w = 0, h = 0;
  if (TTF_SizeUTF8(font, s, &w, &h) != 0)
    w = 0;
  TTF_SetFontStyle(font, old);
  return w;
}

int text_width_style_ui(UI *ui, TTF_Font *font, int style, const char *s) {
  if (!s || !s[0])
    return 0;
  TTF_Font *use_font = font;
  int use_style = style;
  if (style & TTF_STYLE_ITALIC) {
    if (font == ui->font_small && ui->font_small_i)
      use_font = ui->font_small_i;
    else if (font == ui->font_med && ui->font_med_i)
      use_font = ui->font_med_i;
    else if (font == ui->font_big && ui->font_big_i)
      use_font = ui->font_big_i;
    if (use_font != font)
      use_style &= ~TTF_STYLE_ITALIC;
  }
  int old = TTF_GetFontStyle(use_font);
  TTF_SetFontStyle(use_font, use_style);
  int w = 0, h = 0;
  if (TTF_SizeUTF8(use_font, s, &w, &h) != 0)
    w = 0;
  TTF_SetFontStyle(use_font, old);
  return w;
}

/* Draws text centered horizontally at cx, wrapping to multiple lines if it
   exceeds max_width. Returns the total height used (for stacking subsequent
   elements). Splits on spaces, dots, and underscores. */
static int draw_text_wrapped_centered(UI *ui, TTF_Font *font, int cx, int y,
                                      const char *text, SDL_Color col,
                                      int max_width, int line_gap) {
  if (!ui || !font || !text || !text[0] || max_width <= 0)
    return 0;

  int line_h = TTF_FontHeight(font);
  int total_h = 0;

  /* Check if text fits on one line */
  int full_w = text_width(font, text);
  if (full_w <= max_width) {
    int x = cx - full_w / 2;
    draw_text(ui, font, x, y, text, col, false);
    return line_h;
  }

  /* Need to wrap: split into lines */
  char buf[512];
  size_t len = strlen(text);
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, text, len);
  buf[len] = '\0';

  char line[512];
  line[0] = '\0';
  int line_len = 0;
  int current_y = y;

  const char *p = buf;
  while (*p) {
    /* Find next word/token (split on space, dot, underscore) */
    const char *word_start = p;
    while (*p && *p != ' ' && *p != '.' && *p != '_')
      p++;

    /* Include the delimiter */
    bool has_delim = (*p == ' ' || *p == '.' || *p == '_');
    const char *word_end = has_delim ? p + 1 : p;

    size_t word_len = word_end - word_start;
    char word[256];
    if (word_len >= sizeof(word))
      word_len = sizeof(word) - 1;
    memcpy(word, word_start, word_len);
    word[word_len] = '\0';

    /* Test if adding this word exceeds max_width */
    char test_line[512];
    safe_snprintf(test_line, sizeof(test_line), "%s%s", line, word);
    int test_w = text_width(font, test_line);

    if (test_w > max_width && line[0] != '\0') {
      /* Flush current line */
      int lw = text_width(font, line);
      int lx = cx - lw / 2;
      draw_text(ui, font, lx, current_y, line, col, false);
      current_y += line_h + line_gap;
      total_h += line_h + line_gap;

      /* Start new line with current word */
      safe_snprintf(line, sizeof(line), "%s", word);
      line_len = (int)word_len;
    } else {
      /* Append word to current line */
      safe_snprintf(line, sizeof(line), "%s", test_line);
      line_len = (int)strlen(line);
    }

    if (has_delim)
      p = word_end;
  }

  /* Flush last line */
  if (line[0] != '\0') {
    int lw = text_width(font, line);
    int lx = cx - lw / 2;
    draw_text(ui, font, lx, current_y, line, col, false);
    total_h += line_h;
  }

  return total_h > 0 ? total_h : line_h;
}

void draw_rect_fill(UI *ui, int x, int y, int w, int h, SDL_Color c) {
  SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, c.a);
  SDL_Rect r = {x, y, w, h};
  SDL_RenderFillRect(ui->ren, &r);
}

static void draw_circle_fill(UI *ui, int cx, int cy, int r, SDL_Color c) {
  SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, c.a);
  for (int w = 0; w < r * 2; w++) {
    for (int h = 0; h < r * 2; h++) {
      int dx = r - w;
      int dy = r - h;
      if ((dx * dx + dy * dy) <= (r * r)) {
        SDL_RenderDrawPoint(ui->ren, cx + dx, cy + dy);
      }
    }
  }
}

static SDL_Surface *render_text_surface(TTF_Font *font, const char *text,
                                        SDL_Color col) {
  return TTF_RenderUTF8_Blended(font, text, col);
}

void draw_text(UI *ui, TTF_Font *font, int x, int y, const char *text,
               SDL_Color col, bool right_align) {
  if (!text || !text[0])
    return;
  SDL_Surface *s = render_text_surface(font, text, col);
  if (!s)
    return;
  SDL_Texture *t = SDL_CreateTextureFromSurface(ui->ren, s);
  if (!t) {
    SDL_FreeSurface(s);
    return;
  }
  SDL_Rect dst = {x, y, s->w, s->h};
  if (right_align)
    dst.x = x - s->w;
  SDL_RenderCopy(ui->ren, t, NULL, &dst);
  SDL_DestroyTexture(t);
  SDL_FreeSurface(s);
}

/* Main implementation matching ui_shared.h */
void draw_text_input_with_cursor(UI *ui, TTF_Font *font, int x, int y,
                                 const char *buf, const char *placeholder,
                                 SDL_Color text_col, SDL_Color placeholder_col,
                                 SDL_Color cursor_col, int cursor_idx) {
  if (!ui || !font || !buf)
    return;
  (void)cursor_idx;

  bool has_text = buf[0] != 0;
  int cursor_w = text_width(font, "|");
  if (cursor_w <= 0)
    cursor_w = 6;

  if (has_text) {
    draw_text(ui, font, x, y, buf, text_col, false);
  } else if (placeholder && placeholder[0]) {
    int placeholder_x = x + cursor_w + 6;
    draw_text_style(ui, font, placeholder_x, y, placeholder, placeholder_col,
                    false, TTF_STYLE_ITALIC);
  }

  int w = text_width(font, buf);
  draw_text(ui, font, x + w, y, "|", cursor_col, false);
}

/* Shared Stanza Keyboard Drawing */

void draw_text_style(UI *ui, TTF_Font *font, int x, int y, const char *text,
                     SDL_Color color, bool right_align, int style) {
  if (!text || !text[0])
    return;
  TTF_Font *use_font = font;
  int use_style = style;
  if (style & TTF_STYLE_ITALIC) {
    if (font == ui->font_small && ui->font_small_i)
      use_font = ui->font_small_i;
    else if (font == ui->font_med && ui->font_med_i)
      use_font = ui->font_med_i;
    else if (font == ui->font_big && ui->font_big_i)
      use_font = ui->font_big_i;
    if (use_font != font)
      use_style &= ~TTF_STYLE_ITALIC;
  }
  int old = TTF_GetFontStyle(use_font);
  TTF_SetFontStyle(use_font, use_style);
  SDL_Surface *surf = render_text_surface(use_font, text, color);
  if (!surf) {
    TTF_SetFontStyle(use_font, old);
    return;
  }
  SDL_Texture *tex = SDL_CreateTextureFromSurface(ui->ren, surf);
  if (!tex) {
    SDL_FreeSurface(surf);
    TTF_SetFontStyle(use_font, old);
    return;
  }
  int draw_x = x;
  if (right_align)
    draw_x = x - surf->w;
  SDL_Rect dst = {draw_x, y, surf->w, surf->h};
  SDL_RenderCopy(ui->ren, tex, NULL, &dst);
  SDL_DestroyTexture(tex);
  SDL_FreeSurface(surf);
  TTF_SetFontStyle(use_font, old);
}

static void draw_text_baseline(UI *ui, TTF_Font *font, int x, int baseline_y,
                               const char *text, SDL_Color col,
                               bool align_right) {
  if (!text || !text[0])
    return;
  int ascent = TTF_FontAscent(font);
  int y = baseline_y - ascent;
  draw_text(ui, font, x, y, text, col, align_right);
}
void draw_text_style_baseline(UI *ui, TTF_Font *font, int x, int baseline_y,
                              const char *text, SDL_Color col, bool align_right,
                              int style) {
  if (!text || !text[0])
    return;
  int ascent = TTF_FontAscent(font);
  int y = baseline_y - ascent;
  draw_text_style(ui, font, x, y, text, col, align_right, style);
}

void draw_text_centered(UI *ui, TTF_Font *font, int cx, int cy,
                        const char *text, SDL_Color col, bool shadow) {
  if (!text || !text[0])
    return;
  int w = 0, h = 0;
  TTF_SizeUTF8(font, text, &w, &h);
  draw_text(ui, font, cx - w / 2, cy - h / 2, text, col, shadow);
}
static void draw_inline_left(UI *ui, TTF_Font *font_main, TTF_Font *font_sec,
                             int x, int y, const char *main_text,
                             const char *secondary_text, SDL_Color main_col,
                             SDL_Color secondary_col) {
  if (!main_text)
    main_text = "";
  if (!secondary_text)
    secondary_text = "";
  int asc_main = TTF_FontAscent(font_main);
  int asc_sec = TTF_FontAscent(font_sec);
  int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);
  draw_text_baseline(ui, font_main, x, baseline_y, main_text, main_col, false);
  if (secondary_text[0]) {
    int wMain = text_width(font_main, main_text);
    int wGap = (main_text[0] && secondary_text[0]) ? UI_INLINE_GAP_PX : 0;
    int x2 = x + wMain + wGap;
    draw_text_style_baseline(ui, font_sec, x2, baseline_y, secondary_text,
                             secondary_col, false, TTF_STYLE_ITALIC);
  }
}
static void draw_inline_right(UI *ui, TTF_Font *font_main, TTF_Font *font_sec,
                              int xR, int y, const char *italic_text,
                              const char *normal_text, SDL_Color italic_col,
                              SDL_Color normal_col) {
  if (!italic_text)
    italic_text = "";
  if (!normal_text)
    normal_text = "";
  int asc_main = TTF_FontAscent(font_main);
  int asc_sec = TTF_FontAscent(font_sec);
  int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);
  int wItal = text_width_style_ui(ui, font_sec, TTF_STYLE_ITALIC, italic_text);
  int wNorm = text_width(font_main, normal_text);
  int wGap = (italic_text[0] && normal_text[0]) ? UI_INLINE_GAP_PX : 0;
  int xStart = xR - (wItal + wGap + wNorm);
  if (italic_text[0]) {
    draw_text_style_baseline(ui, font_sec, xStart, baseline_y, italic_text,
                             italic_col, false, TTF_STYLE_ITALIC);
  }
  if (normal_text[0]) {
    int x2 = xStart + wItal + wGap;
    draw_text_baseline(ui, font_main, x2, baseline_y, normal_text, normal_col,
                       false);
  }
}
/* ----------------------------- Clock (stacked words)
 * ----------------------------- */
static const char *word_1_to_19(int n) {
  switch (n) {
  case 1:
    return "one";
  case 2:
    return "two";
  case 3:
    return "three";
  case 4:
    return "four";
  case 5:
    return "five";
  case 6:
    return "six";
  case 7:
    return "seven";
  case 8:
    return "eight";
  case 9:
    return "nine";
  case 10:
    return "ten";
  case 11:
    return "eleven";
  case 12:
    return "twelve";
  case 13:
    return "thirteen";
  case 14:
    return "fourteen";
  case 15:
    return "fifteen";
  case 16:
    return "sixteen";
  case 17:
    return "seventeen";
  case 18:
    return "eighteen";
  case 19:
    return "nineteen";
  default:
    return "";
  }
}
static const char *word_tens(int n) {
  switch (n) {
  case 2:
    return "twenty";
  case 3:
    return "thirty";
  case 4:
    return "forty";
  case 5:
    return "fifty";
  default:
    return "";
  }
}
static const char *word_tens_2_to_9(int n) {
  switch (n) {
  case 2:
    return "twenty";
  case 3:
    return "thirty";
  case 4:
    return "forty";
  case 5:
    return "fifty";
  case 6:
    return "sixty";
  case 7:
    return "seventy";
  case 8:
    return "eighty";
  case 9:
    return "ninety";
  default:
    return "";
  }
}
static void number_to_words_0_100(char *out, size_t out_sz, int n) {
  if (!out || out_sz == 0)
    return;
  out[0] = '\0';
  if (n < 0)
    n = 0;
  if (n > 100)
    n = 100;
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
  const char *t = word_tens_2_to_9(tens);
  if (ones == 0) {
    safe_snprintf(out, out_sz, "%s", t);
  } else {
    safe_snprintf(out, out_sz, "%s-%s", t, word_1_to_19(ones));
  }
}
static void minute_to_words(char *out, size_t out_sz, int minute) {
  if (!out || out_sz == 0)
    return;
  out[0] = '\0';
  if (minute < 0)
    minute = 0;
  if (minute > 59)
    minute = 59;
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
  const char *t = word_tens(tens);
  if (ones == 0) {
    safe_snprintf(out, out_sz, "%s", t);
  } else {
    safe_snprintf(out, out_sz, "%s %s", t, word_1_to_19(ones));
  }
}
static const char *hour_to_word_12h(int hour24) {
  int h = hour24 % 24;
  int h12 = h % 12;
  if (h12 == 0)
    h12 = 12;
  return word_1_to_19(h12);
}
struct App;
static void draw_clock_upper_right_stacked(UI *ui, struct App *a, int xR,
                                           int yTop);
static void draw_clock_upper_left_stacked(UI *ui, struct App *a, int xL,
                                          int yTop);
static void draw_battery_upper_right_stacked(UI *ui, struct App *a, int xR,
                                             int yTop);
static void draw_inline_center(UI *ui, TTF_Font *font_main, TTF_Font *font_sec,
                               int xC, int y, const char *italic_text,
                               const char *normal_text, SDL_Color italic_col,
                               SDL_Color normal_col) {
  if (!italic_text)
    italic_text = "";
  if (!normal_text)
    normal_text = "";
  int asc_main = TTF_FontAscent(font_main);
  int asc_sec = TTF_FontAscent(font_sec);
  int baseline_y = y + (asc_main > asc_sec ? asc_main : asc_sec);
  int wItal = text_width_style_ui(ui, font_sec, TTF_STYLE_ITALIC, italic_text);
  int wNorm = text_width(font_main, normal_text);
  int wGap = (italic_text[0] && normal_text[0]) ? UI_INLINE_GAP_PX : 0;
  int total = wItal + wGap + wNorm;
  int xStart = xC - (total / 2);
  if (italic_text[0]) {
    draw_text_style_baseline(ui, font_sec, xStart, baseline_y, italic_text,
                             italic_col, false, TTF_STYLE_ITALIC);
  }
  if (normal_text[0]) {
    int x2 = xStart + wItal + wGap;
    draw_text_baseline(ui, font_main, x2, baseline_y, normal_text, normal_col,
                       false);
  }
}
static void draw_inline_center_normal_then_italic(
    UI *ui, TTF_Font *font_norm, TTF_Font *font_ital, int xC, int y,
    const char *normal_text, const char *italic_text, SDL_Color normal_col,
    SDL_Color italic_col) {
  if (!normal_text)
    normal_text = "";
  if (!italic_text)
    italic_text = "";
  int asc_norm = TTF_FontAscent(font_norm);
  int asc_ital = TTF_FontAscent(font_ital);
  int baseline_y = y + (asc_norm > asc_ital ? asc_norm : asc_ital);
  int wNorm = text_width(font_norm, normal_text);
  int wItal = text_width_style_ui(ui, font_ital, TTF_STYLE_ITALIC, italic_text);
  int wGap = (normal_text[0] && italic_text[0]) ? UI_INLINE_GAP_PX : 0;
  int total = wNorm + wGap + wItal;
  int xStart = xC - (total / 2);
  if (normal_text[0]) {
    draw_text_baseline(ui, font_norm, xStart, baseline_y, normal_text,
                       normal_col, false);
  }
  if (italic_text[0]) {
    int x2 = xStart + wNorm + wGap;
    draw_text_style_baseline(ui, font_ital, x2, baseline_y, italic_text,
                             italic_col, false, TTF_STYLE_ITALIC);
  }
}
static void draw_inline_right_normal_then_italic(UI *ui, TTF_Font *font_norm,
                                                 TTF_Font *font_ital, int xR,
                                                 int y, const char *normal_text,
                                                 const char *italic_text,
                                                 SDL_Color normal_col,
                                                 SDL_Color italic_col) {
  if (!normal_text)
    normal_text = "";
  if (!italic_text)
    italic_text = "";
  int asc_norm = TTF_FontAscent(font_norm);
  int asc_ital = TTF_FontAscent(font_ital);
  int baseline_y = y + (asc_norm > asc_ital ? asc_norm : asc_ital);
  int wNorm = text_width(font_norm, normal_text);
  int wItal = text_width_style_ui(ui, font_ital, TTF_STYLE_ITALIC, italic_text);
  int wGap = (normal_text[0] && italic_text[0]) ? UI_INLINE_GAP_PX : 0;
  int total = wNorm + wGap + wItal;
  int xStart = xR - total;
  if (xStart < 0)
    xStart = 0;
  int x = xStart;
  if (normal_text[0]) {
    draw_text(ui, font_norm, x, baseline_y - asc_norm, normal_text, normal_col,
              false);
    x += wNorm + wGap;
  }
  if (italic_text[0]) {
    draw_text_style(ui, font_ital, x, baseline_y - asc_ital, italic_text,
                    italic_col, false, TTF_STYLE_ITALIC);
  }
}

void draw_strikethrough(UI *ui, int x, int y, int w, int text_h,
                        SDL_Color col) {
  if (w <= 0 || text_h <= 0)
    return;
  int thickness = text_h / 12;
  if (thickness < 3)
    thickness = 3;
  int line_y = y + (text_h / 2) - (thickness / 2) + 6;
  SDL_Rect r = {x, line_y, w, thickness};
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, col.r, col.g, col.b, (col.a ? col.a : 235));
  SDL_RenderFillRect(ui->ren, &r);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
}

static void draw_icon_heart(UI *ui, int x, int y, int scale, SDL_Color color) {
  SDL_SetRenderDrawColor(ui->ren, color.r, color.g, color.b, color.a);
  /* 9x8 pixel heart base */
  SDL_Point points[] = {{1, 0},
                        {2, 0},
                        {6, 0},
                        {7, 0},
                        {0, 1},
                        {1, 1},
                        {2, 1},
                        {3, 1},
                        {5, 1},
                        {6, 1},
                        {7, 1},
                        {8, 1},
                        /* Row 2 (full) */
                        {0, 2},
                        {1, 2},
                        {2, 2},
                        {3, 2},
                        {4, 2},
                        {5, 2},
                        {6, 2},
                        {7, 2},
                        {8, 2},
                        /* Row 3 (full) */
                        {0, 3},
                        {1, 3},
                        {2, 3},
                        {3, 3},
                        {4, 3},
                        {5, 3},
                        {6, 3},
                        {7, 3},
                        {8, 3},
                        /* Row 4 */
                        {1, 4},
                        {2, 4},
                        {3, 4},
                        {4, 4},
                        {5, 4},
                        {6, 4},
                        {7, 4},
                        /* Row 5 */
                        {2, 5},
                        {3, 5},
                        {4, 5},
                        {5, 5},
                        {6, 5},
                        /* Row 6 */
                        {3, 6},
                        {4, 6},
                        {5, 6},
                        /* Row 7 */
                        {4, 7}};
  int count = sizeof(points) / sizeof(points[0]);
  for (int i = 0; i < count; i++) {
    SDL_Rect r = {x + points[i].x * scale, y + points[i].y * scale, scale,
                  scale};
    SDL_RenderFillRect(ui->ren, &r);
  }
}
static void strike_inline_right_italic(UI *ui, TTF_Font *font_norm,
                                       TTF_Font *font_ital, int xR, int y,
                                       const char *normal_text,
                                       const char *italic_text) {
  if (!normal_text)
    normal_text = "";
  if (!italic_text)
    italic_text = "";
  if (!italic_text[0])
    return;
  int asc_norm = TTF_FontAscent(font_norm);
  int asc_ital = TTF_FontAscent(font_ital);
  int baseline_y = y + (asc_norm > asc_ital ? asc_norm : asc_ital);
  int wNorm = text_width(font_norm, normal_text);
  /* Prefer a true italic companion font if available.
   */
  TTF_Font *use_ital = font_ital;
  int ital_style = TTF_STYLE_ITALIC;
  if (font_ital == ui->font_small && ui->font_small_i) {
    use_ital = ui->font_small_i;
    ital_style = 0;
  } else if (font_ital == ui->font_med && ui->font_med_i) {
    use_ital = ui->font_med_i;
    ital_style = 0;
  } else if (font_ital == ui->font_big && ui->font_big_i) {
    use_ital = ui->font_big_i;
    ital_style = 0;
  }
  int old_style = TTF_GetFontStyle(use_ital);
  TTF_SetFontStyle(use_ital, ital_style);
  int wItal = 0, hItal = 0;
  if (TTF_SizeUTF8(use_ital, italic_text, &wItal, &hItal) != 0) {
    wItal = 0;
    hItal = 0;
  }
  TTF_SetFontStyle(use_ital, old_style);
  int wGap = (normal_text[0] && italic_text[0]) ? UI_INLINE_GAP_PX : 0;
  int total = wNorm + wGap + wItal;
  int xStart = xR - total;
  if (xStart < 0)
    xStart = 0;
  int xItal = xStart + (normal_text[0] ? (wNorm + wGap) : 0);
  int yItalTop = baseline_y - asc_ital;
  SDL_Color white = (SDL_Color){255, 255, 255, 255};
  draw_strikethrough(ui, xItal, yItalTop, wItal, hItal, white);
}
static bool is_nav_hint_label(const char *lab) {
  if (!lab || !lab[0])
    return false;
  // Remove bottom D-pad navigation hints only (Up/Down,
  // Left/Right, D-Pad).
  if (strstr(lab, "up/down") != NULL)
    return true;
  if (strstr(lab, "left/right") != NULL)
    return true;
  if (strstr(lab, "d-pad") != NULL)
    return true;
  if (strstr(lab, "dpad") != NULL)
    return true;
  return false;
}
static void draw_pairs_line(UI *ui, TTF_Font *font, int x, int y,
                            SDL_Color main, SDL_Color accent,
                            bool right_align_total, const char **labels,
                            const char **actions, int count) {
  // Filter out navigation hints (Up/Down, Left/Right,
  // D-Pad) while keeping all other hints.
  const char *f_labels[32];
  const char *f_actions[32];
  int n = 0;
  for (int i = 0;
       i < count && n < (int)(sizeof(f_labels) / sizeof(f_labels[0])); i++) {
    const char *lab = labels[i] ? labels[i] : "";
    if (is_nav_hint_label(lab))
      continue;
    f_labels[n] = labels[i];
    f_actions[n] = actions[i];
    n++;
  }
  if (n <= 0)
    return;
  const char *sep = "   ";
  int wSep = text_width(font, sep);
  int total = 0;
  for (int i = 0; i < n; i++) {
    char a_with_space[256];
    const char *act = f_actions[i] ? f_actions[i] : "";
    const char *lab2 = f_labels[i] ? f_labels[i] : "";
    if (act[0])
      safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
    else
      safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);
    total += text_width(font, lab2);
    total += text_width(font, a_with_space);
    if (i != n - 1)
      total += wSep;
  }
  int start_x = x;
  if (right_align_total)
    start_x = x - total;
  int cx = start_x;
  for (int i = 0; i < n; i++) {
    const char *lab2 = f_labels[i] ? f_labels[i] : "";
    const char *act = f_actions[i] ? f_actions[i] : "";
    char a_with_space[256];
    if (act[0])
      safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
    else
      safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);
    int wL = text_width(font, lab2);
    int wA = text_width(font, a_with_space);
    if (lab2[0])
      draw_text(ui, font, cx, y, lab2, main, false);
    if (a_with_space[0])
      draw_text(ui, font, cx + wL, y, a_with_space, accent, false);
    cx += wL + wA;
    if (i != n - 1) {
      draw_text(ui, font, cx, y, sep, main, false);
      cx += wSep;
    }
  }
}
static int ui_bottom_baseline_y(UI *ui) {
  /* Anchor all bottom-row UI text to the same baseline,
   * regardless of font sizes. */
  const int descent = TTF_FontDescent(ui->font_small); /* usually negative */
  return (ui->h - UI_BOTTOM_MARGIN) + descent;
}

int ui_bottom_stack_top_y(UI *ui) {
  /* Align stacked HUD items so their bottom line shares
   * the standard bottom baseline. */
  const int baseline_y = ui_bottom_baseline_y(ui);
  const int yBottomText = baseline_y - TTF_FontAscent(ui->font_med);
  int yStackTop =
      yBottomText - (TTF_FontHeight(ui->font_small) + HUD_STACK_GAP);
  if (yStackTop < UI_MARGIN_TOP)
    yStackTop = UI_MARGIN_TOP;
  return yStackTop;
}

int overlay_bottom_text_limit_y(UI *ui) {
  /* For overlays that share the bottom HUD (clock +
   * battery), keep content above it. */
  const int yStackTop = ui_bottom_stack_top_y(ui);
  /* A small safety gap so text never visually kisses
   * the HUD. */
  return yStackTop - 8;
}

void draw_hint_pairs(UI *ui, TTF_Font *font, SDL_Color main, SDL_Color accent,
                     const char **left_labels, const char **left_actions,
                     int left_count, const char **right_labels,
                     const char **right_actions, int right_count) {
  (void)ui;
  (void)main;
  (void)accent;
  (void)left_labels;
  (void)left_actions;
  (void)left_count;
  (void)right_labels;
  (void)right_actions;
  (void)right_count;
  /* Button hints are intentionally hidden everywhere.
   */
}

void draw_hint_pairs_lr(UI *ui, SDL_Color main, SDL_Color accent,
                        const char **left_labels, const char **left_actions,
                        int left_count, const char **right_labels,
                        const char **right_actions, int right_count) {
  /* Stub */
}

static void draw_hint_pairs_center(UI *ui, SDL_Color main, SDL_Color accent,
                                   const char **labels, const char **actions,
                                   int count) {
  const int baseline_y = ui_bottom_baseline_y(ui);
  const int y = baseline_y - TTF_FontAscent(ui->font_small);
  const char *sep = "   ";
  int wSep = text_width(ui->font_small, sep);
  int total = 0;
  for (int i = 0; i < count; i++) {
    const char *lab = labels[i] ? labels[i] : "";
    const char *act = actions[i] ? actions[i] : "";
    char a_with_space[256];
    if (act[0])
      safe_snprintf(a_with_space, sizeof(a_with_space), " %s", act);
    else
      safe_snprintf(a_with_space, sizeof(a_with_space), "%s", act);
    total += text_width(ui->font_small, lab);
    total += text_width(ui->font_small, a_with_space);
    if (i != count - 1)
      total += wSep;
  }
  int start_x = (ui->w / 2) - (total / 2);
  draw_pairs_line(ui, ui->font_small, start_x, y, main, accent, false, labels,
                  actions, count);
}
// Closing brace for draw_stats (assuming this is the
// correct location based on line number)
/* ----------------------------- App state
 * ----------------------------- */
// Removed duplicate enum definition

static char habits_status_for(const App *a, uint32_t habit_hash, int date);

int kb_row_len(int r);
int kb_row_len(int r);

/* Update system moved to features/updater/updater.c */
/* -------------------------- Focus activity stats
 * -------------------------- */
static void focus_stats_load(App *a, const char *path) {
  if (!a)
    return;
  a->focus_stats_count = 0;
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    config_trim(line);
    if (!line[0] || line[0] == '#')
      continue;
    char *tab = strchr(line, '\t');
    if (!tab)
      continue;
    *tab = 0;
    char *name = line;
    char *sec_str = tab + 1;
    config_trim(name);
    config_trim(sec_str);
    if (!name[0])
      continue;
    uint64_t secs = (uint64_t)strtoull(sec_str, NULL, 10);
    if (secs == 0)
      continue;
    if (a->focus_stats_count >= MAX_FOCUS_STATS)
      break;
    safe_snprintf(a->focus_stats[a->focus_stats_count].name,
                  sizeof(a->focus_stats[a->focus_stats_count].name), "%s",
                  name);
    a->focus_stats[a->focus_stats_count].seconds = secs;
    a->focus_stats_count++;
  }
  fclose(f);
}
static void focus_stats_save(App *a, const char *path) {
  if (!a)
    return;
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  for (int i = 0; i < a->focus_stats_count; i++) {
    if (!a->focus_stats[i].name[0])
      continue;
    fprintf(f, "%s\t%llu\n", a->focus_stats[i].name,
            (unsigned long long)a->focus_stats[i].seconds);
  }
  fclose(f);
}
static void focus_pick_sync_idx(App *a);
static void focus_stats_remove_name(App *a, const char *name) {
  if (!a || !name || !name[0])
    return;
  int w = 0;
  for (int i = 0; i < a->focus_stats_count; i++) {
    if (a->focus_stats[i].name[0] && strcmp(a->focus_stats[i].name, name) == 0)
      continue;
    if (w != i)
      a->focus_stats[w] = a->focus_stats[i];
    w++;
  }
  a->focus_stats_count = w;
  if (a->cfg.focus_activity[0] && strcmp(a->cfg.focus_activity, name) == 0) {
    a->cfg.focus_activity[0] = 0;
    a->focus_activity_dirty = true;
  }
  focus_stats_save(a, FOCUS_STATS_PATH);
  if (a->focus_activity_dirty) {
    config_save(&a->cfg, CONFIG_PATH);
    a->focus_activity_dirty = false;
  }
  focus_pick_sync_idx(a);
}
static int focus_stats_find(App *a, const char *name) {
  if (!a || !name || !name[0])
    return -1;
  for (int i = 0; i < a->focus_stats_count; i++) {
    if (strcmp(a->focus_stats[i].name, name) == 0)
      return i;
  }
  return -1;
}
static void focus_stats_ensure_name(App *a, const char *name) {
  if (!a || !name || !name[0])
    return;
  if (focus_stats_find(a, name) >= 0)
    return;
  if (a->focus_stats_count >= MAX_FOCUS_STATS) {
    /* If full, do not evict real stats just to store a
     * label. */
    return;
  }
  safe_snprintf(a->focus_stats[a->focus_stats_count].name,
                sizeof(a->focus_stats[a->focus_stats_count].name), "%s", name);
  a->focus_stats[a->focus_stats_count].seconds = 0;
  a->focus_stats_count++;
  focus_stats_save(a, FOCUS_STATS_PATH);
}
static void focus_stats_add(App *a, const char *name, uint64_t secs) {
  if (!a || secs == 0)
    return;
  const char *n = (name && name[0]) ? name : "unspecified";
  int idx = focus_stats_find(a, n);
  if (idx >= 0) {
    a->focus_stats[idx].seconds += secs;
    return;
  }
  if (a->focus_stats_count >= MAX_FOCUS_STATS) {
    /* If full, merge into the smallest bucket to avoid
     * losing data. */
    int min_i = 0;
    for (int i = 1; i < a->focus_stats_count; i++) {
      if (a->focus_stats[i].seconds < a->focus_stats[min_i].seconds)
        min_i = i;
    }
    safe_snprintf(a->focus_stats[min_i].name,
                  sizeof(a->focus_stats[min_i].name), "%s", n);
    a->focus_stats[min_i].seconds += secs;
    return;
  }
  safe_snprintf(a->focus_stats[a->focus_stats_count].name,
                sizeof(a->focus_stats[a->focus_stats_count].name), "%s", n);
  a->focus_stats[a->focus_stats_count].seconds = secs;
  a->focus_stats_count++;
}
static void activity_log_append_seconds(uint32_t secs);
static void award_focus_seconds(App *a, uint32_t secs) {
  if (!a || secs == 0)
    return;
  a->cfg.focus_total_seconds += (uint64_t)secs;
  a->cfg.focus_total_sessions += 1;
  activity_log_append_seconds(secs);
  if ((uint64_t)secs > a->cfg.focus_longest_span_seconds)
    a->cfg.focus_longest_span_seconds = (uint64_t)secs;
  focus_stats_add(a, a->run_focus_activity, (uint64_t)secs);
  config_save(&a->cfg, CONFIG_PATH);
  focus_stats_save(a, FOCUS_STATS_PATH);
}
static void capture_run_activity(App *a) {
  if (!a)
    return;
  if (a->cfg.focus_activity[0])
    safe_snprintf(a->run_focus_activity, sizeof(a->run_focus_activity), "%s",
                  a->cfg.focus_activity);
  else
    a->run_focus_activity[0] = 0;
}
static void format_focusing_line(App *a, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  const char *act = (a && a->run_focus_activity[0])
                        ? a->run_focus_activity
                        : (a ? a->cfg.focus_activity : NULL);
  if (act && act[0])
    safe_snprintf(out, cap, "focusing on %s...", act);
  else
    safe_snprintf(out, cap, "focusing...");
}
static void format_duration_hm(uint64_t secs, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  uint64_t mins = (secs + 59) / 60;
  uint64_t h = mins / 60;
  uint64_t m = mins % 60;
  if (h == 0)
    safe_snprintf(out, cap, "%llum", (unsigned long long)m);
  else if (m == 0)
    safe_snprintf(out, cap, "%lluh", (unsigned long long)h);
  else
    safe_snprintf(out, cap, "%lluh %llum", (unsigned long long)h,
                  (unsigned long long)m);
}

/* Date string utils moved */
static void format_as_of_today_phrase(char *out, size_t cap) {
  /* e.g. "the tenth of january" */
  if (!out || cap == 0)
    return;
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  safe_snprintf(out, cap, "the %s of %s", day_ordinal_lower(tmv.tm_mday),
                month_name_lower(tmv.tm_mon));
}
static void activity_log_append_seconds(uint32_t secs) {
  /* Append a dated row so we can build a day-based
     activity heatmap. Format: YYYY-MM-DD<TAB>seconds */
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  char d[16];
  strftime(d, sizeof(d), "%Y-%m-%d", &tmv);
  FILE *f = fopen(ACTIVITY_LOG_PATH, "a");
  if (!f)
    return;
  fprintf(f, "%s\t%u\n", d, (unsigned)secs);
  fclose(f);
}

static void tsv_sanitize_inplace(char *s) {
  if (!s)
    return;
  for (char *p = s; *p; p++) {
    if (*p == '\t' || *p == '\n' || *p == '\r')
      *p = ' ';
  }
}

void focus_history_append(App *a, uint32_t secs, const char *status) {

  if (!a || secs == 0 || !status || !status[0])
    return;

  /* Timestamp at minute resolution. */
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  char dt[32];
  strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M", &tmv);

  char act[64];
  if (a->run_focus_activity[0])
    safe_snprintf(act, sizeof(act), "%s", a->run_focus_activity);
  else if (a->cfg.focus_activity[0])
    safe_snprintf(act, sizeof(act), "%s", a->cfg.focus_activity);
  else
    safe_snprintf(act, sizeof(act), "%s", "unspecified");
  tsv_sanitize_inplace(act);

  FILE *f = fopen(FOCUS_HISTORY_PATH, "a");
  if (!f)
    return;
  fprintf(f, "%s\t%u\t%s\t%s\n", dt, (unsigned)secs, status, act);
  fclose(f);
  if (a)
    a->history_dirty = true;
}

static void focus_history_load_to_cache(App *a) {
  if (!a)
    return;
  int cap = 30;
  FocusHistoryRow *out = a->cached_history_rows;
  FILE *f = fopen(FOCUS_HISTORY_PATH, "r");
  if (!f) {
    a->cached_history_count = 0;
    a->history_dirty = false;
    return;
  }

  /* Ring-buffer so we only keep the last cap rows. */
  FocusHistoryRow *ring =
      (FocusHistoryRow *)calloc((size_t)cap, sizeof(FocusHistoryRow));
  if (!ring) {
    fclose(f);
    a->cached_history_count = 0;
    return;
  }

  char line[512];
  int total = 0;
  while (fgets(line, (int)sizeof(line), f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
      line[--n] = 0;

    char *p1 = strtok(line, "\t");
    char *p2 = strtok(NULL, "\t");
    char *p3 = strtok(NULL, "\t");
    char *p4 = strtok(NULL, ""); /* rest of line */

    if (!p1 || !p2 || !p3 || !p4)
      continue;

    FocusHistoryRow r = {0};
    safe_snprintf(r.dt, sizeof(r.dt), "%s", p1);
    r.seconds = (uint32_t)strtoul(p2, NULL, 10);
    safe_snprintf(r.status, sizeof(r.status), "%s", p3);
    safe_snprintf(r.activity, sizeof(r.activity), "%s", p4);
    tsv_sanitize_inplace(r.activity);

    ring[total % cap] = r;
    total++;
  }
  fclose(f);

  int n = total < cap ? total : cap;
  for (int i = 0; i < n; i++) {
    int src = (total - 1 - i);
    if (src < 0)
      break;
    out[i] = ring[src % cap]; /* newest first */
  }
  free(ring);
  a->cached_history_count = n;
  a->history_dirty = false;
}

static void truncate_to_width(UI *ui, TTF_Font *f, const char *in, int max_w,
                              char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = 0;
  if (!in)
    return;
  safe_snprintf(out, out_sz, "%s", in);
  if (max_w <= 0) {
    out[0] = 0;
    return;
  }
  if (text_width(f, out) <= max_w)
    return;

  const char *ell = "...";
  char buf[256];
  safe_snprintf(buf, sizeof(buf), "%s", out);

  int len = (int)strlen(buf);
  while (len > 0) {
    buf[len] = 0;
    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s%s", buf, ell);
    if (text_width(f, tmp) <= max_w) {
      safe_snprintf(out, out_sz, "%s", tmp);
      return;
    }
    len--;
  }
  safe_snprintf(out, out_sz, "%s", ell);
}

/* -------------------------- Focus activity quick-pick
 * -------------------------- */
typedef struct {
  const char *name;
  uint64_t seconds;
} FocusPickEntry;
/* Build a quick-pick list of known activities.
   Order: (empty), then most-used activities (descending
   seconds), ensuring cfg.focus_activity is included.
   Returns count written to out[]. out_cap must be >= 1.
 */
int focus_pick_build(App *a, const char **out, int out_cap) {
  if (!a || !out || out_cap <= 0)
    return 0;
  int n = 0;
  out[n++] = NULL; /* (empty) */
  /* Collect non-empty names from stats. */
  FocusPickEntry tmp[MAX_FOCUS_STATS];
  int t = 0;
  for (int i = 0; i < a->focus_stats_count && t < MAX_FOCUS_STATS; i++) {
    if (!a->focus_stats[i].name[0])
      continue;
    tmp[t].name = a->focus_stats[i].name;
    tmp[t].seconds = a->focus_stats[i].seconds;
    t++;
  }
  /* Sort by seconds desc, name asc. */
  for (int i = 0; i < t - 1; i++) {
    for (int j = i + 1; j < t; j++) {
      bool swap = false;
      if (tmp[j].seconds > tmp[i].seconds)
        swap = true;
      else if (tmp[j].seconds == tmp[i].seconds &&
               strcmp(tmp[j].name, tmp[i].name) < 0)
        swap = true;
      if (swap) {
        FocusPickEntry s = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = s;
      }
    }
  }
  /* Emit unique names. */
  for (int i = 0; i < t && n < out_cap; i++) {
    const char *nm = tmp[i].name;
    bool dup = false;
    for (int k = 1; k < n; k++) {
      if (out[k] && strcmp(out[k], nm) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup)
      out[n++] = nm;
  }
  /* Ensure current cfg.focus_activity is present (if
   * non-empty). */
  if (a->cfg.focus_activity[0] && n < out_cap) {
    bool found = false;
    for (int k = 1; k < n; k++) {
      if (out[k] && strcmp(out[k], a->cfg.focus_activity) == 0) {
        found = true;
        break;
      }
    }
    if (!found)
      out[n++] = a->cfg.focus_activity;
  }
  return n;
}
static void focus_pick_sync_idx(App *a) {
  if (!a)
    return;
  const char *list[1 + MAX_FOCUS_STATS];
  int n = focus_pick_build(a, list, (int)(sizeof(list) / sizeof(list[0])));
  if (n <= 0) {
    a->focus_pick_idx = 0;
    return;
  }
  int idx = 0; /* empty by default */
  if (a->cfg.focus_activity[0]) {
    for (int i = 1; i < n; i++) {
      if (list[i] && strcmp(list[i], a->cfg.focus_activity) == 0) {
        idx = i;
        break;
      }
    }
  }
  a->focus_pick_idx = idx;
}
static void focus_pick_apply_idx(App *a) {
  if (!a)
    return;
  const char *list[1 + MAX_FOCUS_STATS];
  int n = focus_pick_build(a, list, (int)(sizeof(list) / sizeof(list[0])));
  if (n <= 0)
    return;
  if (a->focus_pick_idx < 0)
    a->focus_pick_idx = 0;
  if (a->focus_pick_idx >= n)
    a->focus_pick_idx = n - 1;
  if (a->focus_pick_idx == 0) {
    a->cfg.focus_activity[0] = 0;
  } else if (list[a->focus_pick_idx]) {
    safe_snprintf(a->cfg.focus_activity, sizeof(a->cfg.focus_activity), "%s",
                  list[a->focus_pick_idx]);
  }
  a->focus_activity_dirty = true;
}
static void focus_pick_cycle(App *a, int dir) {
  if (!a || dir == 0)
    return;
  const char *list[1 + MAX_FOCUS_STATS];
  int n = focus_pick_build(a, list, (int)(sizeof(list) / sizeof(list[0])));
  if (n <= 0)
    return;
  if (a->focus_pick_idx < 0)
    a->focus_pick_idx = 0;
  if (a->focus_pick_idx >= n)
    a->focus_pick_idx = n - 1;
  a->focus_pick_idx += dir;
  if (a->focus_pick_idx < 0)
    a->focus_pick_idx = n - 1;
  if (a->focus_pick_idx >= n)
    a->focus_pick_idx = 0;
  focus_pick_apply_idx(a);
}
/* ----------------------------- UI
 * ----------------------------- */
/* (App forward declaration moved above for Tasks
 * helpers.) */
static SDL_Texture *load_texture(SDL_Renderer *ren, const char *path) {
  if (!is_file(path))
    return NULL;
  SDL_Surface *s = IMG_Load(path);
  if (!s)
    return NULL;
  SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
  SDL_FreeSurface(s);
  return t;
}

/* ----------------------------- UI help overlay (MENU
 * hold)
 * ----------------------------- */
static bool timer_main_screen(const App *a) {
  if (!a)
    return false;
  if (a->screen != SCREEN_TIMER)
    return false;
  if (a->settings_open || a->timer_menu_open || a->booklets.open ||
      a->help_overlay_open || a->end_focus_confirm_open ||
      a->end_focus_summary_open || a->meta_selector_open) {
    return false;
  }
  return true;
}
static void help_overlay_close(App *a) {
  if (!a)
    return;
  if (a->help_overlay_tex) {
    SDL_DestroyTexture(a->help_overlay_tex);
    a->help_overlay_tex = NULL;
  }
  a->help_overlay_open = false;
  a->help_overlay_missing = false;
  a->help_overlay_key[0] = '\0';
  a->help_overlay_path[0] = '\0';
}
static const char *screen_name(Screen screen) {
  switch (screen) {
  case SCREEN_MENU:
    return "menu";
  case SCREEN_POMO_PICK:
    return "pomodoro_pick";
  case SCREEN_CUSTOM_PICK:
    return "timer_pick";
  case SCREEN_MEDITATION_PICK:
    return "meditation_pick";
  case SCREEN_TASKS_PICK:
    return "tasks_pick";
  case SCREEN_TASKS_LIST:
    return "tasks_list";
  case SCREEN_TASKS_TEXT:
    return "tasks_text";
  case SCREEN_HABITS_PICK:
    return "habits_pick";
  case SCREEN_HABITS_LIST:
    return "habits_list";
  case SCREEN_FOCUS_MENU:
    return "focus_menu";
  case SCREEN_FOCUS_TEXT:
    return "focus_text";
  case SCREEN_STATS:
    return "statistics";
  case SCREEN_TIMER:
  default:
    return "timer";
  }
}
static const char *help_overlay_key_for(const App *a) {
  if (!a)
    return "timer";

  /* Top-most transient overlays first. */
  if (a->end_focus_summary_open)
    return "end_focus_summary";
  if (a->end_focus_confirm_open)
    return "end_focus_confirm";
  if (a->booklets.open)
    return (a->booklets.mode == 0) ? "booklets_list" : "booklets_viewer";

  if (a->settings_open) {
    switch (a->settings_view) {
    case SET_VIEW_MAIN:
      return "settings_main";
    case SET_VIEW_SCENE:
      return "settings_scene";
    case SET_VIEW_APPEARANCE:
      return "settings_interface";
    case SET_VIEW_FONTS:
      return "settings_font";
    case SET_VIEW_FONT_SIZES:
      return "settings_font_sizes";
    case SET_VIEW_COLORS:
      return "settings_color";
    case SET_VIEW_SOUNDS:
      return "settings_audio";
    case SET_VIEW_SOUND_VOLUME:
      return "settings_volume";
    case SET_VIEW_SOUND_NOTIFICATIONS:
      return "settings_notifications";
    case SET_VIEW_SOUND_MEDITATION:
      return "settings_notifications";
    case SET_VIEW_MISC:
      return "settings_general";
    default:
      return "settings";
    }
  }

  if (a->timer_menu_open)
    return "menu";

  switch (a->screen) {
  case SCREEN_ROUTINE_LIST:
    return "routine_list";
  case SCREEN_ROUTINE_EDIT:
    return "routine_edit";
  case SCREEN_ROUTINE_ENTRY_PICKER:
    return "focus_menu"; /* Re-use logic for now or
                            "menu" */
  case SCREEN_MENU:
    return "menu";
  case SCREEN_POMO_PICK:
    return "pomodoro_pick";
  case SCREEN_CUSTOM_PICK:
    return "timer_pick";
  case SCREEN_MEDITATION_PICK:
    return "meditation_pick";
  case SCREEN_TASKS_PICK:
    return "tasks_pick";
  case SCREEN_TASKS_LIST:
    return "tasks_list";
  case SCREEN_TASKS_TEXT:
    return "tasks_text";
  case SCREEN_HABITS_PICK:
    return "habits_pick";
  case SCREEN_HABITS_LIST:
    return "habits_list";
  case SCREEN_HABITS_TEXT:
    return "habits_text";
  case SCREEN_QUEST:
    return "quest";
  case SCREEN_FOCUS_MENU:
    return "focus_menu";
  case SCREEN_FOCUS_TEXT:
    return "focus_text";
  case SCREEN_STATS:
    return "statistics";
  case SCREEN_TIMER:
  default:
    return "timer";
  }
}
static void help_overlay_open_for(UI *ui, App *a) {
  if (!ui || !a)
    return;

  const char *key = help_overlay_key_for(a);
  if (!key)
    key = "timer";

  /* Refresh if section changed. */
  if (a->help_overlay_open && strcmp(a->help_overlay_key, key) == 0)
    return;

  if (a->help_overlay_tex) {
    SDL_DestroyTexture(a->help_overlay_tex);
    a->help_overlay_tex = NULL;
  }

  a->help_overlay_open = true;
  safe_snprintf(a->help_overlay_key, sizeof(a->help_overlay_key), "%s", key);
  safe_snprintf(a->help_overlay_path, sizeof(a->help_overlay_path),
                "%s/%s%s.png", UI_HELP_OVERLAY_DIR, UI_HELP_OVERLAY_PREFIX,
                key);

  a->help_overlay_missing = !is_file(a->help_overlay_path);
  if (!a->help_overlay_missing) {
    a->help_overlay_tex = load_texture(ui->ren, a->help_overlay_path);
    if (a->help_overlay_tex)
      SDL_SetTextureBlendMode(a->help_overlay_tex, SDL_BLENDMODE_BLEND);
    a->help_overlay_missing = (a->help_overlay_tex == NULL);
  }

  if (a->help_overlay_missing) {
    log_printf("[help] missing overlay: %s", a->help_overlay_path);
  }
}
static void help_overlay_tick(UI *ui, App *a, Buttons *b) {
  if (!a)
    return;
  uint64_t now = now_ms();

  /* While open: release MENU to close. */
  if (a->help_overlay_open) {
    if (!a->menu_btn_down || (b && (b->b || b->select))) {
      help_overlay_close(a);
      a->ui_needs_redraw = true;
    }
    return;
  }

  /* Closed: if MENU is held long enough, open. */
  if (a->menu_btn_down) {
    if (timer_main_screen(a))
      return;
    if ((now - a->menu_btn_down_ms) >= (uint64_t)UI_HELP_OVERLAY_HOLD_MS) {
      help_overlay_open_for(ui, a);
      a->ui_needs_redraw = true;
    }
  }
}
static void draw_help_overlay(UI *ui, App *a) {
  if (!ui || !a)
    return;
  if (!a->help_overlay_open)
    return;

  if (a->help_overlay_tex) {
    SDL_Rect dst = {0, 0, ui->w, ui->h};
    SDL_RenderCopy(ui->ren, a->help_overlay_tex, NULL, &dst);
  } else if (a->help_overlay_missing && a->help_overlay_path[0]) {
    /* Very subtle on-screen hint so missing PNGs don't
     * look like a broken hold.
     */
    SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
    accent.a = 160;
    draw_text(ui, ui->font_small, UI_MARGIN_X, ui->h - UI_MARGIN_BOTTOM - 28,
              a->help_overlay_path, accent, false);
  }
}

/* -------------------------- TextCache helpers
 * -------------------------- */
static bool sdl_color_eq(SDL_Color a, SDL_Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
static void textcache_free(TextCache *c) {
  if (!c)
    return;
  if (c->tex)
    SDL_DestroyTexture(c->tex);
  c->tex = NULL;
  c->font = NULL;
  c->style = 0;
  c->color = (SDL_Color){0, 0, 0, 0};
  c->text[0] = 0;
  c->w = 0;
  c->h = 0;
}
/* Cached version of draw_text_style(): re-renders only
 * when text/font/style/color changes. */
static void draw_text_cached(UI *ui, TextCache *c, TTF_Font *font, int x, int y,
                             const char *text, SDL_Color col, bool right_align,
                             int style) {
  if (!ui || !c)
    return;
  if (!text || !text[0]) {
    textcache_free(c);
    return;
  }

  /* Match draw_text_style() italic companion swap. */
  TTF_Font *use_font = font;
  int use_style = style;
  if (style & TTF_STYLE_ITALIC) {
    if (style & TTF_STYLE_ITALIC) {
      if (font == ui->font_small && ui->font_small_i)
        use_font = ui->font_small_i;
      else if (font == ui->font_med && ui->font_med_i)
        use_font = ui->font_med_i;
      else if (font == ui->font_big && ui->font_big_i)
        use_font = ui->font_big_i;
      if (use_font != font)
        use_style &= ~TTF_STYLE_ITALIC;
    }
  }

  const bool needs_rerender = (!c->tex) || (c->font != use_font) ||
                              (c->style != use_style) ||
                              (!sdl_color_eq(c->color, col)) ||
                              (strncmp(c->text, text, sizeof(c->text)) != 0);

  if (needs_rerender) {
    if (c->tex)
      SDL_DestroyTexture(c->tex);
    c->tex = NULL;

    int old = TTF_GetFontStyle(use_font);
    TTF_SetFontStyle(use_font, use_style);
    SDL_Surface *surf = render_text_surface(use_font, text, col);
    TTF_SetFontStyle(use_font, old);
    if (!surf) {
      c->text[0] = 0;
      c->font = use_font;
      c->style = use_style;
      c->color = col;
      c->w = 0;
      c->h = 0;
      return;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ui->ren, surf);
    if (!tex) {
      SDL_FreeSurface(surf);
      return;
    }

    c->tex = tex;
    c->font = use_font;
    c->style = use_style;
    c->color = col;
    safe_snprintf(c->text, sizeof(c->text), "%s", text);
    c->w = surf->w;
    c->h = surf->h;
    SDL_FreeSurface(surf);
  }

  if (!c->tex)
    return;
  SDL_Rect dst = {x, y, c->w, c->h};
  if (right_align)
    dst.x -= c->w;
  SDL_RenderCopy(ui->ren, c->tex, NULL, &dst);
}
/* --- Animated overlay (tag-driven PNG sequence) --- */
/*
   Rain overlays are tied to the current ambience
   "effect tag". Convention already used elsewhere in
   the app: ambience_name like "it's raining(rain)" =>
   ambience_tag == "rain" Behavior:
   - Settings toggle (cfg.animations) is the master
   enable.
   - The overlay renders whenever ambience_tag is
   non-empty (e.g., rain/snow/fog).
   - Frames are resolved per-location/per-mood first,
   then fall back to a global folder. Expected asset
   layout (preferred, per mood):
     scenes/<location>/<mood>/overlays/<tag>/<tag>_001.png
   ... If mood is empty (location-level mood):
     scenes/<location>/overlays/<tag>/<tag>_001.png ...
   Fallback:
     overlays/<tag>/<tag>_001.png ...
*/
static void anim_overlay_refresh(UI *ui, App *a);
static void anim_overlay_unload(App *a) {
  if (!a)
    return;
  if (a->anim_overlay_frames) {
    for (int i = 0; i < a->anim_overlay_frame_count; i++) {
      if (a->anim_overlay_frames[i])
        SDL_DestroyTexture(a->anim_overlay_frames[i]);
    }
    free(a->anim_overlay_frames);
  }
  a->anim_overlay_frames = NULL;
  a->anim_overlay_frame_count = 0;
  a->anim_overlay_frame_idx = 0;
  a->anim_overlay_accum = 0.0f;
  a->anim_overlay_last_ms = 0;
}
static bool anim_overlay_should_run(const App *a) {
  if (!a)
    return false;
  /* Master switch in cfg.animations */
  if (!a->cfg.animations)
    return false;
  /* Any non-empty ambience tag enables an animated
     overlay of the same name. Example: "it\'s
     raining(rain)" => tag "rain" =>
     overlays/rain/rain_001.png "snowy(snow)"        =>
     tag "snow" => overlays/snow/snow_001.png
   */
  if (!a->ambience_tag[0])
    return false;
  return true;
}
static bool file_exists_local(const char *path);
static bool anim_overlay_resolve_dir(const App *a, char *out, size_t cap) {
  /* Resolve the first existing directory for the
     current ambience_tag. Search order: 1)
     scenes/<location>/<mood>/overlays 2)
     scenes/<location>/overlays 3) overlays The
     directory is considered valid if
     "<dir>/<tag>_001.png" exists. */
  if (!a || !a->ambience_tag[0])
    return false;
  const char *tag = a->ambience_tag;
  char first[512];
  char dir1[512];
  const char *loc =
      (a->scenes.count > 0) ? a->scenes.items[a->scene_idx] : NULL;
  const char *mood = (a->moods.count > 0) ? a->moods.items[a->mood_idx] : NULL;
  if (loc && mood) {
    safe_snprintf(dir1, sizeof(dir1), "scenes/%s/%s/overlays", loc, mood);
    safe_snprintf(first, sizeof(first), "%s/%s_001.png", dir1, tag);
    if (file_exists_local(first)) {
      SDL_strlcpy(out, dir1, cap);
      return true;
    }
  }
  if (loc) {
    safe_snprintf(dir1, sizeof(dir1), "scenes/%s/overlays", loc);
    safe_snprintf(first, sizeof(first), "%s/%s_001.png", dir1, tag);
    if (file_exists_local(first)) {
      SDL_strlcpy(out, dir1, cap);
      return true;
    }
  }
  safe_snprintf(dir1, sizeof(dir1), "overlays");
  safe_snprintf(first, sizeof(first), "%s/%s_001.png", dir1, tag);
  if (file_exists_local(first)) {
    SDL_strlcpy(out, dir1, cap);
    return true;
  }
  return false;
}
static void animations_set_enabled(UI *ui, App *a, bool enabled) {
  if (!a)
    return;
  a->cfg.animations = enabled ? 1 : 0;
  /* When toggled, immediately resync (may load or
   * unload depending on current tag). */
  anim_overlay_refresh(ui, a);
}
static void anim_overlay_refresh(UI *ui, App *a) {
  if (!a)
    return;
  /* Master toggle off: hard disable. */
  if (!a->cfg.animations) {
    a->anim_overlay_enabled = 0;
    anim_overlay_unload(a);
    return;
  }
  /* Only run when an ambience effect tag is present
   * (e.g., rain/snow/fog). */
  if (!anim_overlay_should_run(a)) {
    a->anim_overlay_enabled = 0;
    anim_overlay_unload(a);
    return;
  }
  if (!ui)
    return;
  char dir[PATH_MAX];
  if (!anim_overlay_resolve_dir(a, dir, sizeof(dir))) {
    a->anim_overlay_enabled = 0;
    /* Keep cfg.animations as user preference; just
     * disable rendering. */
    anim_overlay_unload(a);
    return;
  }
  /* Load PNG sequence: <dir>/<tag>_001.png ... until
   * missing. */
  anim_overlay_unload(a);
  /* Default: advance one PNG every 0.50s (tweakable in
   * code; future: config/UI). */
  if (a->anim_overlay_frame_delay_sec <= 0.0f)
    a->anim_overlay_frame_delay_sec = 0.10f;
  a->anim_overlay_accum = 0.0f;
  a->anim_overlay_frame_idx = 0;
  const int MAX_FRAMES = 240;
  SDL_Texture **frames =
      (SDL_Texture **)calloc((size_t)MAX_FRAMES, sizeof(SDL_Texture *));
  if (!frames) {
    a->anim_overlay_enabled = 0;
    return;
  }
  int count = 0;
  for (int i = 1; i <= MAX_FRAMES; i++) {
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "%s/%s_%03d.png", dir, a->ambience_tag,
                  i);
    SDL_Texture *t = load_texture(ui->ren, path);
    if (!t) {
      if (count > 0)
        break;
      continue;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    frames[count++] = t;
  }
  if (count <= 0) {
    free(frames);
    a->anim_overlay_enabled = 0;
    return;
  }
  a->anim_overlay_frames =
      (SDL_Texture **)realloc(frames, (size_t)count * sizeof(SDL_Texture *));
  if (!a->anim_overlay_frames)
    a->anim_overlay_frames = frames;
  a->anim_overlay_frame_count = count;
  a->anim_overlay_enabled = 1;
}
static void anim_overlay_update(App *a) {
  if (!a || !a->anim_overlay_enabled || !a->anim_overlay_frames ||
      a->anim_overlay_frame_count <= 1)
    return;
  int old_idx = a->anim_overlay_frame_idx;
  uint64_t now = now_ms();
  if (a->anim_overlay_last_ms == 0) {
    a->anim_overlay_last_ms = now;
    return;
  }
  float dt = (float)(now - a->anim_overlay_last_ms) / 1000.0f;
  a->anim_overlay_last_ms = now;
  if (dt < 0.0f)
    dt = 0.0f;
  if (dt > 0.25f)
    dt = 0.25f; /* clamp long hitches */
  a->anim_overlay_accum += dt;
  float frame_time = (a->anim_overlay_frame_delay_sec > 0.0f)
                         ? a->anim_overlay_frame_delay_sec
                         : 0.50f;
  while (a->anim_overlay_accum >= frame_time) {
    a->anim_overlay_accum -= frame_time;
    a->anim_overlay_frame_idx =
        (a->anim_overlay_frame_idx + 1) % a->anim_overlay_frame_count;
  }
  if (a->anim_overlay_frame_idx != old_idx) {
    a->ui_needs_redraw = true;
  }
}
static void anim_overlay_draw(UI *ui, App *a) {
  if (!ui || !a || !a->anim_overlay_enabled || !a->anim_overlay_frames ||
      a->anim_overlay_frame_count <= 0)
    return;
  SDL_Texture *t = a->anim_overlay_frames[a->anim_overlay_frame_idx];
  if (!t)
    return;
  SDL_Rect dst = {0, 0, ui->w, ui->h};
  SDL_RenderCopy(ui->ren, t, NULL, &dst);
}
/* --- End animated overlay --- */
/* --- Active-session resume helpers (used when opening
 * other screens while a timer is running) --- */
static void resume_capture(App *a) {
  a->resume_valid = true;
  a->resume_mode = a->mode;
  a->resume_running = a->running;
  a->resume_paused = a->paused;
  a->resume_session_complete = a->session_complete;
  a->resume_hud_hidden = a->hud_hidden;
  a->resume_meta_selector_open = a->meta_selector_open;
  a->resume_meta_selector_sel = a->meta_selector_sel;
  a->resume_run_focus_seconds = a->run_focus_seconds;
  a->resume_custom_total_seconds = a->custom_total_seconds;
  a->resume_custom_remaining_seconds = a->custom_remaining_seconds;
  a->resume_custom_counting_up_active = a->custom_counting_up_active;
  a->resume_meditation_total_seconds = a->meditation_total_seconds;
  a->resume_meditation_remaining_seconds = a->meditation_remaining_seconds;
  a->resume_meditation_elapsed_seconds = a->meditation_elapsed_seconds;
  a->resume_meditation_bell_interval_seconds =
      a->meditation_bell_interval_seconds;
  a->resume_meditation_run_kind = a->meditation_run_kind;
  a->resume_meditation_guided_repeats_total =
      a->meditation_guided_repeats_total;
  a->resume_meditation_guided_repeats_remaining =
      a->meditation_guided_repeats_remaining;
  a->resume_meditation_half_step_counter = a->meditation_half_step_counter;
  a->resume_meditation_bell_strikes_remaining =
      a->meditation_bell_strikes_remaining;
  a->resume_meditation_bell_strike_elapsed = a->meditation_bell_strike_elapsed;
  safe_snprintf(a->resume_meditation_bell_strike_file,
                sizeof(a->resume_meditation_bell_strike_file), "%s",
                a->meditation_bell_strike_file);
  a->resume_stopwatch_seconds = a->stopwatch_seconds;
  a->resume_stopwatch_lap_count = a->stopwatch_lap_count;
  a->resume_stopwatch_lap_base = a->stopwatch_lap_base;
  for (int i = 0; i < MAX_STOPWATCH_LAPS; i++)
    a->resume_stopwatch_laps[i] = a->stopwatch_laps[i];
  a->resume_breath_phase = a->breath_phase;
  a->resume_breath_phase_elapsed = a->breath_phase_elapsed;
  a->resume_pomo_loops_total = a->pomo_loops_total;
  a->resume_pomo_loops_done = a->pomo_loops_done;
  a->resume_pomo_is_break = a->pomo_is_break;
  a->resume_pomo_break_is_long = a->pomo_break_is_long;
  a->resume_pomo_remaining_seconds = a->pomo_remaining_seconds;
  a->resume_pomo_session_in_pomo = a->pomo_session_in_pomo;
  a->resume_pomo_session_seconds = a->pomo_session_seconds;
  a->resume_pomo_break_seconds = a->pomo_break_seconds;
  a->resume_pomo_long_break_seconds = a->pomo_long_break_seconds;
  a->resume_last_tick_ms = a->last_tick_ms;
  a->resume_tick_accum = a->tick_accum;
}
void resume_restore(App *a) {
  if (!a->resume_valid)
    return;
  a->mode = a->resume_mode;
  a->mode_ever_selected = true;
  a->running = a->resume_running;
  a->paused = a->resume_paused;
  a->session_complete = a->resume_session_complete;
  a->hud_hidden = a->resume_hud_hidden;
  a->meta_selector_open = a->resume_meta_selector_open;
  a->meta_selector_sel = a->resume_meta_selector_sel;
  a->run_focus_seconds = a->resume_run_focus_seconds;
  a->custom_total_seconds = a->resume_custom_total_seconds;
  a->custom_remaining_seconds = a->resume_custom_remaining_seconds;
  a->custom_counting_up_active = a->resume_custom_counting_up_active;
  a->meditation_total_seconds = a->resume_meditation_total_seconds;
  a->meditation_remaining_seconds = a->resume_meditation_remaining_seconds;
  a->meditation_elapsed_seconds = a->resume_meditation_elapsed_seconds;
  a->meditation_bell_interval_seconds =
      a->resume_meditation_bell_interval_seconds;
  a->meditation_run_kind = a->resume_meditation_run_kind;
  a->meditation_guided_repeats_total =
      a->resume_meditation_guided_repeats_total;
  a->meditation_guided_repeats_remaining =
      a->resume_meditation_guided_repeats_remaining;
  a->meditation_half_step_counter = a->resume_meditation_half_step_counter;
  a->meditation_bell_strikes_remaining =
      a->resume_meditation_bell_strikes_remaining;
  a->meditation_bell_strike_elapsed = a->resume_meditation_bell_strike_elapsed;
  safe_snprintf(a->meditation_bell_strike_file,
                sizeof(a->meditation_bell_strike_file), "%s",
                a->resume_meditation_bell_strike_file);
  a->stopwatch_seconds = a->resume_stopwatch_seconds;
  a->stopwatch_lap_count = a->resume_stopwatch_lap_count;
  a->stopwatch_lap_base = a->resume_stopwatch_lap_base;
  for (int i = 0; i < MAX_STOPWATCH_LAPS; i++)
    a->stopwatch_laps[i] = a->resume_stopwatch_laps[i];
  a->breath_phase = a->resume_breath_phase;
  a->breath_phase_elapsed = a->resume_breath_phase_elapsed;
  a->pomo_loops_total = a->resume_pomo_loops_total;
  a->pomo_loops_done = a->resume_pomo_loops_done;
  a->pomo_is_break = a->resume_pomo_is_break;
  a->pomo_break_is_long = a->resume_pomo_break_is_long;
  a->pomo_remaining_seconds = a->resume_pomo_remaining_seconds;
  a->pomo_session_in_pomo = a->resume_pomo_session_in_pomo;
  a->pomo_session_seconds = a->resume_pomo_session_seconds;
  a->pomo_break_seconds = a->resume_pomo_break_seconds;
  a->pomo_long_break_seconds = a->resume_pomo_long_break_seconds;
  a->last_tick_ms = a->resume_last_tick_ms;
  a->tick_accum = a->resume_tick_accum;
  a->end_focus_confirm_open = false;
  a->end_focus_summary_open = false;
  a->screen = SCREEN_TIMER;
  a->timer_menu_open = false;
  /* Do not clear resume_valid here; it is cleared when
   * a new session is started explicitly. */
}
static void resume_clear(App *a) { a->resume_valid = false; }
/* Tasks (folder-driven) */
/* Tasks moved to src/features/tasks/ */
/* Habits (weekly tracker) */
/* habits_marks_free removed */
static int date_int_from_ymd(int y, int m, int d) {
  return y * 10000 + m * 100 + d;
}
static int date_int_from_str(const char *s) {
  if (!s)
    return 0;
  int y = 0, m = 0, d = 0;
  if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3)
    return 0;
  return date_int_from_ymd(y, m, d);
}
static void date_str_from_int(int date, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  int y = date / 10000;
  int m = (date / 100) % 100;
  int d = date % 100;
  safe_snprintf(out, out_sz, "%04d-%02d-%02d", y, m, d);
}
/* habits_week_dates removed */
/* Forward declarations for quest helpers that depend on
 * later utilities. */
static int detected_season_rank_for_month(int month);
static const char *detected_season_name_for_month(int month);

static int quest_season_rank_today(void);
/* Quest module moved to features/quest */
static int quest_today_date_int(void) {
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  return date_int_from_ymd(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}
static int quest_season_rank_today(void) {
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  return detected_season_rank_for_month(tmv.tm_mon);
}

static void quest_get_seasonal_text(char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  const char *season = detected_season_name_for_month(tmv.tm_mon);
  safe_snprintf(out, cap, "a %s day, %s of %s.", season ? season : "seasonal",
                day_ordinal_lower(tmv.tm_mday), month_name_lower(tmv.tm_mon));
}
static int quest_date_int_from_string(const char *s) {
  if (!s)
    return 0;
  int y = 0, m = 0, d = 0;
  if (sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3)
    return 0;
  if (y <= 0 || m < 1 || m > 12 || d < 1 || d > 31)
    return 0;
  return (y * 10000) + (m * 100) + d;
}
static int quest_season_rank_for_date_int(int date) {
  if (date <= 0)
    return quest_season_rank_today();
  int month = (date / 100) % 100;
  if (month < 1 || month > 12)
    return quest_season_rank_today();
  return detected_season_rank_for_month(month - 1);
}
static int quest_required_minutes(const App *a) {
  static const int mins[] = {30, 60, 120, 180, 240};
  int idx = (a ? a->cfg.haiku_difficulty : 0);
  if (idx < 0)
    idx = 0;
  if (idx > 4)
    idx = 4;
  return mins[idx];
}
/* ----------------------------- Font loading / reloading
 * ----------------------------- */
static void ui_close_fonts(UI *ui);
static bool ui_open_fonts(UI *ui, const AppConfig *cfg);

static void ui_close_fonts(UI *ui) {
  if (ui->font_small) {
    TTF_CloseFont(ui->font_small);
    ui->font_small = NULL;
  }
  if (ui->font_med) {
    TTF_CloseFont(ui->font_med);
    ui->font_med = NULL;
  }
  if (ui->font_big) {
    TTF_CloseFont(ui->font_big);
    ui->font_big = NULL;
  }
  if (ui->font_small_i) {
    TTF_CloseFont(ui->font_small_i);
    ui->font_small_i = NULL;
  }
  if (ui->font_med_i) {
    TTF_CloseFont(ui->font_med_i);
    ui->font_med_i = NULL;
  }
  if (ui->font_big_i) {
    TTF_CloseFont(ui->font_big_i);
    ui->font_big_i = NULL;
  }
}

static bool ui_font_file_exists(const char *rel_path) {
  return (access(rel_path, F_OK) == 0);
}

static bool ui_find_italic_companion_file(const char *base_font_file,
                                          char *out_file, size_t out_file_sz) {
  if (!base_font_file || !base_font_file[0])
    return false;
  const char *dot = strrchr(base_font_file, '.');
  if (!dot || dot == base_font_file)
    return false;
  char stem[256];
  char ext[64];
  size_t stem_len = (size_t)(dot - base_font_file);
  if (stem_len >= sizeof(stem))
    stem_len = sizeof(stem) - 1;
  memcpy(stem, base_font_file, stem_len);
  stem[stem_len] = 0;
  safe_snprintf(ext, sizeof(ext), "%s", dot);
  const char *fmts[] = {"%s_i%s",      "%s-Italic%s", "%s-italic%s",
                        "%s_Italic%s", "%s_italic%s", "%sItalic%s"};
  char cand_file[256];
  char cand_path[PATH_MAX];
  for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
    safe_snprintf(cand_file, sizeof(cand_file), fmts[i], stem, ext);
    safe_snprintf(cand_path, sizeof(cand_path), "fonts/%s", cand_file);
    if (ui_font_file_exists(cand_path)) {
      safe_snprintf(out_file, out_file_sz, "%s", cand_file);
      return true;
    }
  }
  return false;
}
static bool ui_open_fonts(UI *ui, const AppConfig *cfg) {
  char path[PATH_MAX];
  safe_snprintf(path, sizeof(path), "fonts/%s", cfg->font_file);
  TTF_Font *fs = TTF_OpenFont(path, cfg->font_small_pt);
  TTF_Font *fm = TTF_OpenFont(path, cfg->font_med_pt);
  TTF_Font *fb = TTF_OpenFont(path, cfg->font_big_pt);
  if (!fs || !fm || !fb) {
    if (fs)
      TTF_CloseFont(fs);
    if (fm)
      TTF_CloseFont(fm);
    if (fb)
      TTF_CloseFont(fb);
    return false;
  }
  /* Try to locate a true italic companion font file by
     convention. If not found, we keep the *_i pointers
     NULL and fall back to SDL_ttf faux-italic styling.
   */
  TTF_Font *fs_i = NULL;
  TTF_Font *fm_i = NULL;
  TTF_Font *fb_i = NULL;
  char ital_file[256];
  if (ui_find_italic_companion_file(cfg->font_file, ital_file,
                                    sizeof(ital_file))) {
    char ipath[PATH_MAX];
    safe_snprintf(ipath, sizeof(ipath), "fonts/%s", ital_file);
    fs_i = TTF_OpenFont(ipath, cfg->font_small_pt);
    fm_i = TTF_OpenFont(ipath, cfg->font_med_pt);
    fb_i = TTF_OpenFont(ipath, cfg->font_big_pt);
    /* If the italic companion is partial or fails,
     * disable all italic companions. */
    if (!fs_i || !fm_i || !fb_i) {
      if (fs_i)
        TTF_CloseFont(fs_i);
      if (fm_i)
        TTF_CloseFont(fm_i);
      if (fb_i)
        TTF_CloseFont(fb_i);
      fs_i = fm_i = fb_i = NULL;
    }
  }
  /* Use default font kerning (SDL_ttf kerning enabled).
   */
  TTF_SetFontKerning(fs, 1);
  TTF_SetFontKerning(fm, 1);
  TTF_SetFontKerning(fb, 1);
  if (fs_i)
    TTF_SetFontKerning(fs_i, 1);
  if (fm_i)
    TTF_SetFontKerning(fm_i, 1);
  if (fb_i)
    TTF_SetFontKerning(fb_i, 1);
  ui_close_fonts(ui);
  ui->font_small = fs;
  ui->font_med = fm;
  ui->font_big = fb;
  ui->font_small_i = fs_i;
  ui->font_med_i = fm_i;
  ui->font_big_i = fb_i;
  return true;
}
static void draw_clock_upper_right_stacked(UI *ui, App *a, int xR, int yTop) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);
  const char *hour_word = hour_to_word_12h(lt.tm_hour);
  char minute_word[64];
  minute_to_words(minute_word, sizeof(minute_word), lt.tm_min);

  /* minute (small italic accent) on top, hour (main) at
   * the bottom */
  int yMin = yTop;
  int yHour = yMin + TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;

  draw_text_cached(ui, &a->cache_clock_min, ui->font_small, xR, yMin,
                   minute_word, accent, true, TTF_STYLE_ITALIC);
  draw_text_cached(ui, &a->cache_clock_hour, ui->font_med, xR, yHour, hour_word,
                   main, true, 0);
}
/* Upper-left stacked clock: hour (normal, med), minute
 * (small italic accent).
 */
static void draw_clock_upper_left_stacked(UI *ui, App *a, int xL, int yTop) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);
  const char *hour_word = hour_to_word_12h(lt.tm_hour);
  char minute_word[64];
  minute_to_words(minute_word, sizeof(minute_word), lt.tm_min);

  /* minute (small italic accent) on top, hour (main) at
   * the bottom */
  int yMin = yTop;
  int yHour = yMin + TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;

  draw_text_cached(ui, &a->cache_clock_min, ui->font_small, xL, yMin,
                   minute_word, accent, false, TTF_STYLE_ITALIC);
  draw_text_cached(ui, &a->cache_clock_hour, ui->font_med, xL, yHour, hour_word,
                   main, false, 0);
}
static void draw_battery_upper_right_stacked(UI *ui, App *a, int xR, int yTop) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  battery_tick_update();
  char top_word[64];
  const char *bottom_word = "percent";
  if (g_batt_percent < 0) {
    safe_snprintf(top_word, sizeof(top_word), "battery");
    bottom_word = "n/a";
  } else {
    number_to_words_0_100(top_word, sizeof(top_word), g_batt_percent);
  }

  /* percent (small italic accent) on top, battery
   * (main) at the bottom */
  int yPercent = yTop;
  int yBattery = yPercent + TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;

  draw_text_cached(ui, &a->cache_batt_bottom, ui->font_small, xR, yPercent,
                   bottom_word, accent, true, TTF_STYLE_ITALIC);
  draw_text_cached(ui, &a->cache_batt_top, ui->font_med, xR, yBattery, top_word,
                   main, true, 0);
}
static void sync_font_list(App *a) {
  sl_free(&a->fonts);
  a->fonts = list_font_files_in("fonts");
  if (a->fonts.count == 0) {
    sl_push(&a->fonts, "munro.ttf");
    sl_sort(&a->fonts);
  }
  int idx = sl_find(&a->fonts, a->cfg.font_file);
  a->font_idx = (idx >= 0) ? idx : 0;
  if (a->fonts.count > 0)
    safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s",
                  a->fonts.items[a->font_idx]);
}
static void sync_meditation_bell_indices(App *a);
static void sync_bell_list(App *a) {
  sl_free(&a->bell_sounds);
  a->bell_sounds = list_wav_files_in("sounds");
  if (a->bell_sounds.count == 0) {
    sl_push(&a->bell_sounds, "bell.wav");
    sl_sort(&a->bell_sounds);
  }
  int p = sl_find(&a->bell_sounds, a->cfg.bell_phase_file);
  int d = sl_find(&a->bell_sounds, a->cfg.bell_done_file);
  if (p < 0) {
    safe_snprintf(a->cfg.bell_phase_file, sizeof(a->cfg.bell_phase_file), "%s",
                  a->bell_sounds.items[0]);
    p = 0;
  }
  if (d < 0) {
    safe_snprintf(a->cfg.bell_done_file, sizeof(a->cfg.bell_done_file), "%s",
                  a->bell_sounds.items[0]);
    d = 0;
  }
  a->bell_phase_idx = p;
  a->bell_done_idx = d;
  sync_meditation_bell_indices(a);
}
static void sync_meditation_bell_indices(App *a) {
  int idx = sl_find(&a->bell_sounds, a->cfg.meditation_start_bell_file);
  if (idx < 0 && a->bell_sounds.count > 0) {
    safe_snprintf(a->cfg.meditation_start_bell_file,
                  sizeof(a->cfg.meditation_start_bell_file), "%s",
                  a->bell_sounds.items[0]);
    idx = 0;
  }
  a->meditation_bell_start_idx = idx;

  idx = sl_find(&a->bell_sounds, a->cfg.meditation_interval_bell_file);
  if (idx < 0 && a->bell_sounds.count > 0) {
    safe_snprintf(a->cfg.meditation_interval_bell_file,
                  sizeof(a->cfg.meditation_interval_bell_file), "%s",
                  a->bell_sounds.items[0]);
    idx = 0;
  }
  a->meditation_bell_interval_idx = idx;

  idx = sl_find(&a->bell_sounds, a->cfg.meditation_end_bell_file);
  if (idx < 0 && a->bell_sounds.count > 0) {
    safe_snprintf(a->cfg.meditation_end_bell_file,
                  sizeof(a->cfg.meditation_end_bell_file), "%s",
                  a->bell_sounds.items[0]);
    idx = 0;
  }
  a->meditation_bell_end_idx = idx;
}
static void sync_meditation_guided_list(App *a) {
  sl_free(&a->meditation_guided_sounds);
  a->meditation_guided_sounds = list_audio_files_in("sounds/meditations");
  if (a->meditation_guided_sounds.count == 0) {
    sl_push(&a->meditation_guided_sounds, "none");
    sl_sort(&a->meditation_guided_sounds);
  }
  int idx = (a->cfg.last_meditation_guided_file[0])
                ? sl_find(&a->meditation_guided_sounds,
                          a->cfg.last_meditation_guided_file)
                : -1;
  if (idx < 0)
    idx = 0;
  a->meditation_guided_idx = idx;
}

static void sync_ambience_list(App *a) {
  sl_free(&a->ambience_sounds);
  a->ambience_sounds = (StrList){0};
  /* Dynamic list: show only WAVs actually present in
     sounds/ambience. We store base names (without .wav)
     in cfg.ambience_name. */
  sl_push(&a->ambience_sounds, "off");
  StrList files = list_wav_files_in("sounds/ambience");
  for (int i = 0; i < files.count; i++) {
    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s", files.items[i]);
    strip_ext_inplace(tmp);
    if (tmp[0] && sl_find(&a->ambience_sounds, tmp) < 0) {
      sl_push(&a->ambience_sounds, tmp);
    }
  }
  sl_free(&files);
  int idx = sl_find(&a->ambience_sounds, a->cfg.ambience_name);
  a->ambience_idx = (idx >= 0) ? idx : 0;
  if (idx < 0) {
    safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s",
                  a->ambience_sounds.items[0]);
  }
}
static void ambience_path_from_name(const App *a, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!a || a->scenes.count == 0)
    return;
  const char *loc = a->scenes.items[a->scene_idx];
  const char *mood = (a->moods.count > 0) ? a->moods.items[a->mood_idx] : "";
  char pref[PATH_MAX];
  /* Preferred names: ambience.mp3, then ambience.wav */
  if (mood && mood[0]) {
    safe_snprintf(pref, sizeof(pref), "scenes/%s/%s/ambience.mp3", loc, mood);
    if (is_file(pref)) {
      safe_snprintf(out, cap, "%s", pref);
      return;
    }
    safe_snprintf(pref, sizeof(pref), "scenes/%s/%s/ambience.wav", loc, mood);
    if (is_file(pref)) {
      safe_snprintf(out, cap, "%s", pref);
      return;
    }
  } else {
    safe_snprintf(pref, sizeof(pref), "scenes/%s/ambience.mp3", loc);
    if (is_file(pref)) {
      safe_snprintf(out, cap, "%s", pref);
      return;
    }
    safe_snprintf(pref, sizeof(pref), "scenes/%s/ambience.wav", loc);
    if (is_file(pref)) {
      safe_snprintf(out, cap, "%s", pref);
      return;
    }
  }
  /* Fallback: first audio file in the mood folder. */
  char root[PATH_MAX];
  if (mood && mood[0])
    safe_snprintf(root, sizeof(root), "scenes/%s/%s", loc, mood);
  else
    safe_snprintf(root, sizeof(root), "scenes/%s", loc);
  StrList aud = list_audio_files_in(root);
  if (aud.count > 0) {
    safe_snprintf(out, cap, "%s/%s", root, aud.items[0]);
  }
  sl_free(&aud);
}
/* Forward declarations for ambience-tag/background
 * helpers (defined below). */
static bool split_trailing_tag(const char *in, char *base_out, size_t base_cap,
                               char *tag_out, size_t tag_cap);
static void sync_ambience_tag_from_cfg(App *a);
static int find_weather_idx_for_base_and_tag(App *a, const char *base,
                                             const char *tag_opt);
static void apply_weather_base_with_current_effect(UI *ui, App *a,
                                                   const char *base,
                                                   bool persist_cfg_weather);
static void apply_detected_weather(UI *ui, App *a, bool persist);
/* If cfg.weather is empty (common when user hasn't
   explicitly cycled backgrounds yet), derive the
   current base background name from the currently
   selected weather file. */
static void current_weather_base_from_state(App *a, char *out, size_t cap);
static bool is_weather_variant_filename(const char *filename);
static void apply_ambience_from_cfg(UI *ui, App *a, bool restart_if_same) {
  (void)restart_if_same;
  if (!a)
    return;
  /* Mood-driven ambience: if ambience is OFF, just
   * pause. */
  if (!a->cfg.ambience_enabled) {
    if (a->audio)
      audio_engine_set_ambience_paused(a->audio, true);
    sync_ambience_tag_from_cfg(a);
    return;
  }
  char path[512];
  ambience_path_from_name(a, path, sizeof(path));
  if (!path[0]) {
    if (a->audio)
      audio_engine_set_ambience_paused(a->audio, true);
    return;
  }
  if (a->audio) {
    /* Avoid re-decoding if the same file is selected
       again; just unpause. (Ambience decode is
       synchronous and can feel laggy on slower
       storage.) */
    audio_engine_play_ambience(a->audio, path, restart_if_same);
    audio_engine_set_ambience_paused(a->audio, false);
    audio_engine_set_ambience_volume(a->audio, a->cfg.vol_ambience);
  }
  sync_ambience_tag_from_cfg(a);
  /* Rain overlay depends on ambience_tag (e.g.
   * "(rain)") and on the master toggle. */
  anim_overlay_refresh(ui, a);
  if (a->cfg.detect_time) {
    apply_detected_weather(ui, a, false);
  } else {
    char base[256] = {0};
    if (a->cfg.weather[0]) {
      safe_snprintf(base, sizeof(base), "%s", a->cfg.weather);
    } else {
      current_weather_base_from_state(a, base, sizeof(base));
    }
    if (base[0]) {
      apply_weather_base_with_current_effect(ui, a, base, false);
    }
  }
}
static void refresh_moods_for_location(App *a) {
  sl_free(&a->moods);
  a->moods = (StrList){0};
  if (a->scenes.count == 0)
    return;
  const char *loc = a->scenes.items[a->scene_idx];
  char root[PATH_MAX];
  safe_snprintf(root, sizeof(root), "scenes/%s", loc);

  /* Moods are subfolders under scenes/<loc>/ (excluding
   * reserved containers).
   */
  StrList raw = list_dirs_in(root);
  for (int i = 0; i < raw.count; i++) {
    const char *dn = raw.items[i];
    if (!dn || !dn[0])
      continue;
    if (strcmp(dn, "overlays") == 0)
      continue;
    if (strcmp(dn, "backgrounds") == 0)
      continue;
    sl_push(&a->moods, dn);
  }
  sl_free(&raw);

  /* Support a location-level mood (no subfolder) if
     either:
       - NEW layout backgrounds exist directly under
     scenes/<loc>/, or in season subfolders
       - Legacy scenes/<loc>/backgrounds exists */
  char legacy_bg[PATH_MAX];
  safe_snprintf(legacy_bg, sizeof(legacy_bg), "%s/backgrounds", root);
  if (dir_has_png_jpg(root) || dir_has_subdir_with_png_jpg(root) ||
      is_dir(legacy_bg)) {
    sl_push(&a->moods, "");
  }

  sl_sort(&a->moods);
  int midx = -1;
  if (a->cfg.ambience_name[0])
    midx = sl_find(&a->moods, a->cfg.ambience_name);
  a->mood_idx = (midx >= 0) ? midx : 0;

  /* Keep cfg.ambience_name in sync with selected mood
   * folder. */
  if (a->moods.count > 0) {
    safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s",
                  a->moods.items[a->mood_idx]);
  }
}

/* ----------------------------- Background refresh
 * ----------------------------- */
static void build_backgrounds_root(const App *a, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!a || a->scenes.count == 0)
    return;
  const char *loc = a->scenes.items[a->scene_idx];
  const char *mood = (a->moods.count > 0) ? a->moods.items[a->mood_idx] : "";

  /* Prefer NEW layout:
       scenes/<loc>/<mood>/<phase>/*.png
       scenes/<loc>/<mood>/*.png
     (No "backgrounds/" container.) */
  char base_new[PATH_MAX];
  if (mood && mood[0])
    safe_snprintf(base_new, sizeof(base_new), "scenes/%s/%s", loc, mood);
  else
    safe_snprintf(base_new, sizeof(base_new), "scenes/%s", loc);

  if (is_dir(base_new)) {
    if (a->cfg.season[0]) {
      char cand[PATH_MAX];
      safe_snprintf(cand, sizeof(cand), "%s/%s", base_new, a->cfg.season);
      if (is_dir(cand) && dir_has_png_jpg(cand)) {
        safe_snprintf(out, cap, "%s", cand);
        return;
      }
    }
    if (dir_has_png_jpg(base_new) || dir_has_subdir_with_png_jpg(base_new)) {
      safe_snprintf(out, cap, "%s", base_new);
      return;
    }
  }

  /* Fallback OLD layout (legacy):
       scenes/<loc>/<mood>/backgrounds/<phase>/*.png
       scenes/<loc>/<mood>/backgrounds/*.png */
  char base_old[PATH_MAX];
  if (mood && mood[0])
    safe_snprintf(base_old, sizeof(base_old), "scenes/%s/%s/backgrounds", loc,
                  mood);
  else
    safe_snprintf(base_old, sizeof(base_old), "scenes/%s/backgrounds", loc);

  if (a->cfg.season[0]) {
    char cand[PATH_MAX];
    safe_snprintf(cand, sizeof(cand), "%s/%s", base_old, a->cfg.season);
    if (is_dir(cand)) {
      safe_snprintf(out, cap, "%s", cand);
      return;
    }
  }
  safe_snprintf(out, cap, "%s", base_old);
}

static void build_backgrounds_base_root(const App *a, char *out, size_t cap) {
  /* Base backgrounds root WITHOUT season folder.
     Prefer the NEW layout (no "backgrounds/") when
     present, else fall back to legacy. */
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!a || a->scenes.count == 0)
    return;
  const char *loc = a->scenes.items[a->scene_idx];
  const char *mood = (a->moods.count > 0) ? a->moods.items[a->mood_idx] : "";

  char base_new[PATH_MAX];
  if (mood && mood[0])
    safe_snprintf(base_new, sizeof(base_new), "scenes/%s/%s", loc, mood);
  else
    safe_snprintf(base_new, sizeof(base_new), "scenes/%s", loc);

  if (is_dir(base_new) &&
      (dir_has_png_jpg(base_new) || dir_has_subdir_with_png_jpg(base_new))) {
    safe_snprintf(out, cap, "%s", base_new);
    return;
  }

  if (mood && mood[0])
    safe_snprintf(out, cap, "scenes/%s/%s/backgrounds", loc, mood);
  else
    safe_snprintf(out, cap, "scenes/%s/backgrounds", loc);
}

static void seasons_init(App *a) {
  /* Seasons are optional subfolders under the current
     backgrounds root. NEW layout:
     scenes/<loc>/<mood>/<phase>/*.png Legacy layout:
     scenes/<loc>/<mood>/backgrounds/<phase>/*.png Users
     may prefix season folder names with "(i) " .. "(iv)
     " to force ordering. */
  if (!a)
    return;
  sl_free(&a->seasons);
  a->seasons = (StrList){0};
  char base[PATH_MAX];
  build_backgrounds_base_root(a, base, sizeof(base));
  if (base[0] && is_dir(base)) {
    StrList dirs = list_dirs_in(base);
    for (int i = 0; i < dirs.count; i++) {
      const char *dn = dirs.items[i];
      if (!dn || !dn[0])
        continue;
      if (strcmp(dn, "overlays") == 0)
        continue;
      if (strcmp(dn, "backgrounds") == 0)
        continue;
      char p[PATH_MAX];
      safe_snprintf(p, sizeof(p), "%s/%s", base, dn);
      if (is_dir(p) && dir_has_png_jpg(p)) {
        sl_push(&a->seasons, dn);
      }
    }
    sl_free(&dirs);
    if (a->seasons.count > 1)
      sl_sort_phases(&a->seasons);
  }
  if (a->seasons.count > 0) {
    int idx = -1;
    if (a->cfg.season[0])
      idx = sl_find(&a->seasons, a->cfg.season);
    if (idx < 0)
      idx = 0;
    a->season_idx = idx;
    safe_snprintf(a->cfg.season, sizeof(a->cfg.season), "%s",
                  a->seasons.items[a->season_idx]);
  } else {
    /* No season subfolders found. */
    a->season_idx = 0;
    a->cfg.season[0] = 0;
  }
}

static void refresh_weathers_for_scene(App *a) {
  sl_free(&a->weathers);
  sl_free(&a->weather_bases);
  if (a->scenes.count == 0)
    return;
  char path[PATH_MAX];
  build_backgrounds_root(a, path, sizeof(path));
  a->weathers = list_files_png_in(path);
  /* Build a de-duplicated list of base weather names
     for manual browsing. Any file that looks like
     "<base>(<tag>).png" is treated as a variant and
     will NOT appear as a separate selectable item. */
  for (int i = 0; i < a->weathers.count; i++) {
    const char *fn = a->weathers.items[i];
    if (!fn || !fn[0])
      continue;
    char stem[256];
    safe_snprintf(stem, sizeof(stem), "%s", fn);
    /* Strip extension. */
    char *dot = strrchr(stem, '.');
    if (dot)
      *dot = 0;
    /* If it ends with ")" and has a "(" somewhere,
       treat as a variant and take everything before the
       last "(" as the base name. */
    size_t n = strlen(stem);
    if (n >= 3 && stem[n - 1] == ')') {
      char *lp = strrchr(stem, '(');
      if (lp && lp > stem) {
        *lp = 0;
      }
    }
    if (stem[0] && sl_find(&a->weather_bases, stem) < 0) {
      sl_push(&a->weather_bases, stem);
    }
  }
  if (a->weather_bases.count > 0)
    sl_sort_phases(&a->weather_bases);
  /* Select the current background, respecting any
   * ambience effect tag. */
  int idx = -1;
  if (a->cfg.weather[0]) {
    idx = find_weather_idx_for_base_and_tag(a, a->cfg.weather, a->ambience_tag);
  }
  a->weather_idx = (idx >= 0) ? idx : 0;
  /* Select the base index for browsing (never points at
   * variants). */
  a->weather_base_idx = 0;
  if (a->weather_bases.count > 0) {
    if (a->cfg.weather[0]) {
      int bi = sl_find(&a->weather_bases, a->cfg.weather);
      if (bi >= 0)
        a->weather_base_idx = bi;
    } else {
      /* Derive base from the chosen file. */
      const char *fn = a->weathers.items[a->weather_idx];
      char stem[256];
      safe_snprintf(stem, sizeof(stem), "%s", fn ? fn : "");
      char *dot = strrchr(stem, '.');
      if (dot)
        *dot = 0;
      size_t n = strlen(stem);
      if (n >= 3 && stem[n - 1] == ')') {
        char *lp = strrchr(stem, '(');
        if (lp && lp > stem)
          *lp = 0;
      }
      int bi = sl_find(&a->weather_bases, stem);
      if (bi >= 0)
        a->weather_base_idx = bi;
    }
  }
}
static void update_bg_texture(UI *ui, App *a);
static int detected_phase_rank_for_hour(int hour) {
  /* Local-time buckets (hour is 0..23).
     Rank corresponds to leading tags "(i)".."(vi)" when
     present. Phase A â€“ Dawn       => (i) Phase B â€“
     Morning    => (ii) Phase C â€“ Afternoon  => (iii)
     Phase D â€“ Evening    => (iv)
     Phase E â€“ Dusk       => (v)
     Phase F â€“ Night      => (vi)
     If users provide tagged phases, those win.
     If they do not, we fall back to these broad
     buckets.
  */
  if (hour >= 5 && hour < 7)
    return 1; /* dawn */
  if (hour >= 7 && hour < 11)
    return 2; /* morning */
  if (hour >= 11 && hour < 16)
    return 3; /* afternoon */
  if (hour >= 16 && hour < 19)
    return 4; /* evening */
  if (hour >= 19 && hour < 21)
    return 5; /* dusk */
  return 6;   /* night: 21:00â€“05:00 */
}
/* Legacy names, used as a fallback when no
 * "(i)".."(vi)" tagged phases exist.
 */
static const char *detected_weather_name_for_hour(int hour) {
  if (hour >= 5 && hour < 7)
    return "dawn";
  if (hour >= 7 && hour < 11)
    return "morning";
  if (hour >= 11 && hour < 16)
    return "afternoon";
  if (hour >= 16 && hour < 19)
    return "evening";
  if (hour >= 19 && hour < 21)
    return "dusk";
  return "night";
}
static int detected_season_rank_for_month(int month) {
  /* month is 0..11 (Jan..Dec) */
  if (month >= 2 && month <= 4)
    return 1; /* spring: Mar-May */
  if (month >= 5 && month <= 7)
    return 2; /* summer: Jun-Aug */
  if (month >= 8 && month <= 10)
    return 3; /* autumn: Sep-Nov */
  return 4;   /* winter: Dec-Feb */
}
static const char *detected_season_name_for_month(int month) {
  if (month >= 2 && month <= 4)
    return "spring";
  if (month >= 5 && month <= 7)
    return "summer";
  if (month >= 8 && month <= 10)
    return "autumn";
  return "winter";
}
static bool is_weather_variant_filename(const char *filename) {
  if (!filename || !filename[0])
    return false;
  char tmp[256];
  safe_snprintf(tmp, sizeof(tmp), "%s", filename);
  char *dot = strrchr(tmp, '.');
  if (dot)
    *dot = 0;
  size_t n = strlen(tmp);
  if (n < 3)
    return false;
  if (tmp[n - 1] != ')')
    return false;
  const char *lp = strrchr(tmp, '(');
  const char *rp = strrchr(tmp, ')');
  return (lp && rp && rp > lp && rp[1] == '\0' && lp > tmp);
}
/* ----------------------------- Tag helpers
 * ----------------------------- */
static bool tag_token_is_valid(const char *tag) {
  /* We only treat "(tag)" as a semantic suffix when tag
     looks like a real token (e.g., rain, snow, fog,
     wind_2). This avoids mis-parsing cosmetic
     parentheses used for display-safe folder names like
     "(...)" or "(.)". */
  if (!tag || !tag[0])
    return false;
  for (const unsigned char *p = (const unsigned char *)tag; *p; p++) {
    unsigned char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      continue;
    }
    return false;
  }
  return true;
}
/* Extract a trailing "(tag)" suffix from a name (no
   extension). Example: in="it's raining(rain)" =>
   base="it's raining", tag="rain". Returns true if a
   trailing tag is found, false otherwise. */
static bool split_trailing_tag(const char *in, char *base_out, size_t base_cap,
                               char *tag_out, size_t tag_cap) {
  if (!in || !in[0])
    return false;
  if (base_out && base_cap)
    base_out[0] = 0;
  if (tag_out && tag_cap)
    tag_out[0] = 0;
  const char *lp = strrchr(in, '(');
  const char *rp = strrchr(in, ')');
  if (!lp || !rp || rp < lp) {
    safe_snprintf(base_out, base_cap, "%s", in);
    return false;
  }
  /* Only accept a trailing "(tag)" at the very end. */
  if (rp[1] != '\0') {
    safe_snprintf(base_out, base_cap, "%s", in);
    return false;
  }
  size_t base_len = (size_t)(lp - in);
  size_t tag_len = (size_t)(rp - (lp + 1));
  if (tag_len == 0) {
    safe_snprintf(base_out, base_cap, "%s", in);
    return false;
  }
  if (base_out && base_cap) {
    size_t n = (base_len < base_cap - 1) ? base_len : (base_cap - 1);
    memcpy(base_out, in, n);
    base_out[n] = 0;
    trim_ascii_inplace(base_out);
  }
  if (tag_out && tag_cap) {
    size_t n = (tag_len < tag_cap - 1) ? tag_len : (tag_cap - 1);
    memcpy(tag_out, lp + 1, n);
    tag_out[n] = 0;
    trim_ascii_inplace(tag_out);
  }
  if (!tag_token_is_valid(tag_out)) {
    /* Treat invalid tokens as plain text, not as a
     * semantic tag. */
    if (base_out && base_cap)
      safe_snprintf(base_out, base_cap, "%s", in);
    if (tag_out && tag_cap)
      tag_out[0] = 0;
    return false;
  }
  return true;
}
/* Keep App::ambience_tag in sync with
 * cfg.ambience_name. Convention: no spaces, e.g.
 * "morning(rain)". */
static void sync_ambience_tag_from_cfg(App *a) {
  if (!a)
    return;
  a->ambience_tag[0] = 0;
  if (!a->cfg.ambience_enabled)
    return;
  if (!a->cfg.ambience_name[0])
    return;
  if (strcmp(a->cfg.ambience_name, "off") == 0)
    return;
  /* Only derive an ambience tag when there is an actual
     ambience audio file for this mood. This keeps
     visual-only moods from accidentally enabling
     weather effects/animations. */
  {
    char p[PATH_MAX];
    p[0] = 0;
    ambience_path_from_name(a, p, sizeof(p));
    if (!p[0])
      return;
  }
  char base[256] = {0};
  char tag[64] = {0};
  if (split_trailing_tag(a->cfg.ambience_name, base, sizeof(base), tag,
                         sizeof(tag))) {
    if (tag[0])
      safe_snprintf(a->ambience_tag, sizeof(a->ambience_tag), "%s", tag);
  }
}
/* Find the best matching background file for base +
   optional tag. Tries: base(tag).png/jpg, then
   base.png/jpg. */
static int find_weather_idx_for_base_and_tag(App *a, const char *base,
                                             const char *tag_opt) {
  if (!a || !base || !base[0])
    return -1;
  char want[256];
  if (tag_opt && tag_opt[0]) {
    safe_snprintf(want, sizeof(want), "%s(%s).png", base, tag_opt);
    int idx = sl_find(&a->weathers, want);
    if (idx >= 0)
      return idx;
    safe_snprintf(want, sizeof(want), "%s(%s).jpg", base, tag_opt);
    idx = sl_find(&a->weathers, want);
    if (idx >= 0)
      return idx;
  }
  safe_snprintf(want, sizeof(want), "%s.png", base);
  int idx = sl_find(&a->weathers, want);
  if (idx >= 0)
    return idx;
  safe_snprintf(want, sizeof(want), "%s.jpg", base);
  idx = sl_find(&a->weathers, want);
  if (idx >= 0)
    return idx;
  return -1;
}
static void current_weather_base_from_state(App *a, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!a)
    return;
  if (a->weathers.count <= 0)
    return;
  if (a->weather_idx < 0 || a->weather_idx >= a->weathers.count)
    return;
  const char *fn = a->weathers.items[a->weather_idx];
  if (!fn || !fn[0])
    return;
  char stem[256];
  safe_snprintf(stem, sizeof(stem), "%s", fn);
  char *dot = strrchr(stem, '.');
  if (dot)
    *dot = 0;
  char base[256] = {0};
  char tag[128] = {0};
  if (split_trailing_tag(stem, base, sizeof(base), tag, sizeof(tag))) {
    safe_snprintf(out, cap, "%s", base);
  } else {
    safe_snprintf(out, cap, "%s", stem);
  }
  trim_ascii_inplace(out);
}
/* Apply a base weather name, while respecting the
 * current ambience effect tag (if any). */
static void apply_weather_base_with_current_effect(UI *ui, App *a,
                                                   const char *base,
                                                   bool persist_cfg_weather) {
  if (!a || !base || !base[0])
    return;
  int idx = find_weather_idx_for_base_and_tag(a, base, a->ambience_tag);
  if (idx < 0)
    return; /* scene doesn't provide this weather (or
               any variant) */
  a->weather_idx = idx;
  /* Persist base name only (never persist "(tag)"
   * variants). */
  if (persist_cfg_weather) {
    safe_snprintf(a->cfg.weather, sizeof(a->cfg.weather), "%s", base);
  }
  if (ui)
    update_bg_texture(ui, a);
}
static bool season_name_matches(const char *candidate, const char *want) {
  if (!candidate || !want)
    return false;
  char buf[96];
  safe_snprintf(buf, sizeof(buf), "%s", candidate);
  trim_ascii_inplace(buf);
  ascii_lower_inplace(buf);
  if (strcmp(buf, want) == 0)
    return true;
  if (strcmp(want, "autumn") == 0 && strcmp(buf, "fall") == 0)
    return true;
  return false;
}
static void apply_detected_season(UI *ui, App *a, bool persist) {
  if (!a || !a->cfg.detect_time)
    return;
  if (a->seasons.count == 0)
    return;
  time_t now = time(NULL);
  struct tm lt;
  localtime_r(&now, &lt);
  const int want_rank = detected_season_rank_for_month(lt.tm_mon);
  const char *want_name = detected_season_name_for_month(lt.tm_mon);
  int want_idx = -1;
  for (int i = 0; i < a->seasons.count; i++) {
    const char *s = a->seasons.items[i];
    if (phase_rank_from_leading_tag(s) == want_rank) {
      want_idx = i;
      break;
    }
  }
  if (want_idx < 0) {
    for (int i = 0; i < a->seasons.count; i++) {
      const char *s = a->seasons.items[i];
      if (!s || !s[0])
        continue;
      if (season_name_matches(phase_strip_leading_tag(s), want_name)) {
        want_idx = i;
        break;
      }
    }
  }
  if (want_idx < 0)
    return;
  const char *want = a->seasons.items[want_idx];
  bool changed = (a->season_idx != want_idx);
  if (a->cfg.season[0] && want) {
    if (strcmp(a->cfg.season, want) != 0)
      changed = true;
  } else if (want && want[0]) {
    changed = true;
  }
  if (!changed)
    return;
  a->season_idx = want_idx;
  if (want && want[0]) {
    safe_snprintf(a->cfg.season, sizeof(a->cfg.season), "%s", want);
  }
  refresh_weathers_for_scene(a);
  if (ui)
    update_bg_texture(ui, a);
  if (persist)
    config_save(&a->cfg, CONFIG_PATH);
}
static void apply_detected_weather(UI *ui, App *a, bool persist) {
  if (!a || !a->cfg.detect_time)
    return;
  time_t now = time(NULL);
  struct tm lt;
  localtime_r(&now, &lt);
  /* Prefer "(i)".."(vi)" tagged phases if present, so
   * users can rename phases freely. */
  const int want_rank = detected_phase_rank_for_hour(lt.tm_hour);
  const char *want_base = NULL;
  if (a->weather_bases.count > 0) {
    for (int i = 0; i < a->weather_bases.count; i++) {
      const char *b = a->weather_bases.items[i];
      if (phase_rank_from_leading_tag(b) == want_rank) {
        want_base = b;
        break;
      }
    }
  }
  /* Fallback to legacy names if no tagged phases exist.
   */
  if (!want_base)
    want_base = detected_weather_name_for_hour(lt.tm_hour);
  apply_weather_base_with_current_effect(ui, a, want_base, true);
  if (persist)
    config_save(&a->cfg, CONFIG_PATH);
}
/* ----------------------------- Stack Blur
 * ----------------------------- */
static void stack_blur_rgba(Uint32 *pix, int w, int h, int radius) {
  if (radius < 1)
    return;
  int wm = w - 1;
  int hm = h - 1;
  int wh = w * h;
  int div = radius + radius + 1;
  int *r = (int *)malloc(wh * sizeof(int));
  int *g = (int *)malloc(wh * sizeof(int));
  int *b = (int *)malloc(wh * sizeof(int));
  int rsum, gsum, bsum, x, y, i, p, yp, yi, yw;
  int *vmin = (int *)malloc((MAX(w, h)) * sizeof(int));
  int divsum = (div + 1) >> 1;
  divsum *= divsum;
  int *dv = (int *)malloc(256 * divsum * sizeof(int));
  for (i = 0; i < 256 * divsum; i++) {
    dv[i] = (i / divsum);
  }
  yw = yi = 0;
  int **stack = (int **)malloc(div * sizeof(int *));
  for (i = 0; i < div; i++)
    stack[i] = (int *)malloc(3 * sizeof(int));
  int stackpointer;
  int stackstart;
  int *sir;
  int rbs;
  int r1 = radius + 1;
  int routsum, goutsum, boutsum;
  int rinsum, ginsum, binsum;

  for (y = 0; y < h; y++) {
    rinsum = ginsum = binsum = routsum = goutsum = boutsum = rsum = gsum =
        bsum = 0;
    for (i = -radius; i <= radius; i++) {
      p = pix[yi + MIN(wm, MAX(i, 0))];
      sir = stack[i + radius];
      sir[0] = (p & 0xff0000) >> 16;
      sir[1] = (p & 0x00ff00) >> 8;
      sir[2] = (p & 0x0000ff);
      rbs = r1 - abs(i);
      rsum += sir[0] * rbs;
      gsum += sir[1] * rbs;
      bsum += sir[2] * rbs;
      if (i > 0) {
        rinsum += sir[0];
        ginsum += sir[1];
        binsum += sir[2];
      } else {
        routsum += sir[0];
        goutsum += sir[1];
        boutsum += sir[2];
      }
    }
    stackpointer = radius;

    for (x = 0; x < w; x++) {
      r[yi] = dv[rsum];
      g[yi] = dv[gsum];
      b[yi] = dv[bsum];

      rsum -= routsum;
      gsum -= goutsum;
      bsum -= boutsum;

      stackstart = stackpointer - radius + div;
      sir = stack[stackstart % div];

      routsum -= sir[0];
      goutsum -= sir[1];
      boutsum -= sir[2];

      if (y == 0) {
        vmin[x] = MIN(x + radius + 1, wm);
      }
      p = pix[yw + vmin[x]];

      sir[0] = (p & 0xff0000) >> 16;
      sir[1] = (p & 0x00ff00) >> 8;
      sir[2] = (p & 0x0000ff);

      rinsum += sir[0];
      ginsum += sir[1];
      binsum += sir[2];

      rsum += rinsum;
      gsum += ginsum;
      bsum += binsum;

      stackpointer = (stackpointer + 1) % div;
      sir = stack[(stackpointer) % div];

      routsum += sir[0];
      goutsum += sir[1];
      boutsum += sir[2];

      rinsum -= sir[0];
      ginsum -= sir[1];
      binsum -= sir[2];

      yi++;
    }
    yw += w;
  }
  for (x = 0; x < w; x++) {
    rinsum = ginsum = binsum = routsum = goutsum = boutsum = rsum = gsum =
        bsum = 0;
    yp = -radius * w;
    for (i = -radius; i <= radius; i++) {
      yi = MAX(0, yp) + x;
      sir = stack[i + radius];
      sir[0] = r[yi];
      sir[1] = g[yi];
      sir[2] = b[yi];
      rbs = r1 - abs(i);
      rsum += r[yi] * rbs;
      gsum += g[yi] * rbs;
      bsum += b[yi] * rbs;
      if (i > 0) {
        rinsum += sir[0];
        ginsum += sir[1];
        binsum += sir[2];
      } else {
        routsum += sir[0];
        goutsum += sir[1];
        boutsum += sir[2];
      }
      if (i < hm) {
        yp += w;
      }
    }
    yi = x;
    stackpointer = radius;
    for (y = 0; y < h; y++) {
      pix[yi] = (0xff000000) | (dv[rsum] << 16) | (dv[gsum] << 8) | dv[bsum];

      rsum -= routsum;
      gsum -= goutsum;
      bsum -= boutsum;

      stackstart = stackpointer - radius + div;
      sir = stack[stackstart % div];

      routsum -= sir[0];
      goutsum -= sir[1];
      boutsum -= sir[2];

      if (x == 0) {
        vmin[y] = MIN(y + r1, hm) * w;
      }
      p = x + vmin[y];

      sir[0] = r[p];
      sir[1] = g[p];
      sir[2] = b[p];

      rinsum += sir[0];
      ginsum += sir[1];
      binsum += sir[2];

      rsum += rinsum;
      gsum += ginsum;
      bsum += binsum;

      stackpointer = (stackpointer + 1) % div;
      sir = stack[stackpointer];

      routsum += sir[0];
      goutsum += sir[1];
      boutsum += sir[2];

      rinsum -= sir[0];
      ginsum -= sir[1];
      binsum -= sir[2];

      yi += w;
    }
  }
  free(r);
  free(g);
  free(b);
  free(vmin);
  free(dv);
  for (i = 0; i < div; i++)
    free(stack[i]);
  free(stack);
}

static void update_bg_texture(UI *ui, App *a) {
  if (ui->bg_tex) {
    SDL_DestroyTexture(ui->bg_tex);
    ui->bg_tex = NULL;
  }
  if (ui->bg_blur_tex) {
    SDL_DestroyTexture(ui->bg_blur_tex);
    ui->bg_blur_tex = NULL;
  }
  if (a->scenes.count == 0 || a->weathers.count == 0)
    return;
  const char *scene = a->scenes.items[a->scene_idx];
  const char *file = a->weathers.items[a->weather_idx];
  char root[PATH_MAX];
  build_backgrounds_root(a, root, sizeof(root));
  char path[PATH_MAX];
  safe_snprintf(path, sizeof(path), "%s/%s", root, file);

  /* Load surface directly to generate both normal and
   * blurred textures */
  if (is_file(path)) {
    SDL_Surface *s = IMG_Load(path);
    if (s) {
      ui->bg_tex = SDL_CreateTextureFromSurface(ui->ren, s);

      /* Generate blurred version */
      int sw = ui->w / 8;
      int sh = ui->h / 8;
      if (sw < 16)
        sw = 16;
      if (sh < 16)
        sh = 16;

      /* Convert source to ARGB8888 for safe pixel
       * reading */
      SDL_Surface *s_argb =
          SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
      if (s_argb) {
        Uint32 *blur_buf = (Uint32 *)malloc(sw * sh * sizeof(Uint32));
        if (blur_buf) {
          /* Manual nearest-neighbor downscale to avoid
           * BlitScaled issues */
          if (SDL_MUSTLOCK(s_argb))
            SDL_LockSurface(s_argb);

          Uint8 *src_bytes = (Uint8 *)s_argb->pixels;
          int src_pitch = s_argb->pitch;
          int src_w = s_argb->w;
          int src_h = s_argb->h;

          for (int y = 0; y < sh; y++) {
            int sy = y * src_h / sh;
            Uint8 *row = src_bytes + (sy * src_pitch);
            for (int x = 0; x < sw; x++) {
              int sx = x * src_w / sw;
              /* ARGB8888 is 4 bytes per pixel */
              blur_buf[y * sw + x] = *(Uint32 *)(row + sx * 4);
            }
          }

          if (SDL_MUSTLOCK(s_argb))
            SDL_UnlockSurface(s_argb);

          /* Blur the buffer */
          stack_blur_rgba(blur_buf, sw, sh, 2);

          /* Create texture from buffer using a
           * temporary surface wrapper */
          SDL_Surface *blur_surf = SDL_CreateRGBSurfaceWithFormatFrom(
              blur_buf, sw, sh, 32, sw * 4, SDL_PIXELFORMAT_ARGB8888);

          if (blur_surf) {
            ui->bg_blur_tex = SDL_CreateTextureFromSurface(ui->ren, blur_surf);
            if (ui->bg_blur_tex) {
              SDL_SetTextureScaleMode(ui->bg_blur_tex, SDL_ScaleModeLinear);
              SDL_SetTextureBlendMode(ui->bg_blur_tex, SDL_BLENDMODE_NONE);
            }
            SDL_FreeSurface(blur_surf);
          }
          free(blur_buf);
        }
        SDL_FreeSurface(s_argb);
      }

      SDL_FreeSurface(s);
    }
  }
  /* "scene_name" is the LOCATION label (e.g., Home).
   * Mood is shown on the
   * "...and ..." line. */
  safe_snprintf(a->scene_name, sizeof(a->scene_name), "%s",
                scene ? scene : "home");
  char tmp[256];
  safe_snprintf(tmp, sizeof(tmp), "%s", file);
  char *dot = strrchr(tmp, '.');
  if (dot)
    *dot = 0;
  /* Display base name only (strip trailing "(tag)" from
   * variants like "morning(rain)"). */
  {
    char base[256] = {0};
    char tag[128] = {0};
    if (split_trailing_tag(tmp, base, sizeof(base), tag, sizeof(tag))) {
      safe_snprintf(a->weather_name, sizeof(a->weather_name), "%s",
                    phase_strip_leading_tag(base));
    } else {
      safe_snprintf(a->weather_name, sizeof(a->weather_name), "%s",
                    phase_strip_leading_tag(tmp));
    }
  }
}
static void persist_scene_weather(App *a) {
  /* Persist mood selection (folder name under
   * scenes/<location>/). */
  if (a->moods.count > 0) {
    safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s",
                  a->moods.items[a->mood_idx]);
  }
  /* Persist current phase/base background name into
   * cfg.weather. */
  if (a->weathers.count > 0) {
    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s", a->weathers.items[a->weather_idx]);
    char *dot = strrchr(tmp, '.');
    if (dot)
      *dot = 0;
    /* Persist base name only (strip trailing "(tag)"
     * from variants like "morning(rain)"). */
    {
      char base[256] = {0};
      char tag[128] = {0};
      if (split_trailing_tag(tmp, base, sizeof(base), tag, sizeof(tag))) {
        safe_snprintf(a->cfg.weather, sizeof(a->cfg.weather), "%s", base);
      } else {
        safe_snprintf(a->cfg.weather, sizeof(a->cfg.weather), "%s", tmp);
      }
    }
  }
  config_save(&a->cfg, CONFIG_PATH);
}
static void persist_colors(App *a) { config_save(&a->cfg, CONFIG_PATH); }
static void persist_fonts(App *a) { config_save(&a->cfg, CONFIG_PATH); }
/* ----------------------------- Input mapping
 * ----------------------------- */
static void buttons_clear(Buttons *b) { memset(b, 0, sizeof(*b)); }
static bool is_b_action_button(SDL_GameControllerButton cb, int swap_ab) {
  bool a_press = (cb == SDL_CONTROLLER_BUTTON_A);
  bool b_press = (cb == SDL_CONTROLLER_BUTTON_B);
  if (swap_ab) {
    bool t = a_press;
    a_press = b_press;
    b_press = t;
  }
  return b_press;
}
static bool is_b_action_key(SDL_Keycode k, int swap_ab) {
  bool a_press = (k == SDLK_RETURN);
  bool b_press = (k == SDLK_ESCAPE || k == SDLK_BACKSPACE);
  if (swap_ab) {
    bool t = a_press;
    a_press = b_press;
    b_press = t;
  }
  return b_press;
}
static void map_button(Buttons *b, SDL_ControllerButtonEvent cbe, int swap_ab) {
  SDL_GameControllerButton cb = (SDL_GameControllerButton)cbe.button;
  if (cb == SDL_CONTROLLER_BUTTON_DPAD_UP)
    b->up = true;
  if (cb == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
    b->down = true;
  if (cb == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
    b->left = true;
  if (cb == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
    b->right = true;
  if (cb == SDL_CONTROLLER_BUTTON_START)
    b->start = true;
  if (cb == SDL_CONTROLLER_BUTTON_GUIDE)
    b->menu = true;
  if (cb == SDL_CONTROLLER_BUTTON_BACK)
    b->select = true;
  if (cb == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
    b->l1 = true;
  if (cb == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
    b->r1 = true;
  if (cb == SDL_CONTROLLER_BUTTON_LEFTSTICK)
    b->l3 = true;
  if (cb == SDL_CONTROLLER_BUTTON_RIGHTSTICK)
    b->r3 = true;
  bool a_press = (cb == SDL_CONTROLLER_BUTTON_A);
  bool b_press = (cb == SDL_CONTROLLER_BUTTON_B);
  if (swap_ab) {
    bool t = a_press;
    a_press = b_press;
    b_press = t;
  }
  if (a_press)
    b->a = true;
  if (b_press)
    b->b = true;
  if (cb == SDL_CONTROLLER_BUTTON_X)
    b->x = true;
  if (cb == SDL_CONTROLLER_BUTTON_Y)
    b->y = true;
}
static void map_key(Buttons *b, SDL_KeyboardEvent ke, int swap_ab) {
  SDL_Keycode k = ke.keysym.sym;
  if (k == SDLK_UP)
    b->up = true;
  if (k == SDLK_DOWN)
    b->down = true;
  if (k == SDLK_LEFT)
    b->left = true;
  if (k == SDLK_RIGHT)
    b->right = true;
  if (k == SDLK_TAB)
    b->start = true;
  bool a_press = (k == SDLK_RETURN);
  bool b_press = (k == SDLK_ESCAPE || k == SDLK_BACKSPACE);
  if (swap_ab) {
    bool t = a_press;
    a_press = b_press;
    b_press = t;
  }
  if (a_press)
    b->a = true;
  if (b_press)
    b->b = true;
  if (k == SDLK_x)
    b->x = true;
  if (k == SDLK_y)
    b->y = true;
}
/* ----------------------------- Timing
 * ----------------------------- */
#define BREATH_INHALE_SECONDS 4
#define BREATH_HOLD_SECONDS 7
#define BREATH_EXHALE_SECONDS 8
#define BREATH_POST_HOLD_SECONDS 4
#define BREATH_FPS 30
#define BREATH_STEP_SECONDS (1.0f / (float)BREATH_FPS)
#define BREATH_SCALE_MIN 1.0f
#define BREATH_SCALE_MAX 1.1f

static void fmt_mmss(char *out, size_t cap, uint32_t sec) {
  uint32_t m = sec / 60;
  uint32_t s = sec % 60;
  safe_snprintf(out, cap, "%02u:%02u", m, s);
}
static void fmt_hhmm(char *out, size_t cap, uint32_t sec) {
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  safe_snprintf(out, cap, "%02u:%02u", h, m);
}
// Custom timer and stopwatch formatting with seconds,
// hiding hours when it's 0.
static void fmt_hms_opt_hours(char *out, size_t cap, uint32_t sec) {
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  uint32_t s = sec % 60;
  if (h == 0)
    safe_snprintf(out, cap, "%02u:%02u", m, s);
  else
    safe_snprintf(out, cap, "%02u:%02u:%02u", h, m, s);
}
static uint32_t breath_phase_steps(int phase) {
  switch (phase) {
  case BREATH_PHASE_INHALE:
    return (uint32_t)(BREATH_INHALE_SECONDS / BREATH_STEP_SECONDS);
  case BREATH_PHASE_HOLD:
    return (uint32_t)(BREATH_HOLD_SECONDS / BREATH_STEP_SECONDS);
  case BREATH_PHASE_EXHALE:
    return (uint32_t)(BREATH_EXHALE_SECONDS / BREATH_STEP_SECONDS);
  case BREATH_PHASE_HOLD_POST:
    return (uint32_t)(BREATH_POST_HOLD_SECONDS / BREATH_STEP_SECONDS);
  default:
    return (uint32_t)(BREATH_INHALE_SECONDS / BREATH_STEP_SECONDS);
  }
}
static const char *breath_phase_label(int phase) {
  switch (phase) {
  case BREATH_PHASE_INHALE:
    return "inhale";
  case BREATH_PHASE_HOLD:
    return "hold";
  case BREATH_PHASE_EXHALE:
    return "exhale";
  case BREATH_PHASE_HOLD_POST:
    return "hold";
  default:
    return "inhale";
  }
}
static uint32_t breath_phase_remaining(const App *a) {
  if (!a)
    return BREATH_INHALE_SECONDS;
  uint32_t steps = breath_phase_steps(a->breath_phase);
  if (a->breath_phase_elapsed >= steps)
    return 0;
  uint32_t remaining_steps = steps - a->breath_phase_elapsed;
  /* Convert steps back to seconds: (steps + fps - 1) /
   * fps */
  return (remaining_steps + BREATH_FPS - 1) / BREATH_FPS;
}
static float breath_phase_scale(const App *a) {
  if (!a)
    return BREATH_SCALE_MIN;
  float inhale_steps = (float)breath_phase_steps(BREATH_PHASE_INHALE);
  float exhale_steps = (float)breath_phase_steps(BREATH_PHASE_EXHALE);
  float inhale_step = (BREATH_SCALE_MAX - BREATH_SCALE_MIN) / inhale_steps;
  float exhale_step = (BREATH_SCALE_MAX - BREATH_SCALE_MIN) / exhale_steps;
  switch (a->breath_phase) {
  case BREATH_PHASE_INHALE:
    return BREATH_SCALE_MIN + inhale_step * (float)a->breath_phase_elapsed;
  case BREATH_PHASE_HOLD:
    return BREATH_SCALE_MAX;
  case BREATH_PHASE_EXHALE:
    return BREATH_SCALE_MAX - exhale_step * (float)a->breath_phase_elapsed;
  case BREATH_PHASE_HOLD_POST:
    return BREATH_SCALE_MIN;
  default:
    return BREATH_SCALE_MIN;
  }
}
static void timer_reset(App *a) {
  a->session_complete = false;
  /* Always reveal HUD when resetting/ending a session
     (fixes stuck hidden state after Mindful Breathing).
   */
  a->hud_hidden = false;
  a->paused = false;
  a->tick_accum = 0.0f;
  a->last_tick_ms = now_ms();

  if (a->mode == MODE_POMODORO) {
    /* Reset to the beginning of the configured
     * sequence. */
    a->pomo_loops_done = 0;      /* completed focus sessions */
    a->pomo_is_break = false;    /* start in focus */
    a->pomo_session_in_pomo = 0; /* kept for backwards-compat; unused in new
                                    semantics */
    a->pomo_break_is_long = false;
    if (a->pomo_session_seconds == 0)
      a->pomo_session_seconds = 25 * 60;
    if (a->pomo_break_seconds == 0)
      a->pomo_break_seconds = 5 * 60;
    /* Long break (rest) can be 0 now; do not force a
     * default when it's 0. */
    a->pomo_remaining_seconds = a->pomo_session_seconds;
  } else if (a->mode == MODE_CUSTOM) {
    if (a->custom_counting_up_active)
      a->custom_remaining_seconds = 0;
    else
      a->custom_remaining_seconds = a->custom_total_seconds;
  } else if (a->mode == MODE_MEDITATION) {
    a->meditation_remaining_seconds = a->meditation_total_seconds;
    a->meditation_elapsed_seconds = 0;
    a->meditation_half_step_counter = 0;
    a->meditation_bell_strikes_remaining = 0;
    a->meditation_bell_strike_elapsed = 0;
    a->meditation_bell_strike_file[0] = '\0';
    if (a->meditation_run_kind == 2 && a->meditation_guided_repeats_total > 0) {
      a->meditation_guided_repeats_remaining =
          a->meditation_guided_repeats_total;
      a->breath_phase = BREATH_PHASE_INHALE;
      a->breath_phase_elapsed = 0;
    } else {
      a->meditation_guided_repeats_remaining = 0;
    }
  } else {
    a->stopwatch_seconds = 0;
    a->stopwatch_lap_count = 0;
    a->stopwatch_lap_base = 0;
    for (int i = 0; i < MAX_STOPWATCH_LAPS; i++)
      a->stopwatch_laps[i] = 0;
  }
}
static void timer_reset_keep_paused(App *a) {
  timer_reset(a);
  a->paused = true;
}
static void timer_toggle_pause(App *a) {
  if (!a->running)
    return;
  a->paused = !a->paused;
}
static void play_bell_phase(App *a);
static void play_bell_done(App *a);
static void meditation_queue_bell_sequence(App *a, const char *filename,
                                           int strikes);
static void meditation_bell_strike_update(App *a, float dt_sec);
static void tick_breath_step(App *a) {
  if (!a || !a->running || a->paused)
    return;
  if (!(a->mode == MODE_MEDITATION && a->meditation_run_kind == 2))
    return;
  /* Warmup period: 3 seconds of "Relax..." text before
     breathing starts. Keep scale at MIN (1.0) by not
     advancing phase. */
  if (a->meditation_elapsed_seconds < 3)
    return;
  a->ui_needs_redraw = true;
  a->breath_phase_elapsed++;
  if (a->breath_phase_elapsed >= breath_phase_steps(a->breath_phase)) {
    a->breath_phase_elapsed = 0;
    bool cycle_done = false;
    if (a->breath_phase == BREATH_PHASE_EXHALE) {
      /* Cycle complete (Inhale -> Hold -> Exhale).
         Check repeats logic immediately. Do NOT
         transition to HOLD_POST. */

      /* Decrement repeats if applicable */
      if (a->meditation_guided_repeats_remaining > 0)
        a->meditation_guided_repeats_remaining--;

      if (a->meditation_guided_repeats_remaining == 0) {
        /* SESSION COMPLETE */
        if (a->audio)
          play_bell_done(a);

        /* Stats */
        a->end_focus_last_spent_seconds = a->run_focus_seconds;
        focus_history_append(a, a->run_focus_seconds, "completed");
        award_focus_seconds(a, a->run_focus_seconds);

        /* Reset State */
        a->running = false;
        a->paused = false;
        a->session_complete = true;
        a->run_focus_seconds = 0;
        /* Explicitly unhide HUD */
        a->hud_hidden = false;
        a->ui_needs_redraw = true;
        /* Stop music */
        if (a->audio)
          audio_engine_stop_music(a->audio);

        /* Ensure phase is reset for visual clarity (or
           keep at Exhale?) Resetting to Inhale (1.0
           scale) usually looks cleaner for "Stopped".
         */
        a->breath_phase = BREATH_PHASE_INHALE;
        a->breath_phase_elapsed = 0;
      } else {
        /* Loop back to Inhale */
        a->breath_phase = BREATH_PHASE_INHALE;
      }
    } else {
      /* Normal transition: Inhale -> Hold -> Exhale */
      a->breath_phase++;
    }
  }
}
static void tick_one_second(App *a) {
  if (!a->running || a->paused)
    return;
  if (a->session_complete)
    return;
  /* Any one-second tick affects something visible (time
   * readout, progress, labels). */
  a->ui_needs_redraw = true;
  /* Accumulate focused time for the current run.
     Pomodoro breaks are not counted. */
  if (a->mode == MODE_POMODORO) {
    if (!a->pomo_is_break) {
      a->run_focus_seconds++;
      a->cfg.focus_total_seconds++;
    }
  } else {
    a->run_focus_seconds++;
    a->cfg.focus_total_seconds++;
  }

  if (a->mode == MODE_POMODORO) {
    if (a->pomo_remaining_seconds > 0)
      a->pomo_remaining_seconds--;
    if (a->pomo_remaining_seconds == 0) {
      /* Phase ended: ring, then transition. */
      bool will_finish = false;
      if (!a->pomo_is_break) {
        /* Focus ended -> we have completed one focus
         * session. */
        /* Pomodoro metrics: count only completed focus
         * blocks. */
        a->cfg.pomo_total_blocks += 1;
        {
          uint32_t blk =
              (a->pomo_session_seconds ? a->pomo_session_seconds : 25 * 60);
          a->cfg.pomo_focus_seconds += (uint64_t)blk;
        }
        a->pomo_loops_done++;
        int total_sessions =
            (a->pomo_loops_total <= 0) ? 1 : a->pomo_loops_total;
        if (a->pomo_loops_done >= total_sessions) {
          /* That was the final focus session. Either go
           * to rest (if configured) or finish. */
          if (a->pomo_long_break_seconds > 0) {
            a->pomo_is_break = true;
            a->pomo_break_is_long = true; /* rest */
            a->pomo_remaining_seconds = a->pomo_long_break_seconds;
          } else {
            will_finish = true;
          }
        } else {
          /* Short break only between sessions. */
          a->pomo_is_break = true;
          a->pomo_break_is_long = false;
          uint32_t brk = a->pomo_break_seconds;
          if (brk == 0)
            brk = 5 * 60;
          a->pomo_remaining_seconds = brk;
        }
      } else {
        /* Break/rest ended. */
        if (a->pomo_break_is_long) {
          /* Rest ends the entire run. */
          will_finish = true;
        } else {
          /* Short break ended -> next focus session. */
          a->pomo_is_break = false;
          a->pomo_break_is_long = false;
          a->pomo_remaining_seconds =
              (a->pomo_session_seconds ? a->pomo_session_seconds : 25 * 60);
        }
      }
      if (a->audio) {
        if (will_finish)
          play_bell_done(a);
        else
          play_bell_phase(a);
      }
      if (will_finish) {
        /* Award focused time for completed run
         * (break/rest excluded by run_focus_seconds).
         */
        a->end_focus_last_spent_seconds = a->run_focus_seconds;
        focus_history_append(a, a->run_focus_seconds, "completed");
        award_focus_seconds(a, a->run_focus_seconds);
        if (a->meditation_run_kind == 1 && a->audio) {
          audio_engine_stop_music(a->audio);
        }
        a->running = false;
        a->paused = false;
        a->session_complete = true;
        a->run_focus_seconds = 0;
        return;
      }
    }
  } else if (a->mode == MODE_CUSTOM) {
    if (a->custom_counting_up_active) {
      a->custom_remaining_seconds++;
      /* If target is 0, this is an indefinite
       * stopwatch-like session. */
      /* Count up indefinitely until manually stopped */
    } else {
      if (a->custom_remaining_seconds > 0)
        a->custom_remaining_seconds--;
      if (a->custom_remaining_seconds == 0) {
        if (a->audio) {
          play_bell_done(a);
        }
        /* Award focused time for completed run. */
        a->end_focus_last_spent_seconds = a->run_focus_seconds;
        focus_history_append(a, a->run_focus_seconds, "completed");
        award_focus_seconds(a, a->run_focus_seconds);
        if (a->meditation_run_kind == 1 && a->audio) {
          audio_engine_stop_music(a->audio);
        }
        a->running = false;
        a->paused = false;
        a->session_complete = true;
        a->run_focus_seconds = 0;
        return;
      }
    }
  } else if (a->mode == MODE_MEDITATION) {
    if (a->meditation_remaining_seconds > 0)
      a->meditation_remaining_seconds--;
    a->meditation_elapsed_seconds++;
    if (a->meditation_remaining_seconds == 0) {
      if (a->meditation_run_kind == 0) {
        meditation_queue_bell_sequence(a, a->cfg.meditation_end_bell_file, 2);
      }
      a->end_focus_last_spent_seconds = a->run_focus_seconds;
      focus_history_append(a, a->run_focus_seconds, "completed");
      award_focus_seconds(a, a->run_focus_seconds);
      if (a->meditation_run_kind == 1 && a->audio) {
        audio_engine_stop_music(a->audio);
      }
      a->running = false;
      a->paused = false;
      a->session_complete = true;
      a->run_focus_seconds = 0;
      a->hud_hidden = false; /* Reveal HUD when meditation ends */
      return;
    }
    if (a->meditation_run_kind == 0 &&
        a->meditation_bell_interval_seconds > 0 &&
        (a->meditation_elapsed_seconds % a->meditation_bell_interval_seconds) ==
            0) {
      meditation_queue_bell_sequence(a, a->cfg.meditation_interval_bell_file,
                                     1);
    }
  } else if (a->mode == MODE_STOPWATCH) {
    a->stopwatch_seconds++;
  }
}
void app_update(App *a) {

  uint64_t t = now_ms();
  uint64_t dt = t - a->last_tick_ms;
  a->last_tick_ms = t;
  a->tick_accum += (float)dt / 1000.0f;
  float dt_sec = (float)dt / 1000.0f;
  if (a->mode == MODE_MEDITATION && a->meditation_run_kind == 2) {
    while (a->tick_accum >= BREATH_STEP_SECONDS) {
      a->tick_accum -= BREATH_STEP_SECONDS;
      tick_breath_step(a);
      a->meditation_half_step_counter++;
      if ((a->meditation_half_step_counter % BREATH_FPS) == 0) {
        tick_one_second(a);
      }
    }
  } else {
    while (a->tick_accum >= 1.0f) {
      a->tick_accum -= 1.0f;
      tick_one_second(a);
    }
  }
  meditation_bell_strike_update(a, dt_sec);
}
/* ----------------------------- Music helpers
 * ----------------------------- */
static bool is_audio_file_name(const char *name) {
  return ends_with_icase(name, ".mp3") || ends_with_icase(name, ".wav");
}
/* ----------------------------- Track-number extraction
   ----------------------------- Goal: sort playback by
   embedded track number when present, otherwise fall
   back to a stable name sort. Performance notes:
   - We only touch MP3 metadata (ID3v2/ID3v1). WAV
   typically has no standard, widely used track-number
   tag, so WAVs are treated as "no track" and will be
   name-sorted.
   - For MP3, we only read a small slice of the file:
   the ID3 header and enough bytes to locate a TRCK
   frame. We never decode audio.
*/
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint32_t synchsafe32(const uint8_t *p) {
  /* 7 bits per byte: 0xxxxxxx 0xxxxxxx 0xxxxxxx
   * 0xxxxxxx */
  return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
         ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}
static int parse_track_text_to_int(const char *s) {
  /* Accept: "7", "07", "7/12", "07/12". Returns -1 if
   * not parseable. */
  if (!s)
    return -1;
  while (*s == ' ' || *s == '\t')
    s++;
  if (!isdigit((unsigned char)*s))
    return -1;
  int v = 0;
  while (isdigit((unsigned char)*s)) {
    v = (v * 10) + (*s - '0');
    s++;
  }
  if (v <= 0)
    return -1;
  return v;
}
static int mp3_track_from_id3v2(FILE *f) {
  /* Returns track number or -1 if missing/unreadable.
   */
  uint8_t hdr[10];
  if (fseek(f, 0, SEEK_SET) != 0)
    return -1;
  if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
    return -1;
  if (!(hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3'))
    return -1;
  const uint8_t ver_major = hdr[3];
  /* ID3v2.2 uses 3-byte frame IDs and sizes; uncommon.
   * We support 2.3 and 2.4.
   */
  if (ver_major != 3 && ver_major != 4)
    return -1;
  const uint8_t flags = hdr[5];
  uint32_t tag_size = synchsafe32(&hdr[6]);
  if (tag_size == 0)
    return -1;
  /* Cap tag read to keep it cheap even for pathological
   * files. */
  const uint32_t cap = 256 * 1024;
  if (tag_size > cap)
    tag_size = cap;
  /* Handle extended header if present. */
  uint32_t pos = 10;
  if (flags & 0x40) {
    uint8_t ex[4];
    if (fread(ex, 1, 4, f) != 4)
      return -1;
    uint32_t exsz = (ver_major == 4) ? synchsafe32(ex) : be32(ex);
    /* exsz includes the 4 bytes we just read for v2.3,
     * and usually for v2.4 too. */
    if (exsz > tag_size)
      return -1;
    if (fseek(f, (long)(exsz - 4), SEEK_CUR) != 0)
      return -1;
    pos += exsz;
  }
  /* Walk frames until we find TRCK. */
  while (pos + 10 <= tag_size + 10) {
    uint8_t fh[10];
    if (fread(fh, 1, 10, f) != 10)
      break;
    pos += 10;
    /* Padding: frame id all zero => end. */
    if (fh[0] == 0 && fh[1] == 0 && fh[2] == 0 && fh[3] == 0)
      break;
    char id[5] = {(char)fh[0], (char)fh[1], (char)fh[2], (char)fh[3], 0};
    uint32_t fsz = (ver_major == 4) ? synchsafe32(&fh[4]) : be32(&fh[4]);
    if (fsz == 0)
      continue;
    if (pos + fsz > tag_size + 10)
      break;
    if (strcmp(id, "TRCK") == 0) {
      /* Text frame: [encoding byte][payload]. */
      uint32_t to_read = fsz;
      if (to_read > 512)
        to_read = 512; /* we only need the number */
      uint8_t buf[513];
      if (fread(buf, 1, to_read, f) != to_read)
        return -1;
      buf[to_read] = 0;
      pos += to_read;
      if (fsz > to_read) {
        if (fseek(f, (long)(fsz - to_read), SEEK_CUR) != 0)
          return -1;
        pos += (fsz - to_read);
      }
      /* encoding: 0=ISO-8859-1, 3=UTF-8. We only parse
       * digits, so treat as bytes. */
      const char *txt = (const char *)(buf + 1);
      return parse_track_text_to_int(txt);
    } else {
      if (fseek(f, (long)fsz, SEEK_CUR) != 0)
        break;
      pos += fsz;
    }
  }
  return -1;
}
static int mp3_track_from_id3v1(FILE *f) {
  /* ID3v1.1: last 128 bytes: TAG + ... + (byte 126 = 0)
   * + (byte 127 = track) */
  if (fseek(f, -128, SEEK_END) != 0)
    return -1;
  uint8_t tag[128];
  if (fread(tag, 1, 128, f) != 128)
    return -1;
  if (!(tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G'))
    return -1;
  if (tag[125] == 0 && tag[126] != 0) {
    int tr = (int)tag[126];
    if (tr > 0)
      return tr;
  }
  return -1;
}
static int track_number_from_file(const char *full_path) {
  if (!full_path)
    return -1;
  if (!ends_with_icase(full_path, ".mp3"))
    return -1;
  FILE *f = fopen(full_path, "rb");
  if (!f)
    return -1;
  int tr = mp3_track_from_id3v2(f);
  if (tr < 0)
    tr = mp3_track_from_id3v1(f);
  fclose(f);
  return tr;
}
typedef struct {
  char *name; /* filename only */
  int track;  /* 1..n if found, otherwise large */
} TrackEntry;
static int cmp_track_entry(const void *a, const void *b) {
  const TrackEntry *A = (const TrackEntry *)a;
  const TrackEntry *B = (const TrackEntry *)b;
  if (A->track != B->track)
    return (A->track < B->track) ? -1 : 1;
  int ci = strcasecmp(A->name ? A->name : "", B->name ? B->name : "");
  if (ci != 0)
    return ci;
  return strcmp(A->name ? A->name : "", B->name ? B->name : "");
}

void mood_display_from_folder(const char *in, char *out, size_t cap) {

  /* Folder names may contain parentheses used as "safe"
     stand-ins for leading dots, because hidden
     dot-prefixed names are problematic on-device.
     Conventions supported:
       "(...)" prefix  => "..."
       "(.)"   suffix  => "."
     We apply these replacements for DISPLAY ONLY.
     File/path operations keep the raw name. */
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!in)
    return;
  char tmp[256];
  safe_snprintf(tmp, sizeof(tmp), "%s", in);
  trim_ascii_inplace(tmp);

  /* Replace a leading "(...)" with "..." */
  if (strncmp(tmp, "(...)", 5) == 0) {
    char rest[256];
    safe_snprintf(rest, sizeof(rest), "%s", tmp + 5);
    safe_snprintf(tmp, sizeof(tmp), "...%s", rest);
  }

  /* Replace a trailing "(.)" with "." */
  size_t n = strlen(tmp);
  if (n >= 3 && strcmp(tmp + n - 3, "(.)") == 0) {
    tmp[n - 3] = 0;
    SDL_strlcat(tmp, ".", sizeof(tmp));
  }

  /* Drop any remaining parentheses for display
   * cleanliness. */
  char cleaned[256];
  size_t j = 0;
  for (size_t i = 0; tmp[i] && j + 1 < sizeof(cleaned); i++) {
    if (tmp[i] == '(' || tmp[i] == ')')
      continue;
    cleaned[j++] = tmp[i];
  }
  cleaned[j] = 0;
  trim_ascii_inplace(cleaned);
  safe_snprintf(out, cap, "%s", cleaned);
}
/* 12-char + "..." shortening (user asked to tweak
 * later) */
#define SONG_LABEL_MAX_CHARS 40

static void play_bell_named(App *a, const char *filename) {
  if (!a || !a->audio || !filename || !filename[0])
    return;
  if (!a->cfg.notifications_enabled)
    return;
  char path[PATH_MAX];
  safe_snprintf(path, sizeof(path), "sounds/%s", filename);
  audio_engine_play_sfx(a->audio, path);
}
static void play_bell_phase(App *a) {
  play_bell_named(a, a->cfg.bell_phase_file);
}
static void play_bell_done(App *a) {
  play_bell_named(a, a->cfg.bell_done_file);
}
static void meditation_queue_bell_sequence(App *a, const char *filename,
                                           int strikes) {
  if (!a || !a->audio || !filename || !filename[0])
    return;
  if (!a->cfg.notifications_enabled)
    return;
  if (strikes <= 0)
    return;
  safe_snprintf(a->meditation_bell_strike_file,
                sizeof(a->meditation_bell_strike_file), "%s", filename);
  a->meditation_bell_strikes_remaining = strikes;
  a->meditation_bell_strike_elapsed = 0.0f;
  if (a->meditation_bell_strikes_remaining > 0) {
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "sounds/%s", filename);
    audio_engine_play_sfx(a->audio, path);
    a->meditation_bell_strikes_remaining--;
  }
}
static void meditation_bell_strike_update(App *a, float dt_sec) {
  if (!a || a->meditation_bell_strikes_remaining <= 0)
    return;
  if (!a->cfg.notifications_enabled || !a->audio) {
    a->meditation_bell_strikes_remaining = 0;
    return;
  }
  a->meditation_bell_strike_elapsed += dt_sec;
  if (a->meditation_bell_strike_elapsed >= 1.0f) {
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "sounds/%s",
                  a->meditation_bell_strike_file);
    audio_engine_play_sfx(a->audio, path);
    a->meditation_bell_strikes_remaining--;
    a->meditation_bell_strike_elapsed = 0.0f;
  }
}

/* ----------------------------- Upper-right HUD stanza
 * renderer (Phase 1)
 * ----------------------------- */
typedef enum {
  HUD_TK_WORD = 0,
  HUD_TK_TOKEN,
  HUD_TK_DOT,
  HUD_TK_COMMA,
  HUD_TK_ELLIPSIS,
  HUD_TK_QMARK,
  HUD_TK_EXCL,
  HUD_TK_COLON,
  HUD_TK_SEMI
} HudTkKind;

typedef enum {
  HUD_DYN_NONE = 0,
  HUD_DYN_SEASON,
  HUD_DYN_BACKGROUND,
  HUD_DYN_PHASE,
  HUD_DYN_LOCATION,
  HUD_DYN_MOOD
} HudDyn;

static bool hud_token_name_to_dyn(const char *name, HudDyn *out) {
  if (!name || !name[0])
    return false;
  char low[32];
  size_t n = strlen(name);
  if (n >= sizeof(low))
    n = sizeof(low) - 1;
  for (size_t i = 0; i < n; i++)
    low[i] = (char)tolower((unsigned char)name[i]);
  low[n] = 0;
  if (strcmp(low, "season") == 0) {
    *out = HUD_DYN_SEASON;
    return true;
  }
  if (strcmp(low, "background") == 0) {
    *out = HUD_DYN_BACKGROUND;
    return true;
  }
  if (strcmp(low, "phase") == 0) {
    *out = HUD_DYN_PHASE;
    return true;
  }
  if (strcmp(low, "location") == 0) {
    *out = HUD_DYN_LOCATION;
    return true;
  }
  if (strcmp(low, "mood") == 0) {
    *out = HUD_DYN_MOOD;
    return true;
  }
  return false;
}

static const char *hud_dyn_resolve(HudDyn d, const char *season,
                                   const char *background, const char *phase,
                                   const char *location, const char *mood) {
  switch (d) {
  case HUD_DYN_SEASON:
    return season ? season : "";
  case HUD_DYN_BACKGROUND:
    return background ? background : "";
  case HUD_DYN_PHASE:
    return phase ? phase : "";
  case HUD_DYN_LOCATION:
    return location ? location : "";
  case HUD_DYN_MOOD:
    return mood ? mood : "";
  default:
    return "";
  }
}

static bool hud_tpl_contains_mood_token(const char *tpl) {
  if (!tpl || !tpl[0])
    return false;
  const char *p = tpl;
  while ((p = strchr(p, '<')) != NULL) {
    const char *q = strchr(p, '>');
    if (!q)
      break;
    char name[32] = {0};
    size_t n = (size_t)(q - (p + 1));
    if (n >= sizeof(name))
      n = sizeof(name) - 1;
    memcpy(name, p + 1, n);
    name[n] = 0;
    HudDyn d = HUD_DYN_NONE;
    if (hud_token_name_to_dyn(name, &d) && d == HUD_DYN_MOOD)
      return true;
    p = q + 1;
  }
  return false;
}

static const char *hud_next_tk(const char *s, char *out, size_t cap,
                               HudTkKind *kind, HudDyn *dyn) {
  if (out && cap)
    out[0] = 0;
  if (kind)
    *kind = HUD_TK_WORD;
  if (dyn)
    *dyn = HUD_DYN_NONE;
  if (!s)
    return NULL;

  const char *p = s;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (!*p)
    return p;

  if (p[0] == '<') {
    const char *q = strchr(p, '>');
    if (!q) {
      /* Malformed; consume as word. */
    } else {
      char name[32] = {0};
      size_t n = (size_t)(q - (p + 1));
      if (n >= sizeof(name))
        n = sizeof(name) - 1;
      memcpy(name, p + 1, n);
      name[n] = 0;
      HudDyn d = HUD_DYN_NONE;
      if (hud_token_name_to_dyn(name, &d)) {
        if (kind)
          *kind = HUD_TK_TOKEN;
        if (dyn)
          *dyn = d;
        /* out carries the canonical token text (not
         * used directly). */
        if (out && cap)
          safe_snprintf(out, cap, "%s", name);
        return q + 1;
      }
      /* Unknown token: skip it entirely. */
      return q + 1;
    }
  }

  if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
    if (kind)
      *kind = HUD_TK_ELLIPSIS;
    if (out && cap)
      safe_snprintf(out, cap, "%s", "...");
    return p + 3;
  }
  if (p[0] == '.') {
    if (kind)
      *kind = HUD_TK_DOT;
    if (out && cap)
      safe_snprintf(out, cap, "%s", ".");
    return p + 1;
  }
  if (p[0] == ',') {
    if (kind)
      *kind = HUD_TK_COMMA;
    if (out && cap)
      safe_snprintf(out, cap, "%s", ",");
    return p + 1;
  }
  if (p[0] == '?') {
    if (kind)
      *kind = HUD_TK_QMARK;
    if (out && cap)
      safe_snprintf(out, cap, "%s", "?");
    return p + 1;
  }
  if (p[0] == '!') {
    if (kind)
      *kind = HUD_TK_EXCL;
    if (out && cap)
      safe_snprintf(out, cap, "%s", "!");
    return p + 1;
  }
  if (p[0] == ':') {
    if (kind)
      *kind = HUD_TK_COLON;
    if (out && cap)
      safe_snprintf(out, cap, "%s", ":");
    return p + 1;
  }
  if (p[0] == ';') {
    if (kind)
      *kind = HUD_TK_SEMI;
    if (out && cap)
      safe_snprintf(out, cap, "%s", ";");
    return p + 1;
  }

  /* Word literal: stop on whitespace, punctuation, or
   * '<'. */
  const char *q = p;
  while (*q && !isspace((unsigned char)*q) && *q != '<' && *q != '.' &&
         *q != ',' && *q != '?' && *q != '!' && *q != ':' && *q != ';')
    q++;
  if (out && cap) {
    size_t n = (size_t)(q - p);
    if (n >= cap)
      n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
  }
  if (kind)
    *kind = HUD_TK_WORD;
  return q;
}

/* Measure and (optionally) draw one stanza line,
   right-aligned to xR. Returns the rendered width (0 if
   nothing was rendered). */
int hud_draw_tpl_line(UI *ui, int xR, int y, const char *tpl, SDL_Color main,
                      SDL_Color accent, const char *season,
                      const char *background,

                      const char *phase, const char *location, const char *mood,
                      bool do_draw, bool *out_contains_mood) {
  if (out_contains_mood)
    *out_contains_mood = false;
  if (!tpl || !tpl[0])
    return 0;

  const bool has_mood = hud_tpl_contains_mood_token(tpl);
  if (out_contains_mood)
    *out_contains_mood = has_mood;
  if (has_mood && (!mood || !mood[0])) {
    /* Mood line with no mood: hide it completely. */
    return 0;
  }

  int w_space = text_width(ui->font_small, " ");
  if (w_space <= 0)
    w_space = 6;

  /* Pass 1: measure */
  int total_w = 0;
  bool has_prev = false;
  bool no_space_after = false;

  const char *p = tpl;
  while (p && *p) {
    char buf[128] = {0};
    HudTkKind k = HUD_TK_WORD;
    HudDyn d = HUD_DYN_NONE;
    const char *next = hud_next_tk(p, buf, sizeof(buf), &k, &d);
    if (!next)
      break;
    p = next;

    const char *seg = NULL;
    if (k == HUD_TK_TOKEN) {
      seg = hud_dyn_resolve(d, season, background, phase, location, mood);
      if (!seg || !seg[0])
        continue;
    } else {
      if (!buf[0])
        continue;
      seg = buf;
    }

    const bool is_suffix =
        (k == HUD_TK_DOT || k == HUD_TK_COMMA || k == HUD_TK_QMARK ||
         k == HUD_TK_EXCL || k == HUD_TK_COLON || k == HUD_TK_SEMI);
    if (is_suffix && !has_prev)
      continue;

    if (!is_suffix) {
      if (has_prev && !no_space_after)
        total_w += w_space;
    }

    int style = TTF_STYLE_ITALIC;
    TTF_Font *use_font = ui->font_small;

    if (k == HUD_TK_TOKEN) {
      if (d == HUD_DYN_LOCATION) {
        style = TTF_STYLE_NORMAL;
        use_font = ui->font_small;
      } else {
        style = TTF_STYLE_ITALIC;
      }
    } else {
      /* Custom words: Normal */
      style = TTF_STYLE_NORMAL;
    }

    int w = text_width_style_ui(ui, use_font, style, seg);
    if (w <= 0)
      continue;

    total_w += w;
    has_prev = true;
    no_space_after = (k == HUD_TK_ELLIPSIS);
  }

  if (total_w <= 0)
    return 0;
  if (!do_draw)
    return total_w;

  /* Pass 2: draw */
  /* Align by baseline so mixed font sizes (Small/Med)
     look correct. The row is allocated with Medium
     height (max), so use Medium ascent as anchor. */
  int max_ascent = TTF_FontAscent(ui->font_med);
  int baseline_y = y + max_ascent;

  int x = xR - total_w;
  has_prev = false;
  no_space_after = false;
  p = tpl;

  while (p && *p) {
    char buf[128] = {0};
    HudTkKind k = HUD_TK_WORD;
    HudDyn d = HUD_DYN_NONE;
    const char *next = hud_next_tk(p, buf, sizeof(buf), &k, &d);
    if (!next)
      break;
    p = next;

    const char *seg = NULL;
    if (k == HUD_TK_TOKEN) {
      seg = hud_dyn_resolve(d, season, background, phase, location, mood);
      if (!seg || !seg[0])
        continue;
    } else {
      if (!buf[0])
        continue;
      seg = buf;
    }

    const bool is_suffix =
        (k == HUD_TK_DOT || k == HUD_TK_COMMA || k == HUD_TK_QMARK ||
         k == HUD_TK_EXCL || k == HUD_TK_COLON || k == HUD_TK_SEMI);
    if (is_suffix && !has_prev)
      continue;

    if (!is_suffix) {
      if (has_prev && !no_space_after)
        x += w_space;
    }

    int style = TTF_STYLE_ITALIC;
    SDL_Color col = main;
    TTF_Font *use_font = ui->font_small;

    if (k == HUD_TK_TOKEN) {
      if (d == HUD_DYN_LOCATION) {
        /* User Req: Normal style, Small font, Main
         * color */
        style = TTF_STYLE_NORMAL;
        col = main;
        use_font = ui->font_small;
      } else {
        /* Other tokens: Italic, Small, Accent */
        style = TTF_STYLE_ITALIC;
        col = accent;
      }
    } else {
      /* Custom words: Normal style, Small font, Main
       * color */
      style = TTF_STYLE_NORMAL;
      col = main;
    }

    /* Draw with baseline alignment */
    draw_text_style_baseline(ui, use_font, x, baseline_y, seg, col, false,
                             style);

    /* Advance x using the width of the rendered segment
     * in its specific font */
    x += text_width_style_ui(ui, use_font, style, seg);

    has_prev = true;
    no_space_after = (k == HUD_TK_ELLIPSIS);
  }

  return total_w;
}

/* ---------------------- HUD stanza overrides (Phase 2)
 * ----------------------
 */
static void draw_poetic_stack_upper_right(UI *ui, App *a, int xR, int y_top,
                                          SDL_Color main, SDL_Color accent);

/* ---------------------- Stanza puzzle editor (Phase 3)
 * ----------------------
 */

static const char *stz_piece_label(StanzaPiece p) {
  switch (p) {
  case STZ_P_SEASON:
    return "<season>";
  case STZ_P_BACKGROUND:
    return "<background>";
  case STZ_P_PHASE:
    return "<phase>";
  case STZ_P_LOCATION:
    return "<location>";
  case STZ_P_MOOD:
    return "<mood>";
  case STZ_P_FOR:
    return "for";
  case STZ_P_AND:
    return "and";
  case STZ_P_NOR:
    return "nor";
  case STZ_P_BUT:
    return "but";
  case STZ_P_OR:
    return "or";
  case STZ_P_YET:
    return "yet";
  case STZ_P_SO:
    return "so";
  case STZ_P_BECAUSE:
    return "because";
  case STZ_P_SINCE:
    return "since";
  case STZ_P_WHILE:
    return "while";
  case STZ_P_IF:
    return "if";
  case STZ_P_DOT:
    return ".";
  case STZ_P_COMMA:
    return ",";
  case STZ_P_QMARK:
    return "?";
  case STZ_P_EXCL:
    return "!";
  case STZ_P_COLON:
    return ":";
  case STZ_P_SEMI:
    return ";";
  case STZ_P_APOSTROPHE:
    return "'";
  case STZ_P_ELLIPSIS:
    return "...";
  default:
    return "";
  }
}
static const char *stz_piece_tray_label(StanzaPiece p) {

  switch (p) {
  case STZ_P_SEASON:
    return "season";
  case STZ_P_BACKGROUND:
    return "background";
  case STZ_P_PHASE:
    return "phase";
  case STZ_P_LOCATION:
    return "location";
  case STZ_P_MOOD:
    return "mood";
  case STZ_P_FOR:
    return "for";
  case STZ_P_AND:
    return "and";
  case STZ_P_NOR:
    return "nor";
  case STZ_P_BUT:
    return "but";
  case STZ_P_OR:
    return "or";
  case STZ_P_YET:
    return "yet";
  case STZ_P_SO:
    return "so";
  case STZ_P_BECAUSE:
    return "because";
  case STZ_P_SINCE:
    return "since";
  case STZ_P_WHILE:
    return "while";
  case STZ_P_IF:
    return "if";
  case STZ_P_CUSTOM:
    return "custom piece";
  case STZ_P_DOT:
    return ".";
  case STZ_P_COMMA:
    return ",";
  case STZ_P_QMARK:
    return "?";
  case STZ_P_EXCL:
    return "!";
  case STZ_P_COLON:
    return ":";
  case STZ_P_SEMI:
    return ";";
  case STZ_P_APOSTROPHE:
    return "'";
  default:
    return "";
  }
}
static bool stz_piece_is_suffix(StanzaPiece p) {
  return (p == STZ_P_DOT || p == STZ_P_COMMA || p == STZ_P_QMARK ||
          p == STZ_P_EXCL || p == STZ_P_COLON || p == STZ_P_SEMI ||
          p == STZ_P_APOSTROPHE);
}
static bool stz_piece_no_space_after(StanzaPiece p) {
  return (p == STZ_P_ELLIPSIS || p == STZ_P_APOSTROPHE);
}
static bool stz_piece_is_token(StanzaPiece p) {
  return (p == STZ_P_SEASON || p == STZ_P_BACKGROUND || p == STZ_P_PHASE ||
          p == STZ_P_LOCATION || p == STZ_P_MOOD);
}
static int stz_piece_style(StanzaPiece p) {
  /* Match HUD: location is normal, everything else
   * italic. */
  if (p == STZ_P_LOCATION || p == STZ_P_CUSTOM)
    return 0;
  return TTF_STYLE_ITALIC;
}
static SDL_Color stz_piece_color(StanzaPiece p, SDL_Color main,
                                 SDL_Color accent) {
  if (stz_piece_is_token(p)) {
    return (p == STZ_P_LOCATION) ? main : accent;
  }
  /* conjunctions + punctuation are "main" */
  return main;
}

static const char *stz_custom_label_or_default(const char *custom);
static const char *stz_piece_line_label(const StanzaDisplay *d, StanzaPiece p) {
  if (!d)
    return "";
  switch (p) {
  case STZ_P_SEASON:
    return d->season ? d->season : "";
  case STZ_P_BACKGROUND:
    return d->background ? d->background : "";
  case STZ_P_PHASE:
    return d->phase ? d->phase : "";
  case STZ_P_LOCATION:
    return d->location ? d->location : "";
  case STZ_P_MOOD:
    return d->mood ? d->mood : "";
  case STZ_P_CUSTOM:
    return stz_custom_label_or_default(d->custom);
  default:
    return stz_piece_label(p);
  }
}
static bool stz_word_to_piece(const char *w, StanzaPiece *out) {
  if (!w || !w[0] || !out)
    return false;
  if (strcasecmp(w, "for") == 0) {
    *out = STZ_P_FOR;
    return true;
  }
  if (strcasecmp(w, "and") == 0) {
    *out = STZ_P_AND;
    return true;
  }
  if (strcasecmp(w, "nor") == 0) {
    *out = STZ_P_NOR;
    return true;
  }
  if (strcasecmp(w, "but") == 0) {
    *out = STZ_P_BUT;
    return true;
  }
  if (strcasecmp(w, "or") == 0) {
    *out = STZ_P_OR;
    return true;
  }
  if (strcasecmp(w, "yet") == 0) {
    *out = STZ_P_YET;
    return true;
  }
  if (strcasecmp(w, "so") == 0) {
    *out = STZ_P_SO;
    return true;
  }
  if (strcasecmp(w, "because") == 0) {
    *out = STZ_P_BECAUSE;
    return true;
  }
  if (strcasecmp(w, "since") == 0) {
    *out = STZ_P_SINCE;
    return true;
  }
  if (strcasecmp(w, "while") == 0) {
    *out = STZ_P_WHILE;
    return true;
  }
  if (strcasecmp(w, "if") == 0) {
    *out = STZ_P_IF;
    return true;
  }
  return false;
}
static StanzaPiece stz_dyn_to_piece(HudDyn d) {
  switch (d) {
  case HUD_DYN_SEASON:
    return STZ_P_SEASON;
  case HUD_DYN_BACKGROUND:
    return STZ_P_BACKGROUND;
  case HUD_DYN_PHASE:
    return STZ_P_PHASE;
  case HUD_DYN_LOCATION:
    return STZ_P_LOCATION;
  case HUD_DYN_MOOD:
    return STZ_P_MOOD;
  default:
    return STZ_P_NONE;
  }
}
static int stz_parse_line_to_pieces(const char *tpl, int *out,
                                    char custom_words[][64], int cap) {
  if (!out || cap <= 0)
    return 0;
  int len = 0;
  if (!tpl || !tpl[0])
    return 0;

  const char *p = tpl;
  while (p && *p) {
    char buf[128] = {0};
    HudTkKind k = HUD_TK_WORD;
    HudDyn d = HUD_DYN_NONE;
    const char *next = hud_next_tk(p, buf, sizeof(buf), &k, &d);
    if (!next)
      break;
    p = next;

    StanzaPiece sp = STZ_P_NONE;
    if (k == HUD_TK_TOKEN)
      sp = stz_dyn_to_piece(d);
    else if (k == HUD_TK_DOT)
      sp = STZ_P_DOT;
    else if (k == HUD_TK_COMMA)
      sp = STZ_P_COMMA;
    else if (k == HUD_TK_QMARK)
      sp = STZ_P_QMARK;
    else if (k == HUD_TK_EXCL)
      sp = STZ_P_EXCL;
    else if (k == HUD_TK_COLON)
      sp = STZ_P_COLON;
    else if (k == HUD_TK_SEMI)
      sp = STZ_P_SEMI;
    else if (k == HUD_TK_ELLIPSIS)
      sp = STZ_P_ELLIPSIS;
    else if (k == HUD_TK_WORD) {
      StanzaPiece wp = STZ_P_NONE;
      if (stz_word_to_piece(buf, &wp))
        sp = wp;
      else
        sp = STZ_P_CUSTOM;
    }

    if (sp == STZ_P_NONE)
      continue;
    if (len < cap) {
      out[len] = (int)sp;
      if (custom_words) {
        if (sp == STZ_P_CUSTOM) {
          safe_snprintf(custom_words[len], sizeof(custom_words[len]), "%s",
                        buf);
        } else {
          custom_words[len][0] = 0;
        }
      }
      len++;
    }
  }
  return len;
}
static void stz_buf_append(char *out, size_t cap, const char *s) {
  if (!out || cap == 0 || !s)
    return;
  size_t used = strlen(out);
  if (used >= cap - 1)
    return;
  size_t add = strlen(s);
  if (add > cap - 1 - used)
    add = cap - 1 - used;
  if (add == 0)
    return;
  memcpy(out + used, s, add);
  out[used + add] = 0;
}
static const char *stz_custom_label_or_default(const char *custom) {
  return (custom && custom[0]) ? custom : "";
}
static void stz_serialize_line_from_pieces(const int *pcs,
                                           const char custom_words[][64],
                                           int len, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!pcs || len <= 0)
    return;

  bool has_prev = false;
  bool no_space_after = false;

  for (int i = 0; i < len; i++) {
    StanzaPiece p = (StanzaPiece)pcs[i];
    const char *seg = (p == STZ_P_CUSTOM && custom_words)
                          ? stz_custom_label_or_default(custom_words[i])
                          : stz_piece_label(p);
    if (!seg || !seg[0])
      continue;

    const bool is_suffix = stz_piece_is_suffix(p);
    if (is_suffix && !has_prev)
      continue;

    if (!is_suffix) {
      if (has_prev && !no_space_after)
        stz_buf_append(out, cap, " ");
    }

    stz_buf_append(out, cap, seg);

    has_prev = true;
    no_space_after = stz_piece_no_space_after(p);
  }
}
static void stz_editor_load_from_work(App *a) {
  if (!a)
    return;
  for (int row = 0; row < 3; row++) {
    for (int i = 0; i < 24; i++) {
      a->stanza_line_custom[row][i][0] = 0;
    }
  }
  a->stanza_line_len[0] = stz_parse_line_to_pieces(
      a->stanza_work1, a->stanza_line_pieces[0], a->stanza_line_custom[0], 24);
  a->stanza_line_len[1] = stz_parse_line_to_pieces(
      a->stanza_work2, a->stanza_line_pieces[1], a->stanza_line_custom[1], 24);
  a->stanza_line_len[2] = stz_parse_line_to_pieces(
      a->stanza_work3, a->stanza_line_pieces[2], a->stanza_line_custom[2], 24);

  a->stanza_cursor_row = 0;
  a->stanza_line_cursor[0] = a->stanza_line_len[0];
  a->stanza_line_cursor[1] = a->stanza_line_len[1];
  a->stanza_line_cursor[2] = a->stanza_line_len[2];
  a->stanza_tray_col[0] = 0;
  a->stanza_tray_col[1] = 0;
  a->stanza_tray_col[2] = 0;
  a->stanza_tray_col[3] = 0;

  a->stanza_holding = false;
  a->stanza_hold_piece = (int)STZ_P_NONE;
  a->stanza_hold_custom[0] = 0;
}

static void stz_editor_sync_work(App *a) {
  if (!a)
    return;
  stz_serialize_line_from_pieces(
      a->stanza_line_pieces[0], a->stanza_line_custom[0], a->stanza_line_len[0],
      a->stanza_work1, sizeof(a->stanza_work1));
  stz_serialize_line_from_pieces(
      a->stanza_line_pieces[1], a->stanza_line_custom[1], a->stanza_line_len[1],
      a->stanza_work2, sizeof(a->stanza_work2));
  stz_serialize_line_from_pieces(
      a->stanza_line_pieces[2], a->stanza_line_custom[2], a->stanza_line_len[2],
      a->stanza_work3, sizeof(a->stanza_work3));
}
static bool stz_piece_is_tag(StanzaPiece p) {
  return (p == STZ_P_LOCATION || p == STZ_P_MOOD || p == STZ_P_PHASE ||
          p == STZ_P_BACKGROUND);
}
static bool stz_tag_in_lines(const App *a, StanzaPiece p) {
  if (!a || !stz_piece_is_tag(p))
    return false;
  for (int row = 0; row < 3; row++) {
    int len = a->stanza_line_len[row];
    for (int i = 0; i < len; i++) {
      if ((StanzaPiece)a->stanza_line_pieces[row][i] == p)
        return true;
    }
  }
  return false;
}

/* Trays */
static const StanzaPiece STZ_TRAY_TAGS[] = {STZ_P_LOCATION, STZ_P_MOOD,
                                            STZ_P_PHASE, STZ_P_BACKGROUND};

static int stz_tray_count_for_row(const App *a, int row) {
  if (row == 3) {
    int count = 0;
    for (int i = 0; i < (int)(sizeof(STZ_TRAY_TAGS) / sizeof(STZ_TRAY_TAGS[0]));
         i++) {
      StanzaPiece p = STZ_TRAY_TAGS[i];
      if (p == STZ_P_CUSTOM || !stz_tag_in_lines(a, p))
        count++;
    }
    return count;
  }
  if (row == 4)
    return 1;
  return 0;
}
static StanzaPiece stz_tray_piece_for_row_col(const App *a, int row, int col) {
  if (row == 3) {
    int n = 0;
    StanzaPiece avail[8];
    for (int i = 0; i < (int)(sizeof(STZ_TRAY_TAGS) / sizeof(STZ_TRAY_TAGS[0]));
         i++) {
      StanzaPiece p = STZ_TRAY_TAGS[i];
      if (p == STZ_P_CUSTOM || !stz_tag_in_lines(a, p))
        avail[n++] = p;
    }
    if (n <= 0)
      return STZ_P_NONE;
    if (col < 0)
      col = 0;
    if (col >= n)
      col = n - 1;
    return avail[col];
  }
  if (row == 4) {
    (void)col;
    return STZ_P_CUSTOM;
  }
  return STZ_P_NONE;
}

/* Draw a line of pieces, right-aligned, with an
 * insertion cursor. If preview_piece is set, it is
 * drawn at the cursor. */
static const char *stz_piece_line_label_custom(const StanzaDisplay *d,
                                               StanzaPiece p,
                                               const char *custom_word) {
  if (p == STZ_P_CUSTOM)
    return stz_custom_label_or_default(custom_word);
  return stz_piece_line_label(d, p);
}
static void stz_draw_piece_line_right(
    UI *ui, const StanzaDisplay *disp, int xR, int y, const int *pcs,
    const char custom_words[][64], int len, int cursor, SDL_Color main,
    SDL_Color accent, SDL_Color hi, bool draw_cursor, bool show_empty_cursor,
    SDL_Color empty_col, StanzaPiece preview_piece,
    const char *preview_custom) {
  if (!ui)
    return;

  if (len < 0)
    len = 0;
  if (cursor < 0)
    cursor = 0;
  if (cursor > len)
    cursor = len;

  int w_space = text_width(ui->font_small, " ");
  if (w_space <= 0)
    w_space = 6;

  int w_cursor = text_width(ui->font_small, "|");
  if (w_cursor <= 0)
    w_cursor = 6;

  /* Measure */
  int total_w = 0;
  bool has_prev = false;
  bool no_space_after = false;

  for (int i = 0; i <= len; i++) {
    if (draw_cursor && i == cursor) {
      if (preview_piece != STZ_P_NONE &&
          !(stz_piece_is_suffix(preview_piece) && !has_prev)) {
        const char *seg =
            stz_piece_line_label_custom(disp, preview_piece, preview_custom);
        if (seg && seg[0]) {
          const bool is_suffix = stz_piece_is_suffix(preview_piece);
          if (!is_suffix) {
            if (has_prev && !no_space_after)
              total_w += w_space;
          }

          int style = stz_piece_style(preview_piece);
          int w = text_width_style_ui(ui, ui->font_small, style, seg);
          if (w > 0) {
            total_w += w;
            has_prev = true;
            no_space_after = stz_piece_no_space_after(preview_piece);
          }
        }
      } else {
        if (has_prev && !no_space_after)
          total_w += w_space;
        total_w += w_cursor;
        has_prev = true;
        no_space_after = false;
      }
    }
    if (i == len)
      break;

    StanzaPiece p = (StanzaPiece)pcs[i];
    const char *seg = stz_piece_line_label_custom(
        disp, p, custom_words ? custom_words[i] : NULL);
    if (!seg || !seg[0])
      continue;

    const bool is_suffix = stz_piece_is_suffix(p);
    if (is_suffix && !has_prev)
      continue;

    if (!is_suffix) {
      if (has_prev && !no_space_after)
        total_w += w_space;
    }

    int style = stz_piece_style(p);
    int w = text_width_style_ui(ui, ui->font_small, style, seg);
    if (w <= 0)
      continue;

    total_w += w;
    has_prev = true;
    no_space_after = stz_piece_no_space_after(p);
  }

  if (total_w <= 0) {
    if (draw_cursor || show_empty_cursor) {
      int x = xR - w_cursor;
      SDL_Color col = draw_cursor ? hi : empty_col;
      draw_text(ui, ui->font_small, x, y, "|", col, false);
    }
    return;
  }
  int x = xR - total_w;

  /* Draw */
  has_prev = false;
  no_space_after = false;

  for (int i = 0; i <= len; i++) {
    if (draw_cursor && i == cursor) {
      if (preview_piece != STZ_P_NONE &&
          !(stz_piece_is_suffix(preview_piece) && !has_prev)) {
        const char *seg =
            stz_piece_line_label_custom(disp, preview_piece, preview_custom);
        if (seg && seg[0]) {
          const bool is_suffix = stz_piece_is_suffix(preview_piece);
          if (!is_suffix) {
            if (has_prev && !no_space_after)
              x += w_space;
          }

          int style = stz_piece_style(preview_piece);
          draw_text_style(ui, ui->font_small, x, y, seg, hi, false, style);
          x += text_width_style_ui(ui, ui->font_small, style, seg);
          has_prev = true;
          no_space_after = stz_piece_no_space_after(preview_piece);
        }
      } else {
        if (has_prev && !no_space_after)
          x += w_space;
        draw_text(ui, ui->font_small, x, y, "|", hi, false);
        x += w_cursor;
        has_prev = true;
        no_space_after = false;
      }
    }
    if (i == len)
      break;

    StanzaPiece p = (StanzaPiece)pcs[i];
    const char *seg = stz_piece_line_label_custom(
        disp, p, custom_words ? custom_words[i] : NULL);
    if (!seg || !seg[0])
      continue;

    const bool is_suffix = stz_piece_is_suffix(p);
    if (is_suffix && !has_prev)
      continue;

    if (!is_suffix) {
      if (has_prev && !no_space_after)
        x += w_space;
    }

    int style = stz_piece_style(p);
    SDL_Color col = stz_piece_color(p, main, accent);
    draw_text_style(ui, ui->font_small, x, y, seg, col, false, style);
    x += text_width_style_ui(ui, ui->font_small, style, seg);

    has_prev = true;
    no_space_after = stz_piece_no_space_after(p);
  }
}

/* Draw a tray row, right-aligned. Selected col is
 * highlighted by text color. */
static void stz_draw_tray_row_right(UI *ui, const App *a, int xR, int y,
                                    int row, int sel_col, SDL_Color main,
                                    SDL_Color accent, SDL_Color hi) {
  if (!ui)
    return;
  int n = stz_tray_count_for_row(a, row);
  if (n <= 0)
    return;

  const bool use_commas = (row == 3 || row == 4 || row == 5);

  int w_gap = 16;
  int w_comma = text_width(ui->font_small, ",");
  int w_space = text_width(ui->font_small, " ");
  if (w_comma <= 0)
    w_comma = 6;
  if (w_space <= 0)
    w_space = 4;

  int total_w = 0;
  int item_w[32];
  StanzaPiece item_p[32];

  if (n > 32)
    n = 32;

  for (int i = 0; i < n; i++) {
    StanzaPiece p = stz_tray_piece_for_row_col(a, row, i);
    item_p[i] = p;

    const char *seg = stz_piece_tray_label(p);
    int style = stz_piece_style(p);
    if (row == 4)
      style = TTF_STYLE_NORMAL;

    int w = 0;
    if (seg && seg[0])
      w = text_width_style_ui(ui, ui->font_small, style, seg);
    if (w < 0)
      w = 0;

    item_w[i] = w;
    total_w += w;

    if (i < n - 1) {
      total_w += use_commas ? (w_comma + w_space) : w_gap;
    }
  }

  int x = xR - total_w;

  for (int i = 0; i < n; i++) {
    StanzaPiece p = item_p[i];
    const char *seg = stz_piece_tray_label(p);
    if (!seg || !seg[0])
      continue;

    int style = stz_piece_style(p);
    SDL_Color col = (i == sel_col) ? hi : stz_piece_color(p, main, accent);
    draw_text_style(ui, ui->font_small, x, y, seg, col, false, style);
    x += item_w[i];

    if (i < n - 1) {
      if (use_commas) {
        draw_text(ui, ui->font_small, x, y, ",", main, false);
        x += w_comma;
        draw_text(ui, ui->font_small, x, y, " ", main, false);
        x += w_space;
      } else {
        x += w_gap;
      }
    }
  }
}

typedef struct {
  const char *name;
  const char *l1;
  const char *l2;
  const char *l3;
} StanzaPreset;

static const StanzaPreset STANZA_PRESETS[] = {
    {"default", "<phase> <background> <location>", "...and <mood>.", ""},
    {"location + mood", "<location>", "<location>, and <mood>.", ""},
    {"full stack", "<phase> <background> <location>", "<location>, and <mood>.",
     ""},
    {"minimal", "<location>", "...", ""},
    {"no mood line", "<phase> <background> <location>", "", ""},
};
#define STANZA_PRESET_COUNT                                                    \
  ((int)(sizeof(STANZA_PRESETS) / sizeof(STANZA_PRESETS[0])))

static int stanza_override_find(const App *a, const char *loc_key) {
  if (!a || !loc_key || !loc_key[0])
    return -1;
  return sl_find(&a->stanza_loc_keys, loc_key);
}

static void stanza_overrides_load(App *a) {
  if (!a)
    return;
  FILE *f = fopen(HUD_STANZA_OVERRIDES_PATH, "r");
  if (!f)
    return;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    config_trim(line);
    if (!line[0] || line[0] == '#')
      continue;

    char *p1 = strchr(line, '\t');
    if (!p1)
      continue;
    *p1++ = 0;
    char *p2 = strchr(p1, '\t');
    if (!p2)
      continue;
    *p2++ = 0;
    char *p3 = strchr(p2, '\t');
    if (!p3)
      continue;
    *p3++ = 0;

    config_trim(line);
    config_trim(p1);
    config_trim(p2);
    config_trim(p3);

    if (!line[0])
      continue;
    sl_push(&a->stanza_loc_keys, line);
    sl_push(&a->stanza_loc_1, p1);
    sl_push(&a->stanza_loc_2, p2);
    sl_push(&a->stanza_loc_3, p3);
  }

  fclose(f);
}

static void stanza_overrides_save(const App *a) {
  if (!a)
    return;
  FILE *f = fopen(HUD_STANZA_OVERRIDES_PATH, "w");
  if (!f)
    return;

  fprintf(f, "# "
             "location_key\\tstanza1\\tstanza2\\tstanza3\\n");
  const int n = a->stanza_loc_keys.count;
  for (int i = 0; i < n; i++) {
    const char *k =
        a->stanza_loc_keys.items[i] ? a->stanza_loc_keys.items[i] : "";
    const char *l1 = (i < a->stanza_loc_1.count && a->stanza_loc_1.items[i])
                         ? a->stanza_loc_1.items[i]
                         : "";
    const char *l2 = (i < a->stanza_loc_2.count && a->stanza_loc_2.items[i])
                         ? a->stanza_loc_2.items[i]
                         : "";
    const char *l3 = (i < a->stanza_loc_3.count && a->stanza_loc_3.items[i])
                         ? a->stanza_loc_3.items[i]
                         : "";
    fprintf(f, "%s\t%s\t%s\t%s\n", k, l1, l2, l3);
  }

  fclose(f);
}

static void stanza_override_set(App *a, const char *loc_key, const char *l1,
                                const char *l2, const char *l3) {
  if (!a || !loc_key || !loc_key[0])
    return;

  const char *s1 = l1 ? l1 : "";
  const char *s2 = l2 ? l2 : "";
  const char *s3 = l3 ? l3 : "";

  int idx = stanza_override_find(a, loc_key);
  if (idx >= 0) {
    if (idx < a->stanza_loc_1.count) {
      free(a->stanza_loc_1.items[idx]);
      a->stanza_loc_1.items[idx] = str_dup(s1);
    }
    if (idx < a->stanza_loc_2.count) {
      free(a->stanza_loc_2.items[idx]);
      a->stanza_loc_2.items[idx] = str_dup(s2);
    }
    if (idx < a->stanza_loc_3.count) {
      free(a->stanza_loc_3.items[idx]);
      a->stanza_loc_3.items[idx] = str_dup(s3);
    }
    return;
  }

  sl_push(&a->stanza_loc_keys, loc_key);
  sl_push(&a->stanza_loc_1, s1);
  sl_push(&a->stanza_loc_2, s2);
  sl_push(&a->stanza_loc_3, s3);
}

static void stanza_get_active_tpls(App *a, const char **out1, const char **out2,
                                   const char **out3) {
  if (!a) {
    if (out1)
      *out1 = "";
    if (out2)
      *out2 = "";
    if (out3)
      *out3 = "";
    return;
  }

  const char *t1 = a->cfg.hud_stanza1[0] ? a->cfg.hud_stanza1
                                         : "<phase> <background> <location>";
  const char *t2 =
      a->cfg.hud_stanza2[0] ? a->cfg.hud_stanza2 : "...and <mood>.";
  const char *t3 = a->cfg.hud_stanza3;

  if (a->stanza_selector_open) {
    t1 = a->stanza_work1;
    t2 = a->stanza_work2;
    t3 = a->stanza_work3;
  } else {
    const char *loc_key = a->scene_name[0] ? a->scene_name : a->cfg.scene;
    int idx = stanza_override_find(a, loc_key);
    if (idx >= 0) {
      const char *o1 =
          (idx < a->stanza_loc_1.count && a->stanza_loc_1.items[idx])
              ? a->stanza_loc_1.items[idx]
              : "";
      const char *o2 =
          (idx < a->stanza_loc_2.count && a->stanza_loc_2.items[idx])
              ? a->stanza_loc_2.items[idx]
              : "";
      const char *o3 =
          (idx < a->stanza_loc_3.count && a->stanza_loc_3.items[idx])
              ? a->stanza_loc_3.items[idx]
              : "";
      t1 = (o1 && o1[0]) ? o1 : t1;
      t2 = (o2 && o2[0]) ? o2 : t2;
      t3 = (o3 && o3[0]) ? o3 : "";
    }
  }

  if (out1)
    *out1 = t1;
  if (out2)
    *out2 = t2;
  if (out3)
    *out3 = t3;
}

static void stanza_selector_open(App *a) {
  if (!a)
    return;
  const char *t1 = NULL;
  const char *t2 = NULL;
  const char *t3 = NULL;
  const bool was_open = a->stanza_selector_open;
  a->stanza_selector_open = false;
  stanza_get_active_tpls(a, &t1, &t2, &t3);
  a->stanza_selector_open = was_open;

  safe_snprintf(a->stanza_orig1, sizeof(a->stanza_orig1), "%s", t1 ? t1 : "");
  safe_snprintf(a->stanza_orig2, sizeof(a->stanza_orig2), "%s", t2 ? t2 : "");
  safe_snprintf(a->stanza_orig3, sizeof(a->stanza_orig3), "%s", t3 ? t3 : "");

  safe_snprintf(a->stanza_work1, sizeof(a->stanza_work1), "%s",
                a->stanza_orig1);
  safe_snprintf(a->stanza_work2, sizeof(a->stanza_work2), "%s",
                a->stanza_orig2);
  safe_snprintf(a->stanza_work3, sizeof(a->stanza_work3), "%s",
                a->stanza_orig3);
  a->stanza_custom_tag[0] = 0;
  a->stanza_custom_buf[0] = 0;
  a->stanza_custom_kb_row = 0;
  a->stanza_custom_kb_col = 0;
  a->stanza_custom_open = false;

  /* Phase 3 editor: parse current templates into puzzle
   * pieces. */
  stz_editor_load_from_work(a);

  a->stanza_preset_sel = 0;
  a->stanza_save_prompt_open = false;
  a->stanza_save_sel = 0;
  a->stanza_selector_open = true;
}

static void stanza_selector_close(App *a) {
  if (!a)
    return;
  a->stanza_selector_open = false;
  a->stanza_save_prompt_open = false;
  a->stanza_custom_open = false;
}

static void stanza_apply_preset(App *a, int idx) {
  if (!a)
    return;
  if (idx < 0)
    idx = 0;
  if (idx >= STANZA_PRESET_COUNT)
    idx = STANZA_PRESET_COUNT - 1;

  safe_snprintf(a->stanza_work1, sizeof(a->stanza_work1), "%s",
                STANZA_PRESETS[idx].l1 ? STANZA_PRESETS[idx].l1 : "");
  safe_snprintf(a->stanza_work2, sizeof(a->stanza_work2), "%s",
                STANZA_PRESETS[idx].l2 ? STANZA_PRESETS[idx].l2 : "");
  safe_snprintf(a->stanza_work3, sizeof(a->stanza_work3), "%s",
                STANZA_PRESETS[idx].l3 ? STANZA_PRESETS[idx].l3 : "");
}

static void stanza_custom_kb_clamp(App *a);
static void draw_stanza_custom_keyboard(UI *ui, App *a, SDL_Color main,
                                        SDL_Color accent, SDL_Color highlight);
static void handle_stanza_custom_keyboard(App *a, Buttons *b);

static void draw_stanza_selector(UI *ui, App *a, SDL_Color main,
                                 SDL_Color accent) {
  if (!ui || !a)
    return;

  const SDL_Color hi = color_from_idx(a->cfg.highlight_color_idx);

  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 128);
  SDL_Rect dim = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &dim);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);

  /* Live preview: draw exactly where the HUD is. */
  draw_poetic_stack_upper_right(ui, a, ui->w - UI_MARGIN_X, UI_MARGIN_TOP, main,
                                accent);

  const int xR = ui->w - UI_MARGIN_X;
  const int hS = TTF_FontHeight(ui->font_small);
  int stack_h = (hS * 3) + (HUD_STACK_GAP * 2);

  /* User requested significantly higher (100px) placement to close gap */
  /* Adjusted: Lowered by ~24px (from -50 to -26) to avoid clipping and add
   * space */
  int y_tags = UI_MARGIN_TOP + stack_h + 4;
  int y_custom = y_tags + hS + HUD_STACK_GAP;

  int tag_count = stz_tray_count_for_row(a, 3);
  if (tag_count <= 0) {
    a->stanza_tray_col[0] = 0;
    /* Draw "..." placeholder if all tags are used. */
    const char *dots = "...";
    int wDots = text_width(ui->font_small, dots);
    draw_text(ui, ui->font_small, xR - wDots, y_tags, dots, accent, false);
  } else if (a->stanza_tray_col[0] >= tag_count) {
    a->stanza_tray_col[0] = tag_count - 1;
  }
  int custom_count = stz_tray_count_for_row(a, 4);
  if (custom_count <= 0)
    a->stanza_tray_col[1] = 0;
  else if (a->stanza_tray_col[1] >= custom_count)
    a->stanza_tray_col[1] = custom_count - 1;

  /* Trays */
  stz_draw_tray_row_right(ui, a, xR, y_tags, 3,
                          (a->stanza_cursor_row == 3) ? a->stanza_tray_col[0]
                                                      : -1,
                          main, accent, hi);
  stz_draw_tray_row_right(ui, a, xR, y_custom, 4,
                          (a->stanza_cursor_row == 4) ? a->stanza_tray_col[1]
                                                      : -1,
                          main, accent, hi);

  const char *labsL[] = {"D-PAD", "A", "X", "SELECT"};
  const char *actsL[] = {"move", "grab/place", "delete", "custom/reset"};
  const char *labsR[] = {"R3", "B", "Y"};
  const char *actsR[] = {"save", "back", "exit"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 4, labsR, actsR, 3);

  if (a->stanza_save_prompt_open) {
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_Rect rr = {0, 0, ui->w, ui->h};
    SDL_RenderFillRect(ui->ren, &rr);

    const int mw = ui->w - (UI_MARGIN_X * 4);
    const int mh = 180;
    SDL_Rect mrect = {(ui->w - mw) / 2, (ui->h - mh) / 2, mw, mh};
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ui->ren, &mrect);

    int mx = mrect.x + UI_MARGIN_X;
    int my = mrect.y + 20;

    draw_text(ui, ui->font_med, mx, my, "save stanzas", main, false);
    my += TTF_FontHeight(ui->font_med) + 12;

    SDL_Color c0 = (a->stanza_save_sel == 0) ? hi : accent;
    SDL_Color c1 = (a->stanza_save_sel == 1) ? hi : accent;

    draw_text(ui, ui->font_small, mx, my, "app general", c0, false);
    my += TTF_FontHeight(ui->font_small) + 10;

    char lbuf[256];
    const char *loc_key = a->scene_name[0] ? a->scene_name : a->cfg.scene;
    safe_snprintf(lbuf, sizeof(lbuf), "this location (%s)",
                  loc_key ? phase_strip_leading_tag(loc_key) : "");
    draw_text(ui, ui->font_small, mx, my, lbuf, c1, false);
    my += TTF_FontHeight(ui->font_small) + 14;

    const char *labs2L[] = {"A", "B"};
    const char *acts2L[] = {"save", "cancel"};
    draw_hint_pairs_lr(ui, main, accent, labs2L, acts2L, 2, NULL, NULL, 0);
  }
  if (a->stanza_custom_open) {
    draw_stanza_custom_keyboard(ui, a, main, accent, hi);
  }
}

static void handle_stanza_selector(UI *ui, App *a, Buttons *b) {
  (void)ui;
  if (!a || !b)
    return;

  if (a->stanza_custom_open) {
    handle_stanza_custom_keyboard(a, b);
    return;
  }

  if (a->stanza_save_prompt_open) {
    if (b->up || b->down) {
      a->stanza_save_sel = (a->stanza_save_sel == 0) ? 1 : 0;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b) {
      a->stanza_save_prompt_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->a) {
      /* Ensure work strings are current before
       * persisting. */
      stz_editor_sync_work(a);

      if (a->stanza_save_sel == 0) {
        safe_snprintf(a->cfg.hud_stanza1, sizeof(a->cfg.hud_stanza1), "%s",
                      a->stanza_work1);
        safe_snprintf(a->cfg.hud_stanza2, sizeof(a->cfg.hud_stanza2), "%s",
                      a->stanza_work2);
        safe_snprintf(a->cfg.hud_stanza3, sizeof(a->cfg.hud_stanza3), "%s",
                      a->stanza_work3);
        config_save(&a->cfg, CONFIG_PATH);
      } else {
        const char *loc_key = a->scene_name[0] ? a->scene_name : a->cfg.scene;
        stanza_override_set(a, loc_key, a->stanza_work1, a->stanza_work2,
                            a->stanza_work3);
        stanza_overrides_save(a);
      }

      a->stanza_save_prompt_open = false;
      stanza_selector_close(a);
      a->ui_needs_redraw = true;
      return;
    }
    return;
  }

  /* Back/exit */
  if (b->b) {
    stanza_selector_close(a);
    a->ui_needs_redraw = true;
    return;
  }
  if (b->y) {
    stanza_selector_close(a);
    a->meta_selector_open = false;
    a->ui_needs_redraw = true;
    return;
  }

  /* Reset to original. */
  if (b->select) {
    if (a->stanza_cursor_row == 4) {
      int col = a->stanza_tray_col[1];
      StanzaPiece p = stz_tray_piece_for_row_col(a, a->stanza_cursor_row, col);
      if (p == STZ_P_CUSTOM) {
        a->stanza_custom_buf[0] = 0;
        a->stanza_custom_kb_row = 0;
        a->stanza_custom_kb_col = 0;
        stanza_custom_kb_clamp(a);
        a->stanza_custom_open = true;
        a->ui_needs_redraw = true;
        return;
      }
    }
    safe_snprintf(a->stanza_work1, sizeof(a->stanza_work1), "%s",
                  a->stanza_orig1);
    safe_snprintf(a->stanza_work2, sizeof(a->stanza_work2), "%s",
                  a->stanza_orig2);
    safe_snprintf(a->stanza_work3, sizeof(a->stanza_work3), "%s",
                  a->stanza_orig3);
    stz_editor_load_from_work(a);
    a->ui_needs_redraw = true;
    return;
  }

  /* Save prompt */
  if (b->r3) {
    a->stanza_save_prompt_open = true;
    a->stanza_save_sel = 0;
    a->ui_needs_redraw = true;
    return;
  }

  /* Row navigation */
  if (b->up || b->down) {
    int dir = b->down ? 1 : -1;
    a->stanza_cursor_row += dir;
    if (a->stanza_cursor_row < 0)
      a->stanza_cursor_row = 0;
    if (a->stanza_cursor_row > 4)
      a->stanza_cursor_row = 4;
    a->ui_needs_redraw = true;
    return;
  }

  /* Column navigation */
  if (b->left || b->right) {
    int dir = b->right ? 1 : -1;
    int row = a->stanza_cursor_row;

    if (row >= 0 && row <= 2) {
      int *cur = &a->stanza_line_cursor[row];
      int len = a->stanza_line_len[row];
      *cur += dir;
      if (*cur < 0)
        *cur = 0;
      if (*cur > len)
        *cur = len;
    } else {
      int tray = 0;
      if (row == 3)
        tray = 0;
      else
        tray = 1;
      int n = stz_tray_count_for_row(a, row);
      if (n <= 0)
        n = 1;
      a->stanza_tray_col[tray] += dir;
      if (a->stanza_tray_col[tray] < 0)
        a->stanza_tray_col[tray] = 0;
      if (a->stanza_tray_col[tray] >= n)
        a->stanza_tray_col[tray] = n - 1;
    }

    a->ui_needs_redraw = true;
    return;
  }

  /* Delete (line rows only) */
  if (b->x) {
    int row = a->stanza_cursor_row;
    if (row >= 0 && row <= 2) {
      int *lenp = &a->stanza_line_len[row];
      int *cur = &a->stanza_line_cursor[row];
      if (*lenp > 0) {
        int idx = (*cur < *lenp) ? *cur : (*lenp - 1);
        if (idx < 0)
          idx = 0;
        if (idx >= *lenp)
          idx = *lenp - 1;

        for (int i = idx; i < (*lenp - 1); i++) {
          a->stanza_line_pieces[row][i] = a->stanza_line_pieces[row][i + 1];
          safe_snprintf(a->stanza_line_custom[row][i],
                        sizeof(a->stanza_line_custom[row][i]), "%s",
                        a->stanza_line_custom[row][i + 1]);
        }
        if (*lenp > 0)
          a->stanza_line_custom[row][*lenp - 1][0] = 0;
        (*lenp)--;
        if (*cur > *lenp)
          *cur = *lenp;
        stz_editor_sync_work(a);
        a->ui_needs_redraw = true;
      }
    }
    return;
  }

  /* Grab / place */
  if (b->a) {
    int row = a->stanza_cursor_row;

    /* Trays: pick a piece into "holding". */
    if (row >= 3 && row <= 4) {
      int tray = 0;
      if (row == 3)
        tray = 0;
      else
        tray = 1;
      int col = a->stanza_tray_col[tray];
      StanzaPiece p = stz_tray_piece_for_row_col(a, row, col);
      if (row == 3 && stz_piece_is_tag(p) && stz_tag_in_lines(a, p)) {
        return;
      }
      if (p == STZ_P_CUSTOM) {
        a->stanza_custom_buf[0] = 0;
        a->stanza_custom_kb_row = 0;
        a->stanza_custom_kb_col = 0;
        stanza_custom_kb_clamp(a);
        a->stanza_custom_open = true;
        a->ui_needs_redraw = true;
        return;
      }
      if (p != STZ_P_NONE) {
        a->stanza_holding = true;
        a->stanza_hold_piece = (int)p;
        a->ui_needs_redraw = true;
      }
      return;
    }

    /* Lines: grab existing token at cursor (if not
     * holding), or place held token. */
    if (row >= 0 && row <= 2) {
      int *lenp = &a->stanza_line_len[row];
      int *cur = &a->stanza_line_cursor[row];
      if (*cur < 0)
        *cur = 0;
      if (*cur > *lenp)
        *cur = *lenp;

      if (a->stanza_holding) {
        if (*lenp >= 24)
          return; /* full */
        /* insert at cursor */
        for (int i = *lenp; i > *cur; i--) {
          a->stanza_line_pieces[row][i] = a->stanza_line_pieces[row][i - 1];
          safe_snprintf(a->stanza_line_custom[row][i],
                        sizeof(a->stanza_line_custom[row][i]), "%s",
                        a->stanza_line_custom[row][i - 1]);
        }
        a->stanza_line_pieces[row][*cur] = a->stanza_hold_piece;
        if (a->stanza_hold_piece == STZ_P_CUSTOM) {
          safe_snprintf(a->stanza_line_custom[row][*cur],
                        sizeof(a->stanza_line_custom[row][*cur]), "%s",
                        a->stanza_hold_custom);
        } else {
          a->stanza_line_custom[row][*cur][0] = 0;
        }
        (*lenp)++;
        (*cur)++;
        a->stanza_holding = false;
        a->stanza_hold_piece = (int)STZ_P_NONE;
        a->stanza_hold_custom[0] = 0;
        stz_editor_sync_work(a);
        a->ui_needs_redraw = true;
        return;
      }

      /* Not holding: grab token at cursor (token to the
       * right of the bar). */
      if (*cur < *lenp) {
        a->stanza_holding = true;
        a->stanza_hold_piece = a->stanza_line_pieces[row][*cur];
        if (a->stanza_hold_piece == STZ_P_CUSTOM) {
          safe_snprintf(a->stanza_hold_custom, sizeof(a->stanza_hold_custom),
                        "%s", a->stanza_line_custom[row][*cur]);
        } else {
          a->stanza_hold_custom[0] = 0;
        }
        for (int i = *cur; i < (*lenp - 1); i++)
          a->stanza_line_pieces[row][i] = a->stanza_line_pieces[row][i + 1];
        for (int i = *cur; i < (*lenp - 1); i++) {
          safe_snprintf(a->stanza_line_custom[row][i],
                        sizeof(a->stanza_line_custom[row][i]), "%s",
                        a->stanza_line_custom[row][i + 1]);
        }
        if (*lenp > 0)
          a->stanza_line_custom[row][*lenp - 1][0] = 0;
        (*lenp)--;
        if (*cur > *lenp)
          *cur = *lenp;
        stz_editor_sync_work(a);
        a->ui_needs_redraw = true;
        return;
      }
    }
    return;
  }
}

static void hud_collect_display_values(App *a, StanzaDisplay *out,
                                       char *mood_buf, size_t mood_buf_sz) {
  if (!out)
    return;
  /* Resolve dynamic values (display forms). */
  const char *loc_raw = (a && a->scene_name[0]) ? a->scene_name : "";
  const char *bg_raw = (a && a->weather_name[0]) ? a->weather_name : "";
  const char *loc_disp = phase_strip_leading_tag(loc_raw);
  const char *bg_disp = phase_strip_leading_tag(bg_raw);

  const char *season_raw = (a && a->cfg.season[0]) ? a->cfg.season : "";
  const char *season_disp = phase_strip_leading_tag(season_raw);

  /* Mood display: derived from cfg.ambience_name
   * folder/file naming. */
  char amb_raw[256] = {0};
  char amb_base[256] = {0};
  char amb_disp[256] = {0};
  if (a && a->cfg.ambience_name[0]) {
    safe_snprintf(amb_raw, sizeof(amb_raw), "%s", a->cfg.ambience_name);
    trim_ascii_inplace(amb_raw);
    safe_snprintf(amb_base, sizeof(amb_base), "%s", amb_raw);
    strip_ext_inplace(amb_base);
    {
      char base[256] = {0};
      char tag[128] = {0};
      if (split_trailing_tag(amb_base, base, sizeof(base), tag, sizeof(tag))) {
        safe_snprintf(amb_base, sizeof(amb_base), "%s", base);
      }
    }
  }
  if (amb_base[0])
    mood_display_from_folder(amb_base, amb_disp, sizeof(amb_disp));
  safe_snprintf(mood_buf, mood_buf_sz, "%s",
                (amb_disp[0] ? amb_disp : amb_base));

  out->season = season_disp;
  out->background = bg_disp;
  out->phase = season_disp;
  out->location = loc_disp;
  out->mood = mood_buf;
  out->custom = (a ? a->stanza_custom_tag : "");
}

static void draw_poetic_stack_upper_right(UI *ui, App *a, int xR, int y_top,
                                          SDL_Color main, SDL_Color accent) {
  char mood_buf[256] = {0};
  StanzaDisplay disp = {0};
  hud_collect_display_values(a, &disp, mood_buf, sizeof(mood_buf));

  /* Only strike-through when this mood actually has an
   * ambience file. */
  bool ambience_file_present = false;
  {
    char p[PATH_MAX];
    p[0] = 0;
    ambience_path_from_name(a, p, sizeof(p));
    ambience_file_present = (p[0] != 0);
  }
  const bool ambience_audible =
      (a->cfg.ambience_enabled && !a->vol_ambience_muted &&
       a->cfg.vol_ambience > 0);

  const char *tpl1 = NULL;
  const char *tpl2 = NULL;
  const char *tpl3 = NULL;
  stanza_get_active_tpls(a, &tpl1, &tpl2, &tpl3);

  int y = y_top;
  bool contains_mood = false;

  if (a && a->stanza_selector_open) {
    const SDL_Color hi = color_from_idx(a->cfg.highlight_color_idx);
    for (int i = 0; i < 3; i++) {
      StanzaPiece prev =
          a->stanza_holding ? (StanzaPiece)a->stanza_hold_piece : STZ_P_NONE;
      stz_draw_piece_line_right(
          ui, &disp, xR, y, a->stanza_line_pieces[i], a->stanza_line_custom[i],
          a->stanza_line_len[i], a->stanza_line_cursor[i], main, accent, hi,
          (a->stanza_cursor_row == i), true, main,
          (a->stanza_cursor_row == i) ? prev : STZ_P_NONE,
          (a->stanza_cursor_row == i && prev == STZ_P_CUSTOM)
              ? a->stanza_hold_custom
              : NULL);
      y += TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;
    }
    return;
  }

  int w1 = hud_draw_tpl_line(ui, xR, y, tpl1, main, accent, disp.season,
                             disp.background, disp.phase, disp.location,
                             disp.mood, true, &contains_mood);
  if (w1 > 0)
    y = y_top + TTF_FontHeight(ui->font_med) + HUD_STACK_GAP;

  contains_mood = false;
  int w2 = hud_draw_tpl_line(ui, xR, y, tpl2, main, accent, disp.season,
                             disp.background, disp.phase, disp.location,
                             disp.mood, true, &contains_mood);
  if (w2 > 0) {
    if (contains_mood && ambience_file_present && !ambience_audible) {
      int h = TTF_FontHeight(ui->font_small);
      draw_strikethrough(ui, xR - w2, y, w2, h,
                         (SDL_Color){255, 255, 255, 255});
    }
    y += TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;
  }

  contains_mood = false;
  if (tpl3 && tpl3[0]) {
    int w3 = hud_draw_tpl_line(ui, xR, y, tpl3, main, accent, disp.season,
                               disp.background, disp.phase, disp.location,
                               disp.mood, true, &contains_mood);
    if (w3 > 0 && contains_mood && ambience_file_present && !ambience_audible) {
      int h = TTF_FontHeight(ui->font_small);
      draw_strikethrough(ui, xR - w3, y, w3, h,
                         (SDL_Color){255, 255, 255, 255});
    }
  }
}

void draw_top_hud(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

  /* "Home idle" means the user hasn't chosen a timer
     mode yet. We keep the HUD in the calm branding
     state until a mode is explicitly selected. */
  const bool home_idle = (a->landing_idle || !a->mode_ever_selected);
  int xL = UI_MARGIN_X;
  int xR = ui->w - UI_MARGIN_X;
  int y1 = UI_MARGIN_TOP;
  /* When Menu/Settings overlays are open:
     - Top-left: ".stillroom" (medium, italic, main
     color)
     - Top-right: weather/location + secondary ambience
     line (same behavior as elsewhere)
     - Bottom-left: device clock (stacked, minute on
     top, hour on bottom)
     - Bottom-right: device battery (stacked, percent on
     top, "battery" on bottom)
  */
  const bool overlay_open =
      (a->settings_open || a->timer_menu_open || a->booklets.open ||
       a->screen == SCREEN_MENU || a->screen == SCREEN_POMO_PICK ||
       a->screen == SCREEN_CUSTOM_PICK || a->screen == SCREEN_MEDITATION_PICK ||
       a->screen == SCREEN_FOCUS_MENU || a->screen == SCREEN_TASKS_PICK ||
       a->screen == SCREEN_TASKS_LIST || a->screen == SCREEN_HABITS_PICK ||
       a->screen == SCREEN_HABITS_LIST || a->screen == SCREEN_HABITS_TEXT ||
       a->screen == SCREEN_QUEST || a->screen == SCREEN_ROUTINE_EDIT ||
       a->screen == SCREEN_ROUTINE_LIST ||
       a->screen == SCREEN_ROUTINE_ENTRY_PICKER);
  if (home_idle || overlay_open) {
    /* Determine branding text */
    const char *branding_main = ".stillroom";
    const char *branding_sec = NULL;
    char greeting_buf[64];
    char name_buf[64];

    bool show_greeting = false;
    /* Show greeting if user has a name. */
    if (a->cfg.user_name[0]) {
      show_greeting = true;
    }

    if (show_greeting) {
      time_t now = time(NULL);
      struct tm tmv;
      localtime_r(&now, &tmv);
      int h = tmv.tm_hour;
      const char *g = "hello,";
      if (h >= 5 && h < 7)
        g = "quiet dawn,";
      else if (h >= 7 && h < 11)
        g = "slow morning,";
      else if (h >= 11 && h < 16)
        g = "soft afternoon,";
      else if (h >= 16 && h < 19)
        g = "calm evening,";
      else if (h >= 19 && h < 21)
        g = "fading light,";
      else
        g = "still night,";
      safe_snprintf(greeting_buf, sizeof(greeting_buf), "%s", g);
      safe_snprintf(name_buf, sizeof(name_buf), "%s", a->cfg.user_name);
      branding_main = greeting_buf;
      branding_sec = name_buf;
    }

    int style = (branding_sec != NULL) ? 0 : TTF_STYLE_ITALIC;
    draw_text_style(ui, ui->font_med, xL, y1, branding_main, main, false,
                    style);
    if (branding_sec) {
      /* Draw name in accent color, medium font, below
       * greeting */
      int yTag = y1 + TTF_FontHeight(ui->font_med) + 2;
      draw_text(ui, ui->font_med, xL, yTag, branding_sec, accent, false);
    }
#if 0 /* Hide old tagline if greeting is active, or                                                                                                                                                                      \
                                                                                                                                                                                                         just disable it \
                                                                                                                                                                                 entirely as per new                     \
                                                                                                                                                         design */
    /* Tagline (not interactive, purely informational). */
    int yTag = y1 + TTF_FontHeight(ui->font_med) + 2;
    /* Tagline with accent emphasis on "place" and "time". */
    const char *t1 = "a ";
    const char *t2 = "place";
    const char *t3 = " for measured ";
    const char *t4 = "time";
    int w1 = 0, w2 = 0, w3 = 0, w4 = 0, h = 0;
    TTF_SizeUTF8(ui->font_small, t1, &w1, &h);
    TTF_SizeUTF8(ui->font_small, t2, &w2, &h);
    TTF_SizeUTF8(ui->font_small, t3, &w3, &h);
    TTF_SizeUTF8(ui->font_small, t4, &w4, &h);
    draw_text_style(ui, ui->font_small, xL, yTag, t1, main, false,
                    TTF_STYLE_ITALIC);
    draw_text_style(ui, ui->font_small, xL + w1, yTag, t2, accent, false,
                    TTF_STYLE_ITALIC);
    draw_text_style(ui, ui->font_small, xL + w1 + w2, yTag, t3, main, false,
                    TTF_STYLE_ITALIC);
    draw_text_style(ui, ui->font_small, xL + w1 + w2 + w3, yTag, t4, accent,
                    false, TTF_STYLE_ITALIC);
#endif
    /* Upper-right poetic stack. */
    if (a->screen != SCREEN_ROUTINE_EDIT && a->screen != SCREEN_ROUTINE_LIST)
      draw_poetic_stack_upper_right(ui, a, xR, y1, main, accent);
    /* Bottom stacks (clock + battery) are shown for
       overlays/menus. The calm landing screen
       (home_idle) should only show the music footer.
     */
    if (overlay_open && a->screen != SCREEN_ROUTINE_LIST) {
      int yStackTop = ui_bottom_stack_top_y(ui);
      draw_clock_upper_left_stacked(ui, a, xL, yStackTop);
      draw_battery_upper_right_stacked(ui, a, xR, yStackTop);
    }
    return;
  }
  /* Match the "stack" spacing used by battery/clock
   * widgets. */
  int y2 = y1 + TTF_FontHeight(ui->font_med) + HUD_STACK_GAP;
  if (y2 < y1 + 1)
    y2 = y1 + 1;
  /* Left side */
  bool left_is_pomo_progress = false;
  char left_main[256] = {0};
  char left_sec[256] = {0};
  if (a->screen == SCREEN_TIMER) {
    if (a->mode == MODE_POMODORO) {
      left_is_pomo_progress = true;
      if (a->session_complete)
        safe_snprintf(left_sec, sizeof(left_sec), "session complete!");
      else if (a->paused)
        safe_snprintf(left_sec, sizeof(left_sec), "paused");
      else if (!a->running)
        safe_snprintf(left_sec, sizeof(left_sec), "stopped");
      else if (a->pomo_is_break) {
        if (a->pomo_break_is_long)
          safe_snprintf(left_sec, sizeof(left_sec), "resting...");
        else
          safe_snprintf(left_sec, sizeof(left_sec), "taking a break...");
      } else
        format_focusing_line(a, left_sec, sizeof(left_sec));
    } else if (a->mode == MODE_CUSTOM) {
      safe_snprintf(left_main, sizeof(left_main), "timer");
      if (a->session_complete)
        safe_snprintf(left_sec, sizeof(left_sec), "session complete!");
      else if (a->paused)
        safe_snprintf(left_sec, sizeof(left_sec), "paused");
      else if (a->running)
        format_focusing_line(a, left_sec, sizeof(left_sec));
      else
        safe_snprintf(left_sec, sizeof(left_sec), "stopped");
    } else if (a->mode == MODE_MEDITATION) {
      safe_snprintf(left_main, sizeof(left_main), "meditation");
      if (a->session_complete)
        safe_snprintf(left_sec, sizeof(left_sec), "session complete!");
      else if (a->paused)
        safe_snprintf(left_sec, sizeof(left_sec), "paused");
      else if (a->meditation_run_kind == 2 &&
               a->meditation_guided_repeats_remaining > 0) {
        safe_snprintf(left_sec, sizeof(left_sec), "%s...",
                      breath_phase_label(a->breath_phase));
      } else if (a->running)
        safe_snprintf(left_sec, sizeof(left_sec), "meditating...");
      else
        safe_snprintf(left_sec, sizeof(left_sec), "stopped");
    } else {
      safe_snprintf(left_main, sizeof(left_main), "stopwatch");
      if (a->paused)
        safe_snprintf(left_sec, sizeof(left_sec), "paused");
      else
        format_focusing_line(a, left_sec, sizeof(left_sec));
    }
  } else {
    /* Personalized Greeting Mode (Non-Timer Screens) */
    bool show_old_branding = false;

    if (show_old_branding || !a->cfg.user_name[0]) {
      safe_snprintf(left_main, sizeof(left_main), ".stillroom");
#if STILLROOM_SHOW_TAGLINE
      safe_snprintf(left_sec, sizeof(left_sec), "a place for measured time");
#else
      left_sec[0] = '\0';
#endif
    } else {
      /* Time-based greeting */
      time_t now = time(NULL);
      struct tm tmv;
      localtime_r(&now, &tmv);
      int h = tmv.tm_hour;

      const char *greeting = "hello,"; /* fallback */
      if (h >= 5 && h < 7)
        greeting = "quiet dawn,";
      else if (h >= 7 && h < 11)
        greeting = "slow morning,";
      else if (h >= 11 && h < 16)
        greeting = "soft afternoon,";
      else if (h >= 16 && h < 19)
        greeting = "calm evening,";
      else if (h >= 19 && h < 21)
        greeting = "fading light,";
      else
        greeting = "still night,";

      safe_snprintf(left_main, sizeof(left_main), "%s", greeting);
      safe_snprintf(left_sec, sizeof(left_sec), "%s", a->cfg.user_name);
    }
  }
  /* Render left primary line */

  if (left_is_pomo_progress) {
    /* New semantics:
       - repeat = number of focus sessions
       - a "pomodoro" is (focus + break), and it only
       "ends with a break" when a final rest exists.
       This keeps the top-left progress meaningful and
       matches the user's described behavior. */
    int sessions_total = (a->pomo_loops_total > 0) ? a->pomo_loops_total : 1;
    bool has_rest = (a->pomo_long_break_seconds > 0);
    int total_pomos = has_rest
                          ? sessions_total
                          : ((sessions_total > 1) ? (sessions_total - 1) : 1);
    int disp = 1;
    if (has_rest) {
      if (a->pomo_is_break && a->pomo_break_is_long)
        disp = sessions_total; /* in rest */
      else if (a->pomo_is_break)
        disp = a->pomo_loops_done; /* break after focus
                                      k */
      else
        disp = a->pomo_loops_done + 1; /* focus k */
    } else {
      if (sessions_total == 1) {
        disp = 1;
      } else {
        if (a->pomo_is_break)
          disp = a->pomo_loops_done; /* break after
                                        focus k */
        else
          disp = a->pomo_loops_done + 1; /* focus k */
        if (disp > total_pomos)
          disp = total_pomos; /* cap: last focus has no
                                 "pomodoro" without rest */
        if (disp < 1)
          disp = 1;
      }
    }
    if (disp < 1)
      disp = 1;
    if (disp > total_pomos)
      disp = total_pomos;
    const char *ords[] = {"",      "first",    "second",  "third",  "fourth",
                          "fifth", "sixth",    "seventh", "eighth", "ninth",
                          "tenth", "eleventh", "twelfth"};
    const char *nums[] = {"",     "one",    "two",   "three", "four",
                          "five", "six",    "seven", "eight", "nine",
                          "ten",  "eleven", "twelve"};
    const char *ord = (disp >= 1 && disp <= 12) ? ords[disp] : "next";
    const char *num =
        (total_pomos >= 1 && total_pomos <= 12) ? nums[total_pomos] : "many";
    const char *word_pomo = " pomodoro ";
    const char *word_of = "of";
    int w_ord = 0, h_ord = 0;
    int w_p = 0, h_p = 0;
    int w_of = 0, h_of = 0;
    int w_num = 0, h_num = 0;
    TTF_SizeUTF8(ui->font_small, ord, &w_ord, &h_ord);
    TTF_SizeUTF8(ui->font_med, word_pomo, &w_p, &h_p);
    TTF_SizeUTF8(ui->font_small, word_of, &w_of, &h_of);
    TTF_SizeUTF8(ui->font_small, num, &w_num, &h_num);
    int gap = UI_INLINE_GAP_PX;
    int baseline = y1 + TTF_FontAscent(ui->font_med);
    int y_small_top = baseline - TTF_FontAscent(ui->font_small);
    int x = xL;
    draw_text_style(ui, ui->font_small, x, y_small_top, ord, accent, false,
                    TTF_STYLE_ITALIC);
    x += w_ord + gap;
    draw_text(ui, ui->font_med, x, y1, word_pomo, main, false);
    x += w_p;
    draw_text_style(ui, ui->font_small, x, y_small_top, word_of, accent, false,
                    TTF_STYLE_ITALIC);
    x += w_of + gap;
    draw_text_style(ui, ui->font_small, x, y_small_top, num, accent, false,
                    TTF_STYLE_ITALIC);
  } else {
    int style = (strcmp(left_main, ".stillroom") == 0) ? TTF_STYLE_ITALIC : 0;
    draw_text_style(ui, ui->font_med, xL, y1, left_main, main, false, style);
  }
  if (left_sec[0]) {
    /* If showing name (personalized), use Medium Font +
       Accent + Normal style (not italic) If showing
       .stillroom tagline, use Small Font + Accent +
       Italic (legacy) */
    if (a->screen != SCREEN_TIMER && a->cfg.user_name[0] &&
        strcmp(left_main, ".stillroom") != 0) {
      /* Personalized Name: Medium Font, Accent */
      draw_text(ui, ui->font_med, xL, y2, left_sec, accent, false);
    } else {
      /* Tagline or status: Small Font, Italic */
      draw_text_style(ui, ui->font_small, xL, y2, left_sec, accent, false,
                      TTF_STYLE_ITALIC);
    }
  }
  /* Upper-right poetic stack. */
  draw_poetic_stack_upper_right(ui, a, xR, y1, main, accent);
}

static void draw_big_time_upper_left(UI *ui, App *a, const char *s) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  int y2 = UI_MARGIN_TOP + UI_ROW_GAP;
  int y = y2 + UI_ROW_GAP + TIMER_TOP_PAD;
  int x = TIMER_LEFT_X;
  draw_text_cached(ui, &a->cache_big_time, ui->font_big, x, y, s ? s : "", main,
                   false, 0);
}
static void format_ordinal(char *out, size_t out_sz, int n1) {
  /* n1 is 1-based */
  const int n = n1;
  const int mod100 = n % 100;
  const int mod10 = n % 10;
  const char *suf = "th";
  if (mod100 < 11 || mod100 > 13) {
    if (mod10 == 1)
      suf = "st";
    else if (mod10 == 2)
      suf = "nd";
    else if (mod10 == 3)
      suf = "rd";
  }
  safe_snprintf(out, out_sz, "%d%s", n, suf);
}
static void draw_stopwatch_laps(UI *ui, App *a) {
  if (a->stopwatch_lap_count <= 0)
    return;
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  const int x = TIMER_LEFT_X;
  const int y2 = UI_MARGIN_TOP + UI_ROW_GAP;
  const int y_big = y2 + UI_ROW_GAP + TIMER_TOP_PAD;
  /* Place laps under the big time */
  int y = y_big + TTF_FontHeight(ui->font_big) + UI_ROW_GAP;
  const int line_h = TTF_FontHeight(ui->font_small) + 2;
  const int max_lines = 10; /* keep it readable */
  int start = 0;
  if (a->stopwatch_lap_count > max_lines)
    start = a->stopwatch_lap_count - max_lines;
  for (int i = start; i < a->stopwatch_lap_count; i++) {
    char ord[16];
    char t[32];
    /* i is an index into the fixed-size lap buffer; add
     * the base so ordinal labels stay correct after
     * scrolling. */
    format_ordinal(ord, sizeof(ord), a->stopwatch_lap_base + i + 1);
    fmt_hms_opt_hours(t, sizeof(t), a->stopwatch_laps[i]);
    int asc = TTF_FontAscent(ui->font_small);
    int baseline_y = y + asc;
    draw_text_style_baseline(ui, ui->font_small, x, baseline_y, ord, main,
                             false, TTF_STYLE_ITALIC);
    int wOrd = text_width(ui->font_small, ord);
    int x2 = x + wOrd + UI_INLINE_GAP_PX;
    draw_text_style_baseline(ui, ui->font_small, x2, baseline_y, t, accent,
                             false, TTF_STYLE_ITALIC);
    y += line_h;
  }
}
static void draw_session_complete_upper_left(UI *ui, App *a, const char *msg) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Surface *surf = render_text_surface(ui->font_med, msg, main);
  if (!surf)
    return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(ui->ren, surf);
  if (!tex) {
    SDL_FreeSurface(surf);
    return;
  }
  int y2 = UI_MARGIN_TOP + UI_ROW_GAP;
  int y = y2 + UI_ROW_GAP + TIMER_TOP_PAD + 10;
  int x = TIMER_LEFT_X;
  SDL_Rect dst = {x, y, surf->w, surf->h};
  SDL_RenderCopy(ui->ren, tex, NULL, &dst);
  SDL_DestroyTexture(tex);
  SDL_FreeSurface(surf);
}
/* ----------------------------- Settings headers
 * -----------------------------
 */
static const char *settings_title(SettingsView v) {
  switch (v) {
  case SET_VIEW_MAIN:
    return "settings";
  case SET_VIEW_SCENE:
    return "settings, scene";
  case SET_VIEW_APPEARANCE:
    return "settings, interface";
  case SET_VIEW_FONTS:
    return "settings, interface, font";
  case SET_VIEW_FONT_SIZES:
    return "settings, interface, font sizes";
  case SET_VIEW_COLORS:
    return "settings, interface, color";
  case SET_VIEW_SOUNDS:
    return "settings, audio";
  case SET_VIEW_SOUND_VOLUME:
    return "settings, audio, volume";
  case SET_VIEW_SOUND_NOTIFICATIONS:
    return "settings, audio, notification sounds";
  case SET_VIEW_SOUND_MEDITATION:
    return "settings, audio, notification sounds, "
           "meditation";
  case SET_VIEW_MISC:
    return "settings, general";
  case SET_VIEW_ABOUT:
    return "settings, about";
  case SET_VIEW_CHANGELOG:
    return "settings, about, changelog";
  case SET_VIEW_RELEASES:
    return "settings, about, downloads";
  default:
    return "settings";
  }
}
static int about_wrap_count(UI *ui, TTF_Font *font, const char *text,
                            int max_w) {
  if (!ui || !font || !text)
    return 0;
  if (!text[0])
    return 1;
  int count = 0;
  const char *p = text;
  while (*p) {
    if (*p == '\n') {
      count++;
      p++;
      continue;
    }
    char line[4096] = {0};
    int line_len = 0;
    while (*p && *p != '\n') {
      while (*p == ' ')
        p++;
      if (!*p || *p == '\n')
        break;
      char word[512] = {0};
      int wl = 0;
      while (*p && *p != ' ' && *p != '\n' && wl < (int)sizeof(word) - 1) {
        word[wl++] = *p++;
      }
      word[wl] = 0;
      while (*p == ' ')
        p++;
      char trial[4096];
      if (line_len == 0)
        safe_snprintf(trial, sizeof(trial), "%s", word);
      else
        safe_snprintf(trial, sizeof(trial), "%s %s", line, word);
      int w = text_width(font, trial);
      if (w <= max_w || line_len == 0) {
        safe_snprintf(line, sizeof(line), "%s", trial);
        line_len = (int)strlen(line);
      } else {
        count++;
        safe_snprintf(line, sizeof(line), "%s", word);
        line_len = (int)strlen(line);
      }
    }
    if (line_len > 0)
      count++;
    if (*p == '\n')
      p++;
  }
  return count;
}
static bool about_draw_wrapped(UI *ui, TTF_Font *font, int x, int *y, int row_h,
                               int y_bottom, int *skip, SDL_Color color,
                               int style, bool centered, const char *text,
                               int max_w) {
  if (!ui || !font || !y || !skip || !text)
    return false;
  if (!text[0])
    text = "(no release notes available.)";
  const char *p = text;
  while (*p) {
    if (*p == '\n') {
      if (*skip > 0) {
        (*skip)--;
      } else {
        if (*y + TTF_FontHeight(font) > y_bottom)
          return false;
        draw_text_style(ui, font, x, *y, "", color, false, style);
        *y += row_h;
      }
      p++;
      continue;
    }
    char line[4096] = {0};
    int line_len = 0;
    while (*p && *p != '\n') {
      while (*p == ' ')
        p++;
      if (!*p || *p == '\n')
        break;
      char word[512] = {0};
      int wl = 0;
      while (*p && *p != ' ' && *p != '\n' && wl < (int)sizeof(word) - 1) {
        word[wl++] = *p++;
      }
      word[wl] = 0;
      while (*p == ' ')
        p++;
      char trial[4096];
      if (line_len == 0)
        safe_snprintf(trial, sizeof(trial), "%s", word);
      else
        safe_snprintf(trial, sizeof(trial), "%s %s", line, word);
      int w = text_width(font, trial);
      if (w <= max_w || line_len == 0) {
        safe_snprintf(line, sizeof(line), "%s", trial);
        line_len = (int)strlen(line);
      } else {
        if (*skip > 0) {
          (*skip)--;
        } else {
          if (*y + TTF_FontHeight(font) > y_bottom)
            return false;
          int draw_x = x;
          if (centered) {
            draw_x = (ui->w - text_width(font, line)) / 2;
          }
          draw_text_style(ui, font, draw_x, *y, line, color, false, style);
          *y += row_h;
        }
        safe_snprintf(line, sizeof(line), "%s", word);
        line_len = (int)strlen(line);
      }
    }
    if (line_len > 0) {
      if (*skip > 0) {
        (*skip)--;
      } else {
        if (*y + TTF_FontHeight(font) > y_bottom)
          return false;
        int draw_x = x;
        if (centered) {
          draw_x = (ui->w - text_width(font, line)) / 2;
        }
        draw_text_style(ui, font, draw_x, *y, line, color, false, style);
        *y += row_h;
      }
    }
    if (*p == '\n')
      p++;
  }
  return true;
}
static bool about_draw_line(UI *ui, TTF_Font *font, int x, int *y, int row_h,
                            int y_bottom, int *skip, SDL_Color color, int style,
                            const char *text) {
  if (!ui || !font || !y || !skip)
    return false;
  if (*skip > 0) {
    (*skip)--;
    return true;
  }
  if (*y + TTF_FontHeight(font) > y_bottom)
    return false;
  draw_text_style(ui, font, x, *y, text ? text : "", color, false, style);
  *y += row_h;
  return true;
}
static int settings_changelog_line_count(UI *ui, const App *a, int max_w) {
  if (!ui || !a)
    return 0;
  const char *notes = a->update_latest_notes[0]
                          ? a->update_latest_notes
                          : "(no release notes available.)";
  return about_wrap_count(ui, ui->font_small, notes, max_w) + 2;
}
static int settings_changelog_max_scroll(UI *ui, App *a, int max_w) {
  if (!ui || !a)
    return 0;
  const int row_h = UI_ROW_GAP + 6;
  const int header_y = BIG_TIMER_Y;
  const int start_y = header_y + UI_ROW_GAP;
  const int y0 = start_y + 10;
  const int y_bottom = overlay_bottom_text_limit_y(ui);
  int max_rows = (y_bottom - y0) / row_h;
  if (max_rows < 1)
    max_rows = 1;
  int count = settings_changelog_line_count(ui, a, max_w);
  int max_scroll = count - max_rows;
  if (max_scroll < 0)
    max_scroll = 0;
  return max_scroll;
}
static void draw_release_popup(UI *ui, App *a) {
  if (!ui || !a || !a->release_popup_open)
    return;
  if (a->release_sel < 0 || a->release_sel >= a->release_tags.count)
    return;
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 180);
  SDL_Rect r = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &r);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);

  const int x = UI_MARGIN_X;
  const int y = BIG_TIMER_Y;
  const int row_h = UI_ROW_GAP + 6;
  const char *tag = a->release_tags.items[a->release_sel];
  draw_text(ui, ui->font_med, x, y, tag ? tag : "release", main, false);
  const char *notes = "(no release notes available.)";
  if (a->release_sel >= 0 && a->release_sel < a->release_notes.count) {
    const char *cand = a->release_notes.items[a->release_sel];
    if (cand && cand[0])
      notes = cand;
  }
  int y_notes = y + 64;
  int y_bottom = y_notes + (row_h * 4);
  int skip = 0;
  int max_w = ui->w - (UI_MARGIN_X * 2);
  about_draw_wrapped(ui, ui->font_small, x, &y_notes, row_h, y_bottom, &skip,
                     accent, TTF_STYLE_ITALIC, false, notes, max_w);
  if ((a->release_status == RELEASE_STATUS_ERROR ||
       a->release_status == RELEASE_STATUS_APPLIED ||
       a->release_status == RELEASE_STATUS_DOWNLOADING) &&
      a->release_message[0]) {
    draw_text(ui, ui->font_small, x, y_notes + row_h, a->release_message,
              accent, false);
  }
  const char *opt0 = "download & unzip";
  const char *opt1 = "back";
  SDL_Color c0 = (a->release_popup_sel == 0) ? highlight : accent;
  SDL_Color c1 = (a->release_popup_sel == 1) ? highlight : accent;
  int y_opts = y_notes + (row_h * 2);
  draw_text(ui, ui->font_small, x, y_opts, opt0, c0, false);
  draw_text(ui, ui->font_small, x, y_opts + row_h, opt1, c1, false);
}

/* ----------------------------- Settings drawing
 * -----------------------------
 */
/* ----------------------------- Circular Layout
 * -----------------------------
 */

/* Adjusted so band 0 (header) aligns with
   PICKER_HEADER_Y (140). Band 0 y_center = CY -
   3*(BAND_H + GAP) Step = 45 + 22 = 67. 3*67 = 201.
   Target Band 0 Y = 160 + 22 = 182.
   CY = 182 + 195 = 377. WAIT. Band 0 is center-3*step.
   Gap is 20. Step is 65. 3*65 = 195.
   Band 0 Y (center) should be 160 + 22 = 182? No, Band
   0 center aligns with PICKER_HEADER_Y (160) roughly?
   Actually comment says "Target Band 0 Y = 160 + 22 =
   182". So CY = 182 + 195 = 377. */
#define CIRCLE_LAYOUT_CY 377
#define CIRCLE_LAYOUT_R 350
#define CIRCLE_LAYOUT_BANDS 7
#define CIRCLE_LAYOUT_BAND_H 45
#define CIRCLE_LAYOUT_GAP 20 // Reduced to 20

typedef struct {
  int y_center;
  int height;
  int width_at_limit;
  int x_left;
} CircleBand;

static CircleBand get_circle_band(int idx) {
  if (idx < 0 || idx >= CIRCLE_LAYOUT_BANDS)
    return (CircleBand){0};

  // Center index is (BANDS - 1) / 2. For 7 bands: 3.
  int center_idx = (CIRCLE_LAYOUT_BANDS - 1) / 2;
  int offset_steps = idx - center_idx;
  int step = CIRCLE_LAYOUT_BAND_H + CIRCLE_LAYOUT_GAP;
  /* Add extra gap after header (band 0). Shift all
   * other bands down by 22px.
   */
  int extra_offset = (idx > 0) ? 22 : 0;
  int y_c = CIRCLE_LAYOUT_CY + (offset_steps * step) + extra_offset;

  // Calculate safe width
  // The band extends from y_c - H/2 to y_c + H/2.
  // We need the width at the point farthest from circle
  // center (dy_max).
  int y_top = y_c - CIRCLE_LAYOUT_BAND_H / 2;
  int y_bot = y_c + CIRCLE_LAYOUT_BAND_H / 2;

  int dy_top = abs(y_top - CIRCLE_LAYOUT_CY);
  int dy_bot = abs(y_bot - CIRCLE_LAYOUT_CY);
  int dy_max = (dy_top > dy_bot) ? dy_top : dy_bot;

  int half_w = 0;
  if (dy_max < CIRCLE_LAYOUT_R) {
    half_w = (int)sqrt(CIRCLE_LAYOUT_R * CIRCLE_LAYOUT_R - dy_max * dy_max);
  }

  return (CircleBand){.y_center = y_c,
                      .height = CIRCLE_LAYOUT_BAND_H,
                      .width_at_limit = half_w * 2,
                      .x_left = CIRCLE_LAYOUT_CX - half_w};
}

void draw_text_centered_band(UI *ui, TTF_Font *font, int band_idx,
                             const char *text, SDL_Color color) {
  CircleBand b = get_circle_band(band_idx);
  if (b.height == 0)
    return;
  /* Clamp width to USABLE_COLUMN_WIDTH to ensure
   * text stays in central area
   */
  int max_w = b.width_at_limit;
  if (max_w > USABLE_COLUMN_WIDTH)
    max_w = USABLE_COLUMN_WIDTH;

  int text_h = TTF_FontHeight(font);
  int y = b.y_center - text_h / 2;
  int w = text_width(font, text);

  if (w > max_w) {
    /* Use wrapping instead of truncation */
    draw_text_wrapped_centered(ui, font, CIRCLE_LAYOUT_CX, y, text, color,
                               max_w, 2);
  } else {
    int x = CIRCLE_LAYOUT_CX - w / 2;
    draw_text(ui, font, x, y, text, color, false);
  }
}

static void draw_settings_overlay(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);

  /* Header (Band 0) */
  if (a->settings_view == SET_VIEW_MAIN) {
    draw_text_centered_band(ui, ui->font_med, 0, "settings", main);
  } else {
    char hdr[256] = "settings";
    switch (a->settings_view) {
    case SET_VIEW_SCENE:
      safe_snprintf(hdr, sizeof(hdr), "scene");
      break;
    case SET_VIEW_APPEARANCE:
      safe_snprintf(hdr, sizeof(hdr), "interface");
      break;
    case SET_VIEW_FONTS:
      safe_snprintf(hdr, sizeof(hdr), "font");
      break;
    case SET_VIEW_FONT_SIZES:
      safe_snprintf(hdr, sizeof(hdr), "sizes");
      break;
    case SET_VIEW_COLORS:
      safe_snprintf(hdr, sizeof(hdr), "color");
      break;
    case SET_VIEW_SOUNDS:
      safe_snprintf(hdr, sizeof(hdr), "audio");
      break;
    case SET_VIEW_SOUND_VOLUME:
      safe_snprintf(hdr, sizeof(hdr), "volume");
      break;
    case SET_VIEW_SOUND_NOTIFICATIONS:
      safe_snprintf(hdr, sizeof(hdr), "notifications");
      break;
    case SET_VIEW_SOUND_MEDITATION:
      safe_snprintf(hdr, sizeof(hdr), "meditation");
      break;
    case SET_VIEW_MISC:
      safe_snprintf(hdr, sizeof(hdr), "general");
      break;
    case SET_VIEW_ABOUT:
      safe_snprintf(hdr, sizeof(hdr), ".stillroom");
      break;
    case SET_VIEW_CHANGELOG:
      safe_snprintf(hdr, sizeof(hdr), "changelog");
      break;
    case SET_VIEW_RELEASES:
      safe_snprintf(hdr, sizeof(hdr), "downloads");
      break;
    default:
      break;
    }
    draw_text_centered_band(ui, ui->font_med, 0, hdr, main);
  }

  /* Items (Bands 1-4) */
  typedef struct {
    char text[256];
    SDL_Color color;
    bool strikethrough;
  } MenuItem;

  MenuItem items[32];
  for (int i = 0; i < 32; i++) {
    items[i] = (MenuItem){0};
  }
  int count = 0;
  int sel_idx = 0;

  /* Hint Defaults */
  const char **labsL = NULL, **actsL = NULL, **labsR = NULL, **actsR = NULL;
  int nL = 0, nR = 0;
  const char *l_ud[] = {"up/down:", "a:"};
  const char *a_so[] = {"select", "open"};
  const char *l_back[] = {"b:"};
  const char *a_back[] = {"back"};

  if (a->settings_view == SET_VIEW_MAIN) {
    sel_idx = a->settings_sel;
    const char *labels[] = {"general",    "interface", "audio",
                            "statistics", "about",     "exit"};
    count = 6;
    for (int i = 0; i < count; i++) {
      safe_snprintf(items[i].text, sizeof(items[i].text), "%s", labels[i]);
      items[i].color = (i == sel_idx) ? highlight : accent;
      if (i == 4 && a->update_status == UPDATE_STATUS_AVAILABLE) {
        safe_snprintf(items[i].text, sizeof(items[i].text), "about !");
        if (i == sel_idx)
          items[i].color = (SDL_Color){255, 165, 0, 255};
      }
      items[i].strikethrough = false;
    }
    labsL = l_ud;
    actsL = a_so;
    nL = 2;
    labsR = (const char *[]){"b:"};
    actsR = (const char *[]){"close"};
    nR = 1;
  } else if (a->settings_view == SET_VIEW_SCENE) {
    sel_idx = a->scene_sel;
    count = 4;
    safe_snprintf(items[0].text, sizeof(items[0].text), "scene: %s",
                  a->scene_name);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    char mood_disp[256] = {0};
    if (a->cfg.ambience_name[0]) {
      safe_snprintf(mood_disp, sizeof(mood_disp), "%s", a->cfg.ambience_name);
      trim_ascii_inplace(mood_disp);
      strip_ext_inplace(mood_disp);
      char base[256] = {0}, tag[128] = {0};
      if (split_trailing_tag(mood_disp, base, sizeof(base), tag, sizeof(tag))) {
        safe_snprintf(mood_disp, sizeof(mood_disp), "%s", base);
      }
    }
    if (!mood_disp[0] || strcmp(mood_disp, "stillness") == 0)
      safe_snprintf(mood_disp, sizeof(mood_disp), "all is still");
    safe_snprintf(items[1].text, sizeof(items[1].text), "mood: %s", mood_disp);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    safe_snprintf(items[2].text, sizeof(items[2].text), "vibe: %s",
                  a->weather_name);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    safe_snprintf(items[3].text, sizeof(items[3].text), "animations");
    items[3].color = (sel_idx == 3) ? highlight : accent;
    items[3].strikethrough = !a->cfg.animations;

    labsL = (const char *[]){"arrows:", "a:"};
    actsL = (const char *[]){"change", "toggle"};
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_APPEARANCE) {
    sel_idx = a->appearance_sel;
    count = 2;
    safe_snprintf(items[0].text, sizeof(items[0].text), "fonts");
    items[0].color = (sel_idx == 0) ? highlight : accent;
    safe_snprintf(items[1].text, sizeof(items[1].text), "colors");
    items[1].color = (sel_idx == 1) ? highlight : accent;
    labsL = l_ud;
    actsL = a_so;
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_FONTS) {
    sel_idx = a->fonts_sel;
    count = 2;
    safe_snprintf(items[0].text, sizeof(items[0].text), "font: %s",
                  a->cfg.font_file);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    safe_snprintf(items[1].text, sizeof(items[1].text), "font sizes");
    items[1].color = (sel_idx == 1) ? highlight : accent;
    labsL = (const char *[]){"arrows:", "a:"};
    actsL = (const char *[]){"change", "open"};
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_FONT_SIZES) {
    sel_idx = a->sizes_sel;
    count = 3;
    safe_snprintf(items[0].text, sizeof(items[0].text), "small: %d",
                  a->cfg.font_small_pt);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    safe_snprintf(items[1].text, sizeof(items[1].text), "medium: %d",
                  a->cfg.font_med_pt);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    safe_snprintf(items[2].text, sizeof(items[2].text), "big: %d",
                  a->cfg.font_big_pt);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    labsL = (const char *[]){"left/right:"};
    actsL = (const char *[]){"change"};
    nL = 1;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_COLORS) {
    sel_idx = a->colors_sel;
    count = 3;
    safe_snprintf(items[0].text, sizeof(items[0].text), "main: %s",
                  PALETTE[a->cfg.main_color_idx].name);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    safe_snprintf(items[1].text, sizeof(items[1].text), "accent: %s",
                  PALETTE[a->cfg.accent_color_idx].name);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    safe_snprintf(items[2].text, sizeof(items[2].text), "highlight: %s",
                  PALETTE[a->cfg.highlight_color_idx].name);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    labsL = (const char *[]){"left/right:"};
    actsL = (const char *[]){"change"};
    nL = 1;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_SOUNDS) {
    sel_idx = a->sounds_sel;
    count = 2;
    safe_snprintf(items[0].text, sizeof(items[0].text), "volume");
    items[0].color = (sel_idx == 0) ? highlight : accent;
    safe_snprintf(items[1].text, sizeof(items[1].text), "notifications");
    items[1].color = (sel_idx == 1) ? highlight : accent;
    labsL = l_ud;
    actsL = a_so;
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_SOUND_VOLUME) {
    sel_idx = a->volume_sel;
    count = 3;
    int p_music = (a->cfg.vol_music * 100) / 128;
    int p_amb = (a->cfg.vol_ambience * 100) / 128;
    int p_notif = (a->cfg.vol_notifications * 100) / 128;
    safe_snprintf(items[0].text, sizeof(items[0].text), "music: %d%%", p_music);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    items[0].strikethrough = (a->vol_music_muted || a->cfg.vol_music == 0);
    safe_snprintf(items[1].text, sizeof(items[1].text), "ambience: %d%%",
                  p_amb);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    items[1].strikethrough =
        (a->vol_ambience_muted || a->cfg.vol_ambience == 0);
    safe_snprintf(items[2].text, sizeof(items[2].text), "notifications: %d%%",
                  p_notif);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    items[2].strikethrough =
        (a->vol_notif_muted || a->cfg.vol_notifications == 0);
    labsL = (const char *[]){"left/right:", "a:"};
    actsL = (const char *[]){"change", "mute"};
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_SOUND_NOTIFICATIONS) {
    sel_idx = a->notif_sel;
    count = 4;
    safe_snprintf(items[0].text, sizeof(items[0].text), "sound");
    items[0].color = (sel_idx == 0) ? highlight : accent;
    items[0].strikethrough = !a->cfg.notifications_enabled;
    const char *phase = (a->bell_sounds.count > 0 && a->bell_phase_idx >= 0)
                            ? a->bell_sounds.items[a->bell_phase_idx]
                            : "bell";
    char phase_buf[256];
    safe_snprintf(phase_buf, sizeof(phase_buf), "%s", phase);
    strip_ext_inplace(phase_buf);
    safe_snprintf(items[1].text, sizeof(items[1].text), "phase: %s", phase_buf);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    const char *done = (a->bell_sounds.count > 0 && a->bell_done_idx >= 0)
                           ? a->bell_sounds.items[a->bell_done_idx]
                           : "bell";
    char done_buf[256];
    safe_snprintf(done_buf, sizeof(done_buf), "%s", done);
    strip_ext_inplace(done_buf);
    safe_snprintf(items[2].text, sizeof(items[2].text), "session: %s",
                  done_buf);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    safe_snprintf(items[3].text, sizeof(items[3].text), "meditation bells");
    items[3].color = (sel_idx == 3) ? highlight : accent;
    labsL = (const char *[]){"left/right:", "a:", "x:"};
    actsL = (const char *[]){"change", "toggle", "preview"};
    nL = 3;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_SOUND_MEDITATION) {
    sel_idx = a->meditation_notif_sel;
    count = 3;
    const char *start =
        (a->bell_sounds.count > 0 && a->meditation_bell_start_idx >= 0)
            ? a->bell_sounds.items[a->meditation_bell_start_idx]
            : "bell";
    char start_buf[256];
    safe_snprintf(start_buf, sizeof(start_buf), "%s", start);
    strip_ext_inplace(start_buf);
    safe_snprintf(items[0].text, sizeof(items[0].text), "start: %s", start_buf);
    items[0].color = (sel_idx == 0) ? highlight : accent;
    const char *interval =
        (a->bell_sounds.count > 0 && a->meditation_bell_interval_idx >= 0)
            ? a->bell_sounds.items[a->meditation_bell_interval_idx]
            : "bell";
    char interval_buf[256];
    safe_snprintf(interval_buf, sizeof(interval_buf), "%s", interval);
    strip_ext_inplace(interval_buf);
    safe_snprintf(items[1].text, sizeof(items[1].text), "interval: %s",
                  interval_buf);
    items[1].color = (sel_idx == 1) ? highlight : accent;
    const char *end =
        (a->bell_sounds.count > 0 && a->meditation_bell_end_idx >= 0)
            ? a->bell_sounds.items[a->meditation_bell_end_idx]
            : "bell";
    char end_buf[256];
    safe_snprintf(end_buf, sizeof(end_buf), "%s", end);
    strip_ext_inplace(end_buf);
    safe_snprintf(items[2].text, sizeof(items[2].text), "end: %s", end_buf);
    items[2].color = (sel_idx == 2) ? highlight : accent;
    labsL = (const char *[]){"left/right:", "x:"};
    actsL = (const char *[]){"change", "preview"};
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_MISC) {
    sel_idx = a->misc_sel;
    count = 4;
    /* Name option at the top */
    char name_disp[128];
    safe_snprintf(name_disp, sizeof(name_disp), "name: %s",
                  a->cfg.user_name[0] ? a->cfg.user_name : "(none)");
    safe_snprintf(items[0].text, sizeof(items[0].text), "%s", name_disp);
    items[0].color = (sel_idx == 0) ? highlight : accent;

    safe_snprintf(items[1].text, sizeof(items[1].text), "synchronized day");
    items[1].color = (sel_idx == 1) ? highlight : accent;
    items[1].strikethrough = !a->cfg.detect_time;
    safe_snprintf(items[2].text, sizeof(items[2].text), "animations");
    items[2].color = (sel_idx == 2) ? highlight : accent;
    items[2].strikethrough = !a->cfg.animations;
    safe_snprintf(items[3].text, sizeof(items[3].text), "quest difficulty: %s",
                  quest_difficulty_label(a));
    items[3].color = (sel_idx == 3) ? highlight : accent;
    /* Determine hints based on selection */
    if (sel_idx == 0) {
      labsL = (const char *[]){"up/down:", "a:"};
      actsL = (const char *[]){"select", "edit"};
    } else {
      labsL = (const char *[]){"left/right:", "a:"};
      actsL = (const char *[]){"change", "toggle"};
    }
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  } else if (a->settings_view == SET_VIEW_ABOUT) {
    char status_line[256];
    if (a->update_status == UPDATE_STATUS_AVAILABLE)
      safe_snprintf(status_line, sizeof(status_line), "%s: update available",
                    STILLROOM_VERSION);
    else if (a->update_status == UPDATE_STATUS_UP_TO_DATE)
      safe_snprintf(status_line, sizeof(status_line), "%s: up to date",
                    STILLROOM_VERSION);
    else
      safe_snprintf(status_line, sizeof(status_line), "current version: %s",
                    STILLROOM_VERSION);

    draw_text_centered_band(ui, ui->font_small, 1, status_line,
                            (a->settings_about_sel == 0) ? highlight : accent);
    draw_text_centered_band(ui, ui->font_small, 2, "changelog",
                            (a->settings_about_sel == 1) ? highlight : accent);
    draw_text_centered_band(ui, ui->font_small, 3, "optional downloads",
                            (a->settings_about_sel == 2) ? highlight : accent);
    /* Manual composition for heart icon */
    const char *s1 = "made with ";
    const char *s2 = "by pseudoinsomniac";
    const char *s3 = "(on discord)";
    int w1 = text_width(ui->font_small, s1);
    int w2 = text_width(ui->font_small, s2);
    int w3 = text_width(ui->font_small, s3);

    int heart_scale = 3;
    int w_heart = (9 * heart_scale);
    int w_line1 = w1 + w_heart + 8; /* 8px padding */

    /* Center X calculations */
    int x_line1 = (ui->w - w_line1) / 2;
    int x_line2 = (ui->w - w2) / 2;
    int x_line3 = (ui->w - w3) / 2;

    CircleBand b4 = get_circle_band(4);
    int h_text = TTF_FontHeight(ui->font_small);
    /* Push down slightly to create gap from
     * 'optional downloads' */
    int y_start = b4.y_center - (h_text / 2) + 10;

    /* Line 1: "made with [HEART]" */
    draw_text(ui, ui->font_small, x_line1, y_start, s1, main, false);

    int h_heart = (8 * heart_scale);
    int y_heart = y_start + (h_text - h_heart) / 2;

    draw_icon_heart(ui, x_line1 + w1 + 4, y_heart, heart_scale,
                    (SDL_Color){255, 60, 60, 255});

    /* Line 2: "by pseudoinsomniac" */
    draw_text(ui, ui->font_small, x_line2, y_start + h_text + 4, s2, main,
              false);

    /* Line 3: "(on discord)" */
    draw_text(ui, ui->font_small, x_line3, y_start + (h_text * 2) + 8, s3, main,
              false);
    labsL = l_ud;
    actsL = a_so;
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, nL, labsR, actsR, nR);
    return;
  } else if (a->settings_view == SET_VIEW_CHANGELOG) {
    if (!a->update_latest_notes[0]) {
      draw_text_centered_band(ui, ui->font_small, 2, "check online for details",
                              accent);
    } else {
      /* Render wrapped text */
      const char *notes = a->update_latest_notes;
      int max_w = ui->w - (UI_MARGIN_X * 2);
      int start_y = BIG_TIMER_Y + 74; /* Increased gap */
      int y = start_y;
      int row_h = UI_ROW_GAP + 6;
      int y_bottom = overlay_bottom_text_limit_y(ui);
      int skip = a->settings_changelog_scroll;

      about_draw_wrapped(ui, ui->font_small, UI_MARGIN_X, &y, row_h, y_bottom,
                         &skip, main, TTF_STYLE_NORMAL, true, notes, max_w);

      /* Hints */
      const char *labsL[] = {"d-pad:", "scroll"};
      /* Only show arrows if scroll is possible */
      if (a->settings_changelog_scroll > 0 ||
          settings_changelog_max_scroll(ui, (App *)a, max_w) > 0) {
        draw_hint_pairs_lr(ui, main, accent, (const char *[]){"arrows:"},
                           (const char *[]){"scroll"}, 1,
                           (const char *[]){"b:"}, (const char *[]){"back"}, 1);
      } else {
        draw_hint_pairs_lr(ui, main, accent, NULL, NULL, 0,
                           (const char *[]){"b:"}, (const char *[]){"back"}, 1);
      }
      return;
    }
    labsR = l_back;
    actsR = a_back;
    nR = 1;
    draw_hint_pairs_lr(ui, main, accent, NULL, NULL, 0, labsR, actsR, nR);
    return;
  } else if (a->settings_view == SET_VIEW_RELEASES) {
    if (a->release_popup_open) {
      /* Layout:
         Header (implied "downloads") at top.
         [Gap]
         Release Tag (Title)
         [Gap]
         Release Notes (Centered, wrapped)
         [Buttons at bottom]
      */

      const char *tag =
          (a->release_sel >= 0 && a->release_sel < a->release_tags.count)
              ? a->release_tags.items[a->release_sel]
              : "";

      /* 1. Draw Title (Tag) below header gap */
      /* Use slightly more than one row gap to
       * clear the header comfortably */
      int y_tag = BIG_TIMER_Y + UI_ROW_GAP + 16;
      int w_tag = text_width(ui->font_med, tag);
      int x_tag = (ui->w - w_tag) / 2;
      draw_text(ui, ui->font_med, x_tag, y_tag, tag, accent, false);

      /* 2. Release Notes */
      const char *notes = "";
      if (a->release_sel >= 0 && a->release_sel < a->release_notes.count) {
        notes = a->release_notes.items[a->release_sel];
      }

      /* Move buttons down to Band 4 and 5 to give
       * more room for notes */
      int y_buttons_start = get_circle_band(4).y_center - 10;

      if (notes && notes[0]) {
        /* Reduce gap between Title and Notes */
        int y_notes = y_tag + TTF_FontHeight(ui->font_med) + 12;
        int row_h = UI_ROW_GAP + 6;
        /* Increase gap between Notes and Buttons
         */
        int y_bottom = y_buttons_start - 25;
        int skip = 0;
        int max_w = ui->w - (UI_MARGIN_X * 2);

        about_draw_wrapped(ui, ui->font_small, UI_MARGIN_X, &y_notes, row_h,
                           y_bottom, &skip, accent, TTF_STYLE_NORMAL, true,
                           notes, max_w);
      } else {
        /* Fallback for empty notes */
        int y_notes = y_tag + TTF_FontHeight(ui->font_med) + 12;
        const char *fb = "(no notes available)";
        int w_fb = text_width(ui->font_small, fb);
        draw_text(ui, ui->font_small, (ui->w - w_fb) / 2, y_notes, fb, accent,
                  false);
      }

      SDL_Color c0 = (a->release_popup_sel == 0) ? highlight : accent;
      SDL_Color c1 = (a->release_popup_sel == 1) ? highlight : accent;
      /* Place buttons at Band 4 and 5 positions */
      draw_text_centered_band(ui, ui->font_small, 4, "download & unzip", c0);
      draw_text_centered_band(ui, ui->font_small, 5, "cancel", c1);

      labsL = (const char *[]){"up/down:", "a:"};
      actsL = (const char *[]){"select", "confirm"};
      nL = 2;
      labsR = l_back;
      actsR = a_back;
      nR = 1;
      draw_hint_pairs_lr(ui, main, accent, labsL, actsL, nL, labsR, actsR, nR);
      return;
    }

    if (a->release_status == RELEASE_STATUS_LISTING) {
      draw_text_centered_band(ui, ui->font_small, 2, "loading releases...",
                              accent);
      return;
    }
    count = a->release_tags.count;
    if (count == 0) {
      draw_text_centered_band(ui, ui->font_small, 2, "no releases found",
                              accent);
      return;
    }
    sel_idx = a->release_sel;

    for (int i = 0; i < count && i < 32; i++) {
      safe_snprintf(items[i].text, sizeof(items[i].text), "%s",
                    a->release_tags.items[i]);
      items[i].color = (i == sel_idx) ? highlight : accent;
      items[i].strikethrough = false;
    }

    labsL = l_ud;
    actsL = (const char *[]){"select", "options"};
    nL = 2;
    labsR = l_back;
    actsR = a_back;
    nR = 1;
  }

  /* Render generic items */
  int visible_rows =
      CIRCLE_LAYOUT_BANDS - 1; /* Band 0 is header, rest are items */
  int start_row = 0;

  if (count > visible_rows) {
    start_row = sel_idx - (visible_rows / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible_rows > count)
      start_row = count - visible_rows;
  }

  for (int i = 0; i < visible_rows; i++) {
    int idx = start_row + i;
    if (idx >= count)
      break;
    int band = i + 1; /* Bands 1, 2, 3, 4 */

    /* Draw item */
    if (items[idx].strikethrough) {
      int w = text_width(ui->font_small, items[idx].text);
      int x = CIRCLE_LAYOUT_CX - w / 2;
      CircleBand b = get_circle_band(band);
      int y = b.y_center - TTF_FontHeight(ui->font_small) / 2;
      draw_text_centered_band(ui, ui->font_small, band, items[idx].text,
                              items[idx].color);
      draw_strikethrough(ui, x, y, w, TTF_FontHeight(ui->font_small),
                         (SDL_Color){255, 255, 255, 255});
    } else {
      draw_text_centered_band(ui, ui->font_small, band, items[idx].text,
                              items[idx].color);
    }

    /* Arrow indicators for L/R navigable views
     * (selected item only) */
    /* Only show for views where L/R actually
     * changes a value, not navigation menus */
    bool uses_lr = (a->settings_view == SET_VIEW_SOUND_VOLUME ||
                    a->settings_view == SET_VIEW_SOUND_NOTIFICATIONS ||
                    a->settings_view == SET_VIEW_SOUND_MEDITATION ||
                    a->settings_view == SET_VIEW_COLORS ||
                    a->settings_view == SET_VIEW_COLORS ||
                    a->settings_view == SET_VIEW_FONT_SIZES ||
                    (a->settings_view == SET_VIEW_FONTS && sel_idx == 0) ||
                    (a->settings_view == SET_VIEW_MISC && sel_idx == 3));
    if (uses_lr && idx == sel_idx) {
      CircleBand b = get_circle_band(band);
      int w = text_width(ui->font_small, items[idx].text);
      int arrow_gap = 12;
      int x_left = CIRCLE_LAYOUT_CX - w / 2 - arrow_gap -
                   text_width(ui->font_small, "<");
      int x_right = CIRCLE_LAYOUT_CX + w / 2 + arrow_gap;
      int y = b.y_center - TTF_FontHeight(ui->font_small) / 2;
      draw_text(ui, ui->font_small, x_left, y, "<", main, false);
      draw_text(ui, ui->font_small, x_right, y, ">", main, false);
    }
  }

  if (nL > 0 || nR > 0) {
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, nL, labsR, actsR, nR);
  }
}

/* Booklets functions moved to
 * features/booklets/booklets.c */
static void draw_menu(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  draw_top_hud(ui, a);
  const char *items[] = {"timer", "pomodoro", "meditation", "to-do list",
                         "booklets"};
  int n = 5;
  /* Use consistent header Y for all screens */
  int x = UI_MARGIN_X;
  int header_y = PICKER_HEADER_Y;
  int item_h = TTF_FontHeight(ui->font_small);
  /* Show all items (n=6). */
  int visible_rows = 6;
  int start_row = 0;
  /* Scrolling logic removed as 6 items fit with
   * current spacing */
  if (n > visible_rows) {
    /* Safety fallback if n increases later */
    start_row = a->menu_sel - (visible_rows / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible_rows > n)
      start_row = n - visible_rows;
  }

  int y = header_y + TTF_FontHeight(ui->font_med) + PICKER_HEADER_GAP;
  /* DEBUG: Changed header to verify update -
   * Reverting */
  draw_text(ui, ui->font_med, x, header_y, "menu", main, false);
  for (int i = 0; i < 5; i++) {
    int idx = start_row + i;
    if (idx >= n)
      break;
    SDL_Color col = (idx == a->menu_sel) ? highlight : accent;
    /* Determine display index for Y position
     * (0..visible_rows-1) */
    draw_text(ui, ui->font_small, x, y + i * (item_h + MENU_ITEM_GAP),
              items[idx], col, false);
  }
  const char *labsL[] = {"d-pad:", "a:"};
  const char *actsL[] = {"navigate", "select"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, NULL, NULL, 0);
}
/* draw_quest removed (logic now in
 * features/quest/quest.c) */
/* Redundant Tasks draw logic removed. Logic now in
 * features/tasks/tasks.c */
/* Forward declarations */
static int date_int_days_ago(int days);

/* ----------------------------- Routine
 * ----------------------------- */
/* pomo_build_order_string moved to
 * features/timer/timer.c */
/* meditation_total_minutes,
   meditation_bell_max_minutes,
   meditation_minutes_from_label,
   meditation_clamp_bell moved to
   features/meditation/meditation.c */
/* draw_pomo_picker moved to features/timer/timer.c
 */
/* draw_meditation_picker moved to
 * features/meditation/meditation.c */

/* draw_custom_picker moved to
 * features/timer/timer.c */

int kb_row_len(int r) {
  if (r < 0 || r >= KEYBOARD_ROW_COUNT)
    return 0;
  return (int)strlen(kb_rows[r]);
}
static const char *stanza_kb_rows[] = {"abcdefghi", "jklmnopqr", "stuvwxyz",
                                       ".,?!:;\'"};
static const int stanza_kb_row_count = 4;
static int stanza_kb_row_len(int r) {
  if (r < 0 || r >= stanza_kb_row_count)
    return 0;
  return (int)strlen(stanza_kb_rows[r]);
}
static void focus_kb_clamp(App *a) {
  if (!a)
    return;
  if (a->focus_kb_row < 0)
    a->focus_kb_row = 0;
  if (a->focus_kb_row >= KEYBOARD_ROW_COUNT)
    a->focus_kb_row = KEYBOARD_ROW_COUNT - 1;
  int len = kb_row_len(a->focus_kb_row);
  if (len <= 0) {
    a->focus_kb_col = 0;
    return;
  }
  if (a->focus_kb_col < 0)
    a->focus_kb_col = 0;
  if (a->focus_kb_col >= len)
    a->focus_kb_col = len - 1;
}
/* Redundant Tasks kb logic removed. Logic now in
 * features/tasks/tasks.c */
static void stanza_custom_kb_clamp(App *a) {
  if (!a)
    return;
  if (a->stanza_custom_kb_row < 0)
    a->stanza_custom_kb_row = 0;
  if (a->stanza_custom_kb_row >= stanza_kb_row_count)
    a->stanza_custom_kb_row = stanza_kb_row_count - 1;
  int len = stanza_kb_row_len(a->stanza_custom_kb_row);
  if (len <= 0) {
    a->stanza_custom_kb_col = 0;
    return;
  }
  if (a->stanza_custom_kb_col < 0)
    a->stanza_custom_kb_col = 0;
  if (a->stanza_custom_kb_col >= len)
    a->stanza_custom_kb_col = len - 1;
}
/* ----------------------------- Focus entry menu
 * -----------------------------
 */
/* focus_menu_entries_build,
   focus_menu_sync_sel_to_current, draw_focus_menu,
   draw_focus_text moved to
   features/focus_menu/focus_menu.c */
static void handle_focus_menu(App *a, Buttons *b) {
  if (!a)
    return;
  if (a->focus_delete_confirm_open) {
    if (b->left)
      a->focus_delete_confirm_sel = 0;
    if (b->right)
      a->focus_delete_confirm_sel = 1;
    if (b->a) {
      if (a->focus_delete_confirm_sel == 1)
        focus_stats_remove_name(a, a->focus_delete_name);
      a->focus_delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b) {
      a->focus_delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    a->ui_needs_redraw = true;
    return;
  }
  const char *items[1 + MAX_FOCUS_STATS];
  int n_items = focus_menu_entries_build(
      a, items, (int)(sizeof(items) / sizeof(items[0])));
  int total_rows = n_items + 1;
  if (b->b) {
    a->screen = a->focus_menu_return_screen;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->up) {
    a->focus_menu_sel--;
    if (a->focus_menu_sel < 0)
      a->focus_menu_sel = total_rows - 1;
  }
  if (b->down) {
    a->focus_menu_sel++;
    if (a->focus_menu_sel >= total_rows)
      a->focus_menu_sel = 0;
  }
  if (b->l3 && a->focus_menu_sel < n_items) {
    safe_snprintf(a->focus_delete_name, sizeof(a->focus_delete_name), "%s",
                  items[a->focus_menu_sel]);
    a->focus_delete_confirm_open = true;
    a->focus_delete_confirm_sel = 0;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->a) {
    if (a->focus_menu_sel < n_items) {
      safe_snprintf(a->cfg.focus_activity, sizeof(a->cfg.focus_activity), "%s",
                    items[a->focus_menu_sel]);
      focus_stats_ensure_name(a, a->cfg.focus_activity);
      config_save(&a->cfg, CONFIG_PATH);
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
      a->screen = a->focus_menu_return_screen;
      a->ui_needs_redraw = true;
      return;
    }
    a->focus_text_return_screen = SCREEN_FOCUS_MENU;
    a->focus_edit_buf[0] = 0;
    a->focus_kb_row = 0;
    a->focus_kb_col = 0;
    focus_kb_clamp(a);
    a->screen = SCREEN_FOCUS_TEXT;
    a->ui_needs_redraw = true;
    return;
  }
  a->ui_needs_redraw = true;
}
static void draw_stanza_custom_keyboard(UI *ui, App *a, SDL_Color main,
                                        SDL_Color accent, SDL_Color highlight) {
  if (!ui || !a)
    return;
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
  SDL_Rect rr = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &rr);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Centered header */
  const char *hdr = "custom piece";
  int w_hdr = text_width(ui->font_med, hdr);
  draw_text(ui, ui->font_med, cx - w_hdr / 2, header_y, hdr, main, false);

  /* Centered input field */
  int y = header_y + UI_ROW_GAP + 10;
  const char *disp = a->stanza_custom_buf[0] ? a->stanza_custom_buf : "(empty)";
  int w_input = text_width(ui->font_med, disp);
  draw_text_input_with_cursor(ui, ui->font_med, cx - w_input / 2, y,
                              a->stanza_custom_buf, "(empty)", highlight,
                              accent, highlight, 0);

  /* Centered keyboard rows - Shared Stanza Style */
  int yKb = y + TTF_FontHeight(ui->font_med) + 26;
  keyboard_draw(ui, cx, yKb, a->stanza_custom_kb_row, a->stanza_custom_kb_col,
                accent, highlight);
  const char *labsL[] = {"b:", "x:", "y:", "a:"};
  const char *actsL[] = {"cancel", "backspace", "space", "add"};
  const char *labsR[] = {"r3:"};
  const char *actsR[] = {"save"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 4, labsR, actsR, 1);
}
static void handle_focus_text(App *a, Buttons *b) {
  if (!a)
    return;
  /* B backs out without saving. */
  if (b->b) {
    a->screen = a->focus_text_return_screen;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->up)
    a->focus_kb_row--;
  if (b->down)
    a->focus_kb_row++;
  if (b->left)
    a->focus_kb_col--;
  if (b->right)
    a->focus_kb_col++;
  focus_kb_clamp(a);
  /* X backspaces (delete one character). */
  if (b->x) {
    utf8_pop_back_inplace(a->focus_edit_buf);
    a->ui_needs_redraw = true;
    return;
  }
  if (b->y) {
    append_char(a->focus_edit_buf, sizeof(a->focus_edit_buf), ' ');
  }
  if (b->a) {
    int len = kb_row_len(a->focus_kb_row);
    if (len > 0 && a->focus_kb_col >= 0 && a->focus_kb_col < len) {
      char ch = kb_rows[a->focus_kb_row][a->focus_kb_col];
      append_char(a->focus_edit_buf, sizeof(a->focus_edit_buf), ch);
    }
  }
  if (b->r3) {
    safe_snprintf(a->cfg.focus_activity, sizeof(a->cfg.focus_activity), "%s",
                  a->focus_edit_buf);
    /* Persist the label so it survives screen
     * changes and shows up in the pick list. */
    focus_stats_ensure_name(a, a->cfg.focus_activity);
    config_save(&a->cfg, CONFIG_PATH);
    a->focus_activity_dirty = false;
    focus_pick_sync_idx(a);
    a->screen = a->focus_text_return_screen;
    a->ui_needs_redraw = true;
    return;
  }
  a->ui_needs_redraw = true;
}

/* Redundant Tasks logic removed. Use
 * features/tasks/tasks.c */

static void handle_stanza_custom_keyboard(App *a, Buttons *b) {
  if (!a || !b)
    return;
  if (b->b) {
    a->stanza_custom_open = false;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->up)
    a->stanza_custom_kb_row--;
  if (b->down)
    a->stanza_custom_kb_row++;
  if (b->left)
    a->stanza_custom_kb_col--;
  if (b->right)
    a->stanza_custom_kb_col++;
  stanza_custom_kb_clamp(a);
  if (b->x) {
    utf8_pop_back_inplace(a->stanza_custom_buf);
    a->ui_needs_redraw = true;
    return;
  }
  if (b->y) {
    append_char(a->stanza_custom_buf, sizeof(a->stanza_custom_buf), ' ');
  }
  if (b->a) {
    int len = stanza_kb_row_len(a->stanza_custom_kb_row);
    if (len > 0 && a->stanza_custom_kb_col >= 0 &&
        a->stanza_custom_kb_col < len) {
      char ch =
          stanza_kb_rows[a->stanza_custom_kb_row][a->stanza_custom_kb_col];
      append_char(a->stanza_custom_buf, sizeof(a->stanza_custom_buf), ch);
    }
  }
  if (b->r3) {
    safe_snprintf(a->stanza_custom_tag, sizeof(a->stanza_custom_tag), "%s",
                  a->stanza_custom_buf);
    if (a->stanza_custom_tag[0]) {
      a->stanza_holding = true;
      a->stanza_hold_piece = (int)STZ_P_CUSTOM;
      safe_snprintf(a->stanza_hold_custom, sizeof(a->stanza_hold_custom), "%s",
                    a->stanza_custom_tag);
    }
    a->stanza_custom_open = false;
    a->ui_needs_redraw = true;
    return;
  }
  a->ui_needs_redraw = true;
}
/* ----------------------------- Statistics screen
 * -----------------------------
 */
static int focus_stat_cmp_desc(const void *A, const void *B) {
  const FocusStat *a = (const FocusStat *)A;
  const FocusStat *b = (const FocusStat *)B;
  if (a->seconds < b->seconds)
    return 1;
  if (a->seconds > b->seconds)
    return -1;
  return strcmp(a->name, b->name);
}

static const char *stats_section_label(int section) { return "focus"; }

static int stats_pages_for_section(int section) { return 4; }

static int date_int_days_ago(int days) {
  time_t now = time(NULL);
  struct tm base_tm;
  localtime_r(&now, &base_tm);
  base_tm.tm_hour = 12;
  base_tm.tm_min = 0;
  base_tm.tm_sec = 0;
  time_t base = mktime(&base_tm);
  time_t t = base - (time_t)days * 86400;
  struct tm tmv;
  localtime_r(&t, &tmv);
  return date_int_from_ymd(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

static SDL_Color blend_color(SDL_Color a, SDL_Color b, double t) {
  if (t < 0.0)
    t = 0.0;
  if (t > 1.0)
    t = 1.0;
  SDL_Color out = {(uint8_t)(a.r + (b.r - a.r) * t),
                   (uint8_t)(a.g + (b.g - a.g) * t),
                   (uint8_t)(a.b + (b.b - a.b) * t), 255};
  return out;
}
/* Orphaned code fragments removed. */

/* habits_best_current_streak removed */
/* habits_color_cycle removed */

static void draw_stats(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  (void)highlight;

  draw_top_hud(ui, a);

  const int x = UI_MARGIN_X;
  const int header_y = BIG_TIMER_Y;
  const int line_h = TTF_FontHeight(ui->font_small) + 14;
  const int pages = stats_pages_for_section(a->stats_section);
  if (a->stats_page < 0)
    a->stats_page = 0;
  if (a->stats_page >= pages)
    a->stats_page = 0;

  /* Header: "statistics, as of the tenth of
   * january" */
  char as_of_phrase[96];
  format_as_of_today_phrase(as_of_phrase, sizeof(as_of_phrase));

  draw_text(ui, ui->font_med, x, header_y, "statistics,", main, false);

  const int asc_med = TTF_FontAscent(ui->font_med);
  const int asc_small = TTF_FontAscent(ui->font_small);
  const int baseline_y = header_y + (asc_med > asc_small ? asc_med : asc_small);
  const int small_y = baseline_y - asc_small;

  int x2 = x + text_width(ui->font_med, "statistics,") + UI_INLINE_GAP_PX;
  draw_text(ui, ui->font_small, x2, small_y, "as of", main, false);

  TTF_Font *font_small_ital =
      ui->font_small_i ? ui->font_small_i : ui->font_small;
  int x3 = x2 + text_width(ui->font_small, "as of") + UI_INLINE_GAP_PX;
  draw_text(ui, font_small_ital, x3, small_y, as_of_phrase, accent, false);

  const char *section_label = stats_section_label(a->stats_section);
  int x4 = x3 + text_width(ui->font_small, as_of_phrase) + UI_INLINE_GAP_PX;
  draw_text(ui, font_small_ital, x4, small_y, section_label, accent, false);

  int y = header_y + UI_ROW_GAP + 10;
  int y_end = y;

  if (a->stats_section == 0 && a->stats_page == 0) {
    char bufA[64], bufB[64], bufC[64];
    char tmp[64];

    format_duration_hm(a->cfg.focus_total_seconds, bufA, sizeof(bufA));
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y, "total:", bufA,
                     main, accent);
    y += line_h;

    safe_snprintf(tmp, sizeof(tmp), "%llu",
                  (unsigned long long)a->cfg.focus_total_sessions);
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y, "sessions:", tmp,
                     main, accent);
    y += line_h;

    format_duration_hm(a->cfg.focus_longest_span_seconds, bufB, sizeof(bufB));
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y,
                     "longest span:", bufB, main, accent);
    y += line_h;

    /* average duration */
    uint64_t avg_secs = 0;
    if (a->cfg.focus_total_sessions > 0) {
      avg_secs =
          (uint64_t)(a->cfg.focus_total_seconds / a->cfg.focus_total_sessions);
    }
    char bufAvg[64];
    format_duration_hm((uint32_t)avg_secs, bufAvg, sizeof(bufAvg));
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y,
                     "average duration:", bufAvg, main, accent);
    y += line_h;

    safe_snprintf(tmp, sizeof(tmp), "%llu",
                  (unsigned long long)a->cfg.pomo_total_blocks);
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y,
                     "pomodoro blocks:", tmp, main, accent);
    y += line_h;

    format_duration_hm(a->cfg.pomo_focus_seconds, bufC, sizeof(bufC));
    draw_inline_left(ui, ui->font_small, ui->font_small, x, y,
                     "pomodoro focus:", bufC, main, accent);
    y += line_h;

    y_end = y;
  } else if (a->stats_section == 0 && a->stats_page == 1) {
    /* Activity heatmap: 7 columns x 5 rows = last
     * 35 days (oldest -> newest).
     */
    SDL_Color c0 = (SDL_Color){90, 90, 90, 255};    /* 0 */
    SDL_Color c15 = (SDL_Color){220, 190, 70, 255}; /* >= 15m (yellow) */
    SDL_Color c60 = (SDL_Color){95, 210, 125, 255}; /* >= 1h (bright green) */
    SDL_Color c120 = (SDL_Color){25, 140, 70, 255}; /* >= 2h (dark green) */

    /* Optional label */
    draw_text(ui, ui->font_small, x, y, "activity (last 35 days)", main, false);
    y += line_h;

    /* Legend */
    {
      const int s = 18;
      const int g = 10;
      int lx = x;
      int ly = y + (TTF_FontHeight(ui->font_small) - s) / 2;

      SDL_Rect r0 = {lx, ly, s, s};
      SDL_SetRenderDrawColor(ui->ren, c0.r, c0.g, c0.b, c0.a);
      SDL_RenderFillRect(ui->ren, &r0);
      SDL_SetRenderDrawColor(ui->ren, main.r, main.g, main.b, 255);
      SDL_RenderDrawRect(ui->ren, &r0);
      lx += s + 8;
      draw_text(ui, ui->font_small, lx, y, "0", main, false);
      lx += text_width(ui->font_small, "0") + g;

      SDL_Rect r1 = {lx, ly, s, s};
      SDL_SetRenderDrawColor(ui->ren, c15.r, c15.g, c15.b, c15.a);
      SDL_RenderFillRect(ui->ren, &r1);
      SDL_SetRenderDrawColor(ui->ren, main.r, main.g, main.b, 255);
      SDL_RenderDrawRect(ui->ren, &r1);
      lx += s + 8;
      draw_text(ui, ui->font_small, lx, y, "15m", main, false);
      lx += text_width(ui->font_small, "15m") + g;

      SDL_Rect r2 = {lx, ly, s, s};
      SDL_SetRenderDrawColor(ui->ren, c60.r, c60.g, c60.b, c60.a);
      SDL_RenderFillRect(ui->ren, &r2);
      SDL_SetRenderDrawColor(ui->ren, main.r, main.g, main.b, 255);
      SDL_RenderDrawRect(ui->ren, &r2);
      lx += s + 8;
      draw_text(ui, ui->font_small, lx, y, "1h", main, false);
      lx += text_width(ui->font_small, "1h") + g;

      SDL_Rect r3 = {lx, ly, s, s};
      SDL_SetRenderDrawColor(ui->ren, c120.r, c120.g, c120.b, c120.a);
      SDL_RenderFillRect(ui->ren, &r3);
      SDL_SetRenderDrawColor(ui->ren, main.r, main.g, main.b, 255);
      SDL_RenderDrawRect(ui->ren, &r3);
      lx += s + 8;
      draw_text(ui, ui->font_small, lx, y, "2h", main, false);
    }
    y += line_h;

    /* Build the last 35 dates (local time). */
    char dates[35][11];
    uint32_t day_secs[35];
    for (int i = 0; i < 35; i++)
      day_secs[i] = 0;

    time_t now = time(NULL);
    struct tm base_tm;
    localtime_r(&now, &base_tm);
    /* Use midday to avoid DST edge weirdness. */
    base_tm.tm_hour = 12;
    base_tm.tm_min = 0;
    base_tm.tm_sec = 0;
    time_t base = mktime(&base_tm);

    for (int i = 0; i < 35; i++) {
      time_t t = base - (time_t)(34 - i) * 86400;
      struct tm tmv;
      localtime_r(&t, &tmv);
      strftime(dates[i], sizeof(dates[i]), "%Y-%m-%d", &tmv);
    }

    /* Sum activity log state into those days. */
    FILE *f = fopen(ACTIVITY_LOG_PATH, "r");
    if (f) {
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        trim_ascii_inplace(line);
        if (!line[0])
          continue;
        char *tab = strchr(line, '	');
        if (!tab)
          tab = strchr(line, ' ');
        if (!tab)
          continue;
        *tab = 0;
        const char *date = line;
        const char *v = tab + 1;
        while (*v == '	' || *v == ' ')
          v++;
        unsigned long secs = strtoul(v, NULL, 10);
        if (!date[0] || secs == 0)
          continue;
        for (int i = 0; i < 35; i++) {
          if (strcmp(date, dates[i]) == 0) {
            day_secs[i] += (uint32_t)secs;
            break;
          }
        }
      }
      fclose(f);
    }

    /* Draw grid */
    const int cols = 7, rows = 5;
    const int cell = 35 /* +10% */;
    const int gap = 10;

    int grid_x = x;
    int grid_y = y;

    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        int idx = r * cols + c; /* oldest -> newest */
        if (idx < 0 || idx >= 35)
          continue;
        uint32_t secs = day_secs[idx];

        SDL_Color col = c0;
        if (secs >= 7200)
          col = c120;
        else if (secs >= 3600)
          col = c60;
        else if (secs >= 900)
          col = c15;

        SDL_Rect rr = {grid_x + c * (cell + gap), grid_y + r * (cell + gap),
                       cell, cell};
        SDL_SetRenderDrawColor(ui->ren, col.r, col.g, col.b, col.a);
        SDL_RenderFillRect(ui->ren, &rr);
        SDL_SetRenderDrawColor(ui->ren, main.r, main.g, main.b, 255);
        SDL_RenderDrawRect(ui->ren, &rr);
      }
    }

    y_end = grid_y + rows * cell + (rows - 1) * gap;
    y = y_end;
  } else if (a->stats_section == 0 && a->stats_page == 2) {
    /* Page 3: activity history (last 30 sessions).
     */
    if (a->history_dirty) {
      focus_history_load_to_cache(a);
    }
    int n = a->cached_history_count;
    FocusHistoryRow *rows = a->cached_history_rows;

    int y0 = y;
    int y_limit = overlay_bottom_text_limit_y(ui);
    int row_h = TTF_FontHeight(ui->font_small) + 10;

    draw_text_style(ui, ui->font_small, x, y, "history (last 30 sessions)",
                    main, false, TTF_STYLE_ITALIC);
    y += row_h;

    if (n <= 0) {
      draw_text(ui, ui->font_small, x, y, "no history yet.", main, false);
      y += row_h;
      y_end = y;
    } else {
      int visible = (y_limit - y) / row_h;
      if (visible < 1)
        visible = 1;

      int max_scroll = n - visible;
      if (max_scroll < 0)
        max_scroll = 0;
      if (a->stats_history_scroll < 0)
        a->stats_history_scroll = 0;
      if (a->stats_history_scroll > max_scroll)
        a->stats_history_scroll = max_scroll;

      int w_dt = text_width(ui->font_small, "YYYY-MM-DD HH:MM");
      int w_dur = text_width(ui->font_small, "99h 59m");
      int w_st = text_width(ui->font_small, "completed");
      int g = 18;

      int x_dt = x;
      int x_dur = x_dt + w_dt + g;
      int x_st = x_dur + w_dur + g;
      int x_act = x_st + w_st + g;

      /* Column header */
      draw_text_style(ui, ui->font_small, x_dt, y, "date+time", accent, false,
                      TTF_STYLE_ITALIC);
      draw_text_style(ui, ui->font_small, x_dur, y, "duration", accent, false,
                      TTF_STYLE_ITALIC);
      draw_text_style(ui, ui->font_small, x_st, y, "status", accent, false,
                      TTF_STYLE_ITALIC);
      draw_text_style(ui, ui->font_small, x_act, y, "activity", accent, false,
                      TTF_STYLE_ITALIC);
      y += row_h;

      int end_i = a->stats_history_scroll + visible;
      if (end_i > n)
        end_i = n;

      for (int i = a->stats_history_scroll; i < end_i; i++) {
        FocusHistoryRow *r = &rows[i];
        char dur[64];
        format_duration_hm(r->seconds, dur, sizeof(dur));

        draw_text(ui, ui->font_small, x_dt, y, r->dt, main, false);
        draw_text(ui, ui->font_small, x_dur, y, dur, main, false);
        draw_text(ui, ui->font_small, x_st, y, r->status, main, false);

        int max_act_w = (ui->w - UI_MARGIN_X) - x_act;
        if (max_act_w < 0)
          max_act_w = 0;
        char act_fit[96];
        truncate_to_width(ui, ui->font_small, r->activity, max_act_w, act_fit,
                          sizeof(act_fit));
        draw_text(ui, ui->font_small, x_act, y, act_fit, main, false);

        y += row_h;
        if (y + row_h > y_limit)
          break;
      }

      y_end = y;
    }
  }

  else if (a->stats_section == 0) {
    /* Page 4: per-entry breakdown (top buckets).
     */
    int y0 = y;
    if (a->focus_stats_count == 0) {
      draw_text(ui, ui->font_small, x, y, "no per-entry stats yet.", main,
                false);
      y += line_h;
      y_end = y;
    } else {
      FocusStat tmp[MAX_FOCUS_STATS];
      int n = a->focus_stats_count;
      if (n > MAX_FOCUS_STATS)
        n = MAX_FOCUS_STATS;
      for (int i = 0; i < n; i++)
        tmp[i] = a->focus_stats[i];
      for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
          if (tmp[j].seconds > tmp[i].seconds) {
            FocusStat t = tmp[i];
            tmp[i] = tmp[j];
            tmp[j] = t;
          }
        }
      }
      int row_h = UI_ROW_GAP + 8;
      int max_rows = (ui->h - UI_MARGIN_BOTTOM - y0) / row_h;
      if (max_rows < 1)
        max_rows = 1;
      if (max_rows > n)
        max_rows = n;
      for (int i = 0; i < max_rows; i++) {
        char dur[64];
        format_duration_hm(tmp[i].seconds, dur, sizeof(dur));
        char label[160];
        safe_snprintf(label, sizeof(label), "%s:", tmp[i].name);
        draw_inline_left(ui, ui->font_small, ui->font_small, x, y, label, dur,
                         main, accent);
        y += row_h;
      }
      y_end = y;
    }
    /* Habit stats rendering removed */

    /* Page indicator: fixed to the standard
     * bottom-row baseline (bottom-left).
     */
    char page_buf[48];
    safe_snprintf(page_buf, sizeof(page_buf), "%s Â· page %d of %d",
                  section_label, a->stats_page + 1, pages);

    const int baseline_y_bot = ui_bottom_baseline_y(ui);
    const int y_page = baseline_y_bot - TTF_FontAscent(font_small_ital);
    draw_text(ui, font_small_ital, x, y_page, page_buf, accent, false);

    const char *labsL[] = {"L/R", "B"};
    const char *actsL[] = {"page", "back"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, NULL, NULL, 0);
  }
}

static void handle_stats(App *a, Buttons *b) {
  if (!a)
    return;

  if (b->b) {
    if (a->stats_return_to_settings) {
      a->stats_return_to_settings = false;
      a->screen = SCREEN_TIMER;
      a->settings_open = true;
      a->settings_view = SET_VIEW_MAIN;
      a->ui_needs_redraw = true;
      app_reveal_hud(a);
      return;
    }
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = true;
    app_reveal_hud(a);
    a->ui_needs_redraw = true;
    return;
  }

  /* habit section navigation removed */

  const int pages = stats_pages_for_section(a->stats_section);
  if (b->left) {
    a->stats_page = (a->stats_page + pages - 1) % pages;
    a->stats_history_scroll = 0;
    a->stats_list_scroll = 0;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->right) {
    a->stats_page = (a->stats_page + 1) % pages;
    a->stats_history_scroll = 0;
    a->stats_list_scroll = 0;
    a->ui_needs_redraw = true;
    return;
  }
}

static void draw_end_focus_confirm(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  /* Dim background */
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 180);
  SDL_Rect r = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &r);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
  int x = UI_MARGIN_X;
  int y = BIG_TIMER_Y;
  draw_text(ui, ui->font_med, x, y, "end the focus", main, false);
  draw_text(ui, ui->font_small, x, y + 64, "this will end your focus session.",
            accent, false);
  const char *opt0 = "end";
  const char *opt1 = "cancel";
  SDL_Color c0 = (a->end_focus_confirm_sel == 0) ? highlight : accent;
  SDL_Color c1 = (a->end_focus_confirm_sel == 1) ? highlight : accent;
  draw_text(ui, ui->font_small, x, y + 64 + 70, opt0, c0, false);
  draw_text(ui, ui->font_small, x, y + 64 + 70 + (UI_ROW_GAP + 6), opt1, c1,
            false);
}
static void draw_end_focus_summary(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 180);
  SDL_Rect r = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &r);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
  uint32_t secs = a->end_focus_last_spent_seconds;
  uint32_t mins = (secs + 59) / 60;
  char msg[128];
  const char *act =
      (a->run_focus_activity[0])
          ? a->run_focus_activity
          : (a->cfg.focus_activity[0] ? a->cfg.focus_activity : "focus");
  safe_snprintf(msg, sizeof(msg), "spent %u minutes on %s.", mins, act);
  int x = UI_MARGIN_X;
  int y = (ui->h / 2) - 60;
  draw_text(ui, ui->font_med, x, y, msg, main, false);
  draw_text(ui, ui->font_small, x, y + 70, "done",
            color_from_idx(a->cfg.highlight_color_idx), false);
}

static void draw_meta_selector(UI *ui, App *a, SDL_Color main,
                               SDL_Color accent) {
  (void)accent;
  if (!ui || !a)
    return;

  /* Sentence tokens (no hardcoded words):
       <phase> <background-name> <location>
     <mood> D-Pad LEFT/RIGHT selects a token.
     D-Pad UP/DOWN cycles the selected token's
     value. */

  const SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  SDL_Color c_main = main;
  c_main.a = 255;
  SDL_Color c_hi = highlight;
  c_hi.a = 255;

  char season_buf[128] = {0};
  char bg_buf[128] = {0};
  char loc_buf[128] = {0};
  char mood_buf[256] = {0};

  safe_snprintf(season_buf, sizeof(season_buf), "%s",
                a->cfg.season[0] ? phase_strip_leading_tag(a->cfg.season) : "");
  safe_snprintf(bg_buf, sizeof(bg_buf), "%s",
                a->weather_name[0] ? phase_strip_leading_tag(a->weather_name)
                                   : "");
  safe_snprintf(loc_buf, sizeof(loc_buf), "%s",
                a->scene_name[0] ? phase_strip_leading_tag(a->scene_name) : "");

  if (a->cfg.ambience_name[0]) {
    char tmp[256];
    safe_snprintf(tmp, sizeof(tmp), "%s", a->cfg.ambience_name);
    trim_ascii_inplace(tmp);
    strip_ext_inplace(tmp);

    /* Split only real trailing tags (e.g.,
       "(rain)"), then apply display decoding so
       folder-safe naming tricks render nicely
       on-device. */
    char base[256] = {0};
    char tag[128] = {0};
    if (split_trailing_tag(tmp, base, sizeof(base), tag, sizeof(tag)) &&
        base[0]) {
      mood_display_from_folder(base, mood_buf, sizeof(mood_buf));
    } else {
      mood_display_from_folder(tmp, mood_buf, sizeof(mood_buf));
    }
  }

  const char *t_season = season_buf;
  const char *t_bg = bg_buf;
  const char *t_loc = loc_buf;
  const char *t_mood = mood_buf;

  int sel = a->meta_selector_sel;
  if (sel < 0 || sel > 3)
    sel = 0;

  /* Position: Bottom of screen, aligned with
   * hints. */
  int hLine = TTF_FontHeight(ui->font_small);
  int y = ui->h - UI_MARGIN_BOTTOM - hLine;

  /* Inline spacing uses the font's space width.
   */
  int w_space = 0, h_space = 0;
  TTF_SizeUTF8(ui->font_small, " ", &w_space, &h_space);
  int gap = (w_space > 0) ? w_space : 8;

  int w_season =
      (t_season && t_season[0]) ? text_width(ui->font_small, t_season) : 0;
  int w_bg = (t_bg && t_bg[0]) ? text_width(ui->font_small, t_bg) : 0;
  /* Location uses small font per user request
   * for this screen */
  int w_loc = (t_loc && t_loc[0]) ? text_width(ui->font_small, t_loc) : 0;
  int w_mood = (t_mood && t_mood[0]) ? text_width(ui->font_small, t_mood) : 0;

  const char *tokens[] = {t_season, t_bg, t_loc, t_mood};
  int widths[] = {w_season, w_bg, w_loc, w_mood};

  /* Build sentence: "it's a <phase> <background>
   * in <location>, and <mood>."
   */
  /* Connective words with their widths */
  const char *prefix = "it's a ";
  const char *in_word = " in ";
  const char *and_word = ", and ";
  const char *period = ".";

  int w_prefix = text_width(ui->font_small, prefix);
  int w_in = text_width(ui->font_small, in_word);
  int w_and = text_width(ui->font_small, and_word);
  int w_period = text_width(ui->font_small, period);

  int total_w = w_prefix;
  if (w_season > 0)
    total_w += w_season + gap;
  if (w_bg > 0)
    total_w += w_bg;
  if (w_loc > 0)
    total_w += w_in + w_loc;
  if (w_mood > 0)
    total_w += w_and + w_mood;
  total_w += w_period;

  int x_start = (ui->w - total_w) / 2;
  int xcur = x_start;

  /* Draw: "it's a " */
  draw_text(ui, ui->font_small, xcur, y, prefix, c_main, false);
  xcur += w_prefix;

  /* Draw: <phase> */
  if (w_season > 0) {
    draw_text(ui, ui->font_small, xcur, y, t_season, (sel == 0) ? c_hi : c_main,
              false);
    xcur += w_season + gap;
  }

  /* Draw: <background> */
  if (w_bg > 0) {
    draw_text(ui, ui->font_small, xcur, y, t_bg, (sel == 1) ? c_hi : c_main,
              false);
    xcur += w_bg;
  }

  /* Draw: " in " */
  if (w_loc > 0) {
    draw_text(ui, ui->font_small, xcur, y, in_word, c_main, false);
    xcur += w_in;
    /* Draw: <location> */
    draw_text(ui, ui->font_small, xcur, y, t_loc, (sel == 2) ? c_hi : c_main,
              false);
    xcur += w_loc;
  }

  /* Draw: ", and " */
  if (w_mood > 0) {
    draw_text(ui, ui->font_small, xcur, y, and_word, c_main, false);
    xcur += w_and;
    /* Draw: <mood> */
    draw_text(ui, ui->font_small, xcur, y, t_mood, (sel == 3) ? c_hi : c_main,
              false);
    xcur += w_mood;
  }

  /* Draw: "." */
  draw_text(ui, ui->font_small, xcur, y, period, c_main, false);
}

void draw_timer(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  const bool home_idle = (a->landing_idle || !a->mode_ever_selected);
  /* Top HUD: normally shown, but hidden when HUD
   * is toggled off (or the meta selector is
   * open), unless an overlay is open. */
  if ((!a->hud_hidden && !a->meta_selector_open) || a->settings_open ||
      a->booklets.open || a->timer_menu_open) {
    draw_top_hud(ui, a);
  }
  /* Booklets overlay behaves like Settings:
   * replace the timer view while open.
   */
  if (a->booklets.open) {
    draw_booklets(ui, a);
    return;
  }
  /* When settings are open, hide only the big
     time section (and other main-screen elements
     like music label), then draw the settings
     overlay on top. */
  if (a->settings_open) {
    draw_settings_overlay(ui, a);
    return;
  }
  /* If the Timer quick menu is open, behave like
     Settings: don't draw the main timer content
     behind it (prevents "menu" and big time from
     overlapping). */
  if (a->timer_menu_open) {
    draw_timer_quick_menu(ui, a);
    return;
  }
  if (a->end_focus_confirm_open) {
    draw_end_focus_confirm(ui, a);
    return;
  }
  if (a->end_focus_summary_open) {
    draw_end_focus_summary(ui, a);
    return;
  }
  if (a->meta_selector_open) {
    if (a->stanza_selector_open) {
      draw_stanza_selector(ui, a, main, accent);
    } else {
      draw_meta_selector(ui, a, main, accent);
    }
    return;
  }
  /* Home idle screen: music-first, no timer
   * readout. */
  if (!home_idle) {
    /* Special handling for Mindful Breathing: No
     * numeric timer, centered status text. Only
     * when running! */
    if (a->mode == MODE_MEDITATION && a->meditation_run_kind == 2 &&
        a->running) {
      const char *msg = "";
      if (a->meditation_elapsed_seconds == 0)
        msg = "Relax";
      else if (a->meditation_elapsed_seconds == 1)
        msg = "Relax.";
      else if (a->meditation_elapsed_seconds == 2)
        msg = "Relax..";
      else
        msg = breath_phase_label(a->breath_phase);

      /* Draw centered, using Medium font per
       * feedback. */
      int w = text_width(ui->font_med, msg);
      int x = (ui->w - w) / 2;
      int y = (ui->h - TTF_FontHeight(ui->font_med)) / 2;
      draw_text(ui, ui->font_med, x, y, msg, main, false);

      /* Skip standard timer drawing */
      return;
    }

    char t[64] = {0};
    if (a->mode == MODE_POMODORO)
      fmt_mmss(t, sizeof(t), a->pomo_remaining_seconds);
    else if (a->mode == MODE_CUSTOM)
      fmt_hms_opt_hours(t, sizeof(t), a->custom_remaining_seconds);
    else if (a->mode == MODE_MEDITATION)
      fmt_hms_opt_hours(t, sizeof(t), a->meditation_remaining_seconds);
    else
      fmt_hms_opt_hours(t, sizeof(t), a->stopwatch_seconds);
    /* Big time / completion state.
       When a session is complete, the big timer
       should disappear and we show a completion
       message instead. Starting a new session
       clears session_complete in the relevant
       handlers (custom/pomodoro), so the timer
       won't stay hidden for the next run. */
    if (a->session_complete &&
        (a->mode == MODE_CUSTOM || a->mode == MODE_POMODORO ||
         a->mode == MODE_MEDITATION)) {
      /* Big timer hidden on completion; top HUD
       * shows "session complete!". */
    } else {
      /* When the HUD is hidden, reposition the
         timer and show a minimal state label.
         This must only affect the fully clean
         timer view (no overlays).
       */
      const bool hud_hidden_clean =
          (a->hud_hidden && !a->settings_open && !a->timer_menu_open &&
           !a->booklets.open && !a->end_focus_confirm_open &&
           !a->end_focus_summary_open);
      if (hud_hidden_clean) {
        SDL_Color main2 = color_from_idx(a->cfg.main_color_idx);
        SDL_Surface *surf = render_text_surface(ui->font_big, t, main2);
        if (surf) {
          SDL_Texture *tex = SDL_CreateTextureFromSurface(ui->ren, surf);
          if (tex) {
            int wBig = surf->w;
            int hBig = surf->h;
            /* Place timer low enough to sit
             * under the visible background
             * "window" when shifted. */
            /* Stack the big timer and the state
             * label as a single movable block.
             */
            const int hState = TTF_FontHeight(ui->font_med);
            const int stack_h = hBig + HUD_HIDDEN_TIMER_STATE_GAP + hState;
            /* Anchor the stack to the bottom
             * with a tunable pad, then apply a
             * user-tweakable offset. */
            int y_timer = ui->h - UI_MARGIN_BOTTOM -
                          HUD_HIDDEN_STACK_BOTTOM_PAD - stack_h;
            y_timer += HUD_HIDDEN_STACK_Y_OFFSET;
            if (y_timer < UI_MARGIN_TOP)
              y_timer = UI_MARGIN_TOP;
            int x_timer = (ui->w - wBig) / 2;
            SDL_Rect dst = {x_timer, y_timer, wBig, hBig};
            SDL_RenderCopy(ui->ren, tex, NULL, &dst);
            /* State label under the timer (main
             * color, medium, non-italic).
             */
            const char *state = "focusing";
            if (a->paused)
              state = "paused";
            else if (a->mode == MODE_POMODORO && a->running &&
                     a->pomo_is_break) {
              state = a->pomo_break_is_long ? "resting" : "taking a break";
            } else if (a->mode == MODE_MEDITATION) {
              if (a->meditation_run_kind == 2 &&
                  a->meditation_guided_repeats_remaining > 0) {
                state = breath_phase_label(a->breath_phase);
              } else
                state = "meditating";
            } else if (!a->running) {
              state = "paused";
            }
            int wState = text_width(ui->font_med, state);
            int xState = (ui->w - wState) / 2;
            int yState = y_timer + hBig + HUD_HIDDEN_TIMER_STATE_GAP;
            draw_text(ui, ui->font_med, xState, yState, state, main2, false);
            SDL_DestroyTexture(tex);
          }
          SDL_FreeSurface(surf);
        }
      } else {
        draw_big_time_upper_left(ui, a, t);
      }
    }
  }
  if (!home_idle && a->mode == MODE_STOPWATCH && !a->hud_hidden) {
    draw_stopwatch_laps(ui, a);
  }
  if (!a->hud_hidden && !a->meta_selector_open) {
    const char *folder_src = a->music_folder[0] ? a->music_folder : "music";
    const char *song_src = a->music_song[0] ? a->music_song : "off";
    char folder_buf[128];
    char song_buf[128];
    safe_snprintf(folder_buf, sizeof(folder_buf), "%s", folder_src);
    safe_snprintf(song_buf, sizeof(song_buf), "%s", song_src);
    trim_ascii_inplace(folder_buf);
    trim_ascii_inplace(song_buf);
    ascii_lower_inplace(song_buf);
    const int baseline_y = ui_bottom_baseline_y(ui);
    int asc_med = TTF_FontAscent(ui->font_med);
    int asc_small = TTF_FontAscent(ui->font_small);
    int y_music = baseline_y - (asc_med > asc_small ? asc_med : asc_small);
    /* Bottom-right: song (italic, small, accent)
     * + folder (med, main). */
    int xR = ui->w - UI_MARGIN_X;
    draw_inline_right(ui, ui->font_med, ui->font_small, xR, y_music, song_buf,
                      folder_buf, accent, main);
    if (a->music_user_paused && a->cfg.music_enabled) {
      int w_song = 0, h_song = 0;
      int w_folder = 0, h_folder = 0;
      TTF_SizeUTF8(ui->font_small, song_buf, &w_song, &h_song);
      TTF_SizeUTF8(ui->font_med, folder_buf, &w_folder, &h_folder);
      int gap = (song_buf[0] && folder_buf[0]) ? UI_INLINE_GAP_PX : 0;
      int total_w = w_song + gap + w_folder;
      int x_song = xR - total_w;
      int asc_med = TTF_FontAscent(ui->font_med);
      int asc_song = TTF_FontAscent(ui->font_small);
      int baseline = baseline_y;
      int y_song_top = baseline - asc_song;
      SDL_Color white = (SDL_Color){255, 255, 255, 255};
      draw_strikethrough(ui, x_song, y_song_top, w_song, h_song, white);
    }
  }
  if (!a->hud_hidden && !a->meta_selector_open) {
    if (a->paused) {
      /* Text-only swap request: show Reset on Y
       * (text only), without changing behavior.
       */
      const char *labsL[] = {"a:", "x:"};
      const char *actsL[] = {"resume", "end"};
      draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, NULL, NULL, 0);
    } else {
      const char *labsL[] = {"a:"};
      const char *actsL[] = {"pause"};
      /* Timer screen: keep hints minimal. Menu
         is still available on Start, but we
         intentionally do not display it here. */
      draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, NULL, NULL, 0);
    }
  }
}
/* handle_tasks_list removed (duplicate in
 * tasks.c) */

/* handle_quest removed (duplicate in quest.c) */

void handle_pomo_pick(App *a, Buttons *b) {
  if (a)
    focus_pick_sync_idx(a);
  if (b->b) {
    if (a->focus_line_active && a->focus_line_prev_screen == SCREEN_POMO_PICK) {
      a->focus_line_active = false;
    }
    if (a->focus_activity_dirty) {
      config_save(&a->cfg, CONFIG_PATH);
      a->focus_activity_dirty = false;
    }
    if (a->resume_valid) {
      resume_restore(a);
      a->screen = SCREEN_TIMER;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    if (a->nav_from_timer_menu) {
      a->screen = SCREEN_TIMER;
      a->landing_idle = a->nav_prev_landing_idle;
      a->timer_menu_open = true; /* Return to menu, not close */
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = true;
    app_reveal_hud(a);
  }

  /* SELECT completely closes the menu */
  if (b->select) {
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = false;
    a->settings_open = false;
    a->nav_from_timer_menu = false;
    return;
  }
  /* R3 opens focus entry menu (pick / create /
   * remove). */
  if (b->r3) {
    a->focus_menu_return_screen = SCREEN_POMO_PICK;
    a->focus_menu_sel = 0;
    a->focus_delete_confirm_open = false;
    focus_menu_sync_sel_to_current(a);
    a->screen = SCREEN_FOCUS_MENU;
    a->ui_needs_redraw = true;
    return;
  }
  /* Normal selection: 0..3 : session, break,
   * rest, repeats */
  if (b->left) {
    a->pomo_pick_sel--;
    if (a->pomo_pick_sel < 0)
      a->pomo_pick_sel = 3;
  }
  if (b->right) {
    a->pomo_pick_sel++;
    if (a->pomo_pick_sel > 3)
      a->pomo_pick_sel = 0;
  }
  int dir = 0;
  if (b->up)
    dir = +1;
  if (b->down)
    dir = -1;
  if (dir != 0) {
    if (a->pomo_pick_sel == 0) {
      a->pick_pomo_session_min += dir;
      if (a->pick_pomo_session_min < 1)
        a->pick_pomo_session_min = 60;
      if (a->pick_pomo_session_min > 60)
        a->pick_pomo_session_min = 1;
    } else if (a->pomo_pick_sel == 1) {
      a->pick_pomo_break_min += dir;
      if (a->pick_pomo_break_min < 1)
        a->pick_pomo_break_min = 60;
      if (a->pick_pomo_break_min > 60)
        a->pick_pomo_break_min = 1;
    } else if (a->pomo_pick_sel == 2) {
      a->pick_pomo_long_break_min += dir;
      if (a->pick_pomo_long_break_min < 0)
        a->pick_pomo_long_break_min = 60;
      if (a->pick_pomo_long_break_min > 60)
        a->pick_pomo_long_break_min = 0;
    } else {
      a->pick_pomo_loops += dir;
      if (a->pick_pomo_loops < 1)
        a->pick_pomo_loops = 4;
      if (a->pick_pomo_loops > 4)
        a->pick_pomo_loops = 1;
    }
  }
  if (b->a) {
    if (a->meditation_pick_view == 1) {
      const char *med =
          (a->meditation_guided_sounds.count > 0 &&
           a->meditation_guided_idx >= 0 &&
           a->meditation_guided_idx < a->meditation_guided_sounds.count)
              ? a->meditation_guided_sounds.items[a->meditation_guided_idx]
              : NULL;
      if (!med || strcmp(med, "none") == 0) {
        return;
      }
    }
    resume_clear(a);
    a->landing_idle = false;
    a->nav_from_timer_menu = false;
    a->mode = MODE_POMODORO;
    a->mode_ever_selected = true;
    a->cfg.last_pomo_session_min = a->pick_pomo_session_min;
    a->cfg.last_pomo_short_break_min = a->pick_pomo_break_min;
    a->cfg.last_pomo_long_break_min = a->pick_pomo_long_break_min;
    a->cfg.last_pomo_loops = a->pick_pomo_loops;
    config_save(&a->cfg, CONFIG_PATH);
    a->focus_activity_dirty = false;
    /* New semantics:
       - repeat = number of focus sessions (1..4)
       - break only between sessions
       - rest only at the end (optional; 0
       disables) */
    a->pomo_loops_total = a->pick_pomo_loops;
    a->pomo_loops_done = 0;
    a->pomo_is_break = false;
    a->pomo_break_is_long = false;
    a->pomo_session_seconds = (uint32_t)(a->pick_pomo_session_min * 60);
    a->pomo_break_seconds = (uint32_t)(a->pick_pomo_break_min * 60);
    a->pomo_long_break_seconds = (uint32_t)(a->pick_pomo_long_break_min * 60);
    if (a->pomo_session_seconds == 0)
      a->pomo_session_seconds = 60;
    if (a->pomo_break_seconds == 0)
      a->pomo_break_seconds = 60;
    a->pomo_remaining_seconds = a->pomo_session_seconds;
    capture_run_activity(a);
    a->screen = SCREEN_TIMER;
    a->running = true;
    a->session_complete = false;
    a->run_focus_seconds = 0;
    a->paused = false;
    a->tick_accum = 0.0f;
    a->last_tick_ms = now_ms();
  }
}
static void handle_meditation_pick(App *a, Buttons *b) {
  if (a)
    focus_pick_sync_idx(a);
  if (b->b) {
    if (a->focus_line_active &&
        a->focus_line_prev_screen == SCREEN_MEDITATION_PICK) {
      a->focus_line_active = false;
    }
    if (a->focus_activity_dirty) {
      config_save(&a->cfg, CONFIG_PATH);
      a->focus_activity_dirty = false;
    }
    if (a->resume_valid) {
      resume_restore(a);
      a->screen = SCREEN_TIMER;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    if (a->nav_from_timer_menu) {
      a->screen = SCREEN_TIMER;
      a->landing_idle = a->nav_prev_landing_idle;
      a->timer_menu_open = true; /* Return to menu, not close */
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = true;
    app_reveal_hud(a);
  }

  /* SELECT completely closes the menu */
  if (b->select) {
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = false;
    a->settings_open = false;
    a->nav_from_timer_menu = false;
    return;
  }
  if (b->r3) {
    a->focus_menu_return_screen = SCREEN_MEDITATION_PICK;
    a->focus_menu_sel = 0;
    a->focus_delete_confirm_open = false;
    focus_menu_sync_sel_to_current(a);
    a->screen = SCREEN_FOCUS_MENU;
    a->ui_needs_redraw = true;
    return;
  }
  if (b->y || b->x) {
    a->meditation_pick_view = (a->meditation_pick_view + 1) % 3;
    a->ui_needs_redraw = true;
    return;
  }
  if (a->meditation_pick_view == 1) {
    int dir = (b->right ? 1 : 0) + (b->left ? -1 : 0);
    if (dir != 0 && a->meditation_guided_sounds.count > 0) {
      a->meditation_guided_idx += dir;
      if (a->meditation_guided_idx < 0)
        a->meditation_guided_idx = a->meditation_guided_sounds.count - 1;
      if (a->meditation_guided_idx >= a->meditation_guided_sounds.count)
        a->meditation_guided_idx = 0;
      safe_snprintf(
          a->cfg.last_meditation_guided_file,
          sizeof(a->cfg.last_meditation_guided_file), "%s",
          a->meditation_guided_sounds.items[a->meditation_guided_idx]);
      config_save(&a->cfg, CONFIG_PATH);
      a->ui_needs_redraw = true;
    }
  } else if (a->meditation_pick_view == 2) {
    int dir = 0;
    if (b->right)
      dir = +1;
    if (b->left)
      dir = -1;
    if (dir != 0) {
      int v = a->pick_meditation_breaths;
      if (v < 1)
        v = 1;
      if (v > 3)
        v = 3;
      v += dir;
      if (v < 1)
        v = 3;
      if (v > 3)
        v = 1;
      a->pick_meditation_breaths = v;
      a->ui_needs_redraw = true;
    }
  } else {
    if (b->left) {
      a->meditation_pick_sel--;
      if (a->meditation_pick_sel < 0)
        a->meditation_pick_sel = 2;
    }
    if (b->right) {
      a->meditation_pick_sel++;
      if (a->meditation_pick_sel > 2)
        a->meditation_pick_sel = 0;
    }
    int dir = 0;
    if (b->up)
      dir = +1;
    if (b->down)
      dir = -1;
    if (dir != 0) {
      if (a->meditation_pick_sel == 0) {
        a->pick_meditation_hours += dir;
        if (a->pick_meditation_hours < 0)
          a->pick_meditation_hours = 4;
        if (a->pick_meditation_hours > 4)
          a->pick_meditation_hours = 0;
      } else if (a->meditation_pick_sel == 1) {
        a->pick_meditation_minutes += dir;
        if (a->pick_meditation_minutes < 0)
          a->pick_meditation_minutes = 59;
        if (a->pick_meditation_minutes > 59)
          a->pick_meditation_minutes = 0;
      } else {
        int max = meditation_bell_max_minutes(a);
        a->pick_meditation_bell_min += dir;
        if (a->pick_meditation_bell_min < 0)
          a->pick_meditation_bell_min = max;
        if (a->pick_meditation_bell_min > max)
          a->pick_meditation_bell_min = 0;
      }
      meditation_clamp_bell(a);
    }
  }
  if (b->a) {
    resume_clear(a);
    a->landing_idle = false;
    a->nav_from_timer_menu = false;
    a->mode = MODE_MEDITATION;
    a->mode_ever_selected = true;
    meditation_clamp_bell(a);
    a->cfg.last_meditation_h = a->pick_meditation_hours;
    a->cfg.last_meditation_m = a->pick_meditation_minutes;
    a->cfg.last_meditation_bell_min = a->pick_meditation_bell_min;
    a->cfg.last_meditation_breaths = a->pick_meditation_breaths;
    if (a->meditation_guided_sounds.count > 0 &&
        a->meditation_guided_idx >= 0 &&
        a->meditation_guided_idx < a->meditation_guided_sounds.count) {
      safe_snprintf(
          a->cfg.last_meditation_guided_file,
          sizeof(a->cfg.last_meditation_guided_file), "%s",
          a->meditation_guided_sounds.items[a->meditation_guided_idx]);
    }
    config_save(&a->cfg, CONFIG_PATH);
    a->focus_activity_dirty = false;
    a->session_complete = false;
    a->run_focus_seconds = 0;
    a->meditation_elapsed_seconds = 0;
    a->meditation_bell_strikes_remaining = 0;
    a->meditation_bell_strike_elapsed = 0;
    a->meditation_bell_strike_file[0] = '\0';
    if (a->meditation_pick_view == 1) {
      const char *med =
          (a->meditation_guided_sounds.count > 0 &&
           a->meditation_guided_idx >= 0 &&
           a->meditation_guided_idx < a->meditation_guided_sounds.count)
              ? a->meditation_guided_sounds.items[a->meditation_guided_idx]
              : NULL;
      if (!med || strcmp(med, "none") == 0)
        return;
      char med_name[256];
      safe_snprintf(med_name, sizeof(med_name), "%s", med);
      strip_ext_inplace(med_name);
      int minutes = meditation_minutes_from_label(med_name);
      if (minutes <= 0)
        minutes = 1;
      a->meditation_total_seconds = (uint32_t)minutes * 60;
      a->meditation_remaining_seconds = a->meditation_total_seconds;
      a->meditation_bell_interval_seconds = 0;
      a->meditation_run_kind = 1;
      a->meditation_guided_repeats_total = 0;
      a->meditation_guided_repeats_remaining = 0;
      a->meditation_half_step_counter = 0;
      if (a->audio) {
        char pth[PATH_MAX];
        safe_snprintf(pth, sizeof(pth), "sounds/meditations/%s", med);
        /* Guided meditation audio must be
         * stoppable, so route it through the
         * music channel. */
        audio_engine_stop_music(a->audio);
        audio_engine_play_music(a->audio, pth, false);
      }
    } else if (a->meditation_pick_view == 2) {
      int breaths = a->pick_meditation_breaths;
      if (breaths < 1)
        breaths = 1;
      if (breaths > 4)
        breaths = 4;
      uint32_t cycle =
          (uint32_t)(BREATH_INHALE_SECONDS + BREATH_HOLD_SECONDS +
                     BREATH_EXHALE_SECONDS + BREATH_POST_HOLD_SECONDS);
      a->meditation_total_seconds = cycle * (uint32_t)breaths;
      a->meditation_remaining_seconds = a->meditation_total_seconds;
      a->meditation_bell_interval_seconds = 0;
      a->meditation_run_kind = 2;
      a->meditation_guided_repeats_total = breaths;
      a->meditation_guided_repeats_remaining = breaths;
      a->meditation_half_step_counter = 0;
      a->breath_phase = BREATH_PHASE_INHALE;
      a->breath_phase_elapsed = 0;
    } else {
      uint32_t total_min = (uint32_t)meditation_total_minutes(a);
      a->meditation_total_seconds = total_min * 60;
      a->meditation_remaining_seconds = a->meditation_total_seconds;
      a->meditation_bell_interval_seconds =
          (uint32_t)(a->pick_meditation_bell_min * 60);
      a->meditation_run_kind = 0;
      a->meditation_guided_repeats_total = 0;
      a->meditation_guided_repeats_remaining = 0;
      a->meditation_half_step_counter = 0;
    }
    capture_run_activity(a);
    a->screen = SCREEN_TIMER;
    a->running = true;
    a->paused = false;
    if (a->mode == MODE_MEDITATION) {
      /* Meditation sessions should auto-hide the
       * HUD (quiet mode) on start.
       */
      a->hud_hidden = true;
      a->meta_selector_open = false;
      a->settings_open = false;
      a->timer_menu_open = false;
    }
    a->tick_accum = 0.0f;
    a->last_tick_ms = now_ms();
    if (a->meditation_total_seconds == 0) {
      a->running = false;
      a->session_complete = true;
    } else {
      if (a->meditation_pick_view == 0) {
        /* Only queue start bell for Timer mode.
         * Guided/Breathing are silent at start.
         */
        meditation_queue_bell_sequence(a, a->cfg.meditation_start_bell_file, 1);
      }
    }
  }
}
static void cycle_custom_value(App *a, int dir) {
  if (a->custom_field_sel == 0) {
    int v = a->pick_custom_hours + dir;
    if (v < 0)
      v = 24;
    if (v > 24)
      v = 0;
    a->pick_custom_hours = v;
  } else if (a->custom_field_sel == 1) {
    int v = a->pick_custom_minutes + dir;
    if (v < 0)
      v = 59;
    if (v > 59)
      v = 0;
    a->pick_custom_minutes = v;
  } else {
    int v = a->pick_custom_seconds + dir;
    if (v < 0)
      v = 59;
    if (v > 59)
      v = 0;
    a->pick_custom_seconds = v;
  }
}
void handle_custom_pick(App *a, Buttons *b) {
  if (a)
    focus_pick_sync_idx(a);
  if (b->b) {
    if (a->focus_line_active &&
        a->focus_line_prev_screen == SCREEN_CUSTOM_PICK) {
      a->focus_line_active = false;
    }
    if (a->resume_valid) {
      resume_restore(a);
      a->screen = SCREEN_TIMER;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    if (a->nav_from_timer_menu) {
      a->screen = SCREEN_TIMER;
      a->landing_idle = a->nav_prev_landing_idle;
      a->timer_menu_open = true; /* Return to menu, not close */
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = true;
    app_reveal_hud(a);
  }

  /* SELECT completely closes the menu */
  if (b->select) {
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = false;
    a->settings_open = false;
    a->nav_from_timer_menu = false;
    return;
  }
  /* Same physical button as Tasks reset: toggle
     counting up/down inside the timer adjustment
     screen. Some devices map this as X in code,
     others as Y, so accept either. Nowhere else
     is changed. */
  if (b->y || b->x) {
    a->cfg.timer_counting_up = a->cfg.timer_counting_up ? 0 : 1;
    config_save(&a->cfg, CONFIG_PATH);
    a->ui_needs_redraw = true;
  }
  /* R3 opens focus entry menu (pick / create /
   * remove). */
  if (b->r3) {
    a->focus_menu_return_screen = SCREEN_CUSTOM_PICK;
    a->focus_menu_sel = 0;
    a->focus_delete_confirm_open = false;
    focus_menu_sync_sel_to_current(a);
    a->screen = SCREEN_FOCUS_MENU;
    a->ui_needs_redraw = true;
    return;
  }
  /* Normal selection: 0..2 : hours, minutes,
   * seconds */
  if (b->left) {
    a->custom_field_sel--;
    if (a->custom_field_sel < 0)
      a->custom_field_sel = 2;
  }
  if (b->right) {
    a->custom_field_sel++;
    if (a->custom_field_sel > 2)
      a->custom_field_sel = 0;
  }
  if (b->up)
    cycle_custom_value(a, +1);
  if (b->down)
    cycle_custom_value(a, -1);
  if (b->a) {
    resume_clear(a);
    a->landing_idle = false;
    a->nav_from_timer_menu = false;
    a->mode = MODE_CUSTOM;
    a->mode_ever_selected = true;
    a->cfg.last_timer_h = a->pick_custom_hours;
    a->cfg.last_timer_m = a->pick_custom_minutes;
    a->cfg.last_timer_s = a->pick_custom_seconds;
    config_save(&a->cfg, CONFIG_PATH);
    a->focus_activity_dirty = false;
    /* Starting a new timer run should always
     * clear completion and reset focus
     * accumulation. */
    a->session_complete = false;
    a->run_focus_seconds = 0;
    uint32_t total =
        (uint32_t)(a->pick_custom_hours * 3600 + a->pick_custom_minutes * 60 +
                   a->pick_custom_seconds);
    a->custom_total_seconds = total;
    a->custom_counting_up_active = (a->cfg.timer_counting_up != 0);
    capture_run_activity(a);
    a->screen = SCREEN_TIMER;
    a->running = true;
    a->paused = false;
    a->tick_accum = 0.0f;
    a->last_tick_ms = now_ms();
    if (a->custom_counting_up_active) {
      /* Elapsed time starts at 0 and counts up.
       * If the target is 0, it runs
       * indefinitely. */
      a->custom_remaining_seconds = 0;
    } else {
      a->custom_remaining_seconds = a->custom_total_seconds;
      if (a->custom_total_seconds == 0) {
        a->running = false;
        a->session_complete = true;
      }
    }
  }
}
static bool file_exists_local(const char *path) {
  if (!path || !path[0])
    return false;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  fclose(f);
  return true;
}
/* A "Mood" is a shareable bundle:
   scenes/<mood_name>/
     - backgrounds/*.png  (backgrounds for this
   mood)
     - *.wav (optional ambience audio for this
   mood; preferred name: ambience.wav) */
static void mood_find_ambience_path(const char *mood_name, char *out,
                                    size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;
  if (!mood_name || !mood_name[0])
    return;
  char dir[PATH_MAX];
  safe_snprintf(dir, sizeof(dir), "scenes/%s", mood_name);
  /* Preferred: scenes/<mood>/ambience.wav */
  char pref[PATH_MAX];
  safe_snprintf(pref, sizeof(pref), "%s/ambience.wav", dir);
  if (file_exists_local(pref)) {
    safe_snprintf(out, cap, "%s", pref);
    return;
  }
  /* Otherwise: first *.wav in the mood folder.
   */
  StrList wavs = list_wav_files_in(dir);
  if (wavs.count > 0) {
    safe_snprintf(out, cap, "%s/%s", dir, wavs.items[0]);
  }
  sl_free(&wavs);
}
/* When a mood changes, its ambience file becomes
   the active ambience source. This is treated as
   an explicit user action, so we can start it
   immediately.
 */
static void apply_mood_ambience(UI *ui, App *a, bool user_action) {
  (void)ui;
  if (!a)
    return;
  if (a->scenes.count <= 0)
    return;
  const char *mood = a->scenes.items[a->scene_idx];
  char wav[PATH_MAX];
  mood_find_ambience_path(mood, wav, sizeof(wav));
  if (!wav[0]) {
    /* Mood has no ambience. Pause whatever was
     * loaded before (keep it cached).
     */
    a->cfg.ambience_enabled = 0;
    safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s",
                  "off");
    if (a->audio)
      audio_engine_set_ambience_paused(a->audio, true);
    config_save(&a->cfg, CONFIG_PATH);
    return;
  }
  safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s", wav);
  a->cfg.ambience_enabled = user_action ? 1 : 0;
  if (user_action) {
    a->ambience_user_started = true;
  }
  config_save(&a->cfg, CONFIG_PATH);
  if (user_action) {
    apply_ambience_from_cfg(ui, a, false);
  }
}
static void cycle_location(UI *ui, App *a, int dir) {
  /* Location cycling (folders under ./moods). */
  if (!a || a->scenes.count <= 0)
    return;
  a->scene_idx += dir;
  if (a->scene_idx < 0)
    a->scene_idx = a->scenes.count - 1;
  if (a->scene_idx >= a->scenes.count)
    a->scene_idx = 0;
  /* Sync cfg.scene to location folder name for
   * the top-right HUD. */
  safe_snprintf(a->cfg.scene, sizeof(a->cfg.scene), "%s",
                a->scenes.items[a->scene_idx]);
  /* Refresh moods & vibes for the new location.
   */
  refresh_moods_for_location(a);
  seasons_init(a);
  refresh_weathers_for_scene(a);
  {
    char ap[PATH_MAX];
    ambience_path_from_name(a, ap, sizeof(ap));
    a->cfg.ambience_enabled = (ap[0] != 0) ? 1 : 0;
  }
  apply_ambience_from_cfg(ui, a, true);
  update_bg_texture(ui, a);
  anim_overlay_refresh(ui, a);
  persist_scene_weather(a);
}
static void cycle_mood(UI *ui, App *a, int dir) {
  /* Mood cycling (folders under
   * ./scenes/<location>). */
  if (!a || a->moods.count <= 0)
    return;
  a->mood_idx += dir;
  if (a->mood_idx < 0)
    a->mood_idx = a->moods.count - 1;
  if (a->mood_idx >= a->moods.count)
    a->mood_idx = 0;
  safe_snprintf(a->cfg.ambience_name, sizeof(a->cfg.ambience_name), "%s",
                a->moods.items[a->mood_idx]);
  seasons_init(a);
  refresh_weathers_for_scene(a);
  {
    char ap[PATH_MAX];
    ambience_path_from_name(a, ap, sizeof(ap));
    a->cfg.ambience_enabled = (ap[0] != 0) ? 1 : 0;
  }
  apply_ambience_from_cfg(ui, a, true);
  update_bg_texture(ui, a);
  anim_overlay_refresh(ui, a);
  persist_scene_weather(a);
}
static void cycle_season(UI *ui, App *a, int dir) {
  if (!a || a->seasons.count == 0)
    return;
  /* Remember the currently selected
     phase/time-of-day base before we change
     season, so swapping seasons keeps
     "night"/"morning"/etc stable. */
  char prev_base[256] = {0};
  if (a->cfg.weather[0]) {
    safe_snprintf(prev_base, sizeof(prev_base), "%s", a->cfg.weather);
  } else if (a->weather_bases.count > 0 && a->weather_base_idx >= 0 &&
             a->weather_base_idx < a->weather_bases.count) {
    safe_snprintf(prev_base, sizeof(prev_base), "%s",
                  a->weather_bases.items[a->weather_base_idx]);
  }
  int prev_rank = phase_rank_from_leading_tag(prev_base);
  char prev_stripped[256] = {0};
  if (prev_base[0]) {
    safe_snprintf(prev_stripped, sizeof(prev_stripped), "%s",
                  phase_strip_leading_tag(prev_base));
    trim_ascii_inplace(prev_stripped);
  }
  a->season_idx += dir;
  if (a->season_idx < 0)
    a->season_idx = a->seasons.count - 1;
  if (a->season_idx >= a->seasons.count)
    a->season_idx = 0;
  const char *s = a->seasons.items[a->season_idx];
  if (s && s[0]) {
    safe_snprintf(a->cfg.season, sizeof(a->cfg.season), "%s", s);
    config_save(&a->cfg, CONFIG_PATH);
  }
  /* Re-scan backgrounds under the new season
   * folder (if present). */
  refresh_weathers_for_scene(a);
  /* Find the best matching base in the new
     season:
     - Prefer matching by leading phase-rank tag
     (e.g. "(iv)").
     - Otherwise match by stripped base name
     (e.g. "night").
     - Otherwise try exact previous base string.
     - Only if nothing matches, fall back to
     detected time or the first available base.
   */
  const char *want_base = NULL;
  if (prev_rank > 0 && a->weather_bases.count > 0) {
    for (int i = 0; i < a->weather_bases.count; i++) {
      const char *b = a->weather_bases.items[i];
      if (phase_rank_from_leading_tag(b) == prev_rank) {
        want_base = b;
        break;
      }
    }
  }
  if (!want_base && prev_stripped[0] && a->weather_bases.count > 0) {
    for (int i = 0; i < a->weather_bases.count; i++) {
      const char *b = a->weather_bases.items[i];
      const char *bs = phase_strip_leading_tag(b);
      if (bs && bs[0] && strcmp(bs, prev_stripped) == 0) {
        want_base = b;
        break;
      }
    }
  }
  if (!want_base && prev_base[0]) {
    want_base = prev_base;
  }
  if (want_base) {
    apply_weather_base_with_current_effect(ui, a, want_base, true);
    config_save(&a->cfg, CONFIG_PATH);
    return;
  }
  if (a->cfg.detect_time) {
    apply_detected_weather(ui, a, true);
    return;
  }
  if (a->weather_bases.count > 0) {
    apply_weather_base_with_current_effect(ui, a, a->weather_bases.items[0],
                                           true);
    config_save(&a->cfg, CONFIG_PATH);
    return;
  }
  if (ui)
    update_bg_texture(ui, a);
}
static void cycle_weather(UI *ui, App *a, int dir) {
  /* Vibe cycling within the current mood (files
   * under
   * ./scenes/<mood>/backgrounds). */
  if (!a || a->weathers.count <= 0)
    return;
  a->weather_idx += dir;
  if (a->weather_idx < 0)
    a->weather_idx = a->weathers.count - 1;
  if (a->weather_idx >= a->weathers.count)
    a->weather_idx = 0;
  update_bg_texture(ui, a);
  anim_overlay_refresh(ui, a);
  persist_scene_weather(a);
}

static void cycle_weather_base(UI *ui, App *a, int dir) {
  /* "Day" cycling: change the base time-of-day
   * (dawn/morning/...) while preserving the
   * current ambience effect tag. */
  if (!a || a->weather_bases.count <= 0)
    return;
  a->weather_base_idx += dir;
  if (a->weather_base_idx < 0)
    a->weather_base_idx = a->weather_bases.count - 1;
  if (a->weather_base_idx >= a->weather_bases.count)
    a->weather_base_idx = 0;
  const char *base = a->weather_bases.items[a->weather_base_idx];
  if (!base || !base[0])
    return;
  apply_weather_base_with_current_effect(ui, a, base, true);
  anim_overlay_refresh(ui, a);
}
/* Cycle ambience sounds (quick-change on the
   timer screen). This counts as an explicit user
   action, so we can immediately apply it:
   - update cfg.ambience_name /
   cfg.ambience_enabled
   - sync the effect tag (e.g. "rain")
   - restart ambience playback (if enabled)
   - instantly re-resolve the background variant
   (e.g. "morning(rain).png") */
static void cycle_palette_idx(int *idx, int dir) {
  int v = *idx + dir;
  if (PALETTE_SIZE <= 0) {
    *idx = 0;
    return;
  }
  if (v < 0)
    v = PALETTE_SIZE - 1;
  if (v >= PALETTE_SIZE)
    v = 0;
  *idx = v;
}
static void cycle_font(UI *ui, App *a, int dir) {
  if (a->fonts.count <= 0)
    return;
  int prev = a->font_idx;
  a->font_idx += dir;
  if (a->font_idx < 0)
    a->font_idx = a->fonts.count - 1;
  if (a->font_idx >= a->fonts.count)
    a->font_idx = 0;
  safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s",
                a->fonts.items[a->font_idx]);
  AppConfig backup = a->cfg;
  if (!ui_open_fonts(ui, &a->cfg)) {
    a->cfg = backup;
    a->font_idx = prev;
    safe_snprintf(a->cfg.font_file, sizeof(a->cfg.font_file), "%s",
                  a->fonts.items[a->font_idx]);
    ui_open_fonts(ui, &a->cfg);
    return;
  }
  persist_fonts(a);
}
static void clamp_sizes(App *a) {
  if (a->cfg.font_small_pt < 18)
    a->cfg.font_small_pt = 18;
  if (a->cfg.font_med_pt < 18)
    a->cfg.font_med_pt = 18;
  if (a->cfg.font_big_pt < 30)
    a->cfg.font_big_pt = 30;
  if (a->cfg.font_small_pt > 96)
    a->cfg.font_small_pt = 96;
  if (a->cfg.font_med_pt > 120)
    a->cfg.font_med_pt = 120;
  if (a->cfg.font_big_pt > 220)
    a->cfg.font_big_pt = 220;
  if (a->cfg.font_small_pt > a->cfg.font_med_pt)
    a->cfg.font_med_pt = a->cfg.font_small_pt;
  if (a->cfg.font_med_pt > a->cfg.font_big_pt)
    a->cfg.font_big_pt = a->cfg.font_med_pt;
}
static void change_size(UI *ui, App *a, int which, int delta) {
  int prevS = a->cfg.font_small_pt;
  int prevM = a->cfg.font_med_pt;
  int prevB = a->cfg.font_big_pt;
  if (which == 0)
    a->cfg.font_small_pt += delta;
  else if (which == 1)
    a->cfg.font_med_pt += delta;
  else
    a->cfg.font_big_pt += delta;
  clamp_sizes(a);
  if (!ui_open_fonts(ui, &a->cfg)) {
    a->cfg.font_small_pt = prevS;
    a->cfg.font_med_pt = prevM;
    a->cfg.font_big_pt = prevB;
    ui_open_fonts(ui, &a->cfg);
    return;
  }
  persist_fonts(a);
}
void app_reveal_hud(App *a) {
  if (!a)
    return;
  a->hud_hidden = false;
  a->meta_selector_open = false;
}
/* ----------------------------- Screen input
 * ----------------------------- */
void handle_timer_quick_menu(UI *ui, App *a, Buttons *b) {
  (void)ui;
  const int menu_count = 6;
  /* SELECT or B closes the menu. */
  if (b->b || b->select) {
    a->timer_menu_open = false;
    return;
  }
  /* START jumps directly to Settings without
   * needing to close first. */
  if (b->start) {
    a->timer_menu_open = false;
    a->settings_open = true;
    app_reveal_hud(a);
    a->settings_view = SET_VIEW_MAIN;
    a->settings_sel = 0;
    update_check_on_settings_open(a);
    return;
  }

  if (b->up)
    a->menu_sel = (a->menu_sel + menu_count - 1) % menu_count;
  if (b->down)
    a->menu_sel = (a->menu_sel + 1) % menu_count;
  if (b->a) {
    a->timer_menu_open = false;
    /* Only leave the landing (music-first)
       screen when the user actually starts a
       session. Entering pickers from the quick
       menu should not force a mode.
     */
    if (a->menu_sel == 0) {
      /* Timer */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      a->screen = SCREEN_CUSTOM_PICK;
      a->pick_custom_hours = a->cfg.last_timer_h;
      a->pick_custom_minutes = a->cfg.last_timer_m;
      a->pick_custom_seconds = a->cfg.last_timer_s;
      a->custom_field_sel = 0;
      a->cfg.timer_counting_up = 0; /* default OFF each time you enter
                                       timer picker */
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
    } else if (a->menu_sel == 1) {
      /* Pomodoro setup */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      a->screen = SCREEN_POMO_PICK;
      a->pomo_pick_sel = 0;
      a->pick_pomo_session_min = a->cfg.last_pomo_session_min;
      a->pick_pomo_break_min = a->cfg.last_pomo_short_break_min;
      a->pick_pomo_long_break_min = a->cfg.last_pomo_long_break_min;
      a->pick_pomo_loops = a->cfg.last_pomo_loops;
      if (a->pick_pomo_session_min < 1)
        a->pick_pomo_session_min = 1;
      if (a->pick_pomo_session_min > 60)
        a->pick_pomo_session_min = 60;
      if (a->pick_pomo_break_min < 1)
        a->pick_pomo_break_min = 1;
      if (a->pick_pomo_break_min > 60)
        a->pick_pomo_break_min = 60;
      if (a->pick_pomo_long_break_min < 0)
        a->pick_pomo_long_break_min = 0;
      if (a->pick_pomo_long_break_min > 60)
        a->pick_pomo_long_break_min = 60;
      if (a->pick_pomo_loops < 1)
        a->pick_pomo_loops = 1;
      if (a->pick_pomo_loops > 4)
        a->pick_pomo_loops = 4;
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
    } else if (a->menu_sel == 2) {
      /* Meditation setup */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      a->screen = SCREEN_MEDITATION_PICK;
      a->meditation_pick_sel = 0;
      a->meditation_pick_view = 0;
      a->pick_meditation_hours = a->cfg.last_meditation_h;
      a->pick_meditation_minutes = a->cfg.last_meditation_m;
      a->pick_meditation_bell_min = a->cfg.last_meditation_bell_min;
      a->pick_meditation_breaths = a->cfg.last_meditation_breaths;
      if (a->pick_meditation_hours < 0)
        a->pick_meditation_hours = 0;
      if (a->pick_meditation_hours > 4)
        a->pick_meditation_hours = 4;
      if (a->pick_meditation_minutes < 0)
        a->pick_meditation_minutes = 0;
      if (a->pick_meditation_minutes > 59)
        a->pick_meditation_minutes = 59;
      meditation_clamp_bell(a);
      if (a->pick_meditation_breaths < 0)
        a->pick_meditation_breaths = 0;
      if (a->pick_meditation_breaths > 48)
        a->pick_meditation_breaths = 48;
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
      a->focus_activity_dirty = false;
      focus_pick_sync_idx(a);
    } else if (a->menu_sel == 3) {
      /* To-do list */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      a->screen = SCREEN_TASKS_PICK;
      a->tasks.pick_sel = 0;
      a->tasks.sel = 0;
      a->tasks.kind = 0;
      tasks_reload(a);
    } else if (a->menu_sel == 4) {
      /* Routines */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      a->routine.items_n = 0;
      routine_load(a);
      a->screen = SCREEN_ROUTINE_LIST;
      a->routine.sel_row = 0;
      a->routine.sel_day = routine_today_weekday();
      a->routine.sel_phase = ROUTINE_PHASE_ALL;
      a->routine.list_scroll = 0;
    } else if (a->menu_sel == 5) {
      /* Booklets */
      a->nav_from_timer_menu = true;
      a->nav_prev_landing_idle = a->landing_idle;
      booklets_open_toggle(ui, a);
    }
  }
}
/* draw_timer_quick_menu moved to
 * features/timer/timer.c */
static void handle_menu(App *a, Buttons *b) {
  const int menu_count = 6;
  if (b->up)
    a->menu_sel = (a->menu_sel + menu_count - 1) % menu_count;
  if (b->down)
    a->menu_sel = (a->menu_sel + 1) % menu_count;
  if (b->a) {
    a->nav_from_timer_menu = false;
    a->landing_idle = false;
    if (a->menu_sel == 0) {
      /* Timer */
      a->screen = SCREEN_CUSTOM_PICK;
      a->pick_custom_hours = a->cfg.last_timer_h;
      a->pick_custom_minutes = a->cfg.last_timer_m;
      a->pick_custom_seconds = a->cfg.last_timer_s;
      a->custom_field_sel = 0;
      a->cfg.timer_counting_up = 0; /* default OFF each time you enter
                                       timer picker */
    } else if (a->menu_sel == 1) {
      /* Pomodoro setup */
      a->screen = SCREEN_POMO_PICK;
      a->pomo_pick_sel = 0;
      a->pick_pomo_session_min = a->cfg.last_pomo_session_min;
      a->pick_pomo_break_min = a->cfg.last_pomo_short_break_min;
      a->pick_pomo_long_break_min = a->cfg.last_pomo_long_break_min;
      a->pick_pomo_loops = a->cfg.last_pomo_loops;
      if (a->pick_pomo_session_min < 1)
        a->pick_pomo_session_min = 1;
      if (a->pick_pomo_session_min > 60)
        a->pick_pomo_session_min = 60;
      if (a->pick_pomo_break_min < 1)
        a->pick_pomo_break_min = 1;
      if (a->pick_pomo_break_min > 60)
        a->pick_pomo_break_min = 60;
      if (a->pick_pomo_long_break_min < 0)
        a->pick_pomo_long_break_min = 0;
      if (a->pick_pomo_long_break_min > 60)
        a->pick_pomo_long_break_min = 60;
      if (a->pick_pomo_loops < 1)
        a->pick_pomo_loops = 1;
      if (a->pick_pomo_loops > 4)
        a->pick_pomo_loops = 4;
    } else if (a->menu_sel == 2) {
      /* Meditation setup */
      a->screen = SCREEN_MEDITATION_PICK;
      a->meditation_pick_sel = 0;
      a->meditation_pick_view = 0;
      a->pick_meditation_hours = a->cfg.last_meditation_h;
      a->pick_meditation_minutes = a->cfg.last_meditation_m;
      a->pick_meditation_bell_min = a->cfg.last_meditation_bell_min;
      a->pick_meditation_breaths = a->cfg.last_meditation_breaths;
      if (a->pick_meditation_hours < 0)
        a->pick_meditation_hours = 0;
      if (a->pick_meditation_hours > 4)
        a->pick_meditation_hours = 4;
      if (a->pick_meditation_minutes < 0)
        a->pick_meditation_minutes = 0;
      if (a->pick_meditation_minutes > 59)
        a->pick_meditation_minutes = 59;
      meditation_clamp_bell(a);
      if (a->pick_meditation_breaths < 0)
        a->pick_meditation_breaths = 0;
      if (a->pick_meditation_breaths > 48)
        a->pick_meditation_breaths = 48;
    } else if (a->menu_sel == 3) {
      /* To-do list */
      tasks_reload(a);
      a->screen = SCREEN_TASKS_PICK;
      a->tasks.pick_sel = 0;
      a->tasks.sel = 0;
      a->tasks.kind = 0;
    } else if (a->menu_sel == 4) {
      /* Routines (was Habits) */
      /* Manual reload for now */
      a->routine.items_n = 0;
      routine_load(a);
      a->screen = SCREEN_ROUTINE_LIST;
      a->routine.sel_row = 0;
      a->routine.list_scroll = 0;
      a->routine.sel_phase = ROUTINE_PHASE_ALL;
    } else if (a->menu_sel == 5) {
      /* Booklets */
      a->screen = SCREEN_TIMER;
      a->booklets.open = true;
      a->timer_menu_open = false;
    }
  }
}
/* Redundant handle_tasks_pick removed. Logic now
 * in features/tasks/tasks.c */

static void draw_settings_name(UI *ui, App *a) {
  if (!ui || !a)
    return;
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);

  /* Pitch black background */
  draw_rect_fill(ui, 0, 0, ui->w, ui->h, (SDL_Color){0, 0, 0, 255});

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Centered header */
  const char *title = "your name";
  int w_hdr = text_width(ui->font_med, title);
  draw_text(ui, ui->font_med, cx - w_hdr / 2, header_y, title, main, false);

  /* Centered input field */
  int y = header_y + UI_ROW_GAP + 10;
  char display_buf[64];
  safe_snprintf(display_buf, sizeof(display_buf), "%s", a->stanza_custom_buf);
  const char *disp = display_buf[0] ? display_buf : "(empty)";
  int w_input = text_width(ui->font_med, disp);
  draw_text_input_with_cursor(ui, ui->font_med, cx - w_input / 2, y,
                              a->stanza_custom_buf, "(empty)", highlight,
                              accent, highlight, 0);

  /* Centered keyboard rows */
  int yKb = y + TTF_FontHeight(ui->font_med) + 26;
  keyboard_draw(ui, cx, yKb, a->stanza_custom_kb_row, a->stanza_custom_kb_col,
                accent, highlight);

  const char *labsL[] = {"b:", "x:", "y:", "a:"};
  const char *actsL[] = {"cancel", "backspace", "space", "add"};
  const char *labsR[] = {"r3:"};
  const char *actsR[] = {"done"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 4, labsR, actsR, 1);
}

static void handle_settings_name(App *a, Buttons *b) {
  if (!a || !b)
    return;
  /* B backs out without saving. */
  if (b->b) {
    a->screen = SCREEN_TIMER;
    a->settings_open = true;
    a->settings_view = SET_VIEW_MISC;
    a->ui_needs_redraw = true;
    return;
  }

  if (keyboard_update(a, b, a->stanza_custom_buf, sizeof(a->stanza_custom_buf),
                      &a->stanza_custom_kb_row, &a->stanza_custom_kb_col)) {
    a->ui_needs_redraw = true;
  }

  if (b->start || b->r3) {
    safe_snprintf(a->cfg.user_name, sizeof(a->cfg.user_name), "%s",
                  a->stanza_custom_buf);
    config_save(&a->cfg, CONFIG_PATH);
    a->screen = SCREEN_TIMER;
    a->settings_open = true;
    a->settings_view = SET_VIEW_MISC;
    a->ui_needs_redraw = true;
    return;
  }
  a->ui_needs_redraw = true;
}

static void handle_settings(UI *ui, App *a, Buttons *b) {
  /* START toggles Settings: pressing it again
   * closes the menu. */
  if (b->start) {
    a->settings_open = false;
    a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* In Settings, SELECT behaves like A
   * (activate/toggle). Use B to go back/close.
   */
  /* Scene / Mood / Vibe / Animations */
  if (a->settings_view == SET_VIEW_SCENE) {
    /* Four rows: 0 = scene, 1 = mood, 2 = vibe,
     * 3 = animations */
    if (b->up)
      a->scene_sel = (a->scene_sel + 4 - 1) % 4;
    if (b->down)
      a->scene_sel = (a->scene_sel + 1) % 4;
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      if (a->scene_sel == 0) {
        cycle_location(ui, a, dir);
      } else if (a->scene_sel == 1) {
        cycle_mood(ui, a, dir);
      } else if (a->scene_sel == 2) {
        cycle_weather(ui, a, dir);
      } else {
        animations_set_enabled(ui, a, a->cfg.animations ? false : true);
      }
    }
    if (b->a && a->scene_sel == 3) {
      animations_set_enabled(ui, a, a->cfg.animations ? false : true);
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* Appearance */
  if (a->settings_view == SET_VIEW_APPEARANCE) {
    if (b->up)
      a->appearance_sel = (a->appearance_sel + 2 - 1) % 2;
    if (b->down)
      a->appearance_sel = (a->appearance_sel + 1) % 2;
    if (b->a) {
      if (a->appearance_sel == 0) {
        a->settings_view = SET_VIEW_FONTS;
        a->fonts_sel = 0;
      } else {
        a->settings_view = SET_VIEW_COLORS;
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* Fonts */
  if (a->settings_view == SET_VIEW_FONTS) {
    if (b->up)
      a->fonts_sel = (a->fonts_sel + 2 - 1) % 2;
    if (b->down)
      a->fonts_sel = (a->fonts_sel + 1) % 2;
    if (a->fonts_sel == 0) {
      if (b->left)
        cycle_font(ui, a, -1);
      if (b->right)
        cycle_font(ui, a, 1);
    } else {
      if (b->a) {
        a->settings_view = SET_VIEW_FONT_SIZES;
        a->sizes_sel = 0;
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_APPEARANCE;
    return;
  }
  /* Colors */
  if (a->settings_view == SET_VIEW_COLORS) {
    if (b->up)
      a->colors_sel = (a->colors_sel + 2) % 3;
    if (b->down)
      a->colors_sel = (a->colors_sel + 1) % 3;
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      if (a->colors_sel == 0)
        cycle_palette_idx(&a->cfg.main_color_idx, dir);
      else if (a->colors_sel == 1)
        cycle_palette_idx(&a->cfg.accent_color_idx, dir);
      else
        cycle_palette_idx(&a->cfg.highlight_color_idx, dir);
      persist_colors(a);
    }
    if (b->b)
      a->settings_view = SET_VIEW_APPEARANCE;
    return;
  }
  /* Font sizes */
  if (a->settings_view == SET_VIEW_FONT_SIZES) {
    if (b->up)
      a->sizes_sel = (a->sizes_sel + 2) % 3;
    if (b->down)
      a->sizes_sel = (a->sizes_sel + 1) % 3;
    if (b->left)
      change_size(ui, a, a->sizes_sel, -2);
    if (b->right)
      change_size(ui, a, a->sizes_sel, 2);
    if (b->b)
      a->settings_view = SET_VIEW_FONTS;
    return;
  }
  /* Audio */
  if (a->settings_view == SET_VIEW_SOUNDS) {
    if (b->up)
      a->sounds_sel = (a->sounds_sel + 2 - 1) % 2;
    if (b->down)
      a->sounds_sel = (a->sounds_sel + 1) % 2;
    if (b->a) {
      if (a->sounds_sel == 0) {
        a->settings_view = SET_VIEW_SOUND_VOLUME;
        a->volume_sel = 0;
      } else {
        a->settings_view = SET_VIEW_SOUND_NOTIFICATIONS;
        a->notif_sel = 0;
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* Volume */
  if (a->settings_view == SET_VIEW_SOUND_VOLUME) {
    if (b->up)
      a->volume_sel = (a->volume_sel + 2) % 3;
    if (b->down)
      a->volume_sel = (a->volume_sel + 1) % 3;
    if (b->a) {
      if (a->volume_sel == 0) {
        if (!a->vol_music_muted) {
          a->vol_music_saved = a->cfg.vol_music;
          a->cfg.vol_music = 0;
          a->vol_music_muted = true;
        } else {
          a->cfg.vol_music = a->vol_music_saved;
          a->vol_music_muted = false;
        }
      } else if (a->volume_sel == 1) {
        if (!a->vol_ambience_muted) {
          a->vol_ambience_saved = a->cfg.vol_ambience;
          a->cfg.vol_ambience = 0;
          a->vol_ambience_muted = true;
        } else {
          a->cfg.vol_ambience = a->vol_ambience_saved;
          a->vol_ambience_muted = false;
        }
      } else {
        if (!a->vol_notif_muted) {
          a->vol_notif_saved = a->cfg.vol_notifications;
          a->cfg.vol_notifications = 0;
          a->vol_notif_muted = true;
        } else {
          a->cfg.vol_notifications = a->vol_notif_saved;
          a->vol_notif_muted = false;
        }
      }
      if (a->audio) {
        audio_engine_set_music_volume(a->audio, a->cfg.vol_music);
        if (a->audio)
          audio_engine_set_ambience_volume(a->audio, a->cfg.vol_ambience);
        audio_engine_set_sfx_volume(a->audio, a->cfg.vol_notifications);
      }
      config_save(&a->cfg, CONFIG_PATH);
    }
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      int *target = NULL;
      if (a->volume_sel == 0)
        target = &a->cfg.vol_music;
      else if (a->volume_sel == 1)
        target = &a->cfg.vol_ambience;
      else
        target = &a->cfg.vol_notifications;
      if (target) {
        int step = (128 * 5) / 100;
        if (step < 1)
          step = 1;
        *target += dir * step;
        if (*target < 0)
          *target = 0;
        if (*target > 128)
          *target = 128;
        if (a->volume_sel == 0 && *target > 0)
          a->vol_music_muted = false;
        if (a->volume_sel == 1 && *target > 0)
          a->vol_ambience_muted = false;
        if (a->volume_sel == 2 && *target > 0)
          a->vol_notif_muted = false;
        if (a->audio) {
          audio_engine_set_music_volume(a->audio, a->cfg.vol_music);
          if (a->audio)
            audio_engine_set_ambience_volume(a->audio, a->cfg.vol_ambience);
          audio_engine_set_sfx_volume(a->audio, a->cfg.vol_notifications);
        }
        config_save(&a->cfg, CONFIG_PATH);
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* Notification sounds picker */
  if (a->settings_view == SET_VIEW_SOUND_NOTIFICATIONS) {
    if (b->up)
      a->notif_sel = (a->notif_sel + 3) % 4;
    if (b->down)
      a->notif_sel = (a->notif_sel + 1) % 4;
    if (b->a && a->notif_sel == 0) {
      a->cfg.notifications_enabled = a->cfg.notifications_enabled ? 0 : 1;
      config_save(&a->cfg, CONFIG_PATH);
    } else if (b->a && a->notif_sel == 3) {
      a->settings_view = SET_VIEW_SOUND_MEDITATION;
      a->meditation_notif_sel = 0;
      return;
    }
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      if (a->notif_sel == 0) {
        a->cfg.notifications_enabled = a->cfg.notifications_enabled ? 0 : 1;
        config_save(&a->cfg, CONFIG_PATH);
      } else if (a->notif_sel == 1) {
        if (a->bell_sounds.count > 0) {
          a->bell_phase_idx += dir;
          if (a->bell_phase_idx < 0)
            a->bell_phase_idx = a->bell_sounds.count - 1;
          if (a->bell_phase_idx >= a->bell_sounds.count)
            a->bell_phase_idx = 0;
          safe_snprintf(a->cfg.bell_phase_file, sizeof(a->cfg.bell_phase_file),
                        "%s", a->bell_sounds.items[a->bell_phase_idx]);
          config_save(&a->cfg, CONFIG_PATH);
        }
      } else {
        if (a->bell_sounds.count > 0) {
          a->bell_done_idx += dir;
          if (a->bell_done_idx < 0)
            a->bell_done_idx = a->bell_sounds.count - 1;
          if (a->bell_done_idx >= a->bell_sounds.count)
            a->bell_done_idx = 0;
          safe_snprintf(a->cfg.bell_done_file, sizeof(a->cfg.bell_done_file),
                        "%s", a->bell_sounds.items[a->bell_done_idx]);
          config_save(&a->cfg, CONFIG_PATH);
        }
      }
    }
    /* X: preview selected bell */
    if (b->x) {
      if (a->notif_sel == 1) {
        const char *phase =
            (a->bell_sounds.count > 0 && a->bell_phase_idx >= 0 &&
             a->bell_phase_idx < a->bell_sounds.count)
                ? a->bell_sounds.items[a->bell_phase_idx]
                : a->cfg.bell_phase_file;
        if (phase && phase[0] && a->audio) {
          char pth[PATH_MAX];
          safe_snprintf(pth, sizeof(pth), "sounds/%s", phase);
          audio_engine_play_sfx(a->audio, pth);
        }
      } else if (a->notif_sel == 2) {
        const char *done = (a->bell_sounds.count > 0 && a->bell_done_idx >= 0 &&
                            a->bell_done_idx < a->bell_sounds.count)
                               ? a->bell_sounds.items[a->bell_done_idx]
                               : a->cfg.bell_done_file;
        if (done && done[0] && a->audio) {
          char pth[PATH_MAX];
          safe_snprintf(pth, sizeof(pth), "sounds/%s", done);
          audio_engine_play_sfx(a->audio, pth);
        }
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_SOUNDS;
    return;
  }
  if (a->settings_view == SET_VIEW_SOUND_MEDITATION) {
    if (b->up)
      a->meditation_notif_sel = (a->meditation_notif_sel + 2) % 3;
    if (b->down)
      a->meditation_notif_sel = (a->meditation_notif_sel + 1) % 3;
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      if (a->bell_sounds.count > 0) {
        if (a->meditation_notif_sel == 0) {
          a->meditation_bell_start_idx += dir;
          if (a->meditation_bell_start_idx < 0)
            a->meditation_bell_start_idx = a->bell_sounds.count - 1;
          if (a->meditation_bell_start_idx >= a->bell_sounds.count)
            a->meditation_bell_start_idx = 0;
          safe_snprintf(a->cfg.meditation_start_bell_file,
                        sizeof(a->cfg.meditation_start_bell_file), "%s",
                        a->bell_sounds.items[a->meditation_bell_start_idx]);
        } else if (a->meditation_notif_sel == 1) {
          a->meditation_bell_interval_idx += dir;
          if (a->meditation_bell_interval_idx < 0)
            a->meditation_bell_interval_idx = a->bell_sounds.count - 1;
          if (a->meditation_bell_interval_idx >= a->bell_sounds.count)
            a->meditation_bell_interval_idx = 0;
          safe_snprintf(a->cfg.meditation_interval_bell_file,
                        sizeof(a->cfg.meditation_interval_bell_file), "%s",
                        a->bell_sounds.items[a->meditation_bell_interval_idx]);
        } else {
          a->meditation_bell_end_idx += dir;
          if (a->meditation_bell_end_idx < 0)
            a->meditation_bell_end_idx = a->bell_sounds.count - 1;
          if (a->meditation_bell_end_idx >= a->bell_sounds.count)
            a->meditation_bell_end_idx = 0;
          safe_snprintf(a->cfg.meditation_end_bell_file,
                        sizeof(a->cfg.meditation_end_bell_file), "%s",
                        a->bell_sounds.items[a->meditation_bell_end_idx]);
        }
        config_save(&a->cfg, CONFIG_PATH);
      }
    }
    if (b->x) {
      const char *bell = NULL;
      if (a->meditation_notif_sel == 0)
        bell = a->cfg.meditation_start_bell_file;
      else if (a->meditation_notif_sel == 1)
        bell = a->cfg.meditation_interval_bell_file;
      else
        bell = a->cfg.meditation_end_bell_file;
      if (bell && bell[0] && a->audio) {
        char pth[PATH_MAX];
        safe_snprintf(pth, sizeof(pth), "sounds/%s", bell);
        audio_engine_play_sfx(a->audio, pth);
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_SOUND_NOTIFICATIONS;
    return;
  }
  /* General */
  if (a->settings_view == SET_VIEW_MISC) {
    if (b->up)
      /* 4 items now */
      a->misc_sel = (a->misc_sel + 4 - 1) % 4;
    if (b->down)
      a->misc_sel = (a->misc_sel + 1) % 4;

    if (a->misc_sel == 0) {
      /* Name */
      if (b->a) {
        a->screen = SCREEN_SETTINGS_NAME;
        a->settings_open = false; /* Hide settings overlay
                                     temporarily */
        /* Initialize editing buffer with current
         * name */
        safe_snprintf(a->stanza_custom_buf, sizeof(a->stanza_custom_buf), "%s",
                      a->cfg.user_name);
        a->stanza_custom_kb_row = 0;
        a->stanza_custom_kb_col = 0;
        stanza_custom_kb_clamp(a);
        a->ui_needs_redraw = true;
      }
    } else if (a->misc_sel == 3) {
      /* Haiku Difficulty (index 3) */
      int dir = b->right ? 1 : (b->left ? -1 : 0);
      /* SELECT (b->a) disabled per request */
      if (dir != 0) {
        int next = a->cfg.haiku_difficulty + dir;
        if (next < 0)
          next = 0;
        if (next > 4)
          next = 4;
        if (next != a->cfg.haiku_difficulty) {
          a->cfg.haiku_difficulty = next;
          config_save(&a->cfg, CONFIG_PATH);
        }
      }
    } else if (b->a || b->left || b->right) {
      if (a->misc_sel == 1) {
        /* Sync Day (index 1) */
        a->cfg.detect_time = a->cfg.detect_time ? 0 : 1;
        config_save(&a->cfg, CONFIG_PATH);
        if (a->cfg.detect_time) {
          apply_detected_season(ui, a, true);
          apply_detected_weather(ui, a, true);
        }
      } else if (a->misc_sel == 2) {
        /* Animations (index 2) */
        animations_set_enabled(ui, a, a->cfg.animations ? false : true);
        config_save(&a->cfg, CONFIG_PATH);
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  /* About */
  if (a->settings_view == SET_VIEW_ABOUT) {
    if (b->up) {
      a->settings_about_sel = (a->settings_about_sel + 3 - 1) % 3;
      a->ui_needs_redraw = true;
    }
    if (b->down) {
      a->settings_about_sel = (a->settings_about_sel + 1) % 3;
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      if (a->settings_about_sel == 0) {
        if (a->update_status == UPDATE_STATUS_AVAILABLE) {
          if (!a->update_thread) {
            a->update_status = UPDATE_STATUS_DOWNLOADING;
            safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                          "downloading...");
            a->update_anim_last_ms = 0;
            update_start_thread(a, UPDATE_ACTION_DOWNLOAD);
          }
        } else if (a->update_status == UPDATE_STATUS_APPLIED) {
          a->quit_requested = true;
        } else if (a->update_status == UPDATE_STATUS_IDLE ||
                   a->update_status == UPDATE_STATUS_ERROR ||
                   a->update_status == UPDATE_STATUS_UP_TO_DATE) {
          update_start_check(a, true);
        }
      } else {
        if (a->settings_about_sel == 1) {
          a->settings_view = SET_VIEW_CHANGELOG;
          a->settings_changelog_scroll = 0;
        } else if (a->settings_about_sel == 2) {
          a->settings_view = SET_VIEW_RELEASES;
          a->release_sel = 0;
          a->release_scroll = 0;
          a->release_popup_open = false;
          if (a->release_status == RELEASE_STATUS_IDLE ||
              (a->release_status == RELEASE_STATUS_ERROR &&
               a->release_tags.count == 0)) {
            release_start_list_thread(a);
          }
        }
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_MAIN;
    return;
  }
  if (a->settings_view == SET_VIEW_CHANGELOG) {
    if (b->up) {
      if (a->settings_changelog_scroll > 0)
        a->settings_changelog_scroll--;
      a->ui_needs_redraw = true;
    }
    if (b->down) {
      const int max_w = ui->w - (UI_MARGIN_X * 2);
      int max_scroll = settings_changelog_max_scroll(ui, a, max_w);
      if (a->settings_changelog_scroll < max_scroll)
        a->settings_changelog_scroll++;
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      if (a->release_status == RELEASE_STATUS_READY &&
          a->release_tags.count > 0) {
        a->settings_view = SET_VIEW_RELEASES;
        a->release_sel = 0;
        a->release_scroll = 0;
        a->release_popup_open = false;
      } else if (a->release_status != RELEASE_STATUS_LISTING) {
        release_start_list_thread(a);
      }
    }
    if (b->b)
      a->settings_view = SET_VIEW_ABOUT;
    return;
  }
  if (a->settings_view == SET_VIEW_RELEASES) {
    if (a->release_popup_open) {
      if (b->up || b->down) {
        a->release_popup_sel = (a->release_popup_sel == 0) ? 1 : 0;
      }
      if (b->a) {
        if (a->release_popup_sel == 0) {
          if (a->release_sel >= 0 && a->release_sel < a->release_urls.count) {
            release_start_download_thread(
                a, a->release_tags.items[a->release_sel],
                a->release_urls.items[a->release_sel]);
          }
        } else {
          a->release_popup_open = false;
        }
      }
      if (b->b) {
        a->release_popup_open = false;
      }
      a->ui_needs_redraw = true;
      return;
    }
    if (b->up) {
      a->release_sel--;
      if (a->release_sel < 0)
        a->release_sel = a->release_tags.count - 1;
      a->ui_needs_redraw = true;
    }
    if (b->down) {
      a->release_sel++;
      if (a->release_sel >= a->release_tags.count)
        a->release_sel = 0;
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      if (a->release_tags.count > 0) {
        a->release_popup_open = true;
        a->release_popup_sel = 0;
      }
      a->ui_needs_redraw = true;
    }
    if (b->b) {
      a->settings_view = SET_VIEW_ABOUT;
      return;
    }
    return;
  }
  /* Main list */
  if (a->settings_view == SET_VIEW_MAIN) {
    /* Main list:
       0 = general
       1 = interface
       2 = audio
       3 = statistics
       4 = about
       5 = exit
       (Location/mood are handled via the meta
       selector now.) */
    if (a->settings_sel < 0 || a->settings_sel > 5)
      a->settings_sel = 0;
    if (b->up)
      a->settings_sel = (a->settings_sel + 6 - 1) % 6;
    if (b->down)
      a->settings_sel = (a->settings_sel + 1) % 6;

    if (b->a) {
      if (a->settings_sel == 0) {
        a->settings_view = SET_VIEW_MISC;
        a->misc_sel = 0;
      } else if (a->settings_sel == 1) {
        a->settings_view = SET_VIEW_APPEARANCE;
        a->appearance_sel = 0;
      } else if (a->settings_sel == 2) {
        a->settings_view = SET_VIEW_SOUNDS;
        a->sounds_sel = 0;
      } else if (a->settings_sel == 3) {
        a->settings_open = false;
        a->settings_view = SET_VIEW_MAIN;
        a->screen = SCREEN_STATS;
        a->stats_section = 0;
        a->stats_page = 0;
        a->stats_history_scroll = 0;
        a->stats_list_scroll = 0;
        a->stats_return_to_settings = true;
        a->ui_needs_redraw = true;
        return;
      } else if (a->settings_sel == 4) {
        a->settings_view = SET_VIEW_ABOUT;
        a->settings_about_scroll = 0;
        a->settings_about_sel = 0;
        update_start_check(a, true);
      } else if (a->settings_sel == 5) {
        a->quit_requested = true;
        a->settings_open = false;
        a->settings_view = SET_VIEW_MAIN;
        return;
      }
    }

    if (b->b) {
      a->settings_open = false;
      a->settings_view = SET_VIEW_MAIN;
    }
    return;
  }
}
void handle_timer(UI *ui, App *a, Buttons *b) {
  /* Allow swapping between Settings (START) and
   * Menu (SELECT) without closing first. */
  if (a->settings_open) {
    if (b->select) {
      a->settings_open = false;
      a->settings_view = SET_VIEW_MAIN;
      a->timer_menu_open = true;
      app_reveal_hud(a);
      /* Preserve a->menu_sel (last selection)
       * when switching from Settings.
       */
      return;
    }
    handle_settings(ui, a, b);
    return;
  }
  /* Timer quick menu overlay is toggled by
   * SELECT. */
  if (a->timer_menu_open) {
    if (b->start) {
      a->timer_menu_open = false;
      a->settings_open = true;
      app_reveal_hud(a);
      a->settings_view = SET_VIEW_MAIN;
      a->settings_sel = 0;
      update_check_on_settings_open(a);
      return;
    }
    handle_timer_quick_menu(ui, a, b);
    return;
  }
  /* End focus popups (confirmation and summary).
   */
  if (a->end_focus_confirm_open) {
    if (b->up || b->down)
      a->end_focus_confirm_sel = (a->end_focus_confirm_sel == 0) ? 1 : 0;
    if (b->b) {
      a->end_focus_confirm_open = false;
      return;
    }
    if (b->a) {
      if (a->end_focus_confirm_sel == 1) {
        a->end_focus_confirm_open = false;
        return;
      }
      /* End it: award focused time for this run,
       * then show summary. */
      a->end_focus_confirm_open = false;
      a->end_focus_summary_open = true;
      a->end_focus_last_spent_seconds = a->run_focus_seconds;
      focus_history_append(a, a->run_focus_seconds, "ended");
      award_focus_seconds(a, a->run_focus_seconds);
      if (a->mode == MODE_MEDITATION) {
        if (a->meditation_run_kind == 1 && a->audio) {
          audio_engine_stop_music(a->audio);
        }
        /* Ensure visual zoom state is cleared
         * when stopping manually. */
        a->meditation_guided_repeats_remaining = 0;
        a->breath_phase = 0;
      }
      a->running = false;
      a->paused = false;
      a->session_complete = true;
      a->run_focus_seconds = 0;
      a->hud_hidden = false; /* Reveal HUD when meditation is
                                manually ended */
      resume_clear(a);
      return;
    }
    return;
  }
  if (a->end_focus_summary_open) {
    if (b->a || b->b) {
      a->end_focus_summary_open = false;
    }
    return;
  }
  /* If we switched away from an active session
     via the quick menu (e.g. into Stopwatch),
     allow B to return to the previously running
     timer session. */
  if (b->b && a->resume_valid && a->mode != a->resume_mode) {
    resume_restore(a);
    return;
  }
  /* Meta selector view (background-shifted, no
     timer/status text).
     - D-Pad LEFT/RIGHT: select token {season,
     background, location, mood}
     - D-Pad UP/DOWN: cycle the selected token's
     value
     - Y or B: exit */
  if (a->meta_selector_open) {
    if (a->stanza_selector_open) {
      handle_stanza_selector(ui, a, b);
      return;
    }
    if (b->r3) {
      stanza_selector_open(a);
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b || b->y) {
      a->meta_selector_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->left) {
      a->meta_selector_sel = (a->meta_selector_sel + 3) & 3;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->right) {
      a->meta_selector_sel = (a->meta_selector_sel + 1) & 3;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->up || b->down) {
      int dir = b->down ? 1 : -1;
      if (a->meta_selector_sel == 0)
        cycle_season(ui, a, dir);
      else if (a->meta_selector_sel == 1)
        cycle_weather_base(ui, a, dir);
      else if (a->meta_selector_sel == 2)
        cycle_location(ui, a, dir);
      else
        cycle_mood(ui, a, dir);
      return;
    }
    return;
  }
  /* Quick-change on the timer screen.
     - D-Pad LEFT/RIGHT: backgrounds (base only;
     variants remain hidden)
     - D-Pad UP/DOWN: seasons */
  if (b->left)
    cycle_weather(ui, a, -1);
  if (b->right)
    cycle_weather(ui, a, +1);
  if (b->up)
    cycle_season(ui, a, -1);
  if (b->down)
    cycle_season(ui, a, +1);
  /* Settings (START) */
  if (b->start) {
    a->settings_open = true;
    app_reveal_hud(a);
    a->settings_view = SET_VIEW_MAIN;
    a->settings_sel = 0;
    update_check_on_settings_open(a);
    return;
  }
  /* Timer menu (SELECT) */
  if (b->select) {
    /* If a session is active, allow returning
     * after visiting other screens.
     */
    if (a->screen == SCREEN_TIMER && a->running && !a->resume_valid) {
      resume_capture(a);
    }
    a->timer_menu_open = true;
    app_reveal_hud(a);
    a->menu_sel = 0;
    return;
  }
  /* Timer pause/resume.
     On the landing screen we intentionally do
     nothing here (music-first chill view). */
  if (b->a && !a->landing_idle)
    timer_toggle_pause(a);

  /* Folder/Song navigation (no on-screen hints;
   * see Button Map page 2). */
  /* L2/R2: folders */
  if ((b->l1 || b->r1)) {
    int dir = b->r1 ? 1 : -1;
    if (a->music_folders.count > 0) {
      /* Persist last selection for the current
       * folder before switching. */
      music_player_save_state(a);

      const bool was_playing = (a->cfg.music_enabled && !a->music_user_paused);
      a->music_folder_idx += dir;
      if (a->music_folder_idx < 0)
        a->music_folder_idx = a->music_folders.count - 1;
      if (a->music_folder_idx >= a->music_folders.count)
        a->music_folder_idx = 0;
      safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s",
                    a->music_folders.items[a->music_folder_idx]);
      /* Rebuild queue for the new folder.
         - If music was playing, keep playing.
         - If music is off, we still rebuild so
         selection memory stays correct.
       */
      music_player_build_playlist(a, was_playing);
      music_player_refresh_labels(a);
      config_save(&a->cfg, CONFIG_PATH);
    }
  }
  /* L1/R1: songs */
  if (b->l2)
    music_player_prev(a);
  if (b->r2)
    music_player_next(a);

  /* L3: Music On/Off */
  if (b->l3) {
    if (a->cfg.music_enabled) {
      a->cfg.music_enabled = 0;
      music_player_stop(a);
      music_player_refresh_labels(a);
    } else {
      a->cfg.music_enabled = 1;
      music_player_build_playlist(a, true);
    }
    config_save(&a->cfg, CONFIG_PATH);
    a->ui_needs_redraw = true;
  }
  /* R3: Pause/Play (only if music is enabled) */
  if (b->r3) {
    if (a->cfg.music_enabled) {
      if (a->music_user_paused) {
        a->music_user_paused = false;
        audio_engine_set_music_paused(a->audio, false);
      } else {
        a->music_user_paused = true;
        audio_engine_set_music_paused(a->audio, true);
      }
      a->ui_needs_redraw = true;
    }
  }
  /* X:
     - When a focus session is running
     (custom/pomodoro), opens the end-session
     confirm.
     - In Stopwatch mode, records a lap while
     running.
     - When NOT running, toggles counting up/down
     (so the physical button matches "end" during
     a run). */
  if (!a->settings_open && !a->timer_menu_open && !a->end_focus_confirm_open &&
      !a->end_focus_summary_open && b->x) {
    if (a->running && !a->session_complete &&
        (a->mode == MODE_CUSTOM || a->mode == MODE_POMODORO ||
         a->mode == MODE_MEDITATION)) {
      a->end_focus_confirm_open = true;
      a->end_focus_confirm_sel = 0; /* default to end */
      a->ui_needs_redraw = true;
      return;
    }
    if (a->mode == MODE_STOPWATCH && a->running && !a->paused) {
      if (a->stopwatch_lap_count < MAX_STOPWATCH_LAPS) {
        a->stopwatch_laps[a->stopwatch_lap_count++] = a->stopwatch_seconds;
      } else {
        for (int i = 1; i < MAX_STOPWATCH_LAPS; i++)
          a->stopwatch_laps[i - 1] = a->stopwatch_laps[i];
        a->stopwatch_laps[MAX_STOPWATCH_LAPS - 1] = a->stopwatch_seconds;
        a->stopwatch_lap_base++;
      }
      a->ui_needs_redraw = true;
      return;
    }
    if (!a->running) {
      a->cfg.timer_counting_up = a->cfg.timer_counting_up ? 0 : 1;
      config_save(&a->cfg, CONFIG_PATH);
      a->ui_needs_redraw = true;
      return;
    }
  }
  /* B toggles HUD visibility (timer screen
   * only). */
  if (!a->settings_open && !a->timer_menu_open && !a->end_focus_confirm_open &&
      !a->end_focus_summary_open && a->hud_hide_btn_down &&
      !a->hud_hide_hold_triggered) {
    uint64_t now = now_ms();
    if ((now - a->hud_hide_btn_down_ms) >= (uint64_t)HUD_HIDE_HOLD_MS) {
      a->hud_hide_hold_triggered = true;
      a->hud_hidden = !a->hud_hidden;
      if (a->hud_hidden)
        a->meta_selector_open = false;
      a->ui_needs_redraw = true;
      return;
    }
  }
  /* Y opens a background-shifted meta selector
   * (timer screen only). */
  if (!a->settings_open && !a->timer_menu_open && !a->end_focus_confirm_open &&
      !a->end_focus_summary_open && b->y) {
    a->meta_selector_open = !a->meta_selector_open;
    if (a->meta_selector_open) {
      a->meta_selector_sel = 2;
    }
    a->ui_needs_redraw = true;
    return;
  }
  /* NOTE: B no longer exits the app or stops the
   * timer on this screen. */
}

/* ----------------------------- Main
 * ----------------------------- */
int main(int argc, char **argv) {
  chdir_to_exe_dir();

  // 1. Ensure "states" exists for other logging
  ensure_dir_exists("states");

  // 2. Panic log start (now goes to stderr)
  panic_log("=== Booting Stillroom v6 (STDERR DEBUG) ===");
  panic_log("CWD set, states dir checked.");

  migrate_legacy_state();

  /* Logging/crash capture: set up before SDL
   * init so early failures are recorded. */
  log_open(LOG_PATH);
  log_session_header();
  crash_install_handlers();
  SDL_LogSetOutputFunction(sdl_log_to_file, NULL);
  (void)argc;
  (void)argv;

  panic_log("Initializing SDL Video/GameController (Audio excluded)...");

  // 3. Init SDL WITHOUT Audio first, so video failure is distinct from audio
  // failure
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "sdl_init failed: %s\n", SDL_GetError());
    log_printf("sdl_init failed: %s", SDL_GetError());
    panic_log("SDL_Init VIDEO failed: %s", SDL_GetError());
    return 1;
  }

  // 4. Try to init Audio separately. If it fails, we continue without audio.
  bool audio_subsystem_ok = false;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
    audio_subsystem_ok = true;
    panic_log("SDL_Init AUDIO success.");
  } else {
    log_printf(
        "Note: SDL_InitSubSystem(AUDIO) failed: %s. Continuing without sound.",
        SDL_GetError());
    panic_log("SDL_Init AUDIO failed: %s. Continuing silent.", SDL_GetError());
  }

  /* bell is played via audio_engine_play_sfx */
  panic_log("Attempting IMG_Init(0)...");
  int img_inited = IMG_Init(0);
  panic_log("IMG_Init(0) returned: 0x%x", img_inited);

  panic_log("Attempting IMG_Init(PNG|JPG)...");
  int img_flags = IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
  panic_log("IMG_Init(PNG|JPG) returned: 0x%x", img_flags);

  panic_log("Attempting TTF_Init()...");

  if (TTF_Init() != 0) {
    fprintf(stderr, "ttf_init failed: %s\n", TTF_GetError());
    log_printf("ttf_init failed: %s", TTF_GetError());
    panic_log("TTF_Init failed: %s", TTF_GetError());
    return 1;
  }
  panic_log("TTF_Init success.");

  UI ui = (UI){0};
  ui.w = 1024;
  ui.h = 768;
  ui.win =
      SDL_CreateWindow("lofi pomodoro", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, ui.w, ui.h, SDL_WINDOW_SHOWN);
  if (!ui.win) {
    fprintf(stderr, "createwindow failed: %s\n", SDL_GetError());
    log_printf("createwindow failed: %s", SDL_GetError());
    panic_log("SDL_CreateWindow failed: %s", SDL_GetError());
    return 1;
  }
  panic_log("SDL_CreateWindow success.");

  panic_log("Attempting SDL_CreateRenderer (SOFTWARE)...");
  ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_SOFTWARE);

  if (!ui.ren) {
    fprintf(stderr, "createrenderer failed: %s\n", SDL_GetError());
    log_printf("createrenderer failed: %s", SDL_GetError());
    panic_log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return 1;
  }
  panic_log("SDL_CreateRenderer (SW) success.");

  panic_log("Using static App struct (BSS)... Size check: %zu bytes",
            sizeof(App));
  static App app;
  memset(&app, 0, sizeof(app));
  panic_log("App struct cleared. size=%zu", sizeof(app));

  app.history_dirty = true;

  panic_log("Creating update_mutex...");
  app.update_mutex = SDL_CreateMutex();
  if (!app.update_mutex) {
    panic_log("SDL_CreateMutex failed: %s", SDL_GetError());
  } else {
    panic_log("SDL_CreateMutex success.");
  }

  update_capture_exe_path(&app);
  config_defaults(&app.cfg);

  panic_log("Loading config...");
  config_load(&app.cfg, CONFIG_PATH);

  panic_log("Loading stanza overrides...");
  stanza_overrides_load(&app);

  panic_log("Loading focus stats...");
  focus_stats_load(&app, FOCUS_STATS_PATH);

  /* Load persistent per-folder song selections
   * (independent of music on/off).
   */
  panic_log("Loading music state...");
  music_player_load_state(&app);

  /* Always boot with music OFF, regardless of
   * what was saved previously. */
  if (app.cfg.music_enabled) {
    app.cfg.music_enabled = 0;
    config_save(&app.cfg, CONFIG_PATH);
  }

  /* Tasks: load lists + completion state early
   * so the Tasks menu is instant.
   */
  tasks_reload(&app);

  routine_load(&app);
  routine_history_load(&app);
  quest_reload_list(&app);
  if (audio_subsystem_ok) {
    if (audio_engine_init(&app.audio) != AUDIO_OK) {
      app.audio = NULL;
      panic_log("audio_engine_init failed internal setup.");
    } else {
      panic_log("audio_engine_init success.");
    }
  } else {
    app.audio = NULL;
    panic_log("Skipping audio_engine_init (subsystem failed).");
  }
  if (app.audio) {
    audio_engine_set_master_volume(app.audio, app.cfg.vol_master);
    audio_engine_set_music_volume(app.audio, app.cfg.vol_music);
    audio_engine_set_ambience_volume(app.audio, app.cfg.vol_ambience);
    audio_engine_set_sfx_volume(app.audio, app.cfg.vol_notifications);
  }
  safe_snprintf(app.music_folder, sizeof(app.music_folder), "%s", "music");
  safe_snprintf(app.music_song, sizeof(app.music_song), "%s", "off");
  sync_font_list(&app);
  sync_bell_list(&app);
  sync_meditation_guided_list(&app);
  music_player_sync_folder_list(&app);
  /* Build the music queue even if music is OFF,
   * so we can preserve/restore the last
   * selection. */
  music_player_build_playlist(&app, false);
  music_player_refresh_labels(&app);
  /* Ambience never auto-starts (only user action
   * starts it). */
  if (app.cfg.music_enabled && app.audio) {
    /* (Normally unreachable due to forced
     * boot-off, but keep for completeness.)
     */
    music_player_build_playlist(&app, true);
  }
  if (!ui_open_fonts(&ui, &app.cfg)) {
    safe_snprintf(app.cfg.font_file, sizeof(app.cfg.font_file), "%s",
                  "munro.ttf");
    app.cfg.font_small_pt = 42;
    app.cfg.font_med_pt = 50;
    app.cfg.font_big_pt = 100;
    ui_open_fonts(&ui, &app.cfg);
  }

  app.scenes = list_dirs_in("scenes");
  if (app.scenes.count == 0) {
    sl_push(&app.scenes, "home");
    sl_sort(&app.scenes);
  }
  /* Location (top-right HUD) is now the folder
   * name under ./moods. */
  if (!app.cfg.scene[0]) {
    safe_snprintf(app.cfg.scene, sizeof(app.cfg.scene), "%s",
                  app.scenes.items[0]);
  }
  int sidx = sl_find(&app.scenes, app.cfg.scene);
  app.scene_idx = (sidx >= 0) ? sidx : 0;

  seasons_init(&app);
  config_save(&app.cfg, CONFIG_PATH);
  /* Always boot into the default mood. This
     keeps the landing screen deterministic,
     regardless of what was saved in config. */
  safe_snprintf(app.cfg.ambience_name, sizeof(app.cfg.ambience_name), "%s",
                "stillness");
  app.cfg.ambience_enabled = 0;
  refresh_moods_for_location(&app);
  seasons_init(&app);
  refresh_weathers_for_scene(&app);
  /* Mood-driven ambience: auto-enable if the
   * selected mood provides a WAV, and start
   * playback. */
  {
    char ap[PATH_MAX];
    music_player_ambience_path_from_name(&app, ap, sizeof(ap));
    app.cfg.ambience_enabled = (ap[0] != 0) ? 1 : 0;
    apply_ambience_from_cfg(&ui, &app, true);
  }
  /* Auto-select season/weather on launch if
   * enabled. */
  apply_detected_season(&ui, &app, false);
  apply_detected_weather(NULL, &app, false);
  int widx = -1;
  if (app.cfg.weather[0]) {
    /* Respect current ambience effect tag when
     * selecting initial background.
     */
    widx = find_weather_idx_for_base_and_tag(&app, app.cfg.weather,
                                             app.ambience_tag);
  }
  if (widx >= 0)
    app.weather_idx = widx;
  update_bg_texture(&ui, &app);
  /* Load optional animated overlays based on
   * config */
  if (app.cfg.animations) {
    animations_set_enabled(&ui, &app, true);
  } else {
    app.anim_overlay_enabled = 0;
  }
  app.screen = SCREEN_TIMER;
  app.landing_idle = true;
  app.mode_ever_selected = false;
  /* Explicit non-Pomodoro default to avoid
     showing a misleading mode label if any code
     path ever renders a non-home HUD before the
     first selection.
   */
  app.mode = MODE_CUSTOM;
  app.running = false;
  app.paused = false;
  app.session_complete = false;
  app.menu_sel = 0;
  app.pomo_pick_sel = 0;
  app.pick_pomo_session_min = 25;
  app.pick_pomo_break_min = 5;
  app.pick_pomo_loops = 4;
  app.pomo_session_seconds = 25 * 60;
  app.pomo_break_seconds = 5 * 60;
  app.last_tick_ms = now_ms();
  SDL_GameController *pad = NULL;
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      pad = SDL_GameControllerOpen(i);
      if (pad)
        break;
    }
  }

  /* Power tuning:
     We do not redraw every loop. We wait for
     input, timer ticks, or animation frames.
     These timeouts keep input responsive without
     burning battery on a static screen.
  */
  const int IDLE_WAIT_MS = 1000; /* when nothing is running/animating
                                  */
  const int RUN_WAIT_MS = 100;   /* timer/stopwatch active */
  const int ANIM_WAIT_MS = 60;   /* animated overlay active (caps
                                    wakeups) */
  /* Force an initial draw. */
  app.ui_needs_redraw = true;
  bool quit = false;
  while (!quit) {
    Buttons b;
    buttons_clear(&b);
    /* Decide how long we can sleep.
       Input wakes us immediately; otherwise we
       wake for timer ticks or animations. */
    int wait_ms = IDLE_WAIT_MS;
    if (app.running && !app.paused)
      wait_ms = RUN_WAIT_MS;
    if (app.anim_overlay_enabled && app.anim_overlay_frame_count > 1) {
      if (wait_ms > ANIM_WAIT_MS)
        wait_ms = ANIM_WAIT_MS;
    }
    if (app.menu_btn_down && !app.help_overlay_open) {
      if (wait_ms > 30)
        wait_ms = 30;
    }
    if (app.hud_hide_btn_down && !app.hud_hide_hold_triggered) {
      if (wait_ms > 30)
        wait_ms = 30;
    }

    SDL_Event e;
    bool got_event = SDL_WaitEventTimeout(&e, wait_ms);
    if (got_event) {
      /* Process the first event returned by
       * WaitEventTimeout, then drain the queue.
       */
      for (;;) {
        if (e.type == SDL_QUIT) {
          quit = true;
          app.ui_needs_redraw = true;
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
          /* Track held MENU (GUIDE) for
           * contextual help overlay. */
          if (e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
            if (!app.menu_btn_down) {
              app.menu_btn_down = true;
              app.menu_btn_down_ms = now_ms();
              if (timer_main_screen(&app)) {
                quest_reload_list(&app);
                quest_sync_daily(&app);
              }
            }
          }
          if (is_b_action_button(e.cbutton.button, app.cfg.swap_ab)) {
            if (!app.hud_hide_btn_down) {
              app.hud_hide_btn_down = true;
              app.hud_hide_hold_triggered = false;
              app.hud_hide_btn_down_ms = now_ms();
            }
          }
          map_button(&b, e.cbutton, app.cfg.swap_ab);
          app.ui_needs_redraw = true;
        } else if (e.type == SDL_CONTROLLERBUTTONUP) {
          if (e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
            uint64_t held_ms = now_ms() - app.menu_btn_down_ms;
            bool short_press = held_ms < (uint64_t)UI_HELP_OVERLAY_HOLD_MS;
            app.menu_btn_down = false;
            if (short_press && app.screen == SCREEN_QUEST) {
              app.screen = SCREEN_TIMER;
              app.timer_menu_open = false;
              app.settings_open = false;
              app.nav_from_timer_menu = false;
              app.quest_from_timer_button = false;
            } else if (short_press && timer_main_screen(&app)) {
              app.quest_from_timer_button = true;
              app.screen = SCREEN_QUEST;
              app.timer_menu_open = false;
              app.settings_open = false;
              app.nav_from_timer_menu = false;
            }
            app.ui_needs_redraw = true;
          }
          if (is_b_action_button(e.cbutton.button, app.cfg.swap_ab)) {
            app.hud_hide_btn_down = false;
            app.hud_hide_hold_triggered = false;
          }
        } else if (e.type == SDL_CONTROLLERAXISMOTION) {
          const int TH = 16000;
          if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            if (e.caxis.value > TH && !app.trig_l2_down) {
              b.l2 = true;
              app.trig_l2_down = 1;
              app.ui_needs_redraw = true;
            }
            if (e.caxis.value < TH / 2)
              app.trig_l2_down = 0;
          } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            if (e.caxis.value > TH && !app.trig_r2_down) {
              b.r2 = true;
              app.trig_r2_down = 1;
              app.ui_needs_redraw = true;
            }
            if (e.caxis.value < TH / 2)
              app.trig_r2_down = 0;
          }
        } else if (e.type == SDL_KEYDOWN) {
          if (is_b_action_key(e.key.keysym.sym, app.cfg.swap_ab)) {
            if (!app.hud_hide_btn_down) {
              app.hud_hide_btn_down = true;
              app.hud_hide_hold_triggered = false;
              app.hud_hide_btn_down_ms = now_ms();
            }
          }
          map_key(&b, e.key, app.cfg.swap_ab);
          app.ui_needs_redraw = true;
        } else if (e.type == SDL_KEYUP) {
          if (is_b_action_key(e.key.keysym.sym, app.cfg.swap_ab)) {
            app.hud_hide_btn_down = false;
            app.hud_hide_hold_triggered = false;
          }
        }
        if (!SDL_PollEvent(&e))
          break;
      }
    }
    if (app.screen == SCREEN_TIMER)
      app_update(&app);
    update_poll(&app);

    /* MENU button: hold to show contextual UI
     * help overlay (PNG). */
    help_overlay_tick(&ui, &app, &b);

    /* Booklets overlay: handled globally while
     * open. */
    if (!app.help_overlay_open && app.booklets.open) {
      handle_booklets(&ui, &app, &b);
    }
    if (!app.help_overlay_open && !app.booklets.open) {
      if (app.screen == SCREEN_POMO_PICK) {
        handle_pomo_pick(&app, &b);
      } else if (app.screen == SCREEN_MEDITATION_PICK) {
        handle_meditation_pick(&app, &b);
      } else if (app.screen == SCREEN_FOCUS_MENU) {
        handle_focus_menu(&app, &b);
      } else if (app.screen == SCREEN_FOCUS_TEXT) {
        handle_focus_text(&app, &b);
      } else if (app.screen == SCREEN_STATS) {
        handle_stats(&app, &b);
      } else if (app.screen == SCREEN_TASKS_PICK) {
        handle_tasks_pick(&app, &b);
      } else if (app.screen == SCREEN_TASKS_LIST) {
        handle_tasks_list(&app, &b);
      } else if (app.screen == SCREEN_TASKS_TEXT) {
        handle_tasks_text(&app, &b);

      } else if (app.screen == SCREEN_ROUTINE_LIST) {
        if (app.routine.grid_view)
          handle_routine_grid(&app, &b);
        else
          handle_routine_list(&app, &b);
      } else if (app.screen == SCREEN_ROUTINE_EDIT) {
        handle_routine_edit(&app, &b);
      } else if (app.screen == SCREEN_ROUTINE_ENTRY_PICKER) {
        handle_routine_entry_picker(&app, &b);
      } else if (app.screen == SCREEN_ROUTINE_ENTRY_TEXT) {
        handle_routine_entry_text(&app, &b);
      } else if (app.screen == SCREEN_SETTINGS_NAME) {
        handle_settings_name(&app, &b);
      } else if (app.screen == SCREEN_QUEST) {
        handle_quest(&app, &b);
      } else if (app.screen == SCREEN_CUSTOM_PICK) {
        handle_custom_pick(&app, &b);
      } else if (app.screen == SCREEN_TIMER) {
        handle_timer(&ui, &app, &b);
      }
    }
    if (app.quit_requested) {
      quit = true;
    }
    music_player_update(&app);
    anim_overlay_update(&app);
    if (app.ui_needs_redraw) {
      SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 255);
      SDL_RenderClear(ui.ren);
      /* Dim background whenever any overlay/menu
       * is open, so all menus feel consistent.
       */
      bool want_dim = (app.screen != SCREEN_TIMER) || app.settings_open ||
                      app.timer_menu_open || app.booklets.open ||
                      app.help_overlay_open || app.end_focus_confirm_open ||
                      app.end_focus_summary_open;
      /* Base background first */
      bool breathe_bg =
          (app.screen == SCREEN_TIMER &&
           (app.mode == MODE_MEDITATION && app.meditation_run_kind == 2 &&
            app.meditation_guided_repeats_remaining > 0)) &&
          !app.settings_open && !app.timer_menu_open && !app.booklets.open &&
          !app.help_overlay_open && !app.end_focus_confirm_open &&
          !app.end_focus_summary_open;
      bool shift_bg_hidden =
          (app.screen == SCREEN_TIMER && app.hud_hidden && !app.settings_open &&
           !app.timer_menu_open && !app.booklets.open &&
           !app.help_overlay_open && !app.end_focus_confirm_open &&
           !app.end_focus_summary_open);
      if (shift_bg_hidden) {
        if (breathe_bg)
          draw_bg_scaled(&ui, breath_phase_scale(&app), 0, true);
        else
          draw_bg_normal_shifted(&ui, BG_HIDDEN_SHIFT_Y);
      } else {
        if (want_dim)
          draw_bg_blurred(&ui);
        else if (app.booklets.open && app.booklets.mode == 1)
          draw_bg_faint(&ui, 128);
        else if (breathe_bg)
          draw_bg_scaled(&ui, breath_phase_scale(&app), 0, true);
        else
          draw_bg_normal(&ui);
      }
      /* Optional animated overlay (e.g., rain)
       */
      anim_overlay_draw(&ui, &app);

      if (app.screen == SCREEN_POMO_PICK)
        draw_pomo_picker(&ui, &app);
      else if (app.screen == SCREEN_MEDITATION_PICK)
        draw_meditation_picker(&ui, &app);
      else if (app.screen == SCREEN_CUSTOM_PICK)
        draw_custom_picker(&ui, &app);
      else if (app.screen == SCREEN_FOCUS_MENU)
        draw_focus_menu(&ui, &app);
      else if (app.screen == SCREEN_FOCUS_TEXT)
        draw_focus_text(&ui, &app);
      else if (app.screen == SCREEN_STATS)
        draw_stats(&ui, &app);
      else if (app.screen == SCREEN_TASKS_PICK)
        draw_tasks_pick(&ui, &app);
      else if (app.screen == SCREEN_TASKS_LIST)
        draw_tasks_list(&ui, &app);
      else if (app.screen == SCREEN_TASKS_TEXT)
        draw_tasks_text(&ui, &app);

      else if (app.screen == SCREEN_ROUTINE_LIST) {
        if (app.routine.grid_view)
          draw_routine_grid(&ui, &app);
        else
          draw_routine_list(&ui, &app);
      } else if (app.screen == SCREEN_ROUTINE_EDIT)
        draw_routine_edit(&ui, &app);
      else if (app.screen == SCREEN_SETTINGS_NAME)
        draw_settings_name(&ui, &app);
      else if (app.screen == SCREEN_ROUTINE_ENTRY_PICKER)
        draw_routine_entry_picker(&ui, &app);
      else if (app.screen == SCREEN_ROUTINE_ENTRY_TEXT)
        draw_routine_entry_text(&ui, &app);
      else if (app.screen == SCREEN_QUEST)
        draw_quest(&ui, &app);
      else
        draw_timer(&ui, &app);
      if (app.booklets.open) {
        draw_booklets(&ui, &app);
      }
      draw_help_overlay(&ui, &app);
      SDL_RenderPresent(ui.ren);
      app.ui_needs_redraw = false;
    }
  }
  if (app.running &&
      (app.mode == MODE_POMODORO || app.mode == MODE_CUSTOM ||
       app.mode == MODE_MEDITATION) &&
      app.run_focus_seconds > 0) {
    focus_history_append(&app, app.run_focus_seconds, "aborted");
  }

  config_save(&app.cfg, CONFIG_PATH);
  if (pad)
    SDL_GameControllerClose(pad);
  sl_free(&app.scenes);
  sl_free(&app.weathers);
  sl_free(&app.weather_bases);
  sl_free(&app.fonts);
  sl_free(&app.bell_sounds);
  sl_free(&app.meditation_guided_sounds);
  sl_free(&app.stanza_loc_keys);
  sl_free(&app.stanza_loc_1);
  sl_free(&app.stanza_loc_2);
  sl_free(&app.stanza_loc_3);
  booklets_render_clear(&app);
  config_save(&app.cfg, CONFIG_PATH);
  routine_history_save(&app);

  booklets_list_clear(&app);
  textcache_free(&app.cache_big_time);
  textcache_free(&app.cache_clock_min);
  textcache_free(&app.cache_clock_hour);
  textcache_free(&app.cache_batt_bottom);
  textcache_free(&app.cache_batt_top);
  anim_overlay_unload(&app);
  help_overlay_close(&app);
  if (ui.bg_tex)
    SDL_DestroyTexture(ui.bg_tex);
  if (ui.bg_blur_tex)
    SDL_DestroyTexture(ui.bg_blur_tex);
  ui_close_fonts(&ui);
  SDL_DestroyRenderer(ui.ren);
  SDL_DestroyWindow(ui.win);
  music_player_stop(&app);
  sl_free(&app.music_folders);
  sl_free(&app.musicq.tracks);
  audio_engine_quit(&app.audio);
  sl_free(&app.tasks.lists);
  sl_free(&app.tasks.files);
  sl_free(&app.tasks.items);

  sl_free(&app.haiku_files);
  quest_tokens_clear(&app);

  sl_free(&app.release_tags);
  sl_free(&app.release_urls);
  sl_free(&app.release_notes);
  hashes_free(&app.tasks.done, &app.tasks.done_n);
  TTF_Quit();
  IMG_Quit();
  log_printf("Normal shutdown.");
  log_close();
  SDL_Quit();
  return 0;
}
