#include "features/updater/update_zip.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>

static void update_safe_snprintf(char *dst, size_t cap, const char *fmt, ...) {
  if (!dst || cap == 0)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dst, cap, fmt, ap);
  va_end(ap);
  dst[cap - 1] = '\0';
}

static bool update_shell_quote(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return false;
  if (!in) {
    out[0] = 0;
    return true;
  }
  size_t len = strlen(in);
  if (len == 0) {
    if (out_sz < 3)
      return false;
    strcpy(out, "''");
    return true;
  }
  size_t need = 2; /* quotes */
  for (size_t i = 0; i < len; i++) {
    need += (in[i] == '\'') ? 4 : 1;
  }
  if (need + 1 > out_sz)
    return false;
  char *p = out;
  *p++ = '\'';
  for (size_t i = 0; i < len; i++) {
    if (in[i] == '\'') {
      *p++ = '\'';
      *p++ = '\\';
      *p++ = '\'';
      *p++ = '\'';
    } else {
      *p++ = in[i];
    }
  }
  *p++ = '\'';
  *p = '\0';
  return true;
}

static bool update_mkpath(const char *path, mode_t mode) {
  if (!path || !path[0])
    return false;
  char tmp[PATH_MAX];
  update_safe_snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0)
    return false;
  if (tmp[len - 1] == '/')
    tmp[len - 1] = 0;
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(tmp, mode) != 0 && errno != EEXIST)
        return false;
      *p = '/';
    }
  }
  if (mkdir(tmp, mode) != 0 && errno != EEXIST)
    return false;
  return true;
}

static bool update_copy_file(const char *src, const char *dst, mode_t mode) {
  FILE *in = fopen(src, "rb");
  if (!in)
    return false;
  FILE *out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  char buf[8192];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    fwrite(buf, 1, n, out);
  }
  fclose(out);
  fclose(in);
  chmod(dst, mode & 0777);
  return true;
}

static bool update_copy_tree(const char *src, const char *dst, char *err,
                             size_t err_sz) {
  struct stat st;
  if (stat(src, &st) != 0) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "patching failed");
    return false;
  }
  if (S_ISDIR(st.st_mode)) {
    if (!update_mkpath(dst, 0755)) {
      if (err && err_sz)
        update_safe_snprintf(err, err_sz, "%s", "patching failed");
      return false;
    }
    DIR *dir = opendir(src);
    if (!dir) {
      if (err && err_sz)
        update_safe_snprintf(err, err_sz, "%s", "patching failed");
      return false;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      char src_path[PATH_MAX];
      char dst_path[PATH_MAX];
      update_safe_snprintf(src_path, sizeof(src_path), "%s/%s", src,
                           entry->d_name);
      update_safe_snprintf(dst_path, sizeof(dst_path), "%s/%s", dst,
                           entry->d_name);
      if (!update_copy_tree(src_path, dst_path, err, err_sz)) {
        closedir(dir);
        return false;
      }
    }
    closedir(dir);
    return true;
  }
  struct stat dst_st;
  if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
    update_remove_tree(dst);
  }
  /* Overwrite existing files so updates replace previous content. */
  if (!update_copy_file(src, dst, st.st_mode)) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "patching failed");
    return false;
  }
  return true;
}

bool update_extract_zip_dir(const char *zip_path, const char *dest_dir,
                            char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = 0;
  if (!zip_path || !zip_path[0] || !dest_dir || !dest_dir[0]) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "unzip path missing");
    return false;
  }
  if (!update_mkpath(dest_dir, 0755)) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "unzip path build failed");
    return false;
  }
  int zip_err = 0;
  zip_t *za = zip_open(zip_path, 0, &zip_err);
  if (!za) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "unzip failed");
    return false;
  }
  zip_int64_t num_entries = zip_get_num_entries(za, 0);
  for (zip_int64_t i = 0; i < num_entries; i++) {
    const char *name = zip_get_name(za, i, 0);
    if (!name)
      continue;
    char full_path[PATH_MAX];
    update_safe_snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, name);
    size_t name_len = strlen(name);
    if (name_len > 0 && name[name_len - 1] == '/') {
      update_mkpath(full_path, 0755);
      continue;
    }
    char *last_slash = strrchr(full_path, '/');
    if (last_slash) {
      *last_slash = '\0';
      update_mkpath(full_path, 0755);
      *last_slash = '/';
    }
    zip_file_t *zf = zip_fopen_index(za, i, 0);
    if (!zf)
      continue;
    FILE *out = fopen(full_path, "wb");
    if (!out) {
      zip_fclose(zf);
      continue;
    }
    char buf[8192];
    zip_int64_t bytes_read;
    while ((bytes_read = zip_fread(zf, buf, sizeof(buf))) > 0) {
      fwrite(buf, 1, (size_t)bytes_read, out);
    }
    fclose(out);
    zip_fclose(zf);
    if (strstr(name, ".elf") || strstr(name, ".sh")) {
      chmod(full_path, 0755);
    }
  }
  zip_close(za);
  return true;
}

bool update_remove_tree(const char *path) {
  if (!path || !path[0])
    return false;
  char qpath[1024];
  if (!update_shell_quote(path, qpath, sizeof(qpath)))
    return false;
  char cmd[1200];
  update_safe_snprintf(cmd, sizeof(cmd), "rm -rf %s 2>/dev/null", qpath);
  (void)system(cmd);
  return true;
}

bool update_find_single_root(const char *base_dir, char *out, size_t out_sz) {
  if (!base_dir || !base_dir[0] || !out || out_sz == 0)
    return false;
  DIR *dir = opendir(base_dir);
  if (!dir)
    return false;
  int count = 0;
  char candidate[PATH_MAX] = {0};
  bool candidate_is_dir = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    count++;
    if (count > 1)
      break;
    char path[PATH_MAX];
    update_safe_snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
      update_safe_snprintf(candidate, sizeof(candidate), "%s", path);
      candidate_is_dir = true;
    }
  }
  closedir(dir);
  if (count == 1 && candidate_is_dir) {
    update_safe_snprintf(out, out_sz, "%s", candidate);
    return true;
  }
  update_safe_snprintf(out, out_sz, "%s", base_dir);
  return true;
}

bool update_sync_directories(const char *src, const char *dst, char *err,
                             size_t err_sz) {
  if (err && err_sz)
    err[0] = 0;
  if (!src || !src[0] || !dst || !dst[0]) {
    if (err && err_sz)
      update_safe_snprintf(err, err_sz, "%s", "apply path missing");
    return false;
  }
  return update_copy_tree(src, dst, err, err_sz);
}
