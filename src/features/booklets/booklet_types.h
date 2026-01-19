#ifndef BOOKLET_TYPES_H
#define BOOKLET_TYPES_H

#include "../../utils/string_utils.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct BookletsState {
  bool open;
  int mode; /* 0=list, 1=viewer */
  int list_sel;
  int list_scroll;
  int idx;
  StrList files;
  StrList titles;
  StrList render_lines;
  uint8_t *render_is_header;
  int render_flags_cap;
  int page;
  int scroll;
  int *page_starts;
  int page_count;
  int page_cap;
} BookletsState;

#endif // BOOKLET_TYPES_H
