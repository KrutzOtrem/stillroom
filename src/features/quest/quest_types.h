#ifndef QUEST_TYPES_H
#define QUEST_TYPES_H

#include "../../utils/string_utils.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct QuestHaiku {
  char file[PATH_MAX];
  char display[128];
  char *raw;
  StrList tokens;
  uint8_t *token_is_word;
  int token_flags_cap;
  int word_count;
  int *word_ranks;
} QuestHaiku;

#endif // QUEST_TYPES_H
