#ifndef FOCUS_MENU_H
#define FOCUS_MENU_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdint.h>

#include "../../app.h"

/* Focus menu UI */
void draw_focus_menu(UI *ui, App *a);
void draw_focus_text(UI *ui, App *a);

/* Focus menu utilities (called from pickers) */
void focus_menu_sync_sel_to_current(App *a);
int focus_menu_entries_build(App *a, const char **out, int out_cap);

#endif /* FOCUS_MENU_H */
