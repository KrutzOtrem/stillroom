#include "keyboard.h"
#include "../utils/string_utils.h"
#include <string.h>

static const char *kb_rows[] = {"abcdefghi", "jklmnopqr", "stuvwxyz",
                                ".,?!:;'"};

static int get_row_len(int r) {
  if (r < 0 || r >= KEYBOARD_ROW_COUNT)
    return 0;
  return (int)strlen(kb_rows[r]);
}

void keyboard_draw(UI *ui, int cx, int y_top, int row, int col,
                   SDL_Color accent, SDL_Color hi) {
  if (!ui)
    return;

  int key_gap = 18;
  int key_h = TTF_FontHeight(ui->font_small) + 16;

  for (int r = 0; r < KEYBOARD_ROW_COUNT; r++) {
    const char *rw = kb_rows[r];
    int len = (int)strlen(rw);

    /* Calculate row width for centering */
    int row_w = 0;
    for (int c = 0; c < len; c++) {
      char ch[2] = {rw[c], 0};
      row_w += text_width(ui->font_small, ch);
    }
    row_w += (len - 1) * key_gap;

    int kx = cx - row_w / 2;
    for (int c = 0; c < len; c++) {
      char ch[2] = {rw[c], 0};
      SDL_Color color = accent;
      if (r == row && c == col) {
        color = hi;
      }

      draw_text(ui, ui->font_small, kx, y_top + r * key_h, ch, color, false);
      kx += text_width(ui->font_small, ch) + key_gap;
    }
  }
}

static void keyboard_clamp(int *row, int *col) {
  if (*row < 0)
    *row = KEYBOARD_ROW_COUNT - 1;
  if (*row >= KEYBOARD_ROW_COUNT)
    *row = 0;

  int len = get_row_len(*row);
  if (len == 0) {
    *col = 0;
    return;
  }

  if (*col < 0)
    *col = len - 1;
  if (*col >= len)
    *col = 0;
}

bool keyboard_update(App *a, Buttons *b, char *buf, size_t cap, int *row,
                     int *col) {
  if (!a || !b || !buf || !row || !col)
    return false;

  bool modified = false;

  /* Navigation */
  if (b->up) {
    (*row)--;
    keyboard_clamp(row, col);
  }
  if (b->down) {
    (*row)++;
    keyboard_clamp(row, col);
  }
  if (b->left) {
    (*col)--;
    keyboard_clamp(row, col);
  }
  if (b->right) {
    (*col)++;
    keyboard_clamp(row, col);
  }

  /* Actions */
  if (b->x) { /* Backspace */
    utf8_pop_back_inplace(buf);
    modified = true;
  }
  if (b->y) { /* Space */
    size_t n = strlen(buf);
    if (n + 1 < cap) {
      buf[n] = ' ';
      buf[n + 1] = 0;
      modified = true;
    }
  }
  if (b->a) { /* Type character */
    if (*row >= 0 && *row < KEYBOARD_ROW_COUNT) {
      int len = get_row_len(*row);
      if (*col >= 0 && *col < len) {
        char ch = kb_rows[*row][*col];
        size_t n = strlen(buf);
        if (n + 1 < cap) {
          buf[n] = ch;
          buf[n + 1] = 0;
          modified = true;
        }
      }
    }
  }

  return modified;
}
