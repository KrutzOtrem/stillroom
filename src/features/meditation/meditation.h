#ifndef MEDITATION_H
#define MEDITATION_H

#include "../../app.h"

/* Picker helpers */
int meditation_total_minutes(const App *a);
int meditation_bell_max_minutes(const App *a);
int meditation_minutes_from_label(const char *label);
void meditation_clamp_bell(App *a);

/* Picker UI */
void draw_meditation_picker(UI *ui, App *a);

#endif /* MEDITATION_H */
