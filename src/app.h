#ifndef APP_H
#define APP_H

#include "audio_engine.h"
#include "features/booklets/booklet_types.h"
#include "features/quest/quest_types.h"
#include "features/routines/routine_types.h"
#include "features/tasks/task_types.h"
#include "utils/string_utils.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef MAX_STOPWATCH_LAPS
#define MAX_STOPWATCH_LAPS 5
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef STATES_DIR
#define STATES_DIR "states"
#endif

#define STATES_TASKS_DIR STATES_DIR "/tasks"
#define CONFIG_PATH STATES_DIR "/config.txt"
#define FOCUS_STATS_PATH STATES_DIR "/focus_stats.txt"
#define LOG_PATH STATES_DIR "/log.txt"
#define BOOKLETS_STATE_PATH STATES_DIR "/booklets_state.txt"
#define MUSIC_STATE_PATH STATES_DIR "/music_state.txt"
#define HUD_STANZA_OVERRIDES_PATH STATES_DIR "/hud_stanzas.txt"
#define ACTIVITY_LOG_PATH STATES_DIR "/activity_log.txt"
#define FOCUS_HISTORY_PATH STATES_DIR "/focus_history.txt"

#define MAX_FOCUS_STATS 32

typedef enum {
  SCREEN_MENU = 0,
  SCREEN_POMO_PICK,
  SCREEN_CUSTOM_PICK,
  SCREEN_ROUTINE_ENTRY_PICKER,
  SCREEN_ROUTINE_ENTRY_TEXT,
  SCREEN_MEDITATION_PICK,
  SCREEN_TASKS_PICK,
  SCREEN_TASKS_LIST,
  SCREEN_TASKS_TEXT,
  SCREEN_TIMER,

  SCREEN_QUEST,
  SCREEN_FOCUS_MENU,
  SCREEN_FOCUS_TEXT,
  SCREEN_STATS,
  SCREEN_ROUTINE_LIST,
  SCREEN_ROUTINE_EDIT,
  SCREEN_HABITS_PICK,
  SCREEN_HABITS_LIST,
  SCREEN_HABITS_TEXT,
  SCREEN_SETTINGS_NAME,
} Screen;

typedef enum {
  MODE_POMODORO = 0,
  MODE_CUSTOM,
  MODE_STOPWATCH,
  MODE_MEDITATION,
} RunMode;

typedef enum {
  BREATH_PHASE_INHALE = 0,
  BREATH_PHASE_HOLD,
  BREATH_PHASE_EXHALE,
  BREATH_PHASE_HOLD_POST,
} BreathPhase;

typedef struct Buttons {
  bool up, down, left, right;
  bool a, b, x, y;
  bool start;
  bool menu;
  bool select;
  bool l1, r1;
  bool l2, r2;
  bool l3, r3;
} Buttons;

typedef struct FocusStat {
  char name[64];
  uint64_t seconds;
} FocusStat;

typedef struct QuestHaiku QuestHaiku;

typedef enum {
  SET_VIEW_MAIN = 0,
  SET_VIEW_SCENE,
  SET_VIEW_APPEARANCE,
  SET_VIEW_FONTS,
  SET_VIEW_COLORS,
  SET_VIEW_SOUNDS,
  SET_VIEW_SOUND_VOLUME,
  SET_VIEW_SOUND_NOTIFICATIONS,
  SET_VIEW_SOUND_MEDITATION,
  SET_VIEW_MISC,
  SET_VIEW_FONT_SIZES,
  SET_VIEW_ABOUT,
  SET_VIEW_CHANGELOG,
  SET_VIEW_RELEASES,
} SettingsView;

typedef enum {
  UPDATE_STATUS_IDLE = 0,
  UPDATE_STATUS_CHECKING,
  UPDATE_STATUS_UP_TO_DATE,
  UPDATE_STATUS_AVAILABLE,
  UPDATE_STATUS_DOWNLOADING,
  UPDATE_STATUS_DOWNLOADED,
  UPDATE_STATUS_PATCHING,
  UPDATE_STATUS_APPLIED,
  UPDATE_STATUS_ERROR,
} UpdateStatus;

typedef enum {
  RELEASE_STATUS_IDLE = 0,
  RELEASE_STATUS_LISTING,
  RELEASE_STATUS_READY,
  RELEASE_STATUS_DOWNLOADING,
  RELEASE_STATUS_APPLIED,
  RELEASE_STATUS_ERROR,
} ReleaseStatus;

typedef enum {
  UPDATE_ACTION_NONE = 0,
  UPDATE_ACTION_CHECK,
  UPDATE_ACTION_DOWNLOAD,
  UPDATE_ACTION_LIST_RELEASES,
  UPDATE_ACTION_DOWNLOAD_RELEASE,
} UpdateAction;

typedef struct {
  UpdateStatus status;
  char message[256];
  char latest_tag[64];
  char download_url[512];
  char latest_notes[2048];
} UpdateResult;

typedef struct {
  ReleaseStatus status;
  char message[256];
} ReleaseResult;

typedef struct {
  char dt[32]; /* YYYY-MM-DD HH:MM */
  uint32_t seconds;
  char status[16]; /* completed | ended | aborted */
  char activity[64];
} FocusHistoryRow;

typedef struct {
  char scene[128];
  char weather[128];
  char season[96]; /* season folder name (may include "(i) " ordering prefix) */
  int detect_time;
  int swap_ab;
  int animations; /* 0/1: master switch for animated overlays (rain, snow, etc.)
                   */
  int haiku_difficulty;         /* 0..4: casual..ascetic */
  int quest_anchor_date;        /* YYYYMMDD, stays until quest reset */
  int quest_anchor_season_rank; /* 1..4 */
  int quest_completed;          /* 0/1 */
  int quest_cycle; /* Counter for cycling through quests on the same day */
  uint64_t quest_spent_seconds; /* Focus seconds spent on previous quests */
  int palette_version;
  int main_color_idx;
  int accent_color_idx;
  int highlight_color_idx;
  char font_file[256];
  int font_small_pt;
  int font_med_pt;
  int font_big_pt;
  int music_enabled;
  char music_folder[128];
  int ambience_enabled;
  char ambience_name[128]; /* logical name, mapped to a file */
  int notifications_enabled;
  int vol_master;        /* 0..128 */
  int vol_music;         /* 0..128 */
  int vol_ambience;      /* 0..128 */
  int vol_notifications; /* 0..128 */
  /* Bells */
  char bell_phase_file[256];
  char bell_done_file[256];
  char meditation_start_bell_file[256];
  char meditation_interval_bell_file[256];
  char meditation_end_bell_file[256];
  /* Last-used picker values */
  int last_timer_h;
  int last_timer_m;
  int last_timer_s;
  int last_meditation_h;
  int last_meditation_m;
  int last_meditation_bell_min;
  int last_meditation_breaths;
  char last_meditation_guided_file[256];
  int last_pomo_session_min;
  int last_pomo_short_break_min;
  int last_pomo_long_break_min;
  int last_pomo_loops;
  /* Focus statistics (initial skeleton).
     Total focused seconds accrued via explicit user action ("end the focus").
     Break time is never counted. */
  uint64_t focus_total_seconds;
  uint64_t focus_total_sessions; /* count of awarded focus runs */
  uint64_t pomo_total_blocks;    /* completed pomodoro focus blocks */
  uint64_t pomo_focus_seconds;   /* sum of completed pomodoro focus blocks */
  uint64_t focus_longest_span_seconds; /* max awarded focus seconds in a run */
  /* Current focus label (shown as "focusing on <label>..."). */
  char focus_activity[64];
  /* Timer option: count up (stopwatch-like) instead of down.
     Persisted so the timer picker remembers the mode. */
  int timer_counting_up;
  /* Upper-right HUD stanza templates (Phase 1: template-driven renderer).
     Each line is a token template like: "<phase> <background> <location>".
     Supported tokens: <phase> <background> <phase> <location> <mood>
     Supported words: for and nor but or yet so
     Supported punctuation: . , ...
  */
  char hud_stanza1[256];
  char hud_stanza2[256];
  char hud_stanza3[256];
  /* Update source (optional). */
  char update_repo[128];             /* "owner/repo" for GitHub releases */
  char update_asset[128];            /* asset filename to download */
  char update_target_path[PATH_MAX]; /* optional: override apply target */
  char user_name[64];                /* Personalized greeting name */
} AppConfig;

typedef struct UI {
  SDL_Window *win;
  SDL_Renderer *ren;
  /* Base (regular) fonts. */
  TTF_Font *font_small;
  TTF_Font *font_med;
  TTF_Font *font_big;
  /* True italic companions (optional). If NULL, we fall back to SDL_ttf
   * faux-italic styling. */
  TTF_Font *font_small_i;
  TTF_Font *font_med_i;
  TTF_Font *font_big_i;
  SDL_Texture *bg_tex;
  SDL_Texture *bg_blur_tex;
  int w, h;
} UI;

typedef struct TextCache {
  SDL_Texture *tex;
  TTF_Font *font;
  int style;
  SDL_Color color;
  char text[256];
  int w;
  int h;
} TextCache;

typedef struct App {
  StrList scenes;
  StrList weathers;
  /* Mood folders within the selected location (scenes list). */
  StrList moods;
  /* Seasons (orthogonal to mood/vibe). */
  StrList seasons;
  int season_idx;
  int mood_idx;
  /* Base (non-variant) weather names for manual browsing.
     Variants like "morning(rain).png" are NOT shown to the user; they are only
     selected automatically when an ambience effect tag is active. */
  StrList weather_bases;
  int scene_idx;
  int weather_idx;
  int weather_base_idx;
  StrList fonts;
  int font_idx;
  char scene_name[128];
  char weather_name[128];
  char music_folder[128];
  char music_song[256];
  /* Persistent per-folder music selection (folder -> last track filename). */
  StrList music_last_folders;
  StrList music_last_tracks;
  /* Audio */
  AudioEngine *audio;
  StrList music_folders;
  int music_folder_idx;
  int music_sel; /* selection row in music settings */
  bool music_user_paused;
  bool music_has_started;
  /* Booklets (text pages loaded from ./booklets). */
  bool booklets_open;
  int booklets_mode;       /* 0: list, 1: viewer */
  StrList booklets_files;  /* filenames */
  StrList booklets_titles; /* parsed titles */
  int booklets_idx;
  int booklets_list_sel;
  int booklets_list_scroll;
  /* Viewer paging */
  int booklets_page;         /* current page (0-based) */
  int *booklets_page_starts; /* page -> starting render-line index */
  int booklets_page_count;
  int booklets_page_cap;
  /* Legacy scroll (viewer no longer uses this). */
  int booklets_scroll; /* top render-line index */
  /* Landing state: boot into a "chill" screen (background + music) without
     showing any timer/stopwatch/pomodoro state.
     While true:
     - Top-left shows the Stillroom branding/tagline (same as Settings/Menu)
     - Big time readout is hidden
     This flag is cleared as soon as the user chooses an actual mode.
  */
  bool landing_idle;
  /* True once the user explicitly chooses a mode
     (timer/stopwatch/pomodoro/tasks). Used as a belt-and-suspenders so the app
     never shows an unintended default mode label (e.g., "pomodoro") on cold
     start. */
  bool mode_ever_selected;

  int trig_l2_down;
  int trig_r2_down;
  uint64_t input_debounce_ms;
  struct {
    StrList tracks;
    int idx;
    bool active;
  } musicq;
  /* Ambience sounds (hardcoded list mapped to sounds/ambience/*.wav). */
  StrList ambience_sounds;
  int ambience_idx;
  bool ambience_user_started;
  /* Optional effect tag extracted from the selected ambience name (e.g., "rain"
   * from "... (rain)"). */
  char ambience_tag[64];
  /* Animated overlay (PNG frame sequence driven by ambience_tag) */
  int anim_overlay_enabled; /* mirrors cfg.animations, but may be forced off if
                               assets missing */
  SDL_Texture **anim_overlay_frames;
  int anim_overlay_frame_count;
  int anim_overlay_frame_idx;
  float anim_overlay_frame_delay_sec; /* seconds between PNG frames */
  float anim_overlay_accum;
  uint64_t anim_overlay_last_ms;
  /* UI scheduling / power:
     - ui_needs_redraw is set whenever something visible changes (input, timer
     tick, anim frame). The main loop skips rendering when nothing changed,
     letting the device truly idle.
  */
  bool ui_needs_redraw;
  /* Cached rendered text for frequently redrawn UI elements. */
  TextCache cache_big_time;
  TextCache cache_clock_min;
  TextCache cache_clock_hour;
  TextCache cache_batt_bottom;
  TextCache cache_batt_top;
  /* Sounds settings */
  int sounds_sel; /* 0=Music,1=Ambience,2=Notifications,3=Volume */
  int volume_sel; /* 0=Music,1=Ambience,2=Notifications */
  /* Volume mute toggles (A button). When muted, volume is set to 0, and the
   * previous value is restored on unmute. */
  bool vol_music_muted;
  bool vol_ambience_muted;
  bool vol_notif_muted;
  int vol_music_saved;
  int vol_ambience_saved;
  int vol_notif_saved;
  /* Bell sounds */
  StrList bell_sounds;
  int bell_sel; /* selection row in bell settings (0 phase, 1 done) */
  int bell_phase_idx;
  int bell_done_idx;
  int meditation_bell_start_idx;
  int meditation_bell_interval_idx;
  int meditation_bell_end_idx;
  /* History cache */
  FocusHistoryRow cached_history_rows[30];
  int cached_history_count;
  bool history_dirty;
  AppConfig cfg;
  Screen screen;
  RunMode mode;
  bool running;
  bool paused;
  bool hud_hidden;
  bool meta_selector_open;
  int meta_selector_sel; /* 0 season, 1 background (day/base), 2 location, 3
                            mood */
  /* Phase 2: stanza selector shell (inside meta selector). */
  bool stanza_selector_open;
  bool stanza_save_prompt_open;
  int stanza_save_sel; /* 0 app general, 1 location specific */
  int stanza_preset_sel;
  /* Phase 3: stanza puzzle editor (inside stanza selector). */
  int stanza_cursor_row;     /* 0-2: lines, 3: tags, 4: custom */
  int stanza_line_cursor[3]; /* insertion point per line: 0..len */
  int stanza_tray_col[4];    /* cursor column for trays: [0]=tags, [1]=custom */
  bool stanza_holding;
  int stanza_hold_piece; /* StanzaPiece */
  char stanza_hold_custom[64];
  int stanza_line_len[3];
  int stanza_line_pieces[3][24]; /* StanzaPiece ids */
  char stanza_line_custom[3][24][64];
  char stanza_work1[256];
  char stanza_work2[256];
  char stanza_work3[256];
  char stanza_orig1[256];
  char stanza_orig2[256];
  char stanza_orig3[256];
  bool stanza_custom_open;
  char stanza_custom_tag[64];
  char stanza_custom_buf[64];
  int stanza_custom_kb_row;
  int stanza_custom_kb_col;

  /* Per-location stanza overrides (location folder key -> stanza lines). */
  StrList stanza_loc_keys;
  StrList stanza_loc_1;
  StrList stanza_loc_2;
  StrList stanza_loc_3;
  bool session_complete;
  /* Focus tracking for the currently running session.
     - For Pomodoro, counts only focus phases (never breaks).
     - For Timer (count up/down), counts while running.
     Awarded into cfg.focus_total_seconds when the user explicitly ends the
     focus. */
  uint32_t run_focus_seconds;
  char run_focus_activity[64]; /* captured when starting a session */
  FocusStat focus_stats[MAX_FOCUS_STATS];
  int focus_stats_count;
  /* Focus label editor screen state */
  Screen focus_text_return_screen;
  char focus_edit_buf[64];
  int focus_kb_row;
  int focus_kb_col;
  /* Focus entry menu (opened with R3 from pickers). */
  Screen focus_menu_return_screen;
  int focus_menu_sel;
  bool focus_delete_confirm_open;
  int focus_delete_confirm_sel; /* 0 cancel, 1 remove */
  char focus_delete_name[64];
  /* Focus quick-pick (cycling through known activities). */
  int focus_pick_idx;        /* index into the current quick-pick list */
  bool focus_activity_dirty; /* cfg.focus_activity changed but not yet persisted
                              */
  /* Focus line highlight inside pickers (toggled with X). */
  bool focus_line_active;
  int focus_line_prev_sel;
  Screen focus_line_prev_screen;
  /* Statistics screen state. */
  int stats_section;        /* 0: focus, 1: habits */
  int stats_page;           /* section-specific page index */
  int stats_history_scroll; /* scroll offset for history list */
  int stats_list_scroll;    /* scroll offset for list-style stats */
  bool stats_return_to_settings;
  int settings_about_scroll; /* scroll offset for the About settings */
  int settings_about_sel;    /* selected row in About */
  int settings_changelog_scroll;
  /* UI help overlay (PNG shown while holding MENU). */
  bool help_overlay_open;
  bool help_overlay_missing; /* PNG file not found for this section */
  SDL_Texture *help_overlay_tex;
  char help_overlay_key[64];
  char help_overlay_path[PATH_MAX];
  bool menu_btn_down;
  uint64_t menu_btn_down_ms;
  bool hud_hide_btn_down;
  bool hud_hide_hold_triggered;
  uint64_t hud_hide_btn_down_ms;
  /* Custom timer runtime behavior.
     When active, custom_remaining_seconds counts elapsed seconds upward. */
  bool custom_counting_up_active;
  int breath_phase;
  uint32_t breath_phase_elapsed;
  uint32_t meditation_total_seconds;
  uint32_t meditation_remaining_seconds;
  uint32_t meditation_elapsed_seconds;
  uint32_t meditation_bell_interval_seconds;
  int meditation_run_kind; /* 0 timer, 1 guided meditation, 2 guided breathing
                            */
  int meditation_guided_repeats_total;
  int meditation_guided_repeats_remaining;
  int meditation_half_step_counter;
  int meditation_bell_strikes_remaining;
  float meditation_bell_strike_elapsed;
  char meditation_bell_strike_file[256];
  /* End focus flow (replaces reset).
     Opened while paused via the dedicated button (see handle_timer). */
  bool end_focus_confirm_open;
  int end_focus_confirm_sel; /* 0 cancel, 1 end */
  bool end_focus_summary_open;
  uint32_t end_focus_last_spent_seconds;
  int menu_sel;
  bool timer_menu_open;
  bool quit_requested;
  /* Navigation context for screens opened from the Timer quick menu (START).
     This prevents getting stuck in the root menu when backing out of pickers
     opened from the landing screen.
     - nav_from_timer_menu: current picker was opened from the Timer quick menu
     - nav_prev_landing_idle: landing_idle state at the moment we left the timer
     screen
  */
  bool nav_from_timer_menu;
  bool nav_prev_landing_idle;
  /* If a timer session is currently running, we allow jumping into other
     screens (Tasks/Pomo/Timer pick) and then returning without destroying the
     active session. This snapshot captures the active session state. */
  bool resume_valid;
  RunMode resume_mode;
  bool resume_running;
  bool resume_paused;
  bool resume_session_complete;
  bool resume_hud_hidden;
  bool resume_meta_selector_open;
  int resume_meta_selector_sel;
  uint32_t resume_run_focus_seconds;
  uint32_t resume_custom_total_seconds;
  uint32_t resume_custom_remaining_seconds;
  bool resume_custom_counting_up_active;
  uint32_t resume_meditation_total_seconds;
  uint32_t resume_meditation_remaining_seconds;
  uint32_t resume_meditation_elapsed_seconds;
  uint32_t resume_meditation_bell_interval_seconds;
  int resume_meditation_run_kind;
  int resume_meditation_guided_repeats_total;
  int resume_meditation_guided_repeats_remaining;
  int resume_meditation_half_step_counter;
  int resume_meditation_bell_strikes_remaining;
  float resume_meditation_bell_strike_elapsed;
  char resume_meditation_bell_strike_file[256];
  uint32_t resume_stopwatch_seconds;
  uint32_t resume_stopwatch_laps[MAX_STOPWATCH_LAPS];
  int resume_stopwatch_lap_count;
  /* Number of laps that have scrolled out of the visible ring buffer. */
  int resume_stopwatch_lap_base;
  int resume_breath_phase;
  uint32_t resume_breath_phase_elapsed;
  /* Pomodoro runtime state */
  int resume_pomo_loops_total;
  int resume_pomo_loops_done;
  bool resume_pomo_is_break;
  bool resume_pomo_break_is_long;
  uint32_t resume_pomo_remaining_seconds;
  int resume_pomo_session_in_pomo;
  uint32_t resume_pomo_session_seconds;
  uint32_t resume_pomo_break_seconds;
  uint32_t resume_pomo_long_break_seconds;
  uint64_t resume_last_tick_ms;
  float resume_tick_accum;
  /* Update system */
  UpdateStatus update_status;
  UpdateAction update_action;
  UpdateResult update_pending;
  bool update_pending_ready;
  SDL_Thread *update_thread;
  SDL_mutex *update_mutex;
  char update_latest_tag[64];
  char update_download_url[512];
  char update_message[256];
  char update_tmp_path[PATH_MAX];
  char update_exe_path[PATH_MAX];
  char update_apply_path[PATH_MAX];
  char update_download_asset[128];
  bool update_download_is_zip;
  uint64_t update_anim_last_ms;
  uint64_t update_last_check_ms;
  char update_latest_notes[2048];
  /* Optional releases */
  ReleaseStatus release_status;
  ReleaseResult release_pending;
  bool release_pending_ready;
  StrList release_tags;
  StrList release_urls;
  StrList release_notes;
  int release_sel;
  int release_scroll;
  bool release_popup_open;
  int release_popup_sel;
  char release_message[256];
  int pomo_pick_sel;
  int pick_pomo_session_min;
  int pick_pomo_break_min;
  int pick_pomo_long_break_min;
  int pick_pomo_loops;
  int meditation_pick_sel;
  int meditation_pick_view; /* 0 timer, 1 guided meditation, 2 guided breathing
                             */
  int pick_meditation_hours;
  int pick_meditation_minutes;
  int pick_meditation_bell_min;
  int pick_meditation_breaths;
  StrList meditation_guided_sounds;
  int meditation_guided_idx;
  uint32_t pomo_session_seconds;
  uint32_t pomo_break_seconds;
  uint32_t pomo_long_break_seconds;
  int pomo_session_in_pomo; /* 0=first session, 1=second session */
  bool pomo_break_is_long;
  int pick_custom_hours;   // 0..24
  int pick_custom_minutes; // 0..59
  int pick_custom_seconds; // 0..59
  int custom_field_sel;    // 0=hh, 1=mm, 2=ss
  int pomo_loops_total;
  int pomo_loops_done;
  bool pomo_is_break;
  uint32_t pomo_remaining_seconds;
  uint32_t custom_total_seconds;
  uint32_t custom_remaining_seconds;
  uint32_t stopwatch_seconds;
  /* Stopwatch lap snapshots (time since start at the moment of lap). */
  uint32_t stopwatch_laps[MAX_STOPWATCH_LAPS];
  int stopwatch_lap_count;
  /* When more than MAX_STOPWATCH_LAPS laps are taken, older laps scroll out.
     This base tracks how many laps have been scrolled out so ordinal labels
     remain correct. */
  int stopwatch_lap_base;
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
  /* Sounds */
  int notif_sel;
  int meditation_notif_sel;
  int misc_sel;
  /* Tasks */
  TasksState tasks;

  /* Booklets */
  BookletsState booklets;

  /* Quest (daily haiku) */
  StrList haiku_files;
  int haiku_daily_date;
  int haiku_daily_season_rank;
  char haiku_daily_number[64];
  QuestHaiku haiku_left;
  QuestHaiku haiku_right;
  char quest_poet_name[128];
  bool quest_from_timer_button;
  /* Routine: phase-based daily habits */
  RoutineState routine;
} App;

void config_save(const AppConfig *c, const char *path);
void config_load(AppConfig *c, const char *path);
void resume_restore(App *a);

// Time
uint64_t now_ms(void);

// App Control
void app_quit(App *a);
void app_reveal_hud(App *a);

#endif // APP_H
