#ifndef UI_SHARED_H
#define UI_SHARED_H

#include "../app.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Layout Constants - MATCHED TO ORIGINAL stillroom.c */
#define UI_MARGIN_X 30
#define UI_MARGIN_TOP 26
#define UI_MARGIN_BOTTOM 22
#define UI_ROW_GAP 58
#define UI_INLINE_GAP_PX 6
#define UI_BOTTOM_MARGIN 22
#define HUD_STACK_GAP 6
#define BIG_TIMER_Y (UI_MARGIN_TOP + (UI_ROW_GAP * 2) + TIMER_TOP_PAD)
#define TIMER_TOP_PAD 10
#define TIMER_LEFT_X UI_MARGIN_X
#define PICKER_HEADER_Y 160
#define PICKER_HEADER_GAP 42
#define PICKER_ITEM_GAP 20
#define MENU_ITEM_GAP 22
#define HUD_HIDDEN_TIMER_STATE_GAP 3
#define HUD_HIDDEN_STACK_BOTTOM_PAD 70
#define HUD_HIDDEN_STACK_Y_OFFSET 70
#define BG_HIDDEN_SHIFT_Y 120
#define HUD_HIDE_HOLD_MS 450
#define UI_HELP_OVERLAY_HOLD_MS 450
#define UI_HELP_OVERLAY_DIR "layouts"
#define UI_HELP_OVERLAY_PREFIX "ui_"
#define USABLE_COLUMN_WIDTH 540
#define CIRCLE_LAYOUT_CX (1024 / 2)

/* Structs */
typedef struct UI UI;

/* Function Declarations */
void draw_top_hud(UI *ui, App *a);
void draw_text(UI *ui, TTF_Font *font, int x, int y, const char *text,
               SDL_Color col, bool shadow);
void draw_text_input_with_cursor(UI *ui, TTF_Font *font, int x, int y,
                                 const char *text, const char *placeholder,
                                 SDL_Color text_col, SDL_Color placeholder_col,
                                 SDL_Color cursor_col, int cursor_idx);

void draw_text_centered(UI *ui, TTF_Font *font, int cx, int y, const char *text,
                        SDL_Color col, bool shadow);
void draw_text_centered_band(UI *ui, TTF_Font *font, int band, const char *text,
                             SDL_Color col);
void draw_text_style(UI *ui, TTF_Font *font, int x, int y, const char *text,
                     SDL_Color col, bool shadow, int style);
void draw_hint_pairs_lr(UI *ui, SDL_Color main, SDL_Color accent,
                        const char **labels_l, const char **actions_l,
                        int count_l, const char **labels_r,
                        const char **actions_r, int count_r);
void draw_rect_fill(UI *ui, int x, int y, int w, int h, SDL_Color col);
void draw_strikethrough(UI *ui, int x, int y, int w, int h, SDL_Color col);
int text_width(TTF_Font *font, const char *text);
int text_width_style_ui(UI *ui, TTF_Font *font, int style, const char *text);
void ui_draw_circle_striped(UI *ui, int cx, int cy, int r, int segments,
                            int highlight_seg, SDL_Color base_col,
                            SDL_Color high_col, const char **labels,
                            int label_count);
SDL_Color ui_color_from_idx(int idx);

#endif
