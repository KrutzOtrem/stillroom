#ifndef UPDATE_ZIP_H
#define UPDATE_ZIP_H

#include <stdbool.h>
#include <stddef.h>

bool update_extract_zip_dir(const char* zip_path, const char* dest_dir,
                            char* err, size_t err_sz);
bool update_find_single_root(const char* base_dir, char* out, size_t out_sz);
bool update_sync_directories(const char* src, const char* dst, char* err, size_t err_sz);
bool update_remove_tree(const char* path);

#endif
