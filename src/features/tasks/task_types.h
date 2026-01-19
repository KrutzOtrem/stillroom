#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include "../../utils/string_utils.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
  TASKS_TEXT_NONE = 0,
  TASKS_TEXT_NEW_LIST,
  TASKS_TEXT_NEW_ITEM,
} TasksTextMode;

typedef struct {
  /* load any list file from ./states/tasks/ */
  StrList lists;   /* display names (basename without extension) */
  StrList files;   /* relative paths (e.g., states/tasks/foo.txt) */
  StrList items;   /* currently opened list items */
  int pick_sel;    /* selected list row */
  int pick_scroll; /* scroll offset for list-of-lists */
  int sel;         /* selected task row */
  int list_scroll; /* scroll offset within selected task list */
  int kind;        /* current list index in files */
  uint32_t *done;  /* hashes done for current list */
  int done_n;
  char done_path[PATH_MAX]; /* states/tasks/<list>.done */
  int text_return_screen;   /* Cast to/from Screen enum */
  char edit_buf[128];
  int kb_row;
  int kb_col;
  int text_mode; /* TasksTextMode */
  /* Delete confirmation dialog state (matches Focus Menu style) */
  bool delete_confirm_open;
  int delete_confirm_sel; /* 0=cancel, 1=remove */
  char delete_name[128];
  int delete_idx;
  bool delete_list_mode; /* true=list, false=item */
} TasksState;

#endif // TASK_TYPES_H
