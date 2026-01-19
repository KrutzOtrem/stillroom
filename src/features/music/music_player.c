#include "features/music/music_player.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "audio_engine.h"
#include "soundfx.h"
#include "ui/ui_shared.h"
#include "utils/file_utils.h"
#include "utils/string_utils.h"

// Forward declaration from stillroom.c (or utils)
void build_music_root(const App *a, char *out, size_t cap) {
  /* assuming "music" logical name -> ./music local dir */
  if (!a || !out || cap == 0)
    return;
  // If config has absolute path? No, assuming local for now based on legacy
  // code
  char exe[PATH_MAX];
  // Re-implementing logic or we need to expose it.
  // Legacy code used "music" folder relative to exe or CWD.
  // We will assume CWD is set correctly by main.
  safe_snprintf(out, cap, "%s", "music");
}

static void build_current_folder_path(const App *a, char *out, size_t cap) {
  char root[PATH_MAX];
  build_music_root(a, root, sizeof(root));
  if (a->music_folder[0]) {
    safe_snprintf(out, cap, "%s/%s", root, a->music_folder);
  } else {
    safe_snprintf(out, cap, "%s", root);
  }
}

void music_player_init(App *a) {
  if (!a)
    return;
  a->music_folder_idx = 0;
  a->music_folder[0] = 0;
  a->music_song[0] = 0;
  // Ensure audio engine init if not already (it's done in main usually)
}

void music_player_load_state(App *a) {
  if (!a)
    return;
  FILE *f = fopen(MUSIC_STATE_PATH, "r");
  if (!f)
    return;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char key[256], val[256];
    char *eq = strchr(line, '=');
    if (eq) {
      *eq = 0;
      safe_snprintf(key, sizeof(key), "%s", line);
      safe_snprintf(val, sizeof(val), "%s", eq + 1);
      trim_ascii_inplace(key);
      trim_ascii_inplace(val);
      if (strcmp(key, "folder") == 0) {
        sl_push(&a->music_last_folders, val);
      } else if (strcmp(key, "track") == 0) {
        sl_push(&a->music_last_tracks, val);
      }
    }
  }
  fclose(f);
}

void music_player_save_state(const App *a) {
  if (!a)
    return;
  // Update the current folder's last track in the list before saving
  if (a->music_folder[0] && a->music_song[0]) {
    int idx = sl_find(&((App *)a)->music_last_folders, a->music_folder);
    if (idx >= 0) {
      if (idx < a->music_last_tracks.count) {
        free(a->music_last_tracks.items[idx]);
        a->music_last_tracks.items[idx] = strdup(a->music_song);
      }
    } else {
      sl_push(&((App *)a)->music_last_folders, a->music_folder);
      sl_push(&((App *)a)->music_last_tracks, a->music_song);
    }
  }

  FILE *f = fopen(MUSIC_STATE_PATH, "w");
  if (!f)
    return;
  for (int i = 0; i < a->music_last_folders.count; i++) {
    if (i < a->music_last_tracks.count) {
      fprintf(f, "folder=%s\n", a->music_last_folders.items[i]);
      fprintf(f, "track=%s\n", a->music_last_tracks.items[i]);
    }
  }
  fclose(f);
}

void music_player_set_folder(App *a, const char *folder,
                             const char *tracking_filename) {
  if (!a)
    return;
  safe_snprintf(a->music_folder, sizeof(a->music_folder), "%s",
                folder ? folder : "");
  safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s",
                a->music_folder);

  // If specific track requested (e.g. from restore), use it
  if (tracking_filename && tracking_filename[0]) {
    safe_snprintf(a->music_song, sizeof(a->music_song), "%s",
                  tracking_filename);
  } else {
    // Look up last track for this folder
    int idx = sl_find(&a->music_last_folders, a->music_folder);
    if (idx >= 0 && idx < a->music_last_tracks.count) {
      safe_snprintf(a->music_song, sizeof(a->music_song), "%s",
                    a->music_last_tracks.items[idx]);
    } else {
      a->music_song[0] = 0;
    }
  }
}

void music_player_build_playlist(App *a, bool was_playing) {
  if (!a)
    return;
  sl_free(&a->musicq.tracks);
  a->musicq.idx = -1;
  a->musicq.active = false;

  char path[PATH_MAX];
  build_current_folder_path(a, path, sizeof(path));
  if (!is_dir(path))
    return;

  // List all audio files
  DIR *d = opendir(path);
  if (!d)
    return;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    if (ends_with_icase(ent->d_name, ".mp3") ||
        ends_with_icase(ent->d_name, ".wav") ||
        ends_with_icase(ent->d_name, ".ogg")) {
      sl_push(&a->musicq.tracks, ent->d_name);
    }
  }
  closedir(d);

  sl_sort(&a->musicq.tracks);

  if (a->musicq.tracks.count > 0) {
    // Try to find current song
    if (a->music_song[0]) {
      int idx = sl_find(&a->musicq.tracks, a->music_song);
      if (idx >= 0) {
        a->musicq.idx = idx;
      } else {
        a->musicq.idx = 0;
        safe_snprintf(a->music_song, sizeof(a->music_song), "%s",
                      a->musicq.tracks.items[0]);
      }
    } else {
      a->musicq.idx = 0;
      safe_snprintf(a->music_song, sizeof(a->music_song), "%s",
                    a->musicq.tracks.items[0]);
    }

    if (was_playing && a->cfg.music_enabled && !a->music_user_paused) {
      music_player_play(a);
    }
  }
}

void music_player_refresh_labels(App *a) {
  // Just ensures folder idx matches string
  if (!a)
    return;
  if (a->music_folders.count > 0 && a->music_folder[0]) {
    int idx = sl_find(&a->music_folders, a->music_folder);
    if (idx >= 0)
      a->music_folder_idx = idx;
  }
  if (!a->cfg.music_enabled) {
    safe_snprintf(a->music_song, sizeof(a->music_song), "%s", "off");
  }
}

void music_player_play(App *a) {
  if (!a || !a->audio)
    return;
  if (a->musicq.tracks.count == 0)
    return;
  if (a->musicq.idx < 0 || a->musicq.idx >= a->musicq.tracks.count)
    a->musicq.idx = 0;

  const char *track = a->musicq.tracks.items[a->musicq.idx];
  safe_snprintf(a->music_song, sizeof(a->music_song), "%s", track);
  strip_ext_inplace(a->music_song);

  char root[PATH_MAX];
  build_current_folder_path(a, root, sizeof(root));
  char full[PATH_MAX];
  safe_snprintf(full, sizeof(full), "%s/%s", root, track);

  // Use audio engine
  audio_engine_play_music(a->audio, full, false);
  a->musicq.active = true;
  a->music_has_started = true;
}

void music_player_stop(App *a) {
  if (!a || !a->audio)
    return;
  audio_engine_stop_music(a->audio);
  a->musicq.active = false;
}

void music_player_next(App *a) {
  if (!a || a->musicq.tracks.count == 0)
    return;
  a->musicq.idx++;
  if (a->musicq.idx >= a->musicq.tracks.count)
    a->musicq.idx = 0;
  music_player_play(a);
}

void music_player_prev(App *a) {
  if (!a || a->musicq.tracks.count == 0)
    return;
  a->musicq.idx--;
  if (a->musicq.idx < 0)
    a->musicq.idx = a->musicq.tracks.count - 1;
  music_player_play(a);
}

void music_player_sync_folder_list(App *a) {
  if (!a)
    return;
  sl_free(&a->music_folders);
  char root[PATH_MAX];
  build_music_root(a, root, sizeof(root));

  DIR *d = opendir(root);
  if (!d)
    return;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    char full[PATH_MAX];
    safe_snprintf(full, sizeof(full), "%s/%s", root, ent->d_name);
    if (is_dir(full)) {
      sl_push(&a->music_folders, ent->d_name);
    }
  }
  closedir(d);
  sl_sort(&a->music_folders);

  // Sync current selection from config
  int idx = sl_find(&a->music_folders, a->cfg.music_folder);
  a->music_folder_idx = (idx >= 0) ? idx : 0;
  if (a->music_folders.count > 0) {
    // Write back confirmed folder to cfg and runtime mirror
    safe_snprintf(a->cfg.music_folder, sizeof(a->cfg.music_folder), "%s",
                  a->music_folders.items[a->music_folder_idx]);
    safe_snprintf(a->music_folder, sizeof(a->music_folder), "%s",
                  a->cfg.music_folder);
  } else {
    a->music_folder[0] = 0;
  }
}

void music_player_update(App *a) {
  if (!a || !a->audio)
    return;

  // Service the engine
  audio_engine_update(a->audio);

  // Check for track end
  if (audio_engine_pop_music_ended(a->audio)) {
    music_player_next(a);
  }
}

// Ambience helpers
static void build_ambience_path_from_name(const App *a, char *out, size_t cap) {
  if (!a || !out || cap == 0)
    return;
  out[0] = 0;
  if (!a->cfg.ambience_name[0] || strcmp(a->cfg.ambience_name, "off") == 0)
    return;

  // Logic from stillroom.c: try scenes/<location>/moods/... then
  // sounds/ambience/... For brevity/modularity, we'll implement the search
  // logic here or call a shared helper. We need to know the current scene
  // (location) to check scenes/<loc>/moods.

  // 1. Check scene-specific mood
  if (a->scene_name[0]) {
    char check[PATH_MAX];
    // Assuming relative root
    safe_snprintf(check, sizeof(check), "scenes/%s/moods/%s", a->scene_name,
                  a->cfg.ambience_name);
    if (is_dir(check)) {
      // It's a folder mood (containing multiple files? or just a category?)
      // Original logic: "mood_display_from_folder" suggested moods are
      // folders? Actually, if it's a file? Re-reading stillroom.c logic:
      // ambience might be a file inside scenes/<loc>/moods/
    }

    // Check for direct file match in scenes/<loc>/moods/
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "scenes/%s/moods/%s.wav", a->scene_name,
                  a->cfg.ambience_name);
    if (is_file(path)) {
      safe_snprintf(out, cap, "%s", path);
      return;
    }
    safe_snprintf(path, sizeof(path), "scenes/%s/moods/%s.mp3", a->scene_name,
                  a->cfg.ambience_name);
    if (is_file(path)) {
      safe_snprintf(out, cap, "%s", path);
      return;
    }
  }

  // 2. Check global ambience
  char path[PATH_MAX];
  safe_snprintf(path, sizeof(path), "sounds/ambience/%s.wav",
                a->cfg.ambience_name);
  if (is_file(path)) {
    safe_snprintf(out, cap, "%s", path);
    return;
  }
  safe_snprintf(path, sizeof(path), "sounds/ambience/%s.mp3",
                a->cfg.ambience_name);
  if (is_file(path)) {
    safe_snprintf(out, cap, "%s", path);
    return;
  }
}

void music_player_update_ambience(App *a, bool user_action) {
  if (!a || !a->audio)
    return;

  if (!a->cfg.ambience_enabled) {
    audio_engine_stop_ambience(a->audio);
    return;
  }

  char path[PATH_MAX];
  build_ambience_path_from_name(a, path, sizeof(path));

  if (path[0]) {
    // restart_if_same = user_action. If just refreshing (e.g. scene change)
    // and same file, don't restart.
    audio_engine_play_ambience(a->audio, path, user_action);
  } else {
    audio_engine_stop_ambience(a->audio);
  }
}
void music_player_ambience_path_from_name(const App *a, char *out, size_t cap) {
  build_ambience_path_from_name(a, out, cap);
}
