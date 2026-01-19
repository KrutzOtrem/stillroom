#ifndef BOOKLETS_H
#define BOOKLETS_H

#include "../../app.h"

/* Initialization and cleanup */
void booklets_init(App *a);
void booklets_cleanup(App *a);

/* Opening/closing */
void booklets_open_toggle(UI *ui, App *a);
void booklets_open_from_menu(App *a);

/* Input handling */
void handle_booklets(UI *ui, App *a, Buttons *b);

/* Rendering */
void draw_booklets(UI *ui, App *a);

/* State persistence */
int booklets_state_get_page(const char *booklet_file);
void booklets_state_set_page(const char *booklet_file, int page);

void booklets_render_clear(App *a);
void booklets_list_clear(App *a);

#endif // BOOKLETS_H
