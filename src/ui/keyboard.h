#ifndef UI_KEYBOARD_H
#define UI_KEYBOARD_H

#include "../app.h"
#include "../ui/ui_shared.h"

/*
 * Standard "Stanza Builder" styling:
 * Alphabetical layout, centered, punctuation on 4th row.
 */
#define KEYBOARD_ROW_COUNT 4

/* Draw the keyboard centered at (cx, y_top).
 * Highlights the key at (row, col).
 */
void keyboard_draw(UI *ui, int cx, int y_top, int row, int col,
                   SDL_Color accent, SDL_Color hi);

/* Handle standard keyboard input (D-pad nav, A to type, X to backspace, Y to
 * space). Updates row/col selection. Modifies 'buf' (of max capacity 'cap')
 * based on input. Returns true if the buffer was modified (char added/removed).
 */
bool keyboard_update(App *a, Buttons *b, char *buf, size_t cap, int *row,
                     int *col);

#endif
