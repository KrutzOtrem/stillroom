#ifndef TIMER_H
#define TIMER_H

#include "../../app.h"

/* Picker UI */
void draw_pomo_picker(UI *ui, App *a);
void draw_custom_picker(UI *ui, App *a);
void handle_pomo_pick(App *a, Buttons *b);
void handle_custom_pick(App *a, Buttons *b);

/* Timer quick menu */
void draw_timer_quick_menu(UI *ui, App *a);
void handle_timer_quick_menu(UI *ui, App *a, Buttons *b);

/* Main timer screen */
void draw_timer(UI *ui, App *a);
void handle_timer(UI *ui, App *a, Buttons *b);

/* Utility */
void pomo_build_order_string(char *out, size_t cap, int sessions, int current);

#endif /* TIMER_H */
