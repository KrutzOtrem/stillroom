#include "features/updater/updater.h"
#include "app.h"
#include "features/updater/update_zip.h"
#include "utils/string_utils.h"
#include <SDL2/SDL.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef STILLROOM_VERSION
#define STILLROOM_VERSION "v0.8.1"
#endif

/* ----------------------------- Update system
 * -----------------------------
 */
static void update_normalize_version(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = 0;
  if (!in)
    return;
  while (*in == 'v' || *in == 'V' || *in == ' ')
    in++;
  safe_snprintf(out, out_sz, "%s", in);
}
static bool update_next_version_num(const char **p, long *out) {
  if (!p || !*p || !out)
    return false;
  const char *s = *p;
  while (*s && !isdigit((unsigned char)*s)) {
    s++;
  }
  if (!*s) {
    *p = s;
    return false;
  }
  long val = 0;
  while (*s && isdigit((unsigned char)*s)) {
    val = (val * 10L) + (*s - '0');
    s++;
  }
  *out = val;
  *p = s;
  return true;
}
static int update_versions_compare(const char *a, const char *b) {
  char na[64], nb[64];
  update_normalize_version(a, na, sizeof(na));
  update_normalize_version(b, nb, sizeof(nb));
  const char *pa = na;
  const char *pb = nb;
  for (int i = 0; i < 4; i++) {
    long va = 0;
    long vb = 0;
    bool ha = update_next_version_num(&pa, &va);
    bool hb = update_next_version_num(&pb, &vb);
    if (!ha && !hb)
      break;
    if (va < vb)
      return -1;
    if (va > vb)
      return 1;
  }
  return 0;
}
static bool update_versions_equal(const char *a, const char *b) {
  char na[64], nb[64];
  update_normalize_version(a, na, sizeof(na));
  update_normalize_version(b, nb, sizeof(nb));
  return strcmp(na, nb) == 0;
}
static bool shell_quote(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return false;
  if (!in) {
    out[0] = 0;
    return false;
  }
  size_t used = 0;
  if (used + 1 >= out_sz)
    return false;
  out[used++] = '\'';
  for (const char *p = in; *p; p++) {
    if (*p == '\'') {
      const char *esc = "'\\''";
      size_t n = strlen(esc);
      if (used + n >= out_sz)
        return false;
      memcpy(out + used, esc, n);
      used += n;
    } else {
      if (used + 1 >= out_sz)
        return false;
      out[used++] = *p;
    }
  }
  if (used + 1 >= out_sz)
    return false;
  out[used++] = '\'';
  out[used] = 0;
  return true;
}
static bool path_dirname(const char *in, char *out, size_t out_sz) {
  if (!in || !in[0] || !out || out_sz == 0)
    return false;
  safe_snprintf(out, out_sz, "%s", in);
  size_t n = strlen(out);
  while (n > 0 && out[n - 1] == '/') {
    out[n - 1] = 0;
    n--;
  }
  char *slash = strrchr(out, '/');
  if (!slash)
    return false;
  if (slash == out) {
    slash[1] = 0;
    return true;
  }
  *slash = 0;
  return true;
}
static void update_append_cacert_flag(const App *a, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = 0;
  char dir[PATH_MAX];
  dir[0] = 0;
  if (a && a->update_exe_path[0]) {
    (void)path_dirname(a->update_exe_path, dir, sizeof(dir));
  }
  // Prefer bundled CA bundle inside the PAK:
  // <pak>/certs/cacert.pem
  if (dir[0]) {
    char pem[PATH_MAX];
    safe_snprintf(pem, sizeof(pem), "%s/certs/cacert.pem", dir);
    struct stat st;
    if (stat(pem, &st) == 0 && st.st_size > 0) {
      char qpem[512];
      if (shell_quote(pem, qpem, sizeof(qpem))) {
        safe_snprintf(out, out_sz, " --cacert %s", qpem);
        return;
      }
    }
  }
  // Fallback to common system paths
  {
    const char *sys[] = {"/etc/ssl/certs/ca-certificates.crt",
                         "/etc/ssl/cert.pem",
                         "/etc/pki/tls/certs/ca-bundle.crt",
                         "/etc/ssl/certs/ca-bundle.crt", NULL};
    struct stat st;
    for (int i = 0; sys[i]; i++) {
      if (stat(sys[i], &st) == 0 && st.st_size > 0) {
        char qsys[512];
        if (shell_quote(sys[i], qsys, sizeof(qsys))) {
          safe_snprintf(out, out_sz, " --cacert %s", qsys);
        }
        return;
      }
    }
  }
}
static bool run_cmd_capture(const char *cmd, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return false;
  out[0] = 0;
  if (!cmd || !cmd[0])
    return false;
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return false;
  size_t total = 0;
  while (!feof(fp) && total + 1 < out_sz) {
    size_t n = fread(out + total, 1, out_sz - total - 1, fp);
    if (n == 0)
      break;
    total += n;
  }
  out[total] = 0;
  int status = pclose(fp);
  if (status == -1)
    return false;
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    return true;
  return false;
}
static bool json_extract_string_at(const char *key_pos, char *out,
                                   size_t out_sz) {
  if (!key_pos || !out || out_sz == 0)
    return false;
  const char *p = strchr(key_pos, ':');
  if (!p)
    return false;
  p++;
  while (*p && *p != '"')
    p++;
  if (*p != '"')
    return false;
  p++;
  size_t used = 0;
  while (*p && *p != '"' && used + 1 < out_sz) {
    if (*p == '\\' && p[1]) {
      p++;
    }
    out[used++] = *p++;
  }
  out[used] = 0;
  return *p == '"';
}
static bool json_extract_string_at_unescape(const char *key_pos, char *out,
                                            size_t out_sz) {
  if (!key_pos || !out || out_sz == 0)
    return false;
  const char *p = strchr(key_pos, ':');
  if (!p)
    return false;
  p++;
  while (*p && *p != '"')
    p++;
  if (*p != '"')
    return false;
  p++;
  size_t used = 0;
  while (*p && *p != '"' && used + 1 < out_sz) {
    if (*p == '\\' && p[1]) {
      char esc = p[1];
      /* Handle Unicode \uXXXX */
      if (esc == 'u') {
        if (p[2] && p[3] && p[4] && p[5] && isxdigit((unsigned char)p[2]) &&
            isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4]) &&
            isxdigit((unsigned char)p[5])) {
          char h[5] = {p[2], p[3], p[4], p[5], 0};
          int cp = (int)strtol(h, NULL, 16);
          p += 6;
          /* Ensure space for up to 3 bytes */
          if (used + 4 >= out_sz)
            break;
          if (cp <= 0x7F) {
            out[used++] = (char)cp;
          } else if (cp <= 0x7FF) {
            out[used++] = (char)(0xC0 | (cp >> 6));
            out[used++] = (char)(0x80 | (cp & 0x3F));
          } else {
            out[used++] = (char)(0xE0 | (cp >> 12));
            out[used++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[used++] = (char)(0x80 | (cp & 0x3F));
          }
          continue;
        }
      }

      p += 2;
      char outc = esc;
      if (esc == 'n')
        outc = '\n';
      else if (esc == 'r') {
        continue; /* Ignore CR */
      } else if (esc == 't')
        outc = '\t';
      else if (esc == '"' || esc == '\\' || esc == '/')
        outc = esc;

      out[used++] = outc;
      continue;
    }
    out[used++] = *p++;
  }
  out[used] = 0;
  return *p == '"';
}
static const char *json_find_key_in_range(const char *start, const char *end,
                                          const char *key) {
  if (!start || !end || !key || start >= end)
    return NULL;
  size_t klen = strlen(key);
  if (klen == 0)
    return NULL;
  const char *p = start;
  while (p + klen <= end) {
    if (memcmp(p, key, klen) == 0)
      return p;
    p++;
  }
  return NULL;
}
static bool update_is_zip_asset(const char *name);               // Forward decl
static bool update_str_endswith(const char *s, const char *suf); // Forward decl

static bool update_find_release_asset_in_range(const char *start,
                                               const char *end,
                                               const char *asset_filter,
                                               char *url_out, size_t url_sz) {
  if (!start || !end || !url_out || url_sz == 0)
    return false;
  url_out[0] = 0;
  const char *p = start;
  while ((p = json_find_key_in_range(p, end, "\"browser_download_url\"")) !=
         NULL) {
    char candidate[512];
    if (json_extract_string_at(p, candidate, sizeof(candidate))) {
      if (asset_filter && asset_filter[0]) {
        if (strstr(candidate, asset_filter)) {
          safe_snprintf(url_out, url_sz, "%s", candidate);
          return true;
        }
      } else if (update_is_zip_asset(candidate)) {
        safe_snprintf(url_out, url_sz, "%s", candidate);
        return true;
      }
    }
    p++;
  }
  return false;
}
static void release_sanitize_tag(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = 0;
  if (!in)
    return;
  size_t used = 0;
  for (const char *p = in; *p && used + 1 < out_sz; p++) {
    if (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.') {
      out[used++] = *p;
    } else {
      out[used++] = '_';
    }
  }
  out[used] = 0;
}
static bool release_build_download_dir(const App *a, const char *tag, char *out,
                                       size_t out_sz) {
  if (!out || out_sz == 0)
    return false;
  char base[PATH_MAX];
  base[0] = 0;
  if (a && a->update_exe_path[0]) {
    if (!path_dirname(a->update_exe_path, base, sizeof(base))) {
      safe_snprintf(base, sizeof(base), "%s", ".");
    }
  } else {
    safe_snprintf(base, sizeof(base), "%s", ".");
  }
  char safe_tag[128];
  release_sanitize_tag(tag ? tag : "release", safe_tag, sizeof(safe_tag));
  safe_snprintf(out, out_sz, "%s/downloads/%s", base,
                safe_tag[0] ? safe_tag : "release");
  return true;
}
static void update_resolve_sources(App *a, char *repo, size_t repo_sz,
                                   char *asset, size_t asset_sz) {
  const char *repo_env = getenv("STILLROOM_UPDATE_REPO");
  const char *asset_env = getenv("STILLROOM_UPDATE_ASSET");
  const char *use_repo = (a && a->cfg.update_repo[0])
                             ? a->cfg.update_repo
                             : (repo_env ? repo_env : "");
  const char *use_asset = (a && a->cfg.update_asset[0])
                              ? a->cfg.update_asset
                              : (asset_env ? asset_env : "stillroom.elf");
  safe_snprintf(repo, repo_sz, "%s", use_repo);
  safe_snprintf(asset, asset_sz, "%s", use_asset);
}
static bool update_str_endswith(const char *s, const char *suf) {
  if (!s || !suf)
    return false;
  size_t ls = strlen(s);
  size_t lf = strlen(suf);
  if (lf > ls)
    return false;
  return strcmp(s + (ls - lf), suf) == 0;
}
static bool update_is_zip_asset(const char *name) {
  return update_str_endswith(name, ".zip");
}

static bool update_tag_has_version(const char *tag) {
  if (!tag)
    return false;
  for (const char *p = tag; *p; p++) {
    if (isdigit((unsigned char)*p))
      return true;
  }
  return false;
}
static bool update_find_latest_release(const App *a, const char *repo,
                                       const char *asset, char *tag_out,
                                       size_t tag_sz, char *url_out,
                                       size_t url_sz, char *notes_out,
                                       size_t notes_sz, char *err,
                                       size_t err_sz) {
  if (err && err_sz)
    err[0] = 0;
  if (notes_out && notes_sz)
    notes_out[0] = 0;
  if (!repo || !repo[0]) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "update source not configured");
    return false;
  }
  char url[512];
  safe_snprintf(url, sizeof(url),
                "https://api.github.com/repos/%s/"
                "releases?per_page=20",
                repo);
  char qurl[1024];
  if (!shell_quote(url, qurl, sizeof(qurl))) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "update URL build failed");
    return false;
  }
  char cacert_flag[600];
  update_append_cacert_flag(a, cacert_flag, sizeof(cacert_flag));
  char cmd[1400];
  safe_snprintf(cmd, sizeof(cmd),
                "curl -sS -L --fail --connect-timeout 10 "
                "--max-time 20 --retry "
                "2 --retry-delay 2 "
                "-H 'Accept: application/vnd.github+json' "
                "-H 'User-Agent: stillroom-updater' %s%s 2>&1",
                qurl, cacert_flag);
  char json[65536];
  if (!run_cmd_capture(cmd, json, sizeof(json))) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "unable to contact update server");
    return false;
  }
  const char *end_json = json + strlen(json);
  const char *p = json;
  bool found = false;
  char best_tag[64] = {0};
  char best_url[512] = {0};
  char best_notes[2048] = {0};
  while ((p = strstr(p, "\"tag_name\"")) != NULL) {
    char tag[64] = {0};
    if (!json_extract_string_at(p, tag, sizeof(tag))) {
      p++;
      continue;
    }
    const char *next_tag = strstr(p + 1, "\"tag_name\"");
    const char *rel_end = next_tag ? next_tag : end_json;
    if (!update_tag_has_version(tag)) {
      p = rel_end;
      continue;
    }
    char url_candidate[512] = {0};
    if (!update_find_release_asset_in_range(p, rel_end, asset, url_candidate,
                                            sizeof(url_candidate))) {
      p = rel_end;
      continue;
    }
    if (!found || update_versions_compare(tag, best_tag) > 0) {
      safe_snprintf(best_tag, sizeof(best_tag), "%s", tag);
      safe_snprintf(best_url, sizeof(best_url), "%s", url_candidate);
      if (notes_out && notes_sz) {
        const char *body_pos = json_find_key_in_range(p, rel_end, "\"body\"");
        if (body_pos) {
          (void)json_extract_string_at_unescape(body_pos, best_notes,
                                                sizeof(best_notes));
        } else {
          best_notes[0] = 0;
        }
      }
      found = true;
    }
    p = rel_end;
  }
  if (!found) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "release asset not found");
    return false;
  }
  safe_snprintf(tag_out, tag_sz, "%s", best_tag);
  safe_snprintf(url_out, url_sz, "%s", best_url);
  if (notes_out && notes_sz) {
    safe_snprintf(notes_out, notes_sz, "%s", best_notes);
  }
  return true;
}
static void release_list_clear(App *a) {
  if (!a)
    return;
  sl_free(&a->release_tags);
  sl_free(&a->release_urls);
  sl_free(&a->release_notes);
  a->release_tags = (StrList){0};
  a->release_urls = (StrList){0};
  a->release_notes = (StrList){0};
  a->release_sel = 0;
  a->release_scroll = 0;
}
static bool update_fetch_release_list(const App *a, const char *repo,
                                      const char *asset_filter,
                                      StrList *tags_out, StrList *urls_out,
                                      StrList *notes_out, char *err,
                                      size_t err_sz) {
  if (err && err_sz)
    err[0] = 0;
  if (!repo || !repo[0]) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "update source not configured");
    return false;
  }
  char update_asset[128] = {0};
  char update_asset_zip[128] = {0};
  if (!asset_filter || !asset_filter[0]) {
    char resolved_repo[128] = {0};
    update_resolve_sources((App *)a, resolved_repo, sizeof(resolved_repo),
                           update_asset, sizeof(update_asset));
    if (update_asset[0] && update_str_endswith(update_asset, ".elf")) {
      safe_snprintf(update_asset_zip, sizeof(update_asset_zip), "%.*s.zip",
                    (int)(strlen(update_asset) - 4), update_asset);
    }
  }
  char url[512];
  safe_snprintf(url, sizeof(url),
                "https://api.github.com/repos/%s/"
                "releases?per_page=20",
                repo);
  char qurl[1024];
  if (!shell_quote(url, qurl, sizeof(qurl))) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "update URL build failed");
    return false;
  }
  char cacert_flag[600];
  update_append_cacert_flag(a, cacert_flag, sizeof(cacert_flag));
  char cmd[1400];
  safe_snprintf(cmd, sizeof(cmd),
                "curl -sS -L --fail --connect-timeout 10 "
                "--max-time 20 --retry "
                "2 --retry-delay 2 "
                "-H 'Accept: application/vnd.github+json' "
                "-H 'User-Agent: stillroom-updater' %s%s 2>&1",
                qurl, cacert_flag);
  char json[65536];
  if (!run_cmd_capture(cmd, json, sizeof(json))) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "unable to contact update server");
    return false;
  }
  const char *end_json = json + strlen(json);
  const char *p = json;
  while ((p = strstr(p, "\"tag_name\"")) != NULL) {
    char tag[64] = {0};
    if (!json_extract_string_at(p, tag, sizeof(tag))) {
      p++;
      continue;
    }
    const char *next_tag = strstr(p + 1, "\"tag_name\"");
    const char *rel_end = next_tag ? next_tag : end_json;
    if (asset_filter && asset_filter[0] && tag[0] &&
        isdigit((unsigned char)tag[0])) {
      p = rel_end;
      continue;
    }
    if (a && a->update_latest_tag[0] &&
        update_versions_equal(tag, a->update_latest_tag)) {
      p = rel_end;
      continue;
    }
    if (update_versions_equal(tag, STILLROOM_VERSION)) {
      p = rel_end;
      continue;
    }
    if (update_asset[0]) {
      char main_url[512] = {0};
      if (update_find_release_asset_in_range(p, rel_end, update_asset, main_url,
                                             sizeof(main_url))) {
        p = rel_end;
        continue;
      }
      if (update_asset_zip[0] &&
          update_find_release_asset_in_range(p, rel_end, update_asset_zip,
                                             main_url, sizeof(main_url))) {
        p = rel_end;
        continue;
      }
    }
    char url_out[512] = {0};
    if (!update_find_release_asset_in_range(p, rel_end, asset_filter, url_out,
                                            sizeof(url_out))) {
      p = rel_end;
      continue;
    }
    char notes_out_local[4096] = {0};
    const char *body_pos = json_find_key_in_range(p, rel_end, "\"body\"");
    if (body_pos) {
      (void)json_extract_string_at_unescape(body_pos, notes_out_local,
                                            sizeof(notes_out_local));
    }
    sl_push(tags_out, tag);
    sl_push(urls_out, url_out);
    if (notes_out) {
      sl_push(notes_out, notes_out_local);
    }
    p = rel_end;
  }
  if (tags_out->count == 0) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "no downloadable releases found");
    return false;
  }
  return true;
}
static bool update_download_asset(const App *a, const char *url,
                                  const char *tmp_path, char *err,
                                  size_t err_sz) {
  if (err && err_sz)
    err[0] = 0;
  if (!url || !url[0]) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "download URL missing");
    return false;
  }
  char qurl[1024];
  char qpath[1024];
  if (!shell_quote(url, qurl, sizeof(qurl)) ||
      !shell_quote(tmp_path, qpath, sizeof(qpath))) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "download path build failed");
    return false;
  }
  char cmd[1400];
  char cacert_flag[600];
  update_append_cacert_flag(a, cacert_flag, sizeof(cacert_flag));
  safe_snprintf(cmd, sizeof(cmd),
                "curl -L --fail --show-error --connect-timeout "
                "10 --max-time 120 --retry "
                "2 --retry-delay 2 -H \"User-Agent: "
                "stillroom-updater\"%s -o %s %s 2>&1",
                cacert_flag, qpath, qurl);
  int rc = system(cmd);
  if (rc != 0) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "download failed");
    return false;
  }
  struct stat st;
  if (stat(tmp_path, &st) != 0 || st.st_size <= 0) {
    if (err && err_sz)
      safe_snprintf(err, err_sz, "%s", "download file missing");
    return false;
  }
  return true;
}
static void update_resolve_apply_path(App *a, const char *asset, char *out,
                                      size_t out_sz) {
  const char *target_env = getenv("STILLROOM_UPDATE_TARGET");
  const char *use_target = (a && a->cfg.update_target_path[0])
                               ? a->cfg.update_target_path
                               : (target_env ? target_env : "");
  if (use_target[0]) {
    safe_snprintf(out, out_sz, "%s", use_target);
    return;
  }
  if (asset && asset[0] && update_is_zip_asset(asset)) {
    if (a && a->update_exe_path[0]) {
      char dir[PATH_MAX];
      safe_snprintf(dir, sizeof(dir), "%s", a->update_exe_path);
      char *slash = strrchr(dir, '/');
      if (slash) {
        *slash = 0;
        safe_snprintf(out, out_sz, "%s", dir);
        return;
      }
    }
    safe_snprintf(out, out_sz, "%s", ".");
    return;
  }
  if (a && a->update_exe_path[0]) {
    safe_snprintf(out, out_sz, "%s", a->update_exe_path);
    return;
  }
  safe_snprintf(out, out_sz, "%s", "stillroom.elf");
}
/* static void update_capture_exe_path(App *a) { ... } */
/* This function was static local but it seems it is called by main or init?
 * Ah, Step 712 showed it called in main? No, it was a forward decl.
 * Let's keep it static if it's only used here. BUT wait, stillroom.c had it as
 * static. Is it called from outside? "update_capture_exe_path" Checked file
 * outline: it *was* static. But "App" struct has "update_exe_path". The
 * function populates it. Where is it called? Probably in app_init or main. If
 * it's called in stillroom.c's main/init, then I cannot move it to updater.c
 * and keep it static if stillroom.c needs to call it. Or I should move the call
 * to updater? Let's assume for now I should Expose it if I move it. But I will
 * check usage of update_capture_exe_path in stillroom.c later. For now, I will
 * include it here as static and if stillroom.c needs it I'll expose it. Wait, I
 * see "update_capture_exe_path(App *a)" in the code I am moving. And I see it
 * called in "update_start_thread" (indirectly?) No. It uses
 * "a->update_exe_path". It must be initialized somewhere. I will add "void
 * update_init(App *a)" to updater.h and move the logic there? Or just expose
 * "update_capture_exe_path". I'll make it "void update_init(App *a)" and call
 * that from stillroom.c.
 */
void update_capture_exe_path(App *a) {
  if (!a)
    return;
  a->update_exe_path[0] = 0;
  char path[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0)
    return;
  path[n] = '\0';
  safe_snprintf(a->update_exe_path, sizeof(a->update_exe_path), "%s", path);
}

/* Public init func I'll add */
/* void update_init(App *a) { update_capture_exe_path(a); } */
/* But wait, I'm just pasting code now. I'll stick to the existing functions.
   I will make update_capture_exe_path NON-STATIC and add to header?
   Or just keep it here and see if it compiles (if unused in stillroom.c).
*/

typedef struct {
  App *a;
  UpdateAction action;
  char repo[128];
  char asset[128];
  char current_version[64];
  char download_url[512];
  char tmp_path[PATH_MAX];
  char release_tag[64];
} UpdateThreadArgs;

static int update_thread_run(void *data) {
  UpdateResult res;
  memset(&res, 0, sizeof(res));
  ReleaseResult release_res;
  memset(&release_res, 0, sizeof(release_res));
  UpdateThreadArgs *args = (UpdateThreadArgs *)data;
  if (!args || !args->a)
    return 0;
  App *a = args->a;
  UpdateAction action = args->action;
  const char *repo = args->repo;
  const char *asset = args->asset;
  const char *current_version = args->current_version;
  const char *download_url = args->download_url;
  const char *tmp_path = args->tmp_path;
  const char *release_tag = args->release_tag;
  if (!a)
    return 0;
  if (action == UPDATE_ACTION_CHECK) {
    char tag[64] = {0};
    char url[512] = {0};
    char err[256] = {0};
    char notes[2048] = {0};
    if (!update_find_latest_release(a, repo, asset, tag, sizeof(tag), url,
                                    sizeof(url), notes, sizeof(notes), err,
                                    sizeof(err))) {
      res.status = UPDATE_STATUS_ERROR;
      safe_snprintf(res.message, sizeof(res.message), "%s",
                    err[0] ? err : "update check failed");
    } else if (update_versions_compare(tag, current_version) <= 0) {
      res.status = UPDATE_STATUS_UP_TO_DATE;
      safe_snprintf(res.message, sizeof(res.message), "%s",
                    "this build is up to date.");
    } else {
      res.status = UPDATE_STATUS_AVAILABLE;
      safe_snprintf(res.message, sizeof(res.message), "%s",
                    "an update is available.");
      safe_snprintf(res.latest_tag, sizeof(res.latest_tag), "%s", tag);
      safe_snprintf(res.download_url, sizeof(res.download_url), "%s", url);
    }
    if (notes[0]) {
      safe_snprintf(res.latest_notes, sizeof(res.latest_notes), "%s", notes);
    }
  } else if (action == UPDATE_ACTION_DOWNLOAD) {
    char err[256] = {0};
    if (!update_download_asset(a, download_url, tmp_path, err, sizeof(err))) {
      res.status = UPDATE_STATUS_ERROR;
      safe_snprintf(res.message, sizeof(res.message), "%s",
                    err[0] ? err : "download failed");
    } else {
      res.status = UPDATE_STATUS_DOWNLOADED;
      safe_snprintf(res.message, sizeof(res.message), "%s",
                    "download complete.");
    }
  } else if (action == UPDATE_ACTION_LIST_RELEASES) {
    char err[256] = {0};
    StrList tags = (StrList){0};
    StrList urls = (StrList){0};
    StrList notes = (StrList){0};
    if (!update_fetch_release_list(a, repo, "", &tags, &urls, &notes, err,
                                   sizeof(err))) {
      release_res.status = RELEASE_STATUS_ERROR;
      safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                    err[0] ? err : "release list failed");
      sl_free(&tags);
      sl_free(&urls);
      sl_free(&notes);
    } else {
      release_res.status = RELEASE_STATUS_READY;
      safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                    "releases loaded.");
      if (a->update_mutex)
        SDL_LockMutex(a->update_mutex);
      release_list_clear(a);
      a->release_tags = tags;
      a->release_urls = urls;
      a->release_notes = notes;
      a->release_sel = 0;
      a->release_scroll = 0;
      if (a->update_mutex)
        SDL_UnlockMutex(a->update_mutex);
    }
  } else if (action == UPDATE_ACTION_DOWNLOAD_RELEASE) {
    char err[256] = {0};
    if (!download_url || !download_url[0]) {
      release_res.status = RELEASE_STATUS_ERROR;
      safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                    "release URL missing");
    } else if (!update_is_zip_asset(download_url)) {
      release_res.status = RELEASE_STATUS_ERROR;
      safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                    "release asset must be a .zip");
    } else if (!update_download_asset(a, download_url, tmp_path, err,
                                      sizeof(err))) {
      release_res.status = RELEASE_STATUS_ERROR;
      safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                    err[0] ? err : "download failed");
    } else {
      char extract_dir[PATH_MAX];
      char root_dir[PATH_MAX];
      char dest_dir[PATH_MAX];
      safe_snprintf(extract_dir, sizeof(extract_dir), "%s.extract", tmp_path);
      update_remove_tree(extract_dir);
      if (!update_extract_zip_dir(tmp_path, extract_dir, err, sizeof(err))) {
        release_res.status = RELEASE_STATUS_ERROR;
        safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                      err[0] ? err : "unzip failed");
      } else if (!update_find_single_root(extract_dir, root_dir,
                                          sizeof(root_dir))) {
        release_res.status = RELEASE_STATUS_ERROR;
        safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                      "unzip failed");
      } else if (!release_build_download_dir(a, release_tag, dest_dir,
                                             sizeof(dest_dir))) {
        release_res.status = RELEASE_STATUS_ERROR;
        safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                      "download path build failed");
      } else if (!update_sync_directories(root_dir, dest_dir, err,
                                          sizeof(err))) {
        release_res.status = RELEASE_STATUS_ERROR;
        safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                      err[0] ? err : "unzip failed");
      } else {
        release_res.status = RELEASE_STATUS_APPLIED;
        safe_snprintf(release_res.message, sizeof(release_res.message), "%s",
                      "downloaded.");
      }
      update_remove_tree(extract_dir);
      (void)unlink(tmp_path);
    }
  }
  if (a->update_mutex)
    SDL_LockMutex(a->update_mutex);
  if (action == UPDATE_ACTION_LIST_RELEASES ||
      action == UPDATE_ACTION_DOWNLOAD_RELEASE) {
    a->release_pending = release_res;
    a->release_pending_ready = true;
  } else {
    a->update_pending = res;
    a->update_pending_ready = true;
  }
  if (a->update_mutex)
    SDL_UnlockMutex(a->update_mutex);
  free(args);
  return 0;
}

void update_start_thread(App *a, UpdateAction action) {
  if (!a)
    return;
  if (a->update_thread)
    return;
  // Ensure we have the exe path captured
  if (!a->update_exe_path[0]) {
    update_capture_exe_path(a);
  }

  char repo[128], asset[128];
  update_resolve_sources(a, repo, sizeof(repo), asset, sizeof(asset));
  char apply_path[PATH_MAX];
  update_resolve_apply_path(a, asset, apply_path, sizeof(apply_path));
  safe_snprintf(a->update_apply_path, sizeof(a->update_apply_path), "%s",
                apply_path);
  char tmp_path[PATH_MAX];
  if (update_is_zip_asset(asset)) {
    safe_snprintf(tmp_path, sizeof(tmp_path), "%s.zip.new", apply_path);
  } else {
    safe_snprintf(tmp_path, sizeof(tmp_path), "%s.new", apply_path);
  }
  safe_snprintf(a->update_tmp_path, sizeof(a->update_tmp_path), "%s", tmp_path);
  safe_snprintf(a->update_download_asset, sizeof(a->update_download_asset),
                "%s", asset);
  a->update_download_is_zip = update_is_zip_asset(asset);
  UpdateThreadArgs *args =
      (UpdateThreadArgs *)calloc(1, sizeof(UpdateThreadArgs));
  if (!args)
    return;
  args->a = a;
  args->action = action;
  safe_snprintf(args->repo, sizeof(args->repo), "%s", repo);
  safe_snprintf(args->asset, sizeof(args->asset), "%s", asset);
  safe_snprintf(args->current_version, sizeof(args->current_version), "%s",
                STILLROOM_VERSION);
  safe_snprintf(args->download_url, sizeof(args->download_url), "%s",
                a->update_download_url);
  safe_snprintf(args->tmp_path, sizeof(args->tmp_path), "%s",
                a->update_tmp_path);
  a->update_action = action;
  a->update_pending_ready = false;
  a->update_thread = SDL_CreateThread(update_thread_run, "update_thread", args);
}

void release_start_list_thread(App *a) {
  if (!a)
    return;
  if (a->update_thread)
    return;
  // Ensure we have the exe path captured
  if (!a->update_exe_path[0]) {
    update_capture_exe_path(a);
  }

  char repo[128], asset[128];
  update_resolve_sources(a, repo, sizeof(repo), asset, sizeof(asset));
  UpdateThreadArgs *args =
      (UpdateThreadArgs *)calloc(1, sizeof(UpdateThreadArgs));
  if (!args)
    return;
  args->a = a;
  args->action = UPDATE_ACTION_LIST_RELEASES;
  safe_snprintf(args->repo, sizeof(args->repo), "%s", repo);
  if (asset[0] && update_is_zip_asset(asset)) {
    safe_snprintf(args->asset, sizeof(args->asset), "%s", asset);
  } else {
    args->asset[0] = 0;
  }
  a->release_status = RELEASE_STATUS_LISTING;
  safe_snprintf(a->release_message, sizeof(a->release_message), "%s",
                "loading releases...");
  a->release_pending_ready = false;
  a->update_action = UPDATE_ACTION_LIST_RELEASES;
  a->update_thread = SDL_CreateThread(update_thread_run, "update_thread", args);
}

void release_start_download_thread(App *a, const char *tag, const char *url) {
  if (!a || !url || !url[0])
    return;
  if (a->update_thread)
    return;
  if (!a->update_exe_path[0]) {
    update_capture_exe_path(a);
  }

  char safe_tag[128];
  release_sanitize_tag(tag ? tag : "release", safe_tag, sizeof(safe_tag));
  char tmp_path[PATH_MAX];
  safe_snprintf(tmp_path, sizeof(tmp_path), "/tmp/stillroom_release_%s.zip",
                safe_tag[0] ? safe_tag : "download");
  UpdateThreadArgs *args =
      (UpdateThreadArgs *)calloc(1, sizeof(UpdateThreadArgs));
  if (!args)
    return;
  args->a = a;
  args->action = UPDATE_ACTION_DOWNLOAD_RELEASE;
  safe_snprintf(args->download_url, sizeof(args->download_url), "%s", url);
  safe_snprintf(args->tmp_path, sizeof(args->tmp_path), "%s", tmp_path);
  safe_snprintf(args->release_tag, sizeof(args->release_tag), "%s",
                tag ? tag : "release");
  a->release_status = RELEASE_STATUS_DOWNLOADING;
  safe_snprintf(a->release_message, sizeof(a->release_message), "%s",
                "downloading...");
  a->release_pending_ready = false;
  a->update_action = UPDATE_ACTION_DOWNLOAD_RELEASE;
  a->update_thread = SDL_CreateThread(update_thread_run, "update_thread", args);
}

void update_poll(App *a) {
  if (!a)
    return;
  if (a->update_status == UPDATE_STATUS_DOWNLOADING) {
    uint64_t now = now_ms();
    if (now - a->update_anim_last_ms >= 500) {
      a->update_anim_last_ms = now;
      a->ui_needs_redraw = true;
    }
  }
  if (!a->update_thread)
    return;
  bool ready = false;
  UpdateResult res;
  ReleaseResult rel_res;
  memset(&res, 0, sizeof(res));
  memset(&rel_res, 0, sizeof(rel_res));
  if (a->update_mutex)
    SDL_LockMutex(a->update_mutex);
  if (a->update_action == UPDATE_ACTION_LIST_RELEASES ||
      a->update_action == UPDATE_ACTION_DOWNLOAD_RELEASE) {
    ready = a->release_pending_ready;
    if (ready) {
      rel_res = a->release_pending;
      a->release_pending_ready = false;
    }
  } else {
    ready = a->update_pending_ready;
    if (ready) {
      res = a->update_pending;
      a->update_pending_ready = false;
    }
  }
  if (a->update_mutex)
    SDL_UnlockMutex(a->update_mutex);
  if (!ready)
    return;
  SDL_WaitThread(a->update_thread, NULL);
  a->update_thread = NULL;
  if (a->update_action == UPDATE_ACTION_LIST_RELEASES ||
      a->update_action == UPDATE_ACTION_DOWNLOAD_RELEASE) {
    a->release_status = rel_res.status;
    if (rel_res.message[0]) {
      safe_snprintf(a->release_message, sizeof(a->release_message), "%s",
                    rel_res.message);
    }
    a->update_action = UPDATE_ACTION_NONE;
    a->ui_needs_redraw = true;
    return;
  }
  a->update_action = UPDATE_ACTION_NONE;
  a->update_status = res.status;
  if (res.message[0])
    safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                  res.message);
  if (res.latest_tag[0])
    safe_snprintf(a->update_latest_tag, sizeof(a->update_latest_tag), "%s",
                  res.latest_tag);
  if (res.download_url[0])
    safe_snprintf(a->update_download_url, sizeof(a->update_download_url), "%s",
                  res.download_url);
  if (res.status == UPDATE_STATUS_AVAILABLE ||
      res.status == UPDATE_STATUS_UP_TO_DATE) {
    safe_snprintf(a->update_latest_notes, sizeof(a->update_latest_notes), "%s",
                  res.latest_notes);
  }
  a->ui_needs_redraw = true;
  if (res.status == UPDATE_STATUS_DOWNLOADED) {
    a->update_status = UPDATE_STATUS_PATCHING;
    a->ui_needs_redraw = true;
    /* Apply patch */
    const char *apply_path =
        a->update_apply_path[0] ? a->update_apply_path : a->update_exe_path;
    if (apply_path[0]) {
      if (a->update_download_is_zip) {
        char err[256] = {0};
        char extract_dir[PATH_MAX];
        char root_dir[PATH_MAX];
        safe_snprintf(extract_dir, sizeof(extract_dir), "%s.extract",
                      a->update_tmp_path);
        update_remove_tree(extract_dir);
        if (!update_extract_zip_dir(a->update_tmp_path, extract_dir, err,
                                    sizeof(err))) {
          a->update_status = UPDATE_STATUS_ERROR;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        err[0] ? err : "unzip failed.");
        } else if (!update_find_single_root(extract_dir, root_dir,
                                            sizeof(root_dir))) {
          a->update_status = UPDATE_STATUS_ERROR;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        "unzip failed.");
        } else if (!update_sync_directories(root_dir, apply_path, err,
                                            sizeof(err))) {
          a->update_status = UPDATE_STATUS_ERROR;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        err[0] ? err : "patching failed.");
        } else {
          a->update_status = UPDATE_STATUS_APPLIED;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        "update applied.");
          (void)unlink(a->update_tmp_path);
        }
        update_remove_tree(extract_dir);
      } else {
        struct stat st;
        mode_t mode = 0755;
        if (stat(apply_path, &st) == 0)
          mode = st.st_mode;
        char backup[PATH_MAX];
        safe_snprintf(backup, sizeof(backup), "%s.bak", apply_path);
        rename(apply_path, backup);
        if (rename(a->update_tmp_path, apply_path) == 0) {
          chmod(apply_path, mode);
          a->update_status = UPDATE_STATUS_APPLIED;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        "update applied.");
        } else {
          /* rollback */
          rename(backup, apply_path);
          a->update_status = UPDATE_STATUS_ERROR;
          safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                        "patching failed.");
        }
      }
    } else {
      a->update_status = UPDATE_STATUS_ERROR;
      safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                    "app path missing.");
    }
  }
}

void update_start_check(App *a, bool force) {
  if (!a || a->update_thread)
    return;
  if (a->update_status == UPDATE_STATUS_DOWNLOADING ||
      a->update_status == UPDATE_STATUS_PATCHING) {
    return;
  }
  if (!force && (a->update_status == UPDATE_STATUS_AVAILABLE ||
                 a->update_status == UPDATE_STATUS_DOWNLOADED ||
                 a->update_status == UPDATE_STATUS_APPLIED ||
                 a->update_status == UPDATE_STATUS_CHECKING)) {
    return;
  }
  a->update_last_check_ms = now_ms();
  a->update_status = UPDATE_STATUS_CHECKING;
  safe_snprintf(a->update_message, sizeof(a->update_message), "%s",
                "checking for updates...");
  update_start_thread(a, UPDATE_ACTION_CHECK);
}

void update_check_on_settings_open(App *a) {
  if (!a)
    return;
  const uint64_t cooldown_ms = 10ULL * 60ULL * 1000ULL;
  if (a->update_last_check_ms &&
      now_ms() - a->update_last_check_ms < cooldown_ms) {
    return;
  }
  update_start_check(a, false);
}
