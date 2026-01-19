#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../../app.h"
#include "../../ui/keyboard.h"
#include "../../ui/palette.h"
#include "../../ui/ui_shared.h"
#include "../../utils/string_utils.h"
#include "routines.h"

#define ROUTINE_DATA_PATH "states/routines.txt"
#define ROUTINE_HISTORY_ACTIONS_PATH "states/routine_history_actions.txt"
#define ROUTINE_HISTORY_TRIGGERS_PATH "states/routine_history_triggers.txt"

extern int ui_bottom_stack_top_y(UI *ui);

const char *routine_phase_name(int phase) {
  static const char *names[] = {"dawn", "morning", "afternoon", "evening",
                                "dusk", "night",   "all"};
  if (phase < 0 || phase > ROUTINE_PHASE_ALL)
    return "all";
  return names[phase];
}

static const char *routine_day_name(int day) {
  static const char *names[] = {"monday", "tuesday",  "wednesday", "thursday",
                                "friday", "saturday", "sunday"};
  if (day < 0 || day > 6)
    return "monday";
  return names[day];
}

static int routine_date_int(int day_offset) {
  /* Returns YYYYMMDD for today + day_offset (negative = past). */
  time_t now = time(NULL);
  now += day_offset * 86400;
  struct tm tmv;
  localtime_r(&now, &tmv);
  return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

int routine_today_weekday(void) {
  /* Returns 0=Mon..6=Sun */
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  return (tmv.tm_wday + 6) % 7; /* Convert Sun=0 to Mon=0 */
}

static int routine_count_filtered(App *a) {
  /* Count routines matching current day and phase filters. */
  if (!a || !a->routine.items || a->routine.items_n == 0)
    return 0;
  int day_bit = 1 << a->routine.sel_day;
  int count = 0;
  for (int i = 0; i < a->routine.items_n; i++) {
    RoutineItem *r = &a->routine.items[i];
    if (!(r->repeat_days & day_bit))
      continue;
    if (a->routine.sel_phase != ROUTINE_PHASE_ALL) {
      if (!((r->phases >> a->routine.sel_phase) & 1))
        continue;
    }
    count++;
  }
  return count;
}

static int routine_get_filtered_idx(App *a, int visible_idx) {
  /* Get the actual index in routine_items for a given visible index. */
  if (!a || !a->routine.items || a->routine.items_n == 0)
    return -1;
  int day_bit = 1 << a->routine.sel_day;
  int count = 0;
  for (int i = 0; i < a->routine.items_n; i++) {
    RoutineItem *r = &a->routine.items[i];
    if (!(r->repeat_days & day_bit))
      continue;
    if (a->routine.sel_phase != ROUTINE_PHASE_ALL) {
      if (!((r->phases >> a->routine.sel_phase) & 1))
        continue;
    }
    if (count == visible_idx)
      return i;
    count++;
  }
  return -1;
}

static bool routine_is_done(App *a, int routine_idx) {
  /* Check if routine at index is marked done for current selected day. */
  if (!a || routine_idx < 0 || routine_idx >= 64)
    return false;
  /* Calculate date for the selected day of the current week. */
  int today_wday = routine_today_weekday();
  int diff = a->routine.sel_day - today_wday;
  int target_date = routine_date_int(diff);
  for (int i = 0; i < a->routine.completions_n; i++) {
    if (a->routine.completions[i].date == target_date) {
      return (a->routine.completions[i].completed >> routine_idx) & 1;
    }
  }
  return false;
}

void routine_history_save(App *a) {
  if (!a)
    return;
  mkdir("states", 0777);
  FILE *f = fopen(ROUTINE_HISTORY_ACTIONS_PATH, "w");
  if (f) {
    for (int i = 0; i < a->routine.history_actions.count; i++) {
      fprintf(f, "%s\n", a->routine.history_actions.items[i]);
    }
    fclose(f);
  } else {
    /* panic_log not available here? It's in app.h but maybe not exposed as
       function? Actually panic_log is likely a macro or function. Let's check
       if we can use it. If not, just fprintf stderr. */
    fprintf(stderr, "Failed to open %s for writing\n",
            ROUTINE_HISTORY_ACTIONS_PATH);
  }
  f = fopen(ROUTINE_HISTORY_TRIGGERS_PATH, "w");
  if (f) {
    for (int i = 0; i < a->routine.history_triggers.count; i++) {
      fprintf(f, "%s\n", a->routine.history_triggers.items[i]);
    }
    fclose(f);
  }
}

static void routine_history_add(App *a, const char *action,
                                const char *trigger) {
  if (!a)
    return;
  if (action && action[0]) {
    if (sl_find(&a->routine.history_actions, action) < 0) {
      sl_push(&a->routine.history_actions, action);
    }
  }
  if (trigger && trigger[0]) {
    if (sl_find(&a->routine.history_triggers, trigger) < 0) {
      sl_push(&a->routine.history_triggers, trigger);
    }
  }
  routine_history_save(a);
}

const char *routine_history_action_def = "Focus";
const char *routine_history_trigger_def = "Wake up";

void routine_history_load(App *a) {
  if (!a)
    return;
  sl_clear(&a->routine.history_actions);
  sl_clear(&a->routine.history_triggers);

  FILE *f = fopen(ROUTINE_HISTORY_ACTIONS_PATH, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      trim_newline(line);
      if (line[0])
        sl_push(&a->routine.history_actions, line);
    }
    fclose(f);
  } else {
    /* Defaults */
    sl_push(&a->routine.history_actions, routine_history_action_def);
  }

  f = fopen(ROUTINE_HISTORY_TRIGGERS_PATH, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      trim_newline(line);
      if (line[0])
        sl_push(&a->routine.history_triggers, line);
    }
    fclose(f);
  } else {
    /* Defaults */
    sl_push(&a->routine.history_triggers, routine_history_trigger_def);
  }
}

static void routine_push_item(App *a, const char *action, const char *trigger,
                              int dur, int phases, int days, int color) {
  if (!a)
    return;
  if (a->routine.items_n >= a->routine.items_cap) {
    int newcap = a->routine.items_cap ? a->routine.items_cap * 2 : 8;
    RoutineItem *ni = (RoutineItem *)realloc(
        a->routine.items, (size_t)newcap * sizeof(RoutineItem));
    if (!ni)
      return;
    a->routine.items = ni;
    a->routine.items_cap = newcap;
  }
  RoutineItem *r = &a->routine.items[a->routine.items_n++];
  memset(r, 0, sizeof(*r));
  safe_snprintf(r->action, sizeof(r->action), "%s", action);
  safe_snprintf(r->trigger, sizeof(r->trigger), "%s", trigger);
  r->duration_min = dur;
  r->phases = phases;
  r->repeat_days = days;
  r->color_idx = color;
}

void routine_save_data(App *a) {
  if (!a || !a->routine.items)
    return;
  mkdir("states", 0777);
  FILE *f = fopen(ROUTINE_DATA_PATH, "w");
  if (!f)
    return;
  for (int i = 0; i < a->routine.items_n; i++) {
    RoutineItem *r = &a->routine.items[i];
    /* Format: action|trigger|duration|phases|days|color */
    fprintf(f, "%s|%s|%d|%d|%d|%d\n", r->action, r->trigger, r->duration_min,
            r->phases, r->repeat_days, r->color_idx);
  }
  fclose(f);
}

void routine_load(App *a) {
  routine_history_load(a);
  if (!a)
    return;
  FILE *f = fopen(ROUTINE_DATA_PATH, "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    /* strip newline */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = 0;

    char *p = line;
    char *next = strchr(p, '|');
    if (!next)
      continue;
    *next = 0;
    char action[64];
    safe_snprintf(action, sizeof(action), "%s", p);

    p = next + 1;
    next = strchr(p, '|');
    if (!next)
      continue;
    *next = 0;
    char trigger[64];
    safe_snprintf(trigger, sizeof(trigger), "%s", p);

    p = next + 1;
    int dur = atoi(p);

    next = strchr(p, '|');
    if (!next)
      continue;
    p = next + 1;
    int phases = atoi(p);

    next = strchr(p, '|');
    if (!next)
      continue;
    p = next + 1;
    int days = atoi(p);

    next = strchr(p, '|');
    if (!next)
      continue;
    p = next + 1;
    int color = atoi(p);

    routine_push_item(a, action, trigger, dur, phases, days, color);
  }
  fclose(f);
}

static void routine_remove_idx(App *a, int idx) {
  if (!a || idx < 0 || idx >= a->routine.items_n)
    return;
  for (int i = idx; i < a->routine.items_n - 1; i++) {
    a->routine.items[i] = a->routine.items[i + 1];
  }
  a->routine.items_n--;
  routine_save_data(a);
  if (a->routine.sel_row >= routine_count_filtered(a)) {
    a->routine.sel_row = routine_count_filtered(a) - 1;
    if (a->routine.sel_row < 0)
      a->routine.sel_row = 0;
  }
}

static void routine_add_item(App *a) {
  if (!a)
    return;
  routine_push_item(a, a->routine.edit_action, a->routine.edit_trigger,
                    a->routine.edit_duration, a->routine.edit_phases,
                    a->routine.edit_days, a->routine.edit_color);
  routine_save_data(a);
}

static const char *rt_val_1_19[] = {
    "zero",    "one",     "two",       "three",    "four",
    "five",    "six",     "seven",     "eight",    "nine",
    "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
    "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};

static const char *rt_val_tens[] = {"",       "",      "twenty", "thirty",
                                    "forty",  "fifty", "sixty",  "seventy",
                                    "eighty", "ninety"};

static void routine_num_to_words(char *out, int out_sz, int n) {
  if (n < 0)
    n = 0;
  out[0] = 0;

  if (n == 0) {
    safe_snprintf(out, out_sz, "zero");
    return;
  }

  if (n >= 1000) {
    safe_snprintf(out, out_sz, "%d", n); /* Fallback */
    return;
  }

  int hundreds = n / 100;
  int rem = n % 100;

  char buf[64] = {0};

  if (hundreds > 0) {
    safe_snprintf(buf, sizeof(buf), "%s hundred", rt_val_1_19[hundreds]);
    if (rem > 0) {
      /* Add " " separator */
      size_t l = strlen(buf);
      if (l < sizeof(buf) - 1) {
        buf[l] = ' ';
        buf[l + 1] = 0;
      }
    }
  }

  if (rem > 0) {
    char sub[64];
    if (rem < 20) {
      safe_snprintf(sub, sizeof(sub), "%s", rt_val_1_19[rem]);
    } else {
      int t = rem / 10;
      int o = rem % 10;
      if (o == 0)
        safe_snprintf(sub, sizeof(sub), "%s", rt_val_tens[t]);
      else
        safe_snprintf(sub, sizeof(sub), "%s %s", rt_val_tens[t],
                      rt_val_1_19[o]);
    }
    strncat(buf, sub, sizeof(buf) - strlen(buf) - 1);
  }

  safe_snprintf(out, out_sz, "%s", buf);
}

void draw_routine_list(UI *ui, App *a) {
  draw_top_hud(ui, a);
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Top-right stanza: day/date ONLY */
  {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    /* Calculate date for selected day of the week */
    int today_wday = routine_today_weekday();
    int diff = a->routine.sel_day - today_wday;
    time_t target = now + diff * 86400;
    struct tm ttm;
    localtime_r(&target, &ttm);
    static const char *months[] = {
        "january", "february", "march",     "april",   "may",      "june",
        "july",    "august",   "september", "october", "november", "december"};
    char line1[64];
    safe_snprintf(line1, sizeof(line1), "%s, %s %d",
                  routine_day_name(a->routine.sel_day), months[ttm.tm_mon],
                  ttm.tm_mday);
    int x = ui->w - UI_MARGIN_X - text_width(ui->font_small, line1);
    int y1 = UI_MARGIN_TOP;
    draw_text(ui, ui->font_small, x, y1, line1, main, false);

    /* Calculate total and completed duration for filtered routines */
    int total_min = 0;
    int done_min = 0;
    for (int i = 0; i < a->routine.items_n; i++) {
      RoutineItem *r = &a->routine.items[i];
      if (!((r->repeat_days >> a->routine.sel_day) & 1))
        continue;
      if (a->routine.sel_phase != ROUTINE_PHASE_ALL) {
        if (!((r->phases >> a->routine.sel_phase) & 1))
          continue;
      }
      total_min += r->duration_min;
      if (routine_is_done(a, i))
        done_min += r->duration_min;
    }

    /* Progress bar: full-width thin bar at bottom, drains as routines done */
    if (total_min > 0) {
      int bar_h = 6;
      int bar_y = ui->h - bar_h; /* flush with bottom edge */
      int remaining_min = total_min - done_min;
      float pct_remaining = (float)remaining_min / (float)total_min;
      int fill_w = (int)(pct_remaining * (float)ui->w);
      if (fill_w < 0)
        fill_w = 0;
      if (fill_w > ui->w)
        fill_w = ui->w;

      /* Draw fill (accent color, from left) */
      if (fill_w > 0) {
        SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ui->ren, accent.r, accent.g, accent.b, 255);
        SDL_Rect fill = {0, bar_y, fill_w, bar_h};
        SDL_RenderFillRect(ui->ren, &fill);
        SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
      }
    }

    /* Bottom stats: Remaining (Left) and Total (Right) duration.
     * Replaces standard Clock/Battery on this screen. */
    int yStackTop = ui_bottom_stack_top_y(ui);
    int remaining = total_min - done_min;
    if (remaining < 0)
      remaining = 0;

    int yMin = yStackTop;
    int yMain = yMin + TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;

    /* Left Stack: Remaining */
    if (remaining == 0 && done_min > 0) {
      /* "completed" state */
      /* Text is "completed" on bottom line? Or centered?
       * User request: "When remaining is zero... it says `completed`"
       * Assuming typical stack bottom-align. Top line empty?
       */
      draw_text(ui, ui->font_med, UI_MARGIN_X, yMain, "completed", main, false);
    } else {
      /* Pure duration: "thirty" (Top, accent italic), "minutes" (Bottom, main)
       */
      char rem_word[128];
      routine_num_to_words(rem_word, sizeof(rem_word), remaining);

      draw_text_style(ui, ui->font_small, UI_MARGIN_X, yMin, rem_word, accent,
                      false, TTF_STYLE_ITALIC);
      draw_text(ui, ui->font_med, UI_MARGIN_X, yMain, "minutes", main, false);
    }

    /* Right Stack: Total */
    {
      char tot_word[128];
      routine_num_to_words(tot_word, sizeof(tot_word), total_min);
      int xR = ui->w - UI_MARGIN_X;

      /* Align right. Measure both strings. */
      int w_top =
          text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, tot_word);
      int w_bot = text_width(ui->font_med, "minutes");

      /* Generally stacks align to the right edge (xR). */
      draw_text_style(ui, ui->font_small, xR - w_top, yMin, tot_word, accent,
                      false, TTF_STYLE_ITALIC);
      draw_text(ui, ui->font_med, xR - w_bot, yMain, "minutes", main, false);
    }
  }

  /* Header: Phase Name (Centered at BIG_TIMER_Y, No Arrows) */
  {
    int cx = ui->w / 2;
    int header_y = BIG_TIMER_Y;
    const char *phase_name = routine_phase_name(a->routine.sel_phase);
    char header_buf[128];

    if (a->routine.sel_phase == ROUTINE_PHASE_ALL) {
      safe_snprintf(header_buf, sizeof(header_buf), "any time");
    } else {
      safe_snprintf(header_buf, sizeof(header_buf), "%s", phase_name);
    }

    int w = text_width(ui->font_med, header_buf);
    draw_text(ui, ui->font_med, cx - w / 2, header_y, header_buf, main, false);
  }

  int n = routine_count_filtered(a);
  if (n == 0) {
    draw_text_centered_band(ui, ui->font_small, 1, "no routines", accent);
    draw_text_centered_band(ui, ui->font_small, 2, "(press r3 to add)", accent);
    const char *labsL[] = {"l2/r2:", "l1/r1:"};
    const char *actsL[] = {"day", "phase"};
    const char *labsR[] = {"b:", "r3:"};
    const char *actsR[] = {"back", "add"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 2);
    return;
  }

  /* Routine items - 2 lines per item (action + trigger) so fewer visible */
  int visible = 4;
  int start_row = 0;
  if (n > visible) {
    start_row = a->routine.sel_row - (visible / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible > n)
      start_row = n - visible;
  }

  int line_h = TTF_FontHeight(ui->font_small);
  int item_h = line_h * 2 + 8; /* action + trigger + gap */
  /* Header is at BIG_TIMER_Y. Gap to first item is 0.75x item_h (Reduced 50%)
   */
  int y_start = BIG_TIMER_Y + (int)(item_h * 0.75f);

  for (int i = 0; i < visible; i++) {
    int vis_idx = start_row + i;
    if (vis_idx >= n)
      break;
    int real_idx = routine_get_filtered_idx(a, vis_idx);
    if (real_idx < 0)
      continue;
    RoutineItem *r = &a->routine.items[real_idx];

    int y = y_start + i * item_h;
    bool selected = (vis_idx == a->routine.sel_row);

    /* Line 1: "I will <action>     <duration> min" */
    const char *prefix = "I will ";
    int pw = text_width(ui->font_small, prefix);
    int aw = text_width(ui->font_small, r->action);
    char dur[32];
    safe_snprintf(dur, sizeof(dur), "%d min", r->duration_min);
    int dw = text_width(ui->font_small, dur);

    int gap_w = text_width(ui->font_small, "     ") + 20;
    int total_w = pw + aw + gap_w + dw;
    int x = CIRCLE_LAYOUT_CX - total_w / 2;

    bool done = routine_is_done(a, real_idx);

    /* Colors */
    SDL_Color prefix_c = selected ? highlight : main;
    SDL_Color action_c = (r->color_idx >= 0) ? ui_color_from_idx(r->color_idx)
                                             : (selected ? highlight : accent);
    SDL_Color dur_c = selected ? highlight : main;
    SDL_Color sep_c = main;

    /* " for " separator */
    const char *for_str = " for ";
    int for_w = text_width(ui->font_small, for_str);
    draw_text(ui, ui->font_small, x, y, prefix, prefix_c, false);
    draw_text(ui, ui->font_small, x + pw, y, r->action, action_c, false);
    draw_text(ui, ui->font_small, x + pw + aw, y, for_str, sep_c, false);
    draw_text(ui, ui->font_small, x + pw + aw + for_w, y, dur, dur_c, false);

    if (done) {
      draw_strikethrough(ui, x, y, total_w, line_h,
                         (SDL_Color){255, 255, 255, 255});
    }

    /* Line 2: "after I <trigger>" - always show if trigger exists */
    if (r->trigger[0]) {
      char trig[128];
      safe_snprintf(trig, sizeof(trig), "after I %s", r->trigger);
      int tw = text_width(ui->font_small, trig);
      int tx = CIRCLE_LAYOUT_CX - tw / 2;

      SDL_Color trig_c = accent;

      draw_text_style(ui, ui->font_small, tx, y + line_h + 2, trig, trig_c,
                      false, TTF_STYLE_ITALIC);
      if (done) {
        draw_strikethrough(ui, tx, y + line_h + 2, tw, line_h,
                           (SDL_Color){255, 255, 255, 255});
      }
    }
  }

  /* Scroll indicator: dots on the right side */
  if (n > visible) {
    int dot_r = 4;
    int dot_gap = 12;
    int total_dots = n;
    int max_dots = 12; /* Limit to avoid clutter */
    if (total_dots > max_dots)
      total_dots = max_dots;
    int dots_h = total_dots * dot_gap;
    int dot_x = ui->w - UI_MARGIN_X - dot_r;
    int dot_y_start = y_start + (visible * item_h) / 2 - dots_h / 2;

    for (int i = 0; i < total_dots; i++) {
      int dy = dot_y_start + i * dot_gap;
      /* Map dot index to actual position in list */
      int mapped_idx;
      if (n <= max_dots) {
        mapped_idx = i;
      } else {
        /* Scale: first dot = 0, last dot = n-1 */
        mapped_idx = (i * (n - 1)) / (max_dots - 1);
      }
      bool is_current = (mapped_idx == a->routine.sel_row);
      SDL_Color dot_col = is_current ? accent : main;
      dot_col.a = is_current ? 255 : 80;
      /* Draw filled circle */
      SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(ui->ren, dot_col.r, dot_col.g, dot_col.b,
                             dot_col.a);
      SDL_Rect dot_rect = {dot_x - dot_r, dy - dot_r, dot_r * 2, dot_r * 2};
      SDL_RenderFillRect(ui->ren, &dot_rect);
      SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
    }
  }

  /* Delete confirmation popup */
  if (a->routine.delete_confirm_open) {
    draw_rect_fill(ui, 0, 0, ui->w, ui->h, (SDL_Color){0, 0, 0, 255});
    int cy = ui->h / 2;
    SDL_Color dim = {80, 80, 80, 255};
    const char *msg = "delete routine?";
    int w = text_width(ui->font_med, msg);
    draw_text(ui, ui->font_med, CIRCLE_LAYOUT_CX - w / 2, cy - 40, msg, main,
              false);

    int opt_y = cy + 20;
    int opt_gap = 100;

    SDL_Color c0 = (a->routine.delete_confirm_sel == 0) ? highlight : dim;
    SDL_Color c1 = (a->routine.delete_confirm_sel == 1) ? highlight : dim;

    const char *sNo = "no";
    const char *sYes = "yes";
    int wn = text_width(ui->font_med, sNo);
    int wy = text_width(ui->font_med, sYes);

    int cx_no = CIRCLE_LAYOUT_CX - opt_gap / 2;
    int cx_yes = CIRCLE_LAYOUT_CX + opt_gap / 2;

    draw_text(ui, ui->font_med, cx_no - wn / 2, opt_y, sNo, c0, false);
    draw_text(ui, ui->font_med, cx_yes - wy / 2, opt_y, sYes, c1, false);
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"nav", "mark"};
  const char *labsR[] = {"b:", "x:", "y:", "r3:"};
  const char *actsR[] = {"back", "reset", "color", "edit"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 4);
}

void handle_routine_list(App *a, Buttons *b) {
  if (!a || !b)
    return;

  /* Delete confirmation */
  if (a->routine.delete_confirm_open) {
    if (b->left)
      a->routine.delete_confirm_sel = 0;
    if (b->right)
      a->routine.delete_confirm_sel = 1;
    if (b->a) {
      if (a->routine.delete_confirm_sel == 1) {
        routine_remove_idx(a, a->routine.delete_idx);
      }
      a->routine.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b) {
      a->routine.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    a->ui_needs_redraw = true;
    return;
  }

  /* B: back */
  if (b->b) {
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
    return;
  }

  /* SELECT: toggle to grid view */
  if (b->select) {
    if (a->nav_from_timer_menu) {
      a->screen = SCREEN_TIMER;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    a->routine.grid_view = true;
    a->routine.grid_row = a->routine.sel_row;
    a->routine.grid_col = a->routine.sel_day;
    a->ui_needs_redraw = true;
    return;
  }

  /* Left/Right: cycle phases (Dawn...All) */
  if (b->left) {
    a->routine.sel_phase--;
    if (a->routine.sel_phase < 0)
      a->routine.sel_phase = ROUTINE_PHASE_COUNT; /* Wrap to All */
    a->routine.sel_row = 0;
    a->ui_needs_redraw = true;
  }
  if (b->right) {
    a->routine.sel_phase++;
    if (a->routine.sel_phase > ROUTINE_PHASE_COUNT)
      a->routine.sel_phase = 0;
    a->routine.sel_row = 0;
    a->ui_needs_redraw = true;
  }

  /* L2/R2: cycle days of the week */
  if (b->l2) {
    a->routine.sel_day = (a->routine.sel_day + 6) % 7; /* Previous day */
    a->routine.sel_row = 0;
    a->ui_needs_redraw = true;
  }
  if (b->r2) {
    a->routine.sel_day = (a->routine.sel_day + 1) % 7; /* Next day */
    a->routine.sel_row = 0;
    a->ui_needs_redraw = true;
  }

  int n = routine_count_filtered(a);
  if (n > 0) {
    /* Up/Down: navigate items */
    if (b->up) {
      a->routine.sel_row = (a->routine.sel_row + n - 1) % n;
      a->ui_needs_redraw = true;
    }
    if (b->down) {
      a->routine.sel_row = (a->routine.sel_row + 1) % n;
      a->ui_needs_redraw = true;
    }

    /* A: toggle completion */
    if (b->a) {
      int real_idx = routine_get_filtered_idx(a, a->routine.sel_row);
      if (real_idx >= 0 && real_idx < 64) {
        int today_wday = routine_today_weekday();
        int diff = a->routine.sel_day - today_wday;
        int target_date = routine_date_int(diff);
        /* Find or create completion entry */
        int found = -1;
        for (int i = 0; i < a->routine.completions_n; i++) {
          if (a->routine.completions[i].date == target_date) {
            found = i;
            break;
          }
        }
        if (found < 0) {
          /* Add new completion entry */
          if (a->routine.completions_n >= a->routine.completions_cap) {
            int newcap = a->routine.completions_cap
                             ? a->routine.completions_cap * 2
                             : 16;
            RoutineCompletion *nc = (RoutineCompletion *)realloc(
                a->routine.completions,
                (size_t)newcap * sizeof(RoutineCompletion));
            if (nc) {
              a->routine.completions = nc;
              a->routine.completions_cap = newcap;
            }
          }
          if (a->routine.completions_n < a->routine.completions_cap) {
            found = a->routine.completions_n++;
            a->routine.completions[found].date = target_date;
            a->routine.completions[found].completed = 0;
          }
        }
        if (found >= 0) {
          a->routine.completions[found].completed ^= (1ULL << real_idx);
          a->ui_needs_redraw = true;
          /* TODO: save routine data */
        }
      }
    }

    /* X: edit selected */
    if (b->x) {
      int real_idx = routine_get_filtered_idx(a, a->routine.sel_row);
      if (real_idx >= 0) {
        RoutineItem *r = &a->routine.items[real_idx];
        a->routine.edit_return = SCREEN_ROUTINE_LIST;
        safe_snprintf(a->routine.edit_action, sizeof(a->routine.edit_action),
                      "%s", r->action);
        safe_snprintf(a->routine.edit_trigger, sizeof(a->routine.edit_trigger),
                      "%s", r->trigger);
        a->routine.edit_duration = r->duration_min;
        a->routine.edit_phases = r->phases;
        a->routine.edit_days = r->repeat_days;
        a->routine.edit_color = r->color_idx;
        a->routine.edit_idx = real_idx;

        a->routine.edit_field = 0;
        a->routine.edit_active = false;
        a->screen = SCREEN_ROUTINE_EDIT;
        a->ui_needs_redraw = true;
        return;
      }
    }
    /* L3: delete selected */
    if (b->l3) {
      int real_idx = routine_get_filtered_idx(a, a->routine.sel_row);
      if (real_idx >= 0) {
        a->routine.delete_idx = real_idx;
        a->routine.delete_confirm_open = true;
        a->routine.delete_confirm_sel = 0;
        a->ui_needs_redraw = true;
        return;
      }
    }
  }

  /* R3: add new routine */
  if (b->r3) {
    a->routine.edit_return = SCREEN_ROUTINE_LIST;
    a->routine.edit_action[0] = 0;
    a->routine.edit_trigger[0] = 0;
    a->routine.edit_action[0] = 0;
    a->routine.edit_trigger[0] = 0;
    a->routine.edit_duration = 15;
    /* Defaults empty (user request) */
    a->routine.edit_days = 0;
    a->routine.edit_phases = 0;
    a->routine.edit_color = a->cfg.accent_color_idx;
    a->routine.edit_idx = -1; /* New item */
    a->routine.edit_field = 0;
    a->routine.edit_active = false;
    a->input_debounce_ms = now_ms();
    a->routine.kb_row = 0;
    a->routine.kb_col = 0;
    a->screen = SCREEN_ROUTINE_EDIT;
    a->ui_needs_redraw = true;
    return;
  }
}

void draw_routine_grid(UI *ui, App *a) {
  draw_top_hud(ui, a);
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Header: Phase Name */
  {
    int cx = ui->w / 2;
    int header_y = BIG_TIMER_Y;
    const char *phase_name = routine_phase_name(a->routine.sel_phase);
    char header_buf[128];
    if (a->routine.sel_phase == ROUTINE_PHASE_ALL) {
      safe_snprintf(header_buf, sizeof(header_buf), "any time");
    } else {
      safe_snprintf(header_buf, sizeof(header_buf), "%s", phase_name);
    }
    int w = text_width(ui->font_med, header_buf);
    draw_text(ui, ui->font_med, cx - w / 2, header_y, header_buf, main, false);
  }

  /* Build filtered routine list */
  int filtered[64];
  int n = 0;
  for (int i = 0; i < a->routine.items_n && n < 64; i++) {
    RoutineItem *r = &a->routine.items[i];
    /* Skip routines with no scheduled days */
    if (r->repeat_days == 0)
      continue;
    if (a->routine.sel_phase != ROUTINE_PHASE_ALL) {
      if (!((r->phases >> a->routine.sel_phase) & 1))
        continue;
    }
    filtered[n++] = i;
  }

  if (n == 0) {
    draw_text_centered_band(ui, ui->font_small, 1, "no routines", accent);
    draw_text_centered_band(ui, ui->font_small, 2, "(press r3 to add)", accent);
    const char *labsL[] = {"l1/r1:", "select:"};
    const char *actsL[] = {"phase", "list"};
    const char *labsR[] = {"b:", "r3:"};
    const char *actsR[] = {"back", "add"};
    draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 2);
    return;
  }

  /* Clamp grid selection */
  if (a->routine.grid_row >= n)
    a->routine.grid_row = n - 1;
  if (a->routine.grid_row < 0)
    a->routine.grid_row = 0;
  if (a->routine.grid_col < 0)
    a->routine.grid_col = 0;
  if (a->routine.grid_col > 6)
    a->routine.grid_col = 6;

  /* Layout constants - grid columns between x=250 and x=780 */
  int line_h = TTF_FontHeight(ui->font_small);
  int row_h = line_h + 12;
  int y_start = BIG_TIMER_Y + line_h + 50; /* Lowered by 20px */
  int grid_x_start = 250;
  int grid_x_end = 780;
  int grid_w = grid_x_end - grid_x_start;
  int cell_w = grid_w / 7;

  /* Day headers with dates above */
  static const char *day_short[] = {"M", "T", "W", "T", "F", "S", "S"};
  int today_wday = routine_today_weekday();
  for (int d = 0; d < 7; d++) {
    int cx = grid_x_start + d * cell_w + cell_w / 2;
    int tw = text_width(ui->font_small, day_short[d]);
    SDL_Color dc = (d == today_wday) ? highlight : main;

    /* Date number above the day letter */
    int diff = d - today_wday;
    int date_int = routine_date_int(diff);
    int day_of_month = date_int % 100;
    char date_str[8];
    safe_snprintf(date_str, sizeof(date_str), "%d", day_of_month);
    int dw = text_width(ui->font_small, date_str);
    draw_text(ui, ui->font_small, cx - dw / 2, y_start - line_h - 2, date_str,
              dc, false);

    /* Day letter */
    draw_text(ui, ui->font_small, cx - tw / 2, y_start, day_short[d], dc,
              false);
  }

  /* Grid rows */
  int visible_rows = 5;
  int scroll = 0;
  if (n > visible_rows) {
    scroll = a->routine.grid_row - visible_rows / 2;
    if (scroll < 0)
      scroll = 0;
    if (scroll + visible_rows > n)
      scroll = n - visible_rows;
  }

  for (int vi = 0; vi < visible_rows && scroll + vi < n; vi++) {
    int ri = scroll + vi;
    int real_idx = filtered[ri];
    RoutineItem *r = &a->routine.items[real_idx];
    int y = y_start + row_h + vi * row_h;
    bool row_selected = (ri == a->routine.grid_row);

    /* Action name (truncated) - positioned at left edge */
    char name[16];
    safe_snprintf(name, sizeof(name), "%.12s", r->action);
    SDL_Color name_col =
        (r->color_idx >= 0) ? ui_color_from_idx(r->color_idx) : accent;
    if (row_selected)
      name_col = highlight;
    int name_x = grid_x_start - text_width(ui->font_small, name) - 15;
    draw_text(ui, ui->font_small, name_x, y, name, name_col, false);

    /* Day cells */
    for (int d = 0; d < 7; d++) {
      int cx = grid_x_start + d * cell_w + cell_w / 2;
      int cy = y + line_h / 2;
      bool scheduled = (r->repeat_days >> d) & 1;
      bool cell_selected = row_selected && (d == a->routine.grid_col);

      if (!scheduled) {
        /* Not scheduled: draw dash */
        SDL_Color dash_col = {80, 80, 80, 255};
        int dash_w = 16;
        SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ui->ren, dash_col.r, dash_col.g, dash_col.b,
                               dash_col.a);
        SDL_Rect dash = {cx - dash_w / 2, cy - 1, dash_w, 2};
        SDL_RenderFillRect(ui->ren, &dash);
        SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
      } else {
        /* Scheduled: check completion */
        int diff = d - today_wday;
        int date = routine_date_int(diff);
        bool done = false;
        for (int ci = 0; ci < a->routine.completions_n; ci++) {
          if (a->routine.completions[ci].date == date) {
            done = (a->routine.completions[ci].completed >> real_idx) & 1;
            break;
          }
        }
        int circ_r = 12; /* Larger circles */
        if (done) {
          /* Filled circle: use routine's color */
          SDL_Color fill_col =
              (r->color_idx >= 0) ? ui_color_from_idx(r->color_idx) : accent;
          if (cell_selected)
            fill_col = highlight;

          SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(ui->ren, fill_col.r, fill_col.g, fill_col.b,
                                 fill_col.a);
          SDL_Rect circle_rect = {cx - circ_r, cy - circ_r, circ_r * 2,
                                  circ_r * 2};
          SDL_RenderFillRect(ui->ren, &circle_rect);
          SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
        } else {
          /* Ring (outline) */
          SDL_Color ring_col = cell_selected ? highlight : accent;
          SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(ui->ren, ring_col.r, ring_col.g, ring_col.b,
                                 ring_col.a);
          SDL_Rect ring_rect = {cx - circ_r, cy - circ_r, circ_r * 2,
                                circ_r * 2};
          SDL_RenderDrawRect(ui->ren, &ring_rect);
          SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
        }
      }
    }
  }

  /* Progress bar at bottom */
  int total_min = 0, done_min = 0;
  for (int i = 0; i < n; i++) {
    int real_idx = filtered[i];
    RoutineItem *r = &a->routine.items[real_idx];
    /* Sum for all days in the week */
    for (int d = 0; d < 7; d++) {
      if (!((r->repeat_days >> d) & 1))
        continue;
      total_min += r->duration_min;
      int diff = d - today_wday;
      int date = routine_date_int(diff);
      for (int ci = 0; ci < a->routine.completions_n; ci++) {
        if (a->routine.completions[ci].date == date) {
          if ((a->routine.completions[ci].completed >> real_idx) & 1)
            done_min += r->duration_min;
          break;
        }
      }
    }
  }
  if (total_min > 0) {
    int bar_h = 6;
    int bar_y = ui->h - bar_h;
    int remaining_min = total_min - done_min;
    float pct_remaining = (float)remaining_min / (float)total_min;
    int fill_w = (int)(pct_remaining * (float)ui->w);
    if (fill_w > 0) {
      SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(ui->ren, accent.r, accent.g, accent.b, 255);
      SDL_Rect fill = {0, bar_y, fill_w, bar_h};
      SDL_RenderFillRect(ui->ren, &fill);
      SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);
    }
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"nav", "mark"};
  const char *labsR[] = {"b:", "select:", "r3:"};
  const char *actsR[] = {"back", "list", "add"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 3);
}

void handle_routine_grid(App *a, Buttons *b) {
  if (!a || !b)
    return;

  /* B: back */
  if (b->b) {
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
    return;
  }

  /* SELECT: toggle to list view */
  if (b->select) {
    if (a->nav_from_timer_menu) {
      a->screen = SCREEN_TIMER;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      return;
    }
    a->routine.grid_view = false;
    a->routine.sel_row = a->routine.grid_row;
    a->ui_needs_redraw = true;
    return;
  }

  /* L1/R1: cycle phases */
  if (b->l1) {
    a->routine.sel_phase--;
    if (a->routine.sel_phase < 0)
      a->routine.sel_phase = ROUTINE_PHASE_COUNT;
    a->routine.grid_row = 0;
    a->ui_needs_redraw = true;
  }
  if (b->r1) {
    a->routine.sel_phase++;
    if (a->routine.sel_phase > ROUTINE_PHASE_COUNT)
      a->routine.sel_phase = 0;
    a->routine.grid_row = 0;
    a->ui_needs_redraw = true;
  }

  /* Build filtered list for bounds checking */
  int filtered[64];
  int n = 0;
  for (int i = 0; i < a->routine.items_n && n < 64; i++) {
    RoutineItem *r = &a->routine.items[i];
    /* Skip routines with no scheduled days */
    if (r->repeat_days == 0)
      continue;
    if (a->routine.sel_phase != ROUTINE_PHASE_ALL) {
      if (!((r->phases >> a->routine.sel_phase) & 1))
        continue;
    }
    filtered[n++] = i;
  }
  if (n == 0)
    return;

  /* Navigation */
  if (b->up) {
    a->routine.grid_row = (a->routine.grid_row + n - 1) % n;
    a->ui_needs_redraw = true;
  }
  if (b->down) {
    a->routine.grid_row = (a->routine.grid_row + 1) % n;
    a->ui_needs_redraw = true;
  }

  /* Left/Right: skip non-scheduled (dash) cells */
  if (b->left) {
    RoutineItem *r = &a->routine.items[filtered[a->routine.grid_row]];
    int start_col = a->routine.grid_col;
    for (int i = 0; i < 7; i++) {
      a->routine.grid_col = (a->routine.grid_col + 6) % 7; /* Move left */
      if ((r->repeat_days >> a->routine.grid_col) & 1)
        break; /* Found a scheduled cell */
      if (a->routine.grid_col == start_col)
        break; /* Wrapped around, no scheduled cells */
    }
    a->ui_needs_redraw = true;
  }
  if (b->right) {
    RoutineItem *r = &a->routine.items[filtered[a->routine.grid_row]];
    int start_col = a->routine.grid_col;
    for (int i = 0; i < 7; i++) {
      a->routine.grid_col = (a->routine.grid_col + 1) % 7; /* Move right */
      if ((r->repeat_days >> a->routine.grid_col) & 1)
        break; /* Found a scheduled cell */
      if (a->routine.grid_col == start_col)
        break; /* Wrapped around, no scheduled cells */
    }
    a->ui_needs_redraw = true;
  }

  /* A: toggle completion */
  if (b->a) {
    if (a->routine.grid_row >= 0 && a->routine.grid_row < n) {
      int real_idx = filtered[a->routine.grid_row];
      RoutineItem *r = &a->routine.items[real_idx];
      int d = a->routine.grid_col;
      /* Only toggle if scheduled */
      if ((r->repeat_days >> d) & 1) {
        int today_wday = routine_today_weekday();
        int diff = d - today_wday;
        int date = routine_date_int(diff);
        /* Find or create completion entry */
        int found = -1;
        for (int ci = 0; ci < a->routine.completions_n; ci++) {
          if (a->routine.completions[ci].date == date) {
            found = ci;
            break;
          }
        }
        if (found < 0) {
          /* Add completion entry */
          if (a->routine.completions_n >= a->routine.completions_cap) {
            int newcap = a->routine.completions_cap
                             ? a->routine.completions_cap * 2
                             : 16;
            RoutineCompletion *nc = (RoutineCompletion *)realloc(
                a->routine.completions,
                (size_t)newcap * sizeof(RoutineCompletion));
            if (nc) {
              a->routine.completions = nc;
              a->routine.completions_cap = newcap;
            }
          }
          if (a->routine.completions_n < a->routine.completions_cap) {
            found = a->routine.completions_n++;
            a->routine.completions[found].date = date;
            a->routine.completions[found].completed = 0;
          }
        }
        if (found >= 0) {
          a->routine.completions[found].completed ^= (1ULL << real_idx);
          a->ui_needs_redraw = true;
        }
      }
    }
  }
}

static void routine_draw_text_centered(UI *ui, TTF_Font *font, int cx, int y,
                                       const char *text, SDL_Color col) {
  int w = text_width(font, text);
  draw_text(ui, font, cx - w / 2, y, text, col, false);
}

void draw_routine_edit(UI *ui, App *a) {
  draw_top_hud(ui, a);
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);
  SDL_Color dim = {105, 105, 105, 255};

  int cx = ui->w / 2;
  int y_start = BIG_TIMER_Y + 20;
  int row_h = TTF_FontHeight(ui->font_small) + 32;
  int tag_gap = 40;
  int y = y_start;

  /* Field 0: Action */
  {
    SDL_Color lbl_col = main;
    SDL_Color val_col = (a->routine.edit_field == 0) ? highlight : accent;
    const char *label = "i will ";
    char val[128];
    safe_snprintf(val, sizeof(val), "%s%s",
                  a->routine.edit_action[0] ? a->routine.edit_action : "...",
                  (a->routine.edit_field == 0 && a->routine.edit_active) ? "|"
                                                                         : "");
    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, val);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, val, val_col, false);
    y += row_h;
  }

  /* Field 1: Trigger */
  {
    SDL_Color lbl_col = main;
    SDL_Color val_col = (a->routine.edit_field == 1) ? highlight : accent;
    const char *label = "after I ";
    char val[128];
    safe_snprintf(val, sizeof(val), "%s%s",
                  a->routine.edit_trigger[0] ? a->routine.edit_trigger : "...",
                  (a->routine.edit_field == 1 && a->routine.edit_active) ? "|"
                                                                         : "");
    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, val);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, val, val_col, false);
    y += row_h;
  }

  /* Field 2: Duration */
  {
    SDL_Color lbl_col = main;
    SDL_Color val_col = (a->routine.edit_field == 2) ? highlight : accent;
    const char *label = "for ";
    char val[64];
    safe_snprintf(val, sizeof(val), "%d min", a->routine.edit_duration);
    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, val);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, val, val_col, false);
    if (a->routine.edit_field == 2) {
      draw_text(ui, ui->font_small, start_x - 30, y + 4, "<", main, true);
      draw_text(ui, ui->font_small, start_x + w1 + w2 + 30, y + 4, ">", main,
                false);
    }
    y += row_h;
  }

  /* Field 3: Phases */
  {
    SDL_Color lbl_col = main;
    SDL_Color val_col = (a->routine.edit_field == 3) ? highlight : accent;
    char val[256];
    val[0] = 0;
    if (a->routine.edit_phases == 0) {
      strcat(val, "...");
    } else {
      bool first = true;
      for (int i = 0; i < 6; i++) {
        if ((a->routine.edit_phases >> i) & 1) {
          if (!first)
            strcat(val, ", ");
          strcat(val, routine_phase_name(i));
          first = false;
        }
      }
    }
    const char *label = "during ";
    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, val);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, val, val_col, false);

    y += 60; /* Gap for tags */
    static const char *phase_tags[] = {"D", "M", "A", "E", "D", "N"};
    int total_w = 6 * tag_gap;
    int tx = cx - total_w / 2 + tag_gap / 2;
    for (int i = 0; i < 6; i++) {
      bool on = (a->routine.edit_phases >> i) & 1;
      bool focused = (a->routine.edit_field == 3 && a->routine.kb_col == i);
      SDL_Color tcol = on ? highlight : dim;
      if (focused)
        tcol = main;
      routine_draw_text_centered(ui, ui->font_small, tx, y, phase_tags[i],
                                 tcol);
      if (focused)
        draw_rect_fill(ui, tx - 10, y + 24, 20, 2, main);
      tx += tag_gap;
    }
    if (a->routine.edit_field == 3) {
      draw_text(ui, ui->font_small, cx - total_w / 2 - 30, y, "<", main, true);
      draw_text(ui, ui->font_small, cx + total_w / 2 + 30, y, ">", main, false);
    }
    y += 60; /* Extra gap after tags */
  }

  /* Field 4: Days */
  {
    SDL_Color lbl_col = main;
    SDL_Color val_col = (a->routine.edit_field == 4) ? highlight : accent;
    char val[256];
    val[0] = 0;
    if (a->routine.edit_days == 0)
      strcat(val, "never");
    else if (a->routine.edit_days == 127)
      strcat(val, "every day");
    else {
      static const char *day_names[] = {"Mon", "Tue", "Wed", "Thu",
                                        "Fri", "Sat", "Sun"};
      bool first = true;
      for (int i = 0; i < 7; i++) {
        if ((a->routine.edit_days >> i) & 1) {
          if (!first)
            strcat(val, ", ");
          strcat(val, day_names[i]);
          first = false;
        }
      }
    }
    const char *label = "on ";
    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, val);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, val, val_col, false);

    y += 60;
    static const char *day_tags[] = {"M", "T", "W", "T", "F", "S", "S"};
    int total_w = 7 * tag_gap;
    int tx = cx - total_w / 2 + tag_gap / 2;
    for (int i = 0; i < 7; i++) {
      bool on = (a->routine.edit_days >> i) & 1;
      bool focused = (a->routine.edit_field == 4 && a->routine.kb_col == i);
      SDL_Color tcol = on ? highlight : dim;
      if (focused)
        tcol = main;
      routine_draw_text_centered(ui, ui->font_small, tx, y, day_tags[i], tcol);
      if (focused)
        draw_rect_fill(ui, tx - 10, y + 24, 20, 2, main);
      tx += tag_gap;
    }
    if (a->routine.edit_field == 4) {
      draw_text(ui, ui->font_small, cx - total_w / 2 - 30, y, "<", main, true);
      draw_text(ui, ui->font_small, cx + total_w / 2 + 30, y, ">", main, false);
    }
    y += 60;
  }

  /* Field 5: Color */
  {
    SDL_Color lbl_col = main;
    const char *label = "color: ";
    int idx = a->routine.edit_color;
    if (idx < 0)
      idx = 0;
    if (idx >= PALETTE_SIZE)
      idx = 0;
    const char *cname = PALETTE[idx].name;
    SDL_Color val_col = ui_color_from_idx(idx);

    int w1 = text_width(ui->font_small, label);
    int w2 = text_width(ui->font_small, cname);
    int start_x = cx - (w1 + w2) / 2;
    draw_text(ui, ui->font_small, start_x, y, label, lbl_col, false);
    draw_text(ui, ui->font_small, start_x + w1, y, cname, val_col, false);

    if (a->routine.edit_field == 5) {
      draw_text(ui, ui->font_small, start_x - 30, y + 4, "<", main, true);
      draw_text(ui, ui->font_small, start_x + w1 + w2 + 30, y + 4, ">", main,
                false);
    }
  }

  /* Keyboard overlay */
  if ((a->routine.edit_field == 0 || a->routine.edit_field == 1) &&
      a->routine.edit_active) {
    int cx = ui->w / 2;
    int key_h = TTF_FontHeight(ui->font_small) + 16;
    int total_kb_h = KEYBOARD_ROW_COUNT * key_h;
    int yKb = (ui->h - total_kb_h) / 2;

    /* Draw background for overlay */
    draw_rect_fill(ui, 0, 0, ui->w, ui->h,
                   (SDL_Color){0, 0, 0, 255}); // Pitch black background

    /* Re-draw header/input for context */
    // ... For now just draw keyboard over it or rely on existing screen
    // content? The user asked for "Pitch black" background for routines. If
    // this is an overlay, we should probably clear the screen.

    keyboard_draw(ui, cx, yKb, a->routine.kb_row, a->routine.kb_col, accent,
                  highlight);
  }
}

void handle_routine_edit(App *a, Buttons *b) {
  if (!a || !b)
    return;

  /* Debounce R3 to prevent immediate exit when entering from list */
  if (b->r3 && (now_ms() - a->input_debounce_ms < 250)) {
    b->r3 = false;
  }

  /* If active, handle field-specific input */
  if (a->routine.edit_active) {
    if (a->routine.edit_field == 0 || a->routine.edit_field == 1) {
      /* Text input */
      char *buf = (a->routine.edit_field == 0) ? a->routine.edit_action
                                               : a->routine.edit_trigger;
      size_t cap = (a->routine.edit_field == 0)
                       ? sizeof(a->routine.edit_action)
                       : sizeof(a->routine.edit_trigger);

      if (b->b) {
        a->routine.edit_active = false;
        a->ui_needs_redraw = true;
        return;
      }
      /* Use shared Stanza keyboard logic */
      bool modified = keyboard_update(a, b, buf, cap, &a->routine.kb_row,
                                      &a->routine.kb_col);
      if (modified) {
        a->ui_needs_redraw = true;
      }
      if (b->r3) {
        a->routine.edit_active = false; /* Done editing text */
        a->ui_needs_redraw = true;
        return;
      }
      return;
    }
    /* Duration (2), Phases (3), Days (4), Color (5) Logic moved to
     * "Navigation" scope for inline editing */
  }

  /* Navigation mode (and inline editing for non-text fields) */
  if (b->b) {
    a->screen = a->routine.edit_return;
    a->ui_needs_redraw = true;
    return;
  }

  if (b->up) {
    a->routine.edit_field = (a->routine.edit_field + 5) % 6;
    /* Reset sub-cols when changing rows for cleaner state */
    a->routine.kb_col = 0;
    a->ui_needs_redraw = true;
  }
  if (b->down) {
    a->routine.edit_field = (a->routine.edit_field + 1) % 6;
    a->routine.kb_col = 0;
    a->ui_needs_redraw = true;
  }

  /* Field 2 (Duration) - immediate adjust */
  if (a->routine.edit_field == 2) {
    if (b->left) {
      a->routine.edit_duration -= 5;
      if (a->routine.edit_duration < 5)
        a->routine.edit_duration = 5;
      a->ui_needs_redraw = true;
    }
    if (b->right) {
      a->routine.edit_duration += 5;
      if (a->routine.edit_duration > 180)
        a->routine.edit_duration = 180;
      a->ui_needs_redraw = true;
    }
  }
  /* Field 3: Phases (Inline) */
  else if (a->routine.edit_field == 3) {
    if (b->left) {
      a->routine.kb_col = (a->routine.kb_col + 5) % 6;
      a->ui_needs_redraw = true;
    }
    if (b->right) {
      a->routine.kb_col = (a->routine.kb_col + 1) % 6;
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      a->routine.edit_phases ^= (1 << a->routine.kb_col);
      a->ui_needs_redraw = true;
    }
  }
  /* Field 4: Days (Inline) */
  else if (a->routine.edit_field == 4) {
    if (b->left) {
      a->routine.kb_col = (a->routine.kb_col + 6) % 7;
      a->ui_needs_redraw = true;
    }
    if (b->right) {
      a->routine.kb_col = (a->routine.kb_col + 1) % 7;
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      a->routine.edit_days ^= (1 << a->routine.kb_col);
      a->ui_needs_redraw = true;
    }
  }
  /* Field 5: Color (Inline) */
  else if (a->routine.edit_field == 5) {
    if (b->left) {
      a->routine.edit_color--;
      if (a->routine.edit_color < 0)
        a->routine.edit_color = 31; // Max palette index roughly
      a->ui_needs_redraw = true;
    }
    if (b->right) {
      a->routine.edit_color = (a->routine.edit_color + 1) % PALETTE_SIZE;
      a->ui_needs_redraw = true;
    }
  }
  /* Field 0/1: Action/Trigger -> Launch Picker */
  else if (a->routine.edit_field == 0 || a->routine.edit_field == 1) {
    if (b->a) {
      a->routine.entry_picker_target = (a->routine.edit_field == 0) ? 0 : 1;
      a->screen = SCREEN_ROUTINE_ENTRY_PICKER;
      a->routine.entry_picker_sel = 0;
      a->ui_needs_redraw = true;
    }
  }

  /* Save & Add/Update */
  if (b->r3) {
    if (a->routine.edit_action[0]) {
      /* Add to history */
      routine_history_add(a, a->routine.edit_action, a->routine.edit_trigger);

      if (a->routine.edit_idx < 0) {
        /* New item */
        routine_add_item(a);
      } else {
        /* Update existing item */
        if (a->routine.edit_idx < a->routine.items_n) {
          RoutineItem *r = &a->routine.items[a->routine.edit_idx];
          safe_snprintf(r->action, sizeof(r->action), "%s",
                        a->routine.edit_action);
          safe_snprintf(r->trigger, sizeof(r->trigger), "%s",
                        a->routine.edit_trigger);
          r->duration_min = a->routine.edit_duration;
          r->phases = a->routine.edit_phases;
          r->repeat_days = a->routine.edit_days;
          r->color_idx = a->routine.edit_color;
          routine_save_data(a);
        }
      }
    }
    a->screen = a->routine.edit_return;
    a->ui_needs_redraw = true;
    return;
  }
}

void draw_routine_entry_picker(UI *ui, App *a) {
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Header Line */
  {
    const char *header = "entry";
    int w_header = text_width(ui->font_med, header);
    draw_text(ui, ui->font_med, cx - w_header / 2, header_y, header, main,
              false);
  }

  /* Identify items source */
  StrList *list = (a->routine.entry_picker_target == 0)
                      ? &a->routine.history_actions
                      : &a->routine.history_triggers;

  /* Build display list: history items + "create a new entry" */
  int n_items = list->count;
  int total_rows = n_items + 1;

  if (a->routine.entry_picker_sel < 0)
    a->routine.entry_picker_sel = 0;
  if (a->routine.entry_picker_sel >= total_rows)
    a->routine.entry_picker_sel = total_rows - 1;

  int y0 = header_y + 64;
  int row_h = UI_ROW_GAP + 8;
  int visible = 8;
  int top = a->routine.entry_picker_sel - (visible / 2);
  if (top < 0)
    top = 0;
  if (top > total_rows - visible)
    top = total_rows - visible;
  if (top < 0)
    top = 0;

  for (int i = 0; i < visible; i++) {
    int idx = top + i;
    if (idx >= total_rows)
      break;
    int y = y0 + i * row_h;
    SDL_Color c = (idx == a->routine.entry_picker_sel) ? highlight : accent;

    if (idx < n_items) {
      /* History Item */
      const char *txt = list->items[idx];
      int w = text_width(ui->font_small, txt);
      draw_text(ui, ui->font_small, cx - w / 2, y, txt, c, false);
    } else {
      /* Create new entry */
      const char *label = "create a new entry";
      int w = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, label);
      draw_text_style(ui, ui->font_small, cx - w / 2, y, label, c, false,
                      TTF_STYLE_ITALIC);
    }
  }

  /* Hints */
  const char *labsL[] = {"a:", "l3:"};
  const char *actsL[] = {"select", "delete"};
  const char *labsR[] = {"b:"};
  const char *actsR[] = {"back"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 1);

  if (a->routine.delete_confirm_open) {
    /* Dim background/Overlay */
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_Rect r = {0, 0, ui->w, ui->h};
    SDL_RenderFillRect(ui->ren, &r);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);

    int my = BIG_TIMER_Y;
    int cx = ui->w / 2;
    const char *title = "remove entry";
    draw_text(ui, ui->font_med, cx - text_width(ui->font_med, title) / 2, my,
              title, main, false);

    char msg[160];
    const char *itm = (a->routine.entry_picker_sel < list->count)
                          ? list->items[a->routine.entry_picker_sel]
                          : "???";
    safe_snprintf(msg, sizeof(msg), "remove \"%s\" from the list?", itm);
    draw_text(ui, ui->font_small, cx - text_width(ui->font_small, msg) / 2,
              my + 64, msg, accent, false);

    const char *opt0 = "cancel";
    const char *opt1 = "remove";
    SDL_Color c0 = (a->routine.delete_confirm_sel == 0) ? highlight : accent;
    SDL_Color c1 = (a->routine.delete_confirm_sel == 1) ? highlight : accent;

    int oy = my + 128;
    int w0 = text_width(ui->font_small, opt0);
    int w1 = text_width(ui->font_small, opt1);
    int gap = 40;
    int total_opt_w = w0 + gap + w1;
    int start_opt_x = cx - total_opt_w / 2;

    draw_text(ui, ui->font_small, start_opt_x, oy, opt0, c0, false);
    draw_text(ui, ui->font_small, start_opt_x + w0 + gap, oy, opt1, c1, false);

    /* No hints needed for this modal, or standard A:select B:cancel */
  }
}

void draw_routine_entry_text(UI *ui, App *a) {
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Pitch black background */
  draw_rect_fill(ui, 0, 0, ui->w, ui->h, (SDL_Color){0, 0, 0, 255});

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Centered header */
  const char *hdr =
      (a->routine.entry_picker_target == 0) ? "action" : "trigger";
  int w_hdr = text_width(ui->font_med, hdr);
  draw_text(ui, ui->font_med, cx - w_hdr / 2, header_y, hdr, main, false);

  /* Centered input field */
  int y = header_y + UI_ROW_GAP + 10;
  char *buf = (a->routine.entry_picker_target == 0) ? a->routine.edit_action
                                                    : a->routine.edit_trigger;
  const char *disp = buf;
  /* Width calculation - use empty string width if buf is empty to center
   * cursor? YES. */
  int w_input = text_width(ui->font_med, disp[0] ? disp : "(empty)");

  draw_text_input_with_cursor(ui, ui->font_med, cx - w_input / 2, y, buf,
                              "(empty)", highlight, accent, highlight, 0);

  /* Centered keyboard rows */
  int yKb = y + TTF_FontHeight(ui->font_med) + 26;
  keyboard_draw(ui, cx, yKb, a->routine.kb_row, a->routine.kb_col, accent,
                highlight);

  const char *labsL[] = {"b:", "x:", "y:", "a:"};
  const char *actsL[] = {"cancel", "backspace", "space", "add"};
  const char *labsR[] = {"r3:"};
  const char *actsR[] = {"done"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 4, labsR, actsR, 1);
}

void handle_routine_entry_text(App *a, Buttons *b) {
  if (!a || !b)
    return;

  /* Force debounce off to ensure responsiveness */
  a->input_debounce_ms = 0;

  /* Validate target */
  if (a->routine.entry_picker_target != 1)
    a->routine.entry_picker_target = 0;

  char *buf = (a->routine.entry_picker_target == 0) ? a->routine.edit_action
                                                    : a->routine.edit_trigger;
  size_t cap = (a->routine.entry_picker_target == 0)
                   ? sizeof(a->routine.edit_action)
                   : sizeof(a->routine.edit_trigger);

  /* Always allow exit */
  if (b->b) {
    a->screen = SCREEN_ROUTINE_ENTRY_PICKER;
    a->ui_needs_redraw = true;
    return;
  }

  bool input_handled = false;

  /* Use shared Stanza keyboard logic */
  bool modified =
      keyboard_update(a, b, buf, cap, &a->routine.kb_row, &a->routine.kb_col);
  if (modified) {
    a->ui_needs_redraw = true;
  }

  /* Save */
  if (b->r3) {
    trim_ascii_inplace(buf);
    if (buf[0]) {
      a->routine.edit_active = false;
      a->screen = SCREEN_ROUTINE_EDIT;
    }
    input_handled = true;
  }

  if (input_handled) {
    a->ui_needs_redraw = true;
  }
}

void handle_routine_entry_picker(App *a, Buttons *b) {
  if (!a || !b)
    return;

  StrList *list = (a->routine.entry_picker_target == 0)
                      ? &a->routine.history_actions
                      : &a->routine.history_triggers;
  int n_items = list->count;
  int total_rows = n_items + 1;

  if (b->up) {
    a->routine.entry_picker_sel--;
    if (a->routine.entry_picker_sel < 0)
      a->routine.entry_picker_sel = total_rows - 1;
    a->ui_needs_redraw = true;
  }
  if (b->down) {
    a->routine.entry_picker_sel++;
    if (a->routine.entry_picker_sel >= total_rows)
      a->routine.entry_picker_sel = 0;
    a->ui_needs_redraw = true;
  }

  if (a->routine.delete_confirm_open) {
    if (b->left) {
      a->routine.delete_confirm_sel = 0; /* Cancel */
      a->ui_needs_redraw = true;
    }
    if (b->right) {
      a->routine.delete_confirm_sel = 1; /* Remove */
      a->ui_needs_redraw = true;
    }
    if (b->a) {
      if (a->routine.delete_confirm_sel == 1) {
        /* Confirm Remove */
        if (a->routine.entry_picker_sel < n_items) {
          sl_remove_idx(list, a->routine.entry_picker_sel);
          routine_history_save(a);
          int new_n = list->count;
          if (new_n == 0)
            a->routine.entry_picker_sel = 0;
          else if (a->routine.entry_picker_sel >= new_n)
            a->routine.entry_picker_sel = new_n;
        }
      }
      /* Else 0 = Cancel -> just close */
      a->routine.delete_confirm_open = false;
      a->ui_needs_redraw = true;
    }
    if (b->b) {
      a->routine.delete_confirm_open = false;
      a->ui_needs_redraw = true;
    }
    return;
  }

  if (b->b) {
    a->screen = SCREEN_ROUTINE_EDIT;
    a->ui_needs_redraw = true;
    return;
  }

  /* L3 Delete - Open Confirm */
  if (b->l3) {
    if (a->routine.entry_picker_sel < n_items) {
      a->routine.delete_confirm_open = true;
      a->routine.delete_confirm_sel = 0; /* Default to Cancel */
      a->ui_needs_redraw = true;
    }
    return;
  }

  if (b->a) {
    /* Selection made */
    if (a->routine.entry_picker_sel >= n_items) {
      /* Create new entry -> Go to keyboard mode */
      /* Clear buffer first? Or editing? Usually new entry implies empty
       * start.
       */
      char *buf = (a->routine.entry_picker_target == 0)
                      ? a->routine.edit_action
                      : a->routine.edit_trigger;
      buf[0] = 0;

      a->routine.kb_row = 0;
      a->routine.kb_col = 0;
      a->input_debounce_ms = 0; /* Ensure immediate response */
      a->screen = SCREEN_ROUTINE_ENTRY_TEXT;
    } else {
      /* Picked from history -> Fill field & Return to Nav mode */
      const char *txt = list->items[a->routine.entry_picker_sel];
      char *buf = (a->routine.entry_picker_target == 0)
                      ? a->routine.edit_action
                      : a->routine.edit_trigger;
      size_t cap = (a->routine.entry_picker_target == 0)
                       ? sizeof(a->routine.edit_action)
                       : sizeof(a->routine.edit_trigger);
      safe_snprintf(buf, cap, "%s", txt);

      a->routine.entry_picker_sel = 0;
      a->screen = SCREEN_ROUTINE_EDIT;
    }
    a->ui_needs_redraw = true;
  }
}
