#ifndef UPDATER_H
#define UPDATER_H

#include "app.h"
#include <stdbool.h>

void update_start_check(App *a, bool force);
void update_poll(App *a);
void update_check_on_settings_open(App *a);
void update_capture_exe_path(App *a);
void update_start_thread(App *a, UpdateAction action);

/* Thread functions/helpers that might need to be exposed if used elsewhere?
   No, they seem internal, but update_thread_run calls them.
   update_thread_run is passed to SDL_CreateThread, so it needs to be visible to
   updater.c. It doesn't need to be in the header unless main calls it directly.
*/

/* Release management */
void release_start_list_thread(App *a);
void release_start_download_thread(App *a, const char *tag, const char *url);

#endif
