#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "string_utils.h"
#include <stdbool.h>
#include <stddef.h>

bool is_dir(const char *path);
bool is_file(const char *path);
void ensure_dir(const char *path);
char *read_entire_file(const char *path, size_t *out_len);

/* List helpers - return sorted list */
StrList list_dirs_in(const char *root);
StrList list_txt_files_in(const char *root);
StrList list_files_png_in(const char *root);
StrList list_font_files_in(const char *root);
StrList list_wav_files_in(const char *root);
StrList list_audio_files_in(const char *root);
bool dir_has_png_jpg(const char *root);
bool dir_has_subdir_with_png_jpg(const char *root);

bool parse_prefixed_index(const char *name, int *out_idx,
                          const char **out_after);

#endif // FILE_UTILS_H
