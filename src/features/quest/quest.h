#ifndef QUEST_H
#define QUEST_H

#include "quest_types.h"

// Forward declarations
typedef struct App App;
typedef struct UI UI;
typedef struct Buttons Buttons;

// Core Logic
void quest_sync_daily(App *a);
void quest_reload_list(App *a);
void quest_tokens_clear(App *a);
void quest_begin_again(App *a);
void quest_format_date_line(char *out, size_t cap);
int quest_total_minutes(const App *a, int *out_required);
const char *quest_difficulty_label(const App *a);

// Utilities
char *quest_build_display_text(const QuestHaiku *h, int reveal_count);
int quest_line_count(const char *text);

// UI
void draw_quest(UI *ui, App *a);
void handle_quest(App *a, Buttons *b);

#endif // QUEST_H
