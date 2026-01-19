#include "tasks.h"
#include "../../ui/keyboard.h"
#include "../../ui/ui_shared.h"
#include "../../utils/file_utils.h"
#include "../../utils/string_utils.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* -------------------------- Internal Utils -------------------------- */

static bool path_has_ext(const char *name, const char *ext) {
  if (!name || !ext)
    return false;
  size_t ln = strlen(name);
  size_t le = strlen(ext);
  if (ln < le)
    return false;
  return (strcmp(name + (ln - le), ext) == 0);
}

static void tasks_sanitize_label(char *label) {
  if (!label)
    return;
  config_trim(label);
  for (char *p = label; *p; p++) {
    if (*p == '/' || *p == '\\')
      *p = '-';
  }
  if (path_has_ext(label, ".txt")) {
    strip_extension(label, label, strlen(label) + 1);
    config_trim(label);
  }
}

static bool tasks_build_list_path(const char *label, char *out, size_t out_sz) {
  if (!label || !out || out_sz == 0)
    return false;
  char buf[256];
  safe_snprintf(buf, sizeof(buf), "%s", label);
  tasks_sanitize_label(buf);
  if (!buf[0])
    return false;
  if (path_has_ext(buf, ".done")) {
    char fixed[256];
    safe_snprintf(fixed, sizeof(fixed), "%s_list", buf);
    safe_snprintf(buf, sizeof(buf), "%s", fixed);
  }
  safe_snprintf(out, out_sz, "%s/%s", STATES_TASKS_DIR, buf);
  return true;
}

static const char *tasks_current_file(const App *a) {
  if (!a || a->tasks.files.count == 0)
    return NULL;
  if (a->tasks.kind < 0 || a->tasks.kind >= a->tasks.files.count)
    return NULL;
  return a->tasks.files.items[a->tasks.kind];
}

static void tasks_done_path_from_file(const char *tasks_file, char *out,
                                      size_t out_sz) {
  /* states/tasks/foo.txt -> states/tasks/foo.done */
  if (!tasks_file || !out || out_sz == 0)
    return;
  safe_snprintf(out, out_sz, "%s", tasks_file);
  char *dot = strrchr(out, '.');
  if (dot) {
    safe_snprintf(dot, (size_t)(out_sz - (dot - out)), ".done");
  } else {
    size_t n = strlen(out);
    if (n + 5 < out_sz)
      strcat(out, ".done");
  }
}

static StrList tasks_load_txt(const char *path) {
  StrList out = (StrList){0};
  FILE *f = fopen(path, "r");
  if (!f)
    return out;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    /* Trim ASCII whitespace, ignore blanks. */
    config_trim(line);
    if (!line[0])
      continue;
    sl_push(&out, line);
  }
  fclose(f);
  return out;
}

static void tasks_save_txt(const char *path, const StrList *items) {
  if (!path)
    return;
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  if (items) {
    for (int i = 0; i < items->count; i++) {
      fprintf(f, "%s\n", items->items[i]);
    }
  }
  fclose(f);
}

static void tasks_load_done_file(App *a) {
  if (!a || !a->tasks.done_path[0])
    return;
  hashes_free(&a->tasks.done, &a->tasks.done_n);
  char buf[4096];
  buf[0] = 0;
  FILE *f = fopen(a->tasks.done_path, "r");
  if (f) {
    if (fgets(buf, (int)sizeof(buf), f)) {
      config_trim(buf);
    }
    fclose(f);
  }
  hashes_from_csv(buf, &a->tasks.done, &a->tasks.done_n);
}

static void tasks_save_done_file(App *a) {
  if (!a || !a->tasks.done_path[0])
    return;
  char buf[4096];
  hashes_to_csv(a->tasks.done, a->tasks.done_n, buf, sizeof(buf));
  FILE *f = fopen(a->tasks.done_path, "w");
  if (!f)
    return;
  fprintf(f, "%s\n", buf);
  fclose(f);
}

static void tasks_scan_folder(App *a) {
  sl_free(&a->tasks.lists);
  sl_free(&a->tasks.files);
  DIR *d = opendir(STATES_TASKS_DIR);
  if (!d)
    return;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (!de->d_name[0] || de->d_name[0] == '.')
      continue;
    if (path_has_ext(de->d_name, ".done"))
      continue;
    char rel[PATH_MAX];
    safe_snprintf(rel, sizeof(rel), "%s/%s", STATES_TASKS_DIR, de->d_name);
    struct stat st;
    if (stat(rel, &st) != 0)
      continue;
    if (!S_ISREG(st.st_mode))
      continue;
    char disp[256];
    if (path_has_ext(de->d_name, ".txt"))
      strip_extension(de->d_name, disp, sizeof(disp));
    else
      safe_snprintf(disp, sizeof(disp), "%s", de->d_name);
    sl_push(&a->tasks.lists, disp);
    sl_push(&a->tasks.files, rel);
  }
  closedir(d);
  /* Simple alpha sort by display name (bubble-sort: small lists). */
  for (int i = 0; i < a->tasks.lists.count; i++) {
    for (int j = i + 1; j < a->tasks.lists.count; j++) {
      if (strcmp(a->tasks.lists.items[i], a->tasks.lists.items[j]) > 0) {
        char *tmpn = a->tasks.lists.items[i];
        a->tasks.lists.items[i] = a->tasks.lists.items[j];
        a->tasks.lists.items[j] = tmpn;
        char *tmpp = a->tasks.files.items[i];
        a->tasks.files.items[i] = a->tasks.files.items[j];
        a->tasks.files.items[j] = tmpp;
      }
    }
  }
}

static void tasks_open_current(App *a) {
  sl_free(&a->tasks.items);
  a->tasks.items = (StrList){0};
  a->tasks.done_path[0] = 0;
  if (!a || a->tasks.files.count == 0)
    return;
  if (a->tasks.kind < 0)
    a->tasks.kind = 0;
  if (a->tasks.kind >= a->tasks.files.count)
    a->tasks.kind = a->tasks.files.count - 1;
  const char *file = a->tasks.files.items[a->tasks.kind];
  a->tasks.items = tasks_load_txt(file);
  tasks_done_path_from_file(file, a->tasks.done_path,
                            sizeof(a->tasks.done_path));
  tasks_load_done_file(a);
}

/* -------------------------- Public API -------------------------- */

void tasks_init(App *a) {
  ensure_dir(STATES_TASKS_DIR);
  tasks_reload(a);
}

void tasks_cleanup(App *a) {
  sl_free(&a->tasks.lists);
  sl_free(&a->tasks.files);
  sl_free(&a->tasks.items);
  hashes_free(&a->tasks.done, &a->tasks.done_n);
}

void tasks_reload(App *a) {
  tasks_scan_folder(a);
  if (a->tasks.pick_sel < 0)
    a->tasks.pick_sel = 0;
  if (a->tasks.pick_sel >= a->tasks.lists.count)
    a->tasks.pick_sel =
        (a->tasks.lists.count > 0) ? (a->tasks.lists.count - 1) : 0;
  if (a->tasks.kind < 0)
    a->tasks.kind = 0;
  if (a->tasks.kind >= a->tasks.files.count)
    a->tasks.kind = 0;
  tasks_open_current(a);
}

bool task_is_done(App *a, const char *text) {
  uint32_t h = fnv1a32(text);
  return hashes_contains(a->tasks.done, a->tasks.done_n, h);
}

static void tasks_reset(App *a) {
  hashes_free(&a->tasks.done, &a->tasks.done_n);
  tasks_save_done_file(a);
}

static void tasks_toggle_done(App *a, const char *text) {
  uint32_t h = fnv1a32(text);
  if (hashes_contains(a->tasks.done, a->tasks.done_n, h))
    hashes_remove(a->tasks.done, &a->tasks.done_n, h);
  else
    hashes_add(&a->tasks.done, &a->tasks.done_n, h);
  tasks_save_done_file(a);
}

static void tasks_remove_item(App *a, int idx) {
  if (!a)
    return;
  StrList *list = &a->tasks.items;
  if (idx < 0 || idx >= list->count)
    return;
  uint32_t h = fnv1a32(list->items[idx]);
  if (hashes_contains(a->tasks.done, a->tasks.done_n, h)) {
    hashes_remove(a->tasks.done, &a->tasks.done_n, h);
    tasks_save_done_file(a);
  }
  sl_remove_idx(list, idx);
  const char *file = tasks_current_file(a);
  if (file)
    tasks_save_txt(file, list);
  if (a->tasks.sel >= list->count)
    a->tasks.sel = list->count - 1;
  if (a->tasks.sel < 0)
    a->tasks.sel = 0;
  if (list->count == 0)
    a->tasks.list_scroll = 0;
}

static void tasks_remove_list(App *a, int idx) {
  if (!a || idx < 0 || idx >= a->tasks.files.count)
    return;
  const char *file = a->tasks.files.items[idx];
  if (file) {
    char done_path[PATH_MAX];
    tasks_done_path_from_file(file, done_path, sizeof(done_path));
    remove(file);
    remove(done_path);
  }
  tasks_reload(a);
  if (a->tasks.lists.count == 0) {
    a->tasks.pick_sel = 0;
    a->tasks.sel = 0;
    a->tasks.kind = 0;
    return;
  }
  if (idx >= a->tasks.lists.count)
    idx = a->tasks.lists.count - 1;
  if (idx < 0)
    idx = 0;
  a->tasks.pick_sel = idx;
  a->tasks.kind = idx;
  tasks_open_current(a);
}

/* -------------------------- UI / Input -------------------------- */

static int kb_row_len(int r) {
  if (r < 0 || r >= 4)
    return 0;
  /* Basic keyboard rows for tasks text (matches stillroom.c behavior) */
  static const char *rows[] = {"abcdefghi", "jklmnopqr", "stuvwxyz",
                               "0123456789"};
  return (int)strlen(rows[r]);
}

/* Delete confirmation UI (internal helper) */
static void draw_tasks_delete_confirm(UI *ui, App *a) {
  if (!ui || !a || !a->tasks.delete_confirm_open)
    return;

  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);
  int cx = ui->w / 2;

  /* Dim background - Opaque Black */
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
  SDL_Rect r = {0, 0, ui->w, ui->h};
  SDL_RenderFillRect(ui->ren, &r);
  SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_NONE);

  int my = BIG_TIMER_Y;
  const char *title = "remove entry";
  draw_text(ui, ui->font_med, cx - text_width(ui->font_med, title) / 2, my,
            title, main, false);

  char msg[160];
  safe_snprintf(msg, sizeof(msg), "remove \"%s\" from the list?",
                a->tasks.delete_name);
  draw_text(ui, ui->font_small, cx - text_width(ui->font_small, msg) / 2,
            my + 64, msg, accent, false);

  const char *opt0 = "cancel";
  const char *opt1 = "remove";
  SDL_Color c0 = (a->tasks.delete_confirm_sel == 0) ? highlight : accent;
  SDL_Color c1 = (a->tasks.delete_confirm_sel == 1) ? highlight : accent;

  int oy = my + 128;
  int w0 = text_width(ui->font_small, opt0);
  int w_opt1 = text_width(ui->font_small, opt1);
  int gap = 40;
  int total_opt_w = w0 + gap + w_opt1;
  int start_opt_x = cx - total_opt_w / 2;

  draw_text(ui, ui->font_small, start_opt_x, oy, opt0, c0, false);
  draw_text(ui, ui->font_small, start_opt_x + w0 + gap, oy, opt1, c1, false);
}

static void tasks_kb_clamp(App *a) {
  int kb_row_count = 4;
  if (a->tasks.kb_row < 0)
    a->tasks.kb_row = 0;
  if (a->tasks.kb_row >= kb_row_count)
    a->tasks.kb_row = kb_row_count - 1;
  int len = kb_row_len(a->tasks.kb_row);
  /* The last row might differ in length, but our helper above handles 0-9 */
  /* If len is 0, we can't select col 0. */
  if (len == 0)
    a->tasks.kb_col = 0;
  else {
    if (a->tasks.kb_col < 0)
      a->tasks.kb_col = 0;
    if (a->tasks.kb_col >= len)
      a->tasks.kb_col = len - 1;
  }
}

void handle_tasks_pick(App *a, Buttons *b) {
  if (a->tasks.delete_confirm_open) {
    if (b->left) {
      a->tasks.delete_confirm_sel = 0;
      b->left = false;
    }
    if (b->right) {
      a->tasks.delete_confirm_sel = 1;
      b->right = false;
    }
    if (b->a) {
      b->a = false;
      if (a->tasks.delete_confirm_sel == 1) {
        /* Confirmed remove */
        if (a->tasks.delete_list_mode) {
          tasks_remove_list(a, a->tasks.delete_idx);
        }
      }
      a->tasks.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b) {
      b->b = false;
      a->tasks.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    a->ui_needs_redraw = true;
    return;
  }

  if (b->b || b->select) {
    b->b = false;
    b->select = false;
    a->screen = SCREEN_TIMER;
    a->timer_menu_open = true;
    return;
  }
  if (b->r3) {
    b->r3 = false;
    a->tasks.text_return_screen = SCREEN_TASKS_PICK;
    a->tasks.text_mode = TASKS_TEXT_NEW_LIST;
    a->tasks.edit_buf[0] = 0;
    a->tasks.kb_row = 0;
    a->tasks.kb_col = 0;
    tasks_kb_clamp(a);
    a->screen = SCREEN_TASKS_TEXT;
    return;
  }
  if (a->tasks.lists.count > 0) {
    if (b->up) {
      b->up = false;
      a->tasks.pick_sel =
          (a->tasks.pick_sel + a->tasks.lists.count - 1) % a->tasks.lists.count;
    }
    if (b->down) {
      b->down = false;
      a->tasks.pick_sel = (a->tasks.pick_sel + 1) % a->tasks.lists.count;
    }
    if (b->a) {
      b->a = false;
      a->tasks.kind = a->tasks.pick_sel;
      a->tasks.sel = 0;
      a->tasks.list_scroll = 0;
      tasks_open_current(a);
      a->screen = SCREEN_TASKS_LIST;
    }
    if (b->l3) {
      b->l3 = false;
      a->tasks.delete_list_mode = true;
      a->tasks.delete_idx = a->tasks.pick_sel;
      safe_snprintf(a->tasks.delete_name, sizeof(a->tasks.delete_name), "%s",
                    a->tasks.lists.items[a->tasks.pick_sel]);
      a->tasks.delete_confirm_open = true;
      a->tasks.delete_confirm_sel = 0;
      a->ui_needs_redraw = true;
    }
  }
}

void handle_tasks_list(App *a, Buttons *b) {
  if (a->tasks.delete_confirm_open) {
    if (b->left) {
      a->tasks.delete_confirm_sel = 0;
      b->left = false;
    }
    if (b->right) {
      a->tasks.delete_confirm_sel = 1;
      b->right = false;
    }
    if (b->a) {
      b->a = false;
      if (a->tasks.delete_confirm_sel == 1) {
        /* Confirmed remove */
        if (!a->tasks.delete_list_mode) {
          tasks_remove_item(a, a->tasks.delete_idx);
        }
      }
      a->tasks.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    if (b->b) {
      b->b = false;
      a->tasks.delete_confirm_open = false;
      a->ui_needs_redraw = true;
      return;
    }
    a->ui_needs_redraw = true;
    return;
  }

  if (b->b) {
    b->b = false;
    a->screen = SCREEN_TASKS_PICK;
    return;
  }
  if (b->select) {
    b->select = false;
    a->screen = SCREEN_MENU;
    return;
  }
  StrList *list = &a->tasks.items;
  if (list->count > 0) {
    if (b->up) {
      b->up = false;
      a->tasks.sel = (a->tasks.sel + list->count - 1) % list->count;
    }
    if (b->down) {
      b->down = false;
      a->tasks.sel = (a->tasks.sel + 1) % list->count;
    }
    if (b->a) {
      b->a = false;
      tasks_toggle_done(a, list->items[a->tasks.sel]);
    }
    if (b->y) {
      b->y = false;
      tasks_reset(a);
    }
    if (b->l3) {
      b->l3 = false;
      a->tasks.delete_list_mode = false;
      a->tasks.delete_idx = a->tasks.sel;
      safe_snprintf(a->tasks.delete_name, sizeof(a->tasks.delete_name), "%s",
                    list->items[a->tasks.sel]);
      a->tasks.delete_confirm_open = true;
      a->tasks.delete_confirm_sel = 0;
      a->ui_needs_redraw = true;
      return;
    }
  }
  if (b->r3) {
    b->r3 = false;
    a->tasks.text_return_screen = SCREEN_TASKS_LIST;
    a->tasks.text_mode = TASKS_TEXT_NEW_ITEM;
    a->tasks.edit_buf[0] = 0;
    a->tasks.kb_row = 0;
    a->tasks.kb_col = 0;
    tasks_kb_clamp(a);
    a->screen = SCREEN_TASKS_TEXT;
    return;
  }
}

static void append_char(char *buf, size_t cap, char ch) {
  size_t n = strlen(buf);
  if (n + 1 < cap) {
    buf[n] = ch;
    buf[n + 1] = 0;
  }
}

void handle_tasks_text(App *a, Buttons *b) {
  if (!a || !b)
    return;
  if (b->b) {
    if (a->tasks.text_return_screen == 0)
      a->tasks.text_return_screen = SCREEN_TASKS_PICK;
    a->screen = a->tasks.text_return_screen;
    return;
  }
  /* Use shared Stanza keyboard logic */
  bool modified =
      keyboard_update(a, b, a->tasks.edit_buf, sizeof(a->tasks.edit_buf),
                      &a->tasks.kb_row, &a->tasks.kb_col);
  if (modified) {
    a->ui_needs_redraw = true;
  }
  if (b->r3) {
    if (a->tasks.text_mode == TASKS_TEXT_NEW_LIST) {
      char label[128];
      safe_snprintf(label, sizeof(label), "%s", a->tasks.edit_buf);
      tasks_sanitize_label(label);
      if (label[0]) {
        int existing = sl_find(&a->tasks.lists, label);
        if (existing >= 0) {
          a->tasks.pick_sel = existing;
          a->tasks.kind = existing;
          a->tasks.sel = 0;
          a->tasks.list_scroll = 0;
        } else {
          char path[PATH_MAX];
          if (tasks_build_list_path(label, path, sizeof(path))) {
            tasks_save_txt(path, NULL);
            tasks_reload(a);
            int idx = sl_find(&a->tasks.lists, label);
            if (idx >= 0) {
              a->tasks.pick_sel = idx;
              a->tasks.kind = idx;
              a->tasks.sel = 0;
              a->tasks.list_scroll = 0;
            }
          }
        }
      }
    } else if (a->tasks.text_mode == TASKS_TEXT_NEW_ITEM) {
      char item[128];
      safe_snprintf(item, sizeof(item), "%s", a->tasks.edit_buf);
      config_trim(item);
      if (item[0]) {
        const char *file = tasks_current_file(a);
        if (file && sl_find(&a->tasks.items, item) < 0) {
          sl_push(&a->tasks.items, item);
          tasks_save_txt(file, &a->tasks.items);
          a->tasks.sel = a->tasks.items.count - 1;
          a->tasks.list_scroll = 0;
        }
      }
    }
    a->screen = a->tasks.text_return_screen;
    return;
  }
}

void draw_tasks_pick(UI *ui, App *a) {
  draw_top_hud(ui, a);
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Band 0: Header */
  draw_text_centered_band(ui, ui->font_med, 0, "tasks", main);

  if (a->tasks.lists.count == 0) {
    draw_text_centered_band(ui, ui->font_small, 2, "no lists created", accent);
    draw_text_centered_band(ui, ui->font_small, 3, "(press R3 to create)",
                            accent);
    const char *labsR[] = {"b:", "r3:"};
    const char *actsR[] = {"back", "create"};
    draw_hint_pairs_lr(ui, main, accent, NULL, NULL, 0, labsR, actsR, 2);
    return;
  }

  /* Bands 1-4: List items */
  int n = a->tasks.lists.count;
  int visible = 4;
  int start_row = 0;

  /* Centered scrolling */
  if (n > visible) {
    start_row = a->tasks.pick_sel - (visible / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible > n)
      start_row = n - visible;
  }

  for (int i = 0; i < visible; i++) {
    int idx = start_row + i;
    if (idx >= n)
      break;
    int band = i + 1;
    SDL_Color c = (idx == a->tasks.pick_sel) ? highlight : accent;
    draw_text_centered_band(ui, ui->font_small, band, a->tasks.lists.items[idx],
                            c);
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"nav", "open"};
  const char *labsR[] = {"b:", "l3:", "r3:"};
  const char *actsR[] = {"back", "del", "new"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 3);

  if (a->tasks.delete_confirm_open)
    draw_tasks_delete_confirm(ui, a);
}

void draw_tasks_list(UI *ui, App *a) {
  draw_top_hud(ui, a);
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Band 0: Header (List Name) */
  const char *title = "tasks";
  if (a->tasks.kind >= 0 && a->tasks.kind < a->tasks.lists.count) {
    title = a->tasks.lists.items[a->tasks.kind];
  }
  draw_text_centered_band(ui, ui->font_med, 0, title, main);

  if (a->tasks.items.count == 0) {
    draw_text_centered_band(ui, ui->font_small, 2, "empty list", accent);
    draw_text_centered_band(ui, ui->font_small, 3, "(press R3 to add)", accent);
    const char *labsR[] = {"b:", "r3:"};
    const char *actsR[] = {"back", "add"};
    draw_hint_pairs_lr(ui, main, accent, NULL, NULL, 0, labsR, actsR, 2);
    return;
  }

  /* Bands 1-4: Tasks */
  int n = a->tasks.items.count;
  int visible = 4;
  int start_row = 0;

  if (n > visible) {
    start_row = a->tasks.sel - (visible / 2);
    if (start_row < 0)
      start_row = 0;
    if (start_row + visible > n)
      start_row = n - visible;
  }

  for (int i = 0; i < visible; i++) {
    int idx = start_row + i;
    if (idx >= n)
      break;
    int band = i + 1;
    SDL_Color c = (idx == a->tasks.sel) ? highlight : accent;

    /* Check if done */
    const char *txt = a->tasks.items.items[idx];
    uint32_t h = fnv1a32(txt);
    bool done = hashes_contains(a->tasks.done, a->tasks.done_n, h);

    if (done) {
      int w = text_width(ui->font_small, txt);
      int x = CIRCLE_LAYOUT_CX - w / 2;
      /* Get band Y for strikethrough positioning */
      int th = TTF_FontHeight(ui->font_small);
      /* Approximate y from band - bands are centered around CIRCLE_LAYOUT_CY */
      int band_step = 45 + 20; /* CIRCLE_LAYOUT_BAND_H + CIRCLE_LAYOUT_GAP */
      int center_band = 3;
      int y = 377 + ((band - center_band) * band_step) + (band > 0 ? 22 : 0);
      y = y - th / 2;
      draw_text_centered_band(ui, ui->font_small, band, txt, c);
      draw_strikethrough(ui, x, y, w, th, (SDL_Color){255, 255, 255, 255});
    } else {
      draw_text_centered_band(ui, ui->font_small, band, txt, c);
    }
  }

  const char *labsL[] = {"arrows:", "a:"};
  const char *actsL[] = {"nav", "check"};
  const char *labsR[] = {"b:", "y:", "l3:", "r3:"};
  const char *actsR[] = {"back", "reset", "del", "add"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 2, labsR, actsR, 4);

  if (a->tasks.delete_confirm_open)
    draw_tasks_delete_confirm(ui, a);
}

void draw_tasks_text(UI *ui, App *a) {
  if (!ui || !a)
    return;
  SDL_Color main = ui_color_from_idx(a->cfg.main_color_idx);
  SDL_Color accent = ui_color_from_idx(a->cfg.accent_color_idx);
  SDL_Color highlight = ui_color_from_idx(a->cfg.highlight_color_idx);

  /* Pitch black background */
  draw_rect_fill(ui, 0, 0, ui->w, ui->h, (SDL_Color){0, 0, 0, 255});

  int cx = ui->w / 2;
  int header_y = BIG_TIMER_Y;

  /* Centered header */
  const char *title =
      (a->tasks.text_mode == TASKS_TEXT_NEW_LIST) ? "add folder" : "add task";
  int w_hdr = text_width(ui->font_med, title);
  draw_text(ui, ui->font_med, cx - w_hdr / 2, header_y, title, main, false);

  /* Centered input field */
  int y = header_y + UI_ROW_GAP + 10;
  const char *disp = a->tasks.edit_buf;
  /* Width calculation - use empty string width if buf is empty to center
     cursor? Actually if empty, width is 0, so cx - 0 is cx. Cursor drawn at x.
     Perfect. */
  /* Use placeholder width for centering if empty */
  const char *meas = disp[0] ? disp : "(empty)";
  int w_input = text_width(ui->font_med, meas);

  draw_text_input_with_cursor(ui, ui->font_med, cx - w_input / 2, y,
                              a->tasks.edit_buf, "(empty)", highlight, accent,
                              highlight, 0);

  /* Shared Stanza Keyboard */
  int yKb = y + TTF_FontHeight(ui->font_med) + 26;
  keyboard_draw(ui, cx, yKb, a->tasks.kb_row, a->tasks.kb_col, accent,
                highlight);
  const char *labsL[] = {"b:", "x:", "y:", "a:"};
  const char *actsL[] = {"cancel", "backspace", "space", "add"};
  const char *labsR[] = {"r3:"};
  const char *actsR[] = {"save"};
  draw_hint_pairs_lr(ui, main, accent, labsL, actsL, 4, labsR, actsR, 1);
}
