#include "file_utils.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

bool is_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return false;
  return S_ISDIR(st.st_mode);
}

bool is_file(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return false;
  return S_ISREG(st.st_mode);
}

void ensure_dir(const char *path) {
  if (!path || !path[0])
    return;
  if (is_dir(path))
    return;
  // Use mkdir, default mode 0755
#ifdef _WIN32
  mkdir(path);
#else
  mkdir(path, 0755);
#endif
}

char *read_entire_file(const char *path, size_t *out_len) {
  if (out_len)
    *out_len = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = 0;
  if (out_len)
    *out_len = got;
  return buf;
}

StrList list_dirs_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
    // Note: is_dir checks absolute path if we cd'd or relative if not.
    // If root is relative, path is relative.
    if (is_dir(path))
      sl_push(&out, e->d_name);
  }
  closedir(d);
  sl_sort(&out);
  return out;
}

StrList list_files_png_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".png") ||
        ends_with_icase(e->d_name, ".jpg")) {
      sl_push(&out, e->d_name);
    }
  }
  closedir(d);
  sl_sort(&out);
  return out;
}

StrList list_font_files_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".ttf") ||
        ends_with_icase(e->d_name, ".otf")) {
      sl_push(&out, e->d_name);
    }
  }
  closedir(d);
  sl_sort(&out);
  return out;
}

StrList list_wav_files_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".wav")) {
      sl_push(&out, e->d_name);
    }
  }
  closedir(d);
  sl_sort(&out);
  return out;
}

StrList list_audio_files_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".wav") ||
        ends_with_icase(e->d_name, ".mp3")) {
      sl_push(&out, e->d_name);
    }
  }
  closedir(d);
  sl_sort(&out);
  return out;
}

/* Booklet sorting helpers */
bool parse_prefixed_index(const char *name, int *out_idx,
                          const char **out_after) {
  if (!name || !out_idx)
    return false;

  const char *p = name;
  while (*p == ' ' || *p == '\t')
    p++;
  if (!isdigit((unsigned char)*p))
    return false;

  int v = 0;
  const char *digits_start = p;
  while (isdigit((unsigned char)*p)) {
    v = (v * 10) + (*p - '0');
    p++;
  }

  // const char *after_digits = p; // Unused variable warning fix

  /* Skip whitespace after digits (but remember whether it existed). */
  bool had_ws = false;
  while (*p == ' ' || *p == '\t') {
    had_ws = true;
    p++;
  }

  /* Explicit delimiter forms. */
  if (*p == ')' || *p == '.' || *p == '-' || *p == '_') {
    p++;
    while (*p == ' ' || *p == '\t')
      p++;
    *out_idx = v;
    if (out_after)
      *out_after = p;
    return true;
  }

  /* Whitespace-only form: "12 Title". Keep it conservative. */
  if (had_ws && v >= 0 && v <= 999 && *p && !isdigit((unsigned char)*p)) {
    *out_idx = v;
    if (out_after)
      *out_after = p;
    return true;
  }

  (void)digits_start;
  return false;
}

static int cmp_booklet_files(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  int ia = 2147483647, ib = 2147483647;
  parse_prefixed_index(*sa, &ia, NULL);
  parse_prefixed_index(*sb, &ib, NULL);
  if (ia != ib)
    return (ia < ib) ? -1 : 1;
  return strcmp(*sa, *sb);
}

static void sl_sort_booklets(StrList *l) {
  if (!l || l->count <= 1)
    return;
  qsort(l->items, (size_t)l->count, sizeof(char *), cmp_booklet_files);
}

StrList list_txt_files_in(const char *root) {
  StrList out = {0};
  DIR *d = opendir(root);
  if (!d)
    return out;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".txt"))
      sl_push(&out, e->d_name);
  }
  closedir(d);
  sl_sort_booklets(&out);
  return out;
}

bool dir_has_png_jpg(const char *root) {
  DIR *d = opendir(root);
  if (!d)
    return false;
  struct dirent *e;
  bool found = false;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (ends_with_icase(e->d_name, ".png") ||
        ends_with_icase(e->d_name, ".jpg") ||
        ends_with_icase(e->d_name, ".jpeg")) {
      found = true;
      break;
    }
  }
  closedir(d);
  return found;
}

bool dir_has_subdir_with_png_jpg(const char *root) {
  DIR *d = opendir(root);
  if (!d)
    return false;
  struct dirent *e;
  bool found = false;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    char path[PATH_MAX];
    safe_snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
    if (is_dir(path)) {
      if (dir_has_png_jpg(path)) {
        found = true;
        break;
      }
    }
  }
  closedir(d);
  return found;
}
