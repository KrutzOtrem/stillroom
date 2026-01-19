#ifndef TASKS_H
#define TASKS_H

#include "../../app.h"

void tasks_init(App *a);
void tasks_cleanup(App *a);
void tasks_reload(App *a);

void handle_tasks_pick(App *a, Buttons *b);
void handle_tasks_list(App *a, Buttons *b);
void handle_tasks_text(App *a, Buttons *b);

void draw_tasks_pick(UI *ui, App *a);
void draw_tasks_list(UI *ui, App *a);
void draw_tasks_text(UI *ui, App *a);

bool task_is_done(App *a, const char *text);

#endif // TASKS_H
