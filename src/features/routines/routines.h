#ifndef ROUTINES_H
#define ROUTINES_H

#include "routine_types.h"

/* Forward declarations */
typedef struct App App;
typedef struct UI UI;
typedef struct Buttons Buttons;

void routine_load(App *a);
void routine_save_data(App *a);
void routine_history_load(App *a);
void routine_history_save(App *a);
int routine_today_weekday(void);

/* UI Handlers */
void draw_routine_list(UI *ui, App *a);
void handle_routine_list(App *a, Buttons *b);

void draw_routine_grid(UI *ui, App *a);
void handle_routine_grid(App *a, Buttons *b);

void draw_routine_edit(UI *ui, App *a);
void handle_routine_edit(App *a, Buttons *b);

void draw_routine_entry_picker(UI *ui, App *a);
void handle_routine_entry_picker(App *a, Buttons *b);

void draw_routine_entry_text(UI *ui, App *a);
void handle_routine_entry_text(App *a, Buttons *b);

#endif // ROUTINES_H
