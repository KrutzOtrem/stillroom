#include "booklets.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../utils/file_utils.h"
#include "../../utils/string_utils.h"

/* Forward declarations for UI helpers from stillroom.c that we need */
extern int text_width(TTF_Font *f, const char *s);
extern void draw_text(UI *ui, TTF_Font *f, int x, int y, const char *s,
                      SDL_Color c, bool shadow);
extern void draw_text_style(UI *ui, TTF_Font *f, int x, int y, const char *s,
                            SDL_Color c, bool shadow, int style);
extern int text_width_style_ui(UI *ui, TTF_Font *f, int style, const char *s);
extern void draw_top_hud(UI *ui, App *a);
extern int overlay_bottom_text_limit_y(UI *ui);
extern int ui_bottom_stack_top_y(UI *ui);
extern SDL_Color color_from_idx(int idx);
extern bool parse_prefixed_index(const char *s, int *idx, const char **after);
extern char *read_entire_file(const char *path, size_t *out_len);
extern StrList list_txt_files_in(const char *dir);

/* Constants from stillroom.c */
/* Constants from stillroom.c */
#define UI_MARGIN_TOP 26
#define UI_ROW_GAP 58
#define TIMER_TOP_PAD 10
#define BIG_TIMER_Y (UI_MARGIN_TOP + (UI_ROW_GAP * 2) + TIMER_TOP_PAD)
#define UI_MARGIN_X 16
#define HUD_STACK_GAP 6

/* Internal helpers */
static void trim_ascii_inplace_local(char *s) {
  if (!s)
    return;
  char *p = s;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    p++;
  if (p != s)
    memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                   s[n - 1] == '\n')) {
    s[n - 1] = 0;
    n--;
  }
}

void booklets_render_clear(App *a) {
  if (!a)
    return;
  sl_free(&a->booklets.render_lines);
  free(a->booklets.render_is_header);
  a->booklets.render_is_header = NULL;
  a->booklets.render_flags_cap = 0;

  free(a->booklets.page_starts);
  a->booklets.page_starts = NULL;
  a->booklets.page_cap = 0;
  a->booklets.page_count = 0;
  a->booklets.page = 0;
  a->booklets.scroll = 0;
}

static void booklets_pages_push(App *a, int start_line) {
  if (!a)
    return;
  if (a->booklets.page_count + 1 > a->booklets.page_cap) {
    int new_cap = (a->booklets.page_cap == 0) ? 16 : (a->booklets.page_cap * 2);
    int *p = (int *)realloc(a->booklets.page_starts, new_cap * sizeof(int));
    if (!p)
      return;
    a->booklets.page_starts = p;
    a->booklets.page_cap = new_cap;
  }
  a->booklets.page_starts[a->booklets.page_count++] = start_line;
}

static void booklets_pages_build(UI *ui, App *a) {
  if (!a || !ui)
    return;
  free(a->booklets.page_starts);
  a->booklets.page_starts = NULL;
  a->booklets.page_cap = 0;
  a->booklets.page_count = 0;

  booklets_pages_push(a, 0);

  const int header_y = BIG_TIMER_Y;
  const int start_y = header_y + UI_ROW_GAP;
  const int y0 = start_y + 14;
  const int y_bottom = overlay_bottom_text_limit_y(ui);

  int line = 0;
  int page_start = 0;
  int y = y0;

  while (line < a->booklets.render_lines.count) {
    const bool is_hdr =
        (a->booklets.render_is_header && a->booklets.render_is_header[line]);
    TTF_Font *f = is_hdr ? ui->font_med : ui->font_small;
    const int step = TTF_FontHeight(f) + (is_hdr ? 10 : 6);

    if (y + step > y_bottom && line > page_start) {
      page_start = line;
      booklets_pages_push(a, page_start);
      y = y0;
      continue;
    }

    y += step;
    line++;
  }

  if (a->booklets.page_count < 1)
    a->booklets.page_count = 1;
  if (a->booklets.page < 0)
    a->booklets.page = 0;
  if (a->booklets.page >= a->booklets.page_count)
    a->booklets.page = a->booklets.page_count - 1;
}

int booklets_state_get_page(const char *booklet_file) {
  if (!booklet_file || !booklet_file[0])
    return 0;
  FILE *f = fopen(BOOKLETS_STATE_PATH, "r");
  if (!f)
    return 0;
  char line[512];
  int out = 0;
  while (fgets(line, sizeof(line), f)) {
    trim_ascii_inplace_local(line);
    if (!line[0] || line[0] == '#')
      continue;
    char *sep = strchr(line, '\t');
    if (!sep)
      sep = strchr(line, '=');
    if (!sep)
      continue;
    *sep = 0;
    sep++;
    trim_ascii_inplace_local(line);
    trim_ascii_inplace_local(sep);
    if (!line[0] || !sep[0])
      continue;
    if (strcmp(line, booklet_file) == 0) {
      out = atoi(sep);
      break;
    }
  }
  fclose(f);
  if (out < 0)
    out = 0;
  return out;
}

void booklets_state_set_page(const char *booklet_file, int page) {
  if (!booklet_file || !booklet_file[0])
    return;
  if (page < 0)
    page = 0;

  StrList keys = (StrList){0};
  int *vals = NULL;
  int cap = 0;

  FILE *f = fopen(BOOKLETS_STATE_PATH, "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
      trim_ascii_inplace_local(line);
      if (!line[0] || line[0] == '#')
        continue;
      char *sep = strchr(line, '\t');
      if (!sep)
        sep = strchr(line, '=');
      if (!sep)
        continue;
      *sep = 0;
      sep++;
      trim_ascii_inplace_local(line);
      trim_ascii_inplace_local(sep);
      if (!line[0] || !sep[0])
        continue;

      if (keys.count + 1 > cap) {
        int new_cap = (cap == 0) ? 32 : cap * 2;
        int *nv = (int *)realloc(vals, new_cap * sizeof(int));
        if (!nv)
          break;
        vals = nv;
        cap = new_cap;
      }
      sl_push(&keys, line);
      vals[keys.count - 1] = atoi(sep);
    }
    fclose(f);
  }

  int found = -1;
  for (int i = 0; i < keys.count; i++) {
    if (strcmp(keys.items[i], booklet_file) == 0) {
      found = i;
      break;
    }
  }
  if (found >= 0) {
    vals[found] = page;
  } else {
    if (keys.count + 1 > cap) {
      int new_cap = (cap == 0) ? 32 : cap * 2;
      int *nv = (int *)realloc(vals, new_cap * sizeof(int));
      if (nv) {
        vals = nv;
        cap = new_cap;
      }
    }
    if (keys.count + 1 <= cap) {
      sl_push(&keys, booklet_file);
      vals[keys.count - 1] = page;
    }
  }

  f = fopen(BOOKLETS_STATE_PATH, "w");
  if (f) {
    for (int i = 0; i < keys.count; i++) {
      fprintf(f, "%s\t%d\n", keys.items[i], vals ? vals[i] : 0);
    }
    fclose(f);
  }

  sl_free(&keys);
  free(vals);
}

void booklets_list_clear(App *a) {
  if (!a)
    return;
  sl_free(&a->booklets.files);
  sl_free(&a->booklets.titles);
}

static void booklets_sync_list(App *a) {
  if (!a)
    return;
  booklets_list_clear(a);
  a->booklets.files = list_txt_files_in("booklets");
  a->booklets.titles = (StrList){0};
  for (int i = 0; i < a->booklets.files.count; i++) {
    const char *fn = a->booklets.files.items[i];
    const char *after = fn;
    int idx = 0;
    if (parse_prefixed_index(fn, &idx, &after)) {
      char tmp[PATH_MAX];
      safe_snprintf(tmp, sizeof(tmp), "%s", after);
      char *dot = strrchr(tmp, '.');
      if (dot)
        *dot = 0;
      trim_ascii_inplace(tmp);
      sl_push(&a->booklets.titles, tmp);
    } else {
      char tmp[PATH_MAX];
      safe_snprintf(tmp, sizeof(tmp), "%s", fn);
      char *dot = strrchr(tmp, '.');
      if (dot)
        *dot = 0;
      trim_ascii_inplace(tmp);
      sl_push(&a->booklets.titles, tmp);
    }
  }
  a->booklets.idx = 0;
  a->booklets.scroll = 0;
}

static void booklets_push_render_line(UI *ui, App *a, const char *text,
                                      bool is_header) {
  (void)ui;
  if (!a)
    return;
  sl_push(&a->booklets.render_lines, text ? text : "");
  if (a->booklets.render_lines.cap > a->booklets.render_flags_cap) {
    int ncap = a->booklets.render_lines.cap;
    uint8_t *nf =
        (uint8_t *)realloc(a->booklets.render_is_header, (size_t)ncap);
    if (!nf)
      return;
    a->booklets.render_is_header = nf;
    a->booklets.render_flags_cap = ncap;
  }
  if (a->booklets.render_is_header) {
    a->booklets.render_is_header[a->booklets.render_lines.count - 1] =
        is_header ? 1 : 0;
  }
}

static void wrap_and_push(UI *ui, App *a, TTF_Font *font, const char *text,
                          int max_w, bool is_header) {
  if (!text) {
    booklets_push_render_line(ui, a, "", is_header);
    return;
  }
  char buf[4096];
  safe_snprintf(buf, sizeof(buf), "%s", text);
  trim_ascii_inplace(buf);
  if (buf[0] == 0) {
    booklets_push_render_line(ui, a, "", is_header);
    return;
  }
  char *p = buf;
  while (*p) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    char line[4096] = {0};
    int line_len = 0;
    while (*p) {
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
        booklets_push_render_line(ui, a, line, is_header);
        line[0] = 0;
        line_len = 0;
        safe_snprintf(line, sizeof(line), "%s", word);
        line_len = (int)strlen(line);
      }
      if (*p == '\n') {
        p++;
        break;
      }
      if (!*p)
        break;
    }
    if (line_len > 0)
      booklets_push_render_line(ui, a, line, is_header);
  }
}

static void booklets_load_current(UI *ui, App *a) {
  if (!a)
    return;
  booklets_render_clear(a);
  if (a->booklets.files.count <= 0) {
    booklets_push_render_line(ui, a, "no booklets found.", true);
    booklets_push_render_line(
        ui, a, "create a ./booklets folder and put .txt files inside.", false);
    booklets_push_render_line(
        ui, a, "name them like: 1) controls.txt, 2) tips.txt", false);
    booklets_pages_build(ui, a);
    a->booklets.page = 0;
    return;
  }
  if (a->booklets.idx < 0)
    a->booklets.idx = 0;
  if (a->booklets.idx >= a->booklets.files.count)
    a->booklets.idx = a->booklets.files.count - 1;
  char pathbuf[PATH_MAX];
  safe_snprintf(pathbuf, sizeof(pathbuf), "booklets/%s",
                a->booklets.files.items[a->booklets.idx]);
  size_t n = 0;
  char *raw = read_entire_file(pathbuf, &n);
  if (!raw) {
    booklets_push_render_line(ui, a, "failed to read booklet.", true);
    return;
  }
  const int max_w = ui->w - (UI_MARGIN_X * 2);
  char *cur = raw;
  while (cur && *cur) {
    char *nl = strchr(cur, '\n');
    if (nl)
      *nl = 0;
    size_t L = strlen(cur);
    if (L > 0 && cur[L - 1] == '\r')
      cur[L - 1] = 0;
    const char *after = cur;
    int idx = 0;
    bool is_header = parse_prefixed_index(cur, &idx, &after);
    if (is_header) {
      wrap_and_push(ui, a, ui->font_med, after, max_w, true);
    } else {
      wrap_and_push(ui, a, ui->font_small, cur, max_w, false);
    }
    if (!nl)
      break;
    cur = nl + 1;
  }
  free(raw);
  a->booklets.scroll = 0;

  booklets_pages_build(ui, a);
  const char *fn = a->booklets.files.items[a->booklets.idx];
  a->booklets.page = booklets_state_get_page(fn);
  if (a->booklets.page >= a->booklets.page_count)
    a->booklets.page = a->booklets.page_count - 1;
}

void booklets_init(App *a) {
  if (!a)
    return;
  memset(&a->booklets, 0, sizeof(a->booklets));
}

void booklets_cleanup(App *a) {
  if (!a)
    return;
  booklets_render_clear(a);
  booklets_list_clear(a);
}

void booklets_open_toggle(UI *ui, App *a) {
  if (!a)
    return;
  if (a->booklets.open) {
    a->booklets.open = false;
    a->booklets.mode = 0;
    booklets_render_clear(a);
    return;
  }
  a->settings_open = false;
  a->timer_menu_open = false;
  a->screen = SCREEN_TIMER;
  booklets_sync_list(a);
  a->booklets.open = true;
  a->booklets.mode = 0;
  a->booklets.list_sel = 0;
  a->booklets.list_scroll = 0;
  a->booklets.idx = 0;
  booklets_render_clear(a);
}

void booklets_open_from_menu(App *a) {
  if (!a)
    return;
  if (a->booklets.open) {
    a->booklets.open = false;
    a->booklets.mode = 0;
    booklets_render_clear(a);
    return;
  }
  a->settings_open = false;
  a->timer_menu_open = false;
  a->screen = SCREEN_TIMER;
  booklets_sync_list(a);
  a->booklets.open = true;
  a->booklets.mode = 0;
  a->booklets.list_sel = 0;
  a->booklets.list_scroll = 0;
  a->booklets.idx = 0;
  booklets_render_clear(a);
}

void handle_booklets(UI *ui, App *a, Buttons *b) {
  if (!a->booklets.open)
    return;

  /* List mode */
  if (a->booklets.mode == 0) {
    if (b->b) {
      a->booklets.open = false;
      a->booklets.mode = 0;
      a->booklets.list_scroll = 0;
      a->booklets.list_sel = 0;
      booklets_list_clear(a);
      a->timer_menu_open = true;
      if (a->nav_from_timer_menu) {
        a->landing_idle = a->nav_prev_landing_idle;
        a->nav_from_timer_menu = false;
      }
      b->b = false;
      a->ui_needs_redraw = true;
      return;
    }

    if (b->select) {
      a->booklets.open = false;
      a->timer_menu_open = false;
      a->settings_open = false;
      a->nav_from_timer_menu = false;
      a->screen = SCREEN_TIMER;
      b->select = false;
      return;
    }
    if (b->a) {
      a->booklets.idx = a->booklets.list_sel;
      a->booklets.mode = 1;
      booklets_load_current(ui, a);
      a->ui_needs_redraw = true;
      return;
    }
    if (a->booklets.files.count == 0)
      return;

    if (b->up) {
      a->booklets.list_sel =
          (a->booklets.list_sel - 1 + a->booklets.files.count) %
          a->booklets.files.count;
      if (a->booklets.list_sel < a->booklets.list_scroll)
        a->booklets.list_scroll = a->booklets.list_sel;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->down) {
      a->booklets.list_sel =
          (a->booklets.list_sel + 1) % a->booklets.files.count;
      int win = 8;
      if (a->booklets.list_sel >= a->booklets.list_scroll + win)
        a->booklets.list_scroll = a->booklets.list_sel - win + 1;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->left || b->right) {
      int dir = b->right ? 1 : -1;
      a->booklets.list_sel =
          (a->booklets.list_sel + dir + a->booklets.files.count) %
          a->booklets.files.count;
      a->ui_needs_redraw = true;
      return;
    }
    return;
  }

  /* Viewer mode */
  if (b->b) {
    if (a->booklets.files.count > 0) {
      const char *fn = a->booklets.files.items[a->booklets.idx];
      booklets_state_set_page(fn, a->booklets.page);
    }
    a->booklets.mode = 0;
    booklets_render_clear(a);
    a->ui_needs_redraw = true;
    return;
  }

  if (b->select) {
    a->booklets.open = false;
    a->timer_menu_open = false;
    a->settings_open = false;
    a->nav_from_timer_menu = false;
    a->screen = SCREEN_TIMER;
    b->select = false;
    return;
  }

  if (a->booklets.page_count <= 0)
    booklets_pages_build(ui, a);
  bool changed = false;

  if (b->left && a->booklets.page > 0) {
    a->booklets.page--;
    changed = true;
  }
  if (b->right && a->booklets.page + 1 < a->booklets.page_count) {
    a->booklets.page++;
    changed = true;
  }

  if (changed && a->booklets.files.count > 0) {
    const char *fn = a->booklets.files.items[a->booklets.idx];
    booklets_state_set_page(fn, a->booklets.page);
    a->ui_needs_redraw = true;
  }
}

void draw_booklets(UI *ui, App *a) {
  SDL_Color main = color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = color_from_idx(a->cfg.highlight_color_idx);
  draw_top_hud(ui, a);

  int x = UI_MARGIN_X;
  int header_y = BIG_TIMER_Y;
  int start_y = header_y + UI_ROW_GAP;

  if (a->booklets.mode == 0) {
    int header_w = text_width(ui->font_med, "booklets");
    int header_x = (ui->w - header_w) / 2;
    draw_text(ui, ui->font_med, header_x, header_y, "booklets", main, false);
    if (a->booklets.files.count == 0) {
      const char *msg = "no booklets folder or no .txt files found.";
      int msg_w = text_width(ui->font_small, msg);
      int msg_x = (ui->w - msg_w) / 2;
      draw_text_style(ui, ui->font_small, msg_x, start_y + 10, msg, accent,
                      false, TTF_STYLE_ITALIC);
      return;
    }

    int y = start_y + 10;
    int win = 8;
    int start = a->booklets.list_scroll;
    if (start < 0)
      start = 0;
    if (start > a->booklets.files.count - 1)
      start = a->booklets.files.count - 1;
    int end = start + win;
    if (end > a->booklets.files.count)
      end = a->booklets.files.count;

    for (int i = start; i < end; i++) {
      SDL_Color col = (i == a->booklets.list_sel) ? highlight : accent;
      const char *name = (i >= 0 && i < a->booklets.titles.count)
                             ? a->booklets.titles.items[i]
                             : a->booklets.files.items[i];
      int name_w = text_width(ui->font_small, name);
      int name_x = (ui->w - name_w) / 2;
      draw_text(ui, ui->font_small, name_x, y, name, col, false);
      y += UI_ROW_GAP + 8;
    }
    return;
  }

  /* Viewer mode */
  const char *title = (a->booklets.titles.count > 0 && a->booklets.idx >= 0 &&
                       a->booklets.idx < a->booklets.titles.count)
                          ? a->booklets.titles.items[a->booklets.idx]
                          : "booklet";
  draw_text(ui, ui->font_med, x, header_y, title, main, false);

  if (a->booklets.page_count <= 0 || !a->booklets.page_starts)
    booklets_pages_build(ui, a);
  if (a->booklets.page < 0)
    a->booklets.page = 0;
  if (a->booklets.page >= a->booklets.page_count)
    a->booklets.page = a->booklets.page_count - 1;

  int line_start = 0;
  int line_end = a->booklets.render_lines.count;
  if (a->booklets.page_starts && a->booklets.page_count > 0) {
    line_start = a->booklets.page_starts[a->booklets.page];
    if (a->booklets.page + 1 < a->booklets.page_count) {
      line_end = a->booklets.page_starts[a->booklets.page + 1];
    }
  }

  int y = start_y + 14;
  const int y_bottom = overlay_bottom_text_limit_y(ui);

  for (int i = line_start; i < line_end; i++) {
    bool is_header =
        (a->booklets.render_is_header && a->booklets.render_is_header[i]);
    TTF_Font *f = is_header ? ui->font_med : ui->font_small;
    SDL_Color c = is_header ? main : accent;

    int step = TTF_FontHeight(f) + (is_header ? 10 : 6);
    if (y + step > y_bottom)
      break;

    draw_text(ui, f, x, y, a->booklets.render_lines.items[i], c, false);
    y += step;
  }

  if (a->booklets.page_count > 0) {
    char pg[32];
    safe_snprintf(pg, sizeof(pg), "%d/%d", a->booklets.page + 1,
                  a->booklets.page_count);
    int w = text_width_style_ui(ui, ui->font_small, TTF_STYLE_ITALIC, pg);
    int xp = (ui->w / 2) - (w / 2);
    int yStackTop = ui_bottom_stack_top_y(ui);
    int yHour = yStackTop + TTF_FontHeight(ui->font_small) + HUD_STACK_GAP;
    int baselineHour = yHour + TTF_FontAscent(ui->font_med);
    int yp = baselineHour - TTF_FontAscent(ui->font_small);
    draw_text_style(ui, ui->font_small, xp, yp, pg, accent, false,
                    TTF_STYLE_ITALIC);
  }
}
