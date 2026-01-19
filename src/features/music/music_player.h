#ifndef FEATURES_MUSIC_PLAYER_H
#define FEATURES_MUSIC_PLAYER_H

#include "app.h"
#include <stdbool.h>

typedef struct App App;

void music_player_init(App *a);
void music_player_load_state(App *a);
void music_player_save_state(const App *a);

void music_player_set_folder(App *a, const char *folder,
                             const char *tracking_filename);
void music_player_build_playlist(App *a, bool was_playing);
void music_player_refresh_labels(App *a);

void music_player_play(App *a);
void music_player_stop(App *a);
void music_player_next(App *a);
void music_player_prev(App *a);

void music_player_sync_folder_list(App *a);
void music_player_update(App *a);

// Ambience helpers (closely tied to audio state)
void music_player_update_ambience(App *a, bool user_action);
void music_player_ambience_path_from_name(const App *a, char *out, size_t cap);

#endif
