#include "focus_menu.h"
#include "../../ui/keyboard.h"
#include "../../ui/ui_shared.h"
#include "../../utils/string_utils.h"
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
/* extern void draw_text_input_with_cursor(UI *ui, TTF_Font *font, int x, int y,
 * ... removed to match ui_shared.h */
extern void draw_hint_pairs_lr(UI *ui, SDL_Color main, SDL_Color accent,
                               const char **labsL, const char **actsL, int nL,
                               const char **labsR, const char **actsR, int nR);
extern SDL_Color color_from_idx(int idx);

/* External focus functions from stillroom.c */
extern int focus_pick_build(App *a, const char **out, int out_cap);

/* External keyboard helpers from stillroom.c */
extern const char *kb_rows[];
extern const int kb_row_count;
extern int kb_row_len(int r);

/* Constants */
/* Constants - must match stillroom.c */
#define UI_MARGIN_TOP 26
#define UI_ROW_GAP 58
#define TIMER_TOP_PAD 10
#define BIG_TIMER_Y (UI_MARGIN_TOP + (UI_ROW_GAP * 2) + TIMER_TOP_PAD)

int focus_menu_entries_build(App *a, const char **out, int out_cap) {
  if (!a || !out || out_cap <= 0)
    return 0;
  const char *list[1 + MAX_FOCUS_STATS];
  int n = focus_pick_build(a, list, (int)(sizeof(list) / sizeof(list[0])));
  int k = 0;
  for (int i = 1; i < n && k < out_cap; i++) { /* skip empty slot */
    if (list[i] && list[i][0])
      out[k++] = list[i];
  }
  return k;
}

void focus_menu_sync_sel_to_current(App *a) {
  if (!a)
    return;
  const char *items[1 + MAX_FOCUS_STATS];
  int n_items = focus_menu_entries_build(
      a, items, (int)(sizeof(items) / sizeof(items[0])));
  if (!a->cfg.focus_activity[0]) {
    a->focus_menu_sel = 0;
    return;
  }
  for (int i = 0; i < n_items; i++) {
    if (items[i] && strcmp(items[i], a->cfg.focus_activity) == 0) {
      a->focus_menu_sel = i;
      return;
    }
  }
  a->focus_menu_sel = 0;
}

void draw_focus_menu(UI *ui, App *a) {
  SDL_Color main_col = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Header Line */
  {
    const char *header = "entry";
    int w_header = text_width(ui->font_med, header);
    draw_text(ui, ui->font_med, cx - w_header / 2, header_y, header, main_col,
              false);
  }

  const char *items[1 + MAX_FOCUS_STATS];
  int n_items = focus_menu_entries_build(
      a, items, (int)(sizeof(items) / sizeof(items[0])));
  int total_rows = n_items + 1; /* + create */

  if (a->focus_menu_sel < 0)
    a->focus_menu_sel = 0;
  if (a->focus_menu_sel >= total_rows)
    a->focus_menu_sel = total_rows - 1;

  int y0 = header_y + 64;
  int row_h = UI_ROW_GAP + 8;
  int visible = 8;
  int top = a->focus_menu_sel - (visible / 2);
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
    SDL_Color c = (idx == a->focus_menu_sel) ? highlight : accent;

    if (idx < n_items) {
      /* Centered Item */
      int w = text_width(ui->font_small, items[idx]);
      draw_text(ui, ui->font_small, cx - w / 2, y, items[idx], c, false);
    } else {
      /* Create new entry */
      const char *label = "create a new entry";
      int w = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, label);
      draw_text_style(ui, ui->font_small, cx - w / 2, y, label, c, false,
                      TTF_STYLE_ITALIC);
    }
  }

  if (a->focus_delete_confirm_open) {
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_Rect r = {0, 0, ui->w, ui->h};
    SDL_RenderFillRect(ui->ren, &r);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);

    int my = BIG_TIMER_Y;
    const char *title = "remove entry";
    draw_text(ui, ui->font_med, cx - text_width(ui->font_med, title) / 2, my,
              title, main_col, false);

    char msg[160];
    safe_snprintf(msg, sizeof(msg), "remove \"%s\" from the list?",
                  a->focus_delete_name);
    draw_text(ui, ui->font_small, cx - text_width(ui->font_small, msg) / 2,
              my + 64, msg, accent, false);

    const char *opt0 = "cancel";
    const char *opt1 = "remove";
    SDL_Color c0 = (a->focus_delete_confirm_sel == 0) ? highlight : accent;
    SDL_Color c1 = (a->focus_delete_confirm_sel == 1) ? highlight : accent;

    int oy = my + 128;
    int w0 = text_width(ui->font_small, opt0);
    int w1 = text_width(ui->font_small, opt1);
    int gap = 40;
    int total_opt_w = w0 + gap + w1;
    int start_opt_x = cx - total_opt_w / 2;

    draw_text(ui, ui->font_small, start_opt_x, oy, opt0, c0, false);
    draw_text(ui, ui->font_small, start_opt_x + w0 + gap, oy, opt1, c1, false);
  }
}

void draw_focus_text(UI *ui, App *a) {
  SDL_Color main_col = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);

  /* Pitch black background */
  draw_rect_fill(ui, 0, 0, ui->w, ui->h, (SDL_Color){0, 0, 0, 255});

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Centered header */
  const char *hdr = "focus on";
  int w_hdr = text_width(ui->font_med, hdr);
  draw_text(ui, ui->font_med, cx - w_hdr / 2, header_y, hdr, main_col, false);

  /* Centered input field */
  int y = header_y + UI_ROW_GAP + 10;
  const char *disp = a->focus_edit_buf;
  /* Use placeholder width for centering if empty */
  const char *meas = disp[0] ? disp : "(empty)";
  int w_input = text_width(ui->font_med, meas);

  draw_text_input_with_cursor(ui, ui->font_med, cx - w_input / 2, y,
                              a->focus_edit_buf, "(empty)", highlight, accent,
                              highlight, 0);

  /* Centered keyboard rows - Stanza Style */
  int yKb = y + TTF_FontHeight(ui->font_med) + 26;
  keyboard_draw(ui, cx, yKb, a->focus_kb_row, a->focus_kb_col, accent,
                highlight);
  const char *labsL[] = {"b:", "x:", "y:", "a:"};
  const char *actsL[] = {"cancel", "backspace", "space", "add"};
  const char *labsR[] = {"r3:"};
  const char *actsR[] = {"save"};
  draw_hint_pairs_lr(ui, main_col, accent, labsL, actsL, 4, labsR, actsR, 1);
}
