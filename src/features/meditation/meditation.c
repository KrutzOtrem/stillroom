#include "meditation.h"
#include "../../utils/string_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* External UI helpers from stillroom.c */
extern int text_width(TTF_Font *f, const char *s);
extern int text_width_style_ui(UI *ui, TTF_Font *f, int style, const char *s);
extern void draw_text(UI *ui, TTF_Font *f, int x, int y, const char *s,
                      SDL_Color c, bool shadow);
extern void draw_text_style(UI *ui, TTF_Font *f, int x, int y, const char *s,
                            SDL_Color c, bool shadow, int style);
extern void draw_top_hud(UI *ui, App *a);
extern void draw_strikethrough(UI *ui, int x, int y, int w, int h, SDL_Color c);
extern void draw_hint_pairs_lr(UI *ui, SDL_Color main, SDL_Color accent,
                               const char **labsL, const char **actsL, int nL,
                               const char **labsR, const char **actsR, int nR);
extern SDL_Color color_from_idx(int idx);

/* Constants - must match stillroom.c */
#define UI_MARGIN_TOP 26
#define UI_ROW_GAP 58
#define TIMER_TOP_PAD 10
#define BIG_TIMER_Y (UI_MARGIN_TOP + (UI_ROW_GAP * 2) + TIMER_TOP_PAD)
#define PICKER_ITEM_GAP 20
#define USABLE_COLUMN_WIDTH 540

int meditation_total_minutes(const App *a) {
  if (!a)
    return 0;
  int total = a->pick_meditation_hours * 60 + a->pick_meditation_minutes;
  if (total < 0)
    total = 0;
  return total;
}

int meditation_bell_max_minutes(const App *a) {
  int total = meditation_total_minutes(a);
  int max = total / 2;
  if (max > 30)
    max = 30;
  if (max < 0)
    max = 0;
  return max;
}

int meditation_minutes_from_label(const char *label) {
  if (!label || !label[0])
    return 0;
  const char *open = strchr(label, '(');
  const char *close = strchr(label, ')');
  const char *p = open ? open + 1 : label;
  const char *end = close ? close : label + strlen(label);
  int value = 0;
  bool found = false;
  for (; p < end; p++) {
    if (isdigit((unsigned char)*p)) {
      value = value * 10 + (*p - '0');
      found = true;
    } else if (found) {
      break;
    }
  }
  return found ? value : 0;
}

void meditation_clamp_bell(App *a) {
  if (!a)
    return;
  int max = meditation_bell_max_minutes(a);
  if (a->pick_meditation_bell_min < 0)
    a->pick_meditation_bell_min = 0;
  if (a->pick_meditation_bell_min > max)
    a->pick_meditation_bell_min = max;
}

void draw_meditation_picker(UI *ui, App *a) {
  SDL_Color main_col = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Header */
  const char *title = "meditation";
  int w_title = text_width(ui->font_med, title);
  draw_text(ui, ui->font_med, cx - w_title / 2, header_y, title, main_col,
            false);

  int big_y = header_y + TTF_FontHeight(ui->font_med) + 80;
  int current_y = big_y;

  /* 1. Timer (Big Numbers) */
  {
    bool active = (a->meditation_pick_view == 0);

    char v0[8], v1[8], v2[8];
    safe_snprintf(v0, sizeof(v0), "%02d", a->pick_meditation_hours);
    safe_snprintf(v1, sizeof(v1), "%02d", a->pick_meditation_minutes);
    safe_snprintf(v2, sizeof(v2), "%02d", a->pick_meditation_bell_min);

    const char *mid = "\xC2\xB7";
    int w0, h0, w1, h1, w2, h2, wMid, hMid;
    TTF_SizeUTF8(ui->font_big, v0, &w0, &h0);
    TTF_SizeUTF8(ui->font_big, v1, &w1, &h1);
    TTF_SizeUTF8(ui->font_big, v2, &w2, &h2);
    TTF_SizeUTF8(ui->font_big, mid, &wMid, &hMid);

    int gap = 30;
    int total_nums_w = w0 + gap + wMid + gap + w1 + gap + wMid + gap + w2;
    int start_x = cx - (total_nums_w / 2);
    int x = start_x;
    int y_nums = current_y;

    draw_text(ui, ui->font_big, x, y_nums, v0, main_col, false);
    int cx0 = x + w0 / 2;
    x += w0 + gap;

    draw_text(ui, ui->font_big, x, y_nums + (h0 - hMid) / 2 - 4, mid, main_col,
              false);
    x += wMid + gap;

    draw_text(ui, ui->font_big, x, y_nums, v1, main_col, false);
    int cx1 = x + w1 / 2;
    x += w1 + gap;

    draw_text(ui, ui->font_big, x, y_nums + (h0 - hMid) / 2 - 4, mid, main_col,
              false);
    x += wMid + gap;

    draw_text(ui, ui->font_big, x, y_nums, v2, main_col, false);
    int cx2 = x + w2 / 2;

    /* Labels (hr, mn, bl) */
    int y_labels = y_nums - TTF_FontHeight(ui->font_small) + 4;
    SDL_Color c0 = accent, c1 = accent, c2 = accent;
    if (active) {
      c0 = (a->meditation_pick_sel == 0) ? highlight : accent;
      c1 = (a->meditation_pick_sel == 1) ? highlight : accent;
      c2 = (a->meditation_pick_sel == 2) ? highlight : accent;
    } else {
      /* Dim if inactive */
      c0.r = (Uint8)(c0.r * 0.95f);
      c0.g = (Uint8)(c0.g * 0.95f);
      c0.b = (Uint8)(c0.b * 0.95f);
      c1.r = (Uint8)(c1.r * 0.95f);
      c1.g = (Uint8)(c1.g * 0.95f);
      c1.b = (Uint8)(c1.b * 0.95f);
      c2.r = (Uint8)(c2.r * 0.95f);
      c2.g = (Uint8)(c2.g * 0.95f);
      c2.b = (Uint8)(c2.b * 0.95f);
    }

    int wL0 = text_width(ui->font_small, "hr");
    int wL1 = text_width(ui->font_small, "mn");
    int wL2 = text_width(ui->font_small, "bl");
    draw_text(ui, ui->font_small, cx0 - wL0 / 2, y_labels, "hr", c0, false);
    draw_text(ui, ui->font_small, cx1 - wL1 / 2, y_labels, "mn", c1, false);
    draw_text(ui, ui->font_small, cx2 - wL2 / 2, y_labels, "bl", c2, false);

    if (!active) {
      draw_strikethrough(ui, start_x, y_nums + 10, total_nums_w, h0 - 20,
                         (SDL_Color){255, 255, 255, 255});
    }

    current_y += h0 + PICKER_ITEM_GAP;

    /* Bell Info Line */
    char bell_buf[128];
    if (a->pick_meditation_bell_min == 0) {
      safe_snprintf(bell_buf, sizeof(bell_buf), "bells off.");
    } else {
      safe_snprintf(bell_buf, sizeof(bell_buf), "bell every %d minutes.",
                    a->pick_meditation_bell_min);
    }
    int w_bell =
        text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, bell_buf);

    SDL_Color c_bell = active ? accent : accent;
    if (!active) {
      c_bell.r = (Uint8)(c_bell.r * 0.7f);
      c_bell.g = (Uint8)(c_bell.g * 0.7f);
      c_bell.b = (Uint8)(c_bell.b * 0.7f);
    }
    draw_text_style(ui, ui->font_small, cx - w_bell / 2, current_y, bell_buf,
                    c_bell, false, TTF_STYLE_ITALIC);

    if (!active) {
      draw_strikethrough(ui, cx - w_bell / 2, current_y, w_bell,
                         TTF_FontHeight(ui->font_small),
                         (SDL_Color){255, 255, 255, 255});
    }

    current_y += TTF_FontHeight(ui->font_small) + PICKER_ITEM_GAP + 10;
  }

  /* 2. Guided Meditation */
  {
    bool active = (a->meditation_pick_view == 1);
    SDL_Color col = active ? highlight : accent;

    const char *med =
        (a->meditation_guided_sounds.count > 0 &&
         a->meditation_guided_idx >= 0 &&
         a->meditation_guided_idx < a->meditation_guided_sounds.count)
            ? a->meditation_guided_sounds.items[a->meditation_guided_idx]
            : "none";

    char med_name[256];
    safe_snprintf(med_name, sizeof(med_name), "%s", med ? med : "none");
    strip_ext_inplace(med_name);

    /* Remove "guided:" prefix if present (not doing parsing logic anymore) */

    /* We need draw_text_wrapped_centered but it's not exposed in ui_shared.h!
       Using draw_text_style for now which mimics generic text draw but
       centered. If wrapping is needed we'd need that helper. Actually, let's
       stick to simple centering for names for now, or use draw_text if no
       style. Original used draw_text_wrapped_centered. Since I don't have that
       helper exposed, I will use basic draw_text centered. */

    int w = text_width(ui->font_small, med_name);
    if (w > USABLE_COLUMN_WIDTH)
      w = USABLE_COLUMN_WIDTH; // Simplified clamp

    draw_text(ui, ui->font_small, cx - w / 2, current_y, med_name, col, false);
    int used_h = TTF_FontHeight(ui->font_small); // Approx

    if (!active) {
      /* Strikethrough 100% opacity */
      draw_strikethrough(ui, cx - w / 2, current_y, w,
                         TTF_FontHeight(ui->font_small),
                         (SDL_Color){255, 255, 255, 255});
    } else {
      int arrow_gap = 12;
      int arrow_y = current_y;
      draw_text(ui, ui->font_small,
                cx - w / 2 - arrow_gap - text_width(ui->font_small, "<"),
                arrow_y, "<", main_col, false);
      draw_text(ui, ui->font_small, cx + w / 2 + arrow_gap, arrow_y, ">",
                main_col, false);
    }
    current_y += used_h + PICKER_ITEM_GAP;
  }

  /* 3. Mindful Breathing */
  {
    bool active = (a->meditation_pick_view == 2);
    SDL_Color col = active ? highlight : accent;

    int breaths = a->pick_meditation_breaths;
    if (breaths < 1)
      breaths = 1;
    if (breaths > 3)
      breaths = 3;

    const char *suffix = "once";
    if (breaths == 2)
      suffix = "twice";
    if (breaths == 3)
      suffix = "thrice";

    char buf[128];
    safe_snprintf(buf, sizeof(buf), "mindful breathing %s", suffix);

    int w = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, buf);
    draw_text_style(ui, ui->font_small, cx - w / 2, current_y, buf, col, false,
                    TTF_STYLE_ITALIC);

    if (!active) {
      draw_strikethrough(ui, cx - w / 2, current_y, w,
                         TTF_FontHeight(ui->font_small),
                         (SDL_Color){255, 255, 255, 255});
    } else {
      int arrow_gap = 12;
      int arrow_y = current_y;
      draw_text(ui, ui->font_small,
                cx - w / 2 - arrow_gap - text_width(ui->font_small, "<"),
                arrow_y, "<", main_col, false);
      draw_text(ui, ui->font_small, cx + w / 2 + arrow_gap, arrow_y, ">",
                main_col, false);
    }
    current_y += TTF_FontHeight(ui->font_small) + PICKER_ITEM_GAP;
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"change", "start"};
  const char *labsR[] = {"b:", "y:"};
  const char *actsR[] = {"back", "mode"};
  draw_hint_pairs_lr(ui, main_col, accent, labsL, actsL, 2, labsR, actsR, 2);
}
