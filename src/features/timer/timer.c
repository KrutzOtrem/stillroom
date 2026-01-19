#include "timer.h"
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
extern void draw_text_centered_band(UI *ui, TTF_Font *font, int band_idx,
                                    const char *text, SDL_Color color);
extern SDL_Color color_from_idx(int idx);

/* Constants from stillroom.c - must match original values */
#define UI_MARGIN_TOP 26
#define UI_ROW_GAP 58
#define TIMER_TOP_PAD 10
#define BIG_TIMER_Y (UI_MARGIN_TOP + (UI_ROW_GAP * 2) + TIMER_TOP_PAD)
#define UI_INLINE_GAP_PX 6
#define POMO_VALUE_GAP 10
#define CIRCLE_LAYOUT_BANDS 7

void pomo_build_order_string(char *out, size_t cap, int sessions,
                             int rest_min) {
  if (!out || cap == 0)
    return;
  if (sessions < 1)
    sessions = 1;
  if (sessions > 4)
    sessions = 4;
  if (rest_min < 0)
    rest_min = 0;
  out[0] = 0;
  safe_snprintf(out, cap, "focus");
  for (int i = 2; i <= sessions; i++) {
    safe_snprintf(out + strlen(out),
                  (cap > strlen(out)) ? (cap - strlen(out)) : 0,
                  ".break.focus");
  }
  if (rest_min > 0) {
    safe_snprintf(out + strlen(out),
                  (cap > strlen(out)) ? (cap - strlen(out)) : 0, ".rest");
  }
}

void draw_pomo_picker(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;
  int big_y = (ui->h / 2 - 56) + 10;
  int label_y = big_y - 44;

  const char *header = "pomodoro";
  int w_header = text_width(ui->font_med, header);
  draw_text(ui, ui->font_med, cx - w_header / 2, header_y, header, main, false);

  {
    const char *prefix = "i will focus on";
    int w_prefix = text_width(ui->font_small, prefix);
    char act[96];
    if (a->cfg.focus_activity[0])
      safe_snprintf(act, sizeof(act), "%s...", a->cfg.focus_activity);
    else
      safe_snprintf(act, sizeof(act), "...");
    int w_act = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, act);
    int total_w = w_prefix + UI_INLINE_GAP_PX + w_act;

    int start_x = cx - total_w / 2;
    int y = header_y + UI_ROW_GAP + 6;

    SDL_Color c_prefix = (a->focus_line_active && a->screen == SCREEN_POMO_PICK)
                             ? highlight
                             : main;
    draw_text(ui, ui->font_small, start_x, y, prefix, c_prefix, false);
    draw_text_style(ui, ui->font_small, start_x + w_prefix + UI_INLINE_GAP_PX,
                    y, act, accent, false, TTF_STYLE_ITALIC);
  }

  char v0[8], v1[8], v2[8], v3[8];
  safe_snprintf(v0, sizeof(v0), "%02d", a->pick_pomo_session_min);
  safe_snprintf(v1, sizeof(v1), "%02d", a->pick_pomo_break_min);
  safe_snprintf(v2, sizeof(v2), "%02d", a->pick_pomo_long_break_min);
  safe_snprintf(v3, sizeof(v3), "%02d", a->pick_pomo_loops);

  int w0 = 0, h0 = 0, w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
  TTF_SizeUTF8(ui->font_big, v0, &w0, &h0);
  TTF_SizeUTF8(ui->font_big, v1, &w1, &h1);
  TTF_SizeUTF8(ui->font_big, v2, &w2, &h2);
  TTF_SizeUTF8(ui->font_big, v3, &w3, &h3);
  const char *mid = "\xC2\xB7";
  int wMid = 0, hMid = 0;
  TTF_SizeUTF8(ui->font_big, mid, &wMid, &hMid);

  int total_nums_w = w0 + POMO_VALUE_GAP + wMid + POMO_VALUE_GAP + w1 +
                     POMO_VALUE_GAP + wMid + POMO_VALUE_GAP + w2 +
                     POMO_VALUE_GAP + wMid + POMO_VALUE_GAP + w3;

  int big_left = cx - total_nums_w / 2;

  int x0 = big_left;
  int xDot1 = x0 + w0 + POMO_VALUE_GAP;
  int x1 = xDot1 + wMid + POMO_VALUE_GAP;
  int xDot2 = x1 + w1 + POMO_VALUE_GAP;
  int x2 = xDot2 + wMid + POMO_VALUE_GAP;
  int xDot3 = x2 + w2 + POMO_VALUE_GAP;
  int x3 = xDot3 + wMid + POMO_VALUE_GAP;

  draw_text(ui, ui->font_big, x0, big_y, v0, main, false);
  draw_text(ui, ui->font_big, x1, big_y, v1, main, false);
  draw_text(ui, ui->font_big, x2, big_y, v2, main, false);
  draw_text(ui, ui->font_big, x3, big_y, v3, main, false);

  {
    int asc_big = TTF_FontAscent(ui->font_big);
    int baseline = big_y + asc_big;
    int yMid = baseline - asc_big;
    draw_text(ui, ui->font_big, xDot1, yMid, mid, main, false);
    draw_text(ui, ui->font_big, xDot2, yMid, mid, main, false);
    draw_text(ui, ui->font_big, xDot3, yMid, mid, main, false);
  }

  int cx0 = x0 + (w0 / 2);
  int cx1 = x1 + (w1 / 2);
  int cx2 = x2 + (w2 / 2);
  int cx3 = x3 + (w3 / 2);
  SDL_Color c0 = (a->pomo_pick_sel == 0) ? highlight : accent;
  SDL_Color c1 = (a->pomo_pick_sel == 1) ? highlight : accent;
  SDL_Color c2 = (a->pomo_pick_sel == 2) ? highlight : accent;
  SDL_Color c3 = (a->pomo_pick_sel == 3) ? highlight : accent;

  int wL0 = text_width(ui->font_small, "fo");
  int wL1 = text_width(ui->font_small, "bk");
  int wL2 = text_width(ui->font_small, "re");
  int wL3 = text_width(ui->font_small, "po");
  draw_text(ui, ui->font_small, cx0 - (wL0 / 2), label_y, "fo", c0, false);
  draw_text(ui, ui->font_small, cx1 - (wL1 / 2), label_y, "bk", c1, false);
  draw_text(ui, ui->font_small, cx2 - (wL2 / 2), label_y, "re", c2, false);
  draw_text(ui, ui->font_small, cx3 - (wL3 / 2), label_y, "po", c3, false);

  {
    char order[256];
    pomo_build_order_string(order, sizeof(order), a->pick_pomo_loops,
                            a->pick_pomo_long_break_min);
    int y_order = big_y + TTF_FontHeight(ui->font_big) + 22;
    int w_order =
        text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, order);
    draw_text_style(ui, ui->font_small, cx - w_order / 2, y_order, order,
                    accent, false, TTF_STYLE_ITALIC);
  }

  {
    int sessions = a->pick_pomo_loops;
    if (sessions < 1)
      sessions = 1;
    if (sessions > 4)
      sessions = 4;
    int session_min = a->pick_pomo_session_min;
    int break_min = a->pick_pomo_break_min;
    int rest_min = a->pick_pomo_long_break_min;
    if (session_min < 0)
      session_min = 0;
    if (break_min < 0)
      break_min = 0;
    if (rest_min < 0)
      rest_min = 0;
    int total_min =
        (session_min * sessions) + (break_min * (sessions - 1)) + rest_min;
    if (total_min < 0)
      total_min = 0;

    char total_buf[96];
    if (total_min < 60) {
      safe_snprintf(total_buf, sizeof(total_buf), "of %d minutes.", total_min);
    } else {
      int hours = total_min / 60;
      int mins = total_min % 60;
      if (mins == 0) {
        safe_snprintf(total_buf, sizeof(total_buf), "of %d hour%s.", hours,
                      (hours == 1) ? "" : "s");
      } else {
        safe_snprintf(total_buf, sizeof(total_buf),
                      "of %d hour%s and %d minutes.", hours,
                      (hours == 1) ? "" : "s", mins);
      }
    }
    int y_order2 = big_y + TTF_FontHeight(ui->font_big) + 22;
    int y_total = y_order2 + TTF_FontHeight(ui->font_small) + 6;
    int w_total =
        text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, total_buf);
    draw_text_style(ui, ui->font_small, cx - w_total / 2, y_total, total_buf,
                    accent, false, TTF_STYLE_ITALIC);
  }

  const char *labsL[] = {"up/down:", "left/right:", "a:"};
  const char *actsL[] = {"change", "select", "start"};
  const char *labsR[] = {"b:"};
  const char *actsR[] = {"back"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 3, labsR, actsR, 1);
}

void draw_custom_picker(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;
  int big_y = (ui->h / 2 - 56) + 10;
  int label_y = big_y - 44;

  const char *header = "timer";
  int w_header = text_width(ui->font_med, header);
  draw_text(ui, ui->font_med, cx - w_header / 2, header_y, header, main, false);

  {
    const char *prefix = "i will focus on";
    char act[96];
    if (a->cfg.focus_activity[0])
      safe_snprintf(act, sizeof(act), "%s...", a->cfg.focus_activity);
    else
      safe_snprintf(act, sizeof(act), "...");

    int w_prefix = text_width(ui->font_small, prefix);
    int w_act = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, act);
    int total_w = w_prefix + UI_INLINE_GAP_PX + w_act;
    int start_x = cx - total_w / 2;
    int y = header_y + UI_ROW_GAP + 6;

    SDL_Color c_prefix =
        (a->focus_line_active && a->screen == SCREEN_CUSTOM_PICK) ? highlight
                                                                  : main;

    draw_text(ui, ui->font_small, start_x, y, prefix, c_prefix, false);
    draw_text_style(ui, ui->font_small, start_x + w_prefix + UI_INLINE_GAP_PX,
                    y, act, accent, false, TTF_STYLE_ITALIC);
  }

  char hh[8], mm[8], ss[8];
  safe_snprintf(hh, sizeof(hh), "%02d", a->pick_custom_hours);
  safe_snprintf(mm, sizeof(mm), "%02d", a->pick_custom_minutes);
  safe_snprintf(ss, sizeof(ss), "%02d", a->pick_custom_seconds);

  SDL_Color cHH = (a->custom_field_sel == 0) ? highlight : accent;
  SDL_Color cMM = (a->custom_field_sel == 1) ? highlight : accent;
  SDL_Color cSS = (a->custom_field_sel == 2) ? highlight : accent;

  char full[32];
  safe_snprintf(full, sizeof(full), "%s:%s:%s", hh, mm, ss);

  int wFull = 0, hFull = 0;
  TTF_SizeUTF8(ui->font_big, full, &wFull, &hFull);

  int big_left = cx - wFull / 2;
  draw_text(ui, ui->font_big, big_left, big_y, full, main, false);

  if (a->cfg.timer_counting_up) {
    draw_strikethrough(ui, big_left, big_y, wFull, hFull,
                       (SDL_Color){255, 255, 255, 255});
  }

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

  int wLabHH = text_width(ui->font_small, "hr");
  int wLabMM = text_width(ui->font_small, "mn");
  int wLabSS = text_width(ui->font_small, "sc");

  draw_text(ui, ui->font_small, cxHH - (wLabHH / 2), label_y, "hr", cHH, false);
  draw_text(ui, ui->font_small, cxMM - (wLabMM / 2), label_y, "mn", cMM, false);
  draw_text(ui, ui->font_small, cxSS - (wLabSS / 2), label_y, "sc", cSS, false);

  {
    const char *label = "counting up";
    int y_opt = big_y + TTF_FontHeight(ui->font_big) + 26;
    int w_label =
        text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, label);
    int x_opt = cx - w_label / 2;

    SDL_Color c = accent;
    draw_text_style(ui, ui->font_small, x_opt, y_opt, label, c, false,
                    TTF_STYLE_ITALIC);

    if (!a->cfg.timer_counting_up) {
      int h = TTF_FontHeight(ui->font_small);
      SDL_Color white = (SDL_Color){255, 255, 255, 255};
      draw_strikethrough(ui, x_opt, y_opt, w_label, h, white);
    }
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"change", "start"};
  const char *labsR[] = {"b:", "y:"};
  const char *actsR[] = {"back", "mode"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 2);
}

void draw_timer_quick_menu(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  const char *items[] = {"timer", "pomodoro", "meditation",
                         "tasks", "routines", "booklets"};
  int n = 6;

  draw_text_centered_band(ui, ui->font_med, 0, "menu", main);

  int visible_rows = CIRCLE_LAYOUT_BANDS - 1;
  int start_row = 0;

  if (n > visible_rows) {
    start_row = a->menu_sel - (visible_rows / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible_rows > n)
      start_row = n - visible_rows;
  }

  for (int i = 0; i < visible_rows; i++) {
    int idx = start_row + i;
    if (idx >= n)
      break;
    int band = i + 1;
    SDL_Color c = (idx == a->menu_sel) ? highlight : accent;
    draw_text_centered_band(ui, ui->font_small, band, items[idx], c);
  }

  const char *labsL[] = {"a:"};
  const char *actsL[] = {"select"};
  const char *labsR[] = {"b/start:"};
  const char *actsR[] = {"close"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 1, labsR, actsR, 1);
}
