#include "deps.h"
#include "manifest.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void cache_path(char *dst, size_t dstsz, const char *name) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dst, dstsz, "%s/.cache/smelt/%s", home, name);
}

static void repo_name(char *dst, size_t dstsz, const char *url) {
  const char *last_slash = strrchr(url, '/');
  if (last_slash)
    last_slash++;
  else
    last_slash = url;
  snprintf(dst, dstsz, "%s", last_slash);

  size_t len = strlen(dst);
  if (len > 4 && strcmp(dst + len - 4, ".git") == 0)
    dst[len - 4] = '\0';
}

static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void ensure_dirs(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

static int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in) {
    perror(src);
    return 0;
  }
  FILE *out = fopen(dst, "wb");
  if (!out) {
    perror(dst);
    fclose(in);
    return 0;
  }

  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    fwrite(buf, 1, n, out);

  fclose(in);
  fclose(out);
  return 1;
}

static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static int toml_has_value(const char *section_key, const char *value) {
  FILE *fp = fopen("smelt.toml", "r");
  if (!fp)
    return 0;

  char line[1024];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, value)) {
      found = 1;
      break;
    }
  }
  fclose(fp);
  return found;
}

static int toml_append_array(const char *key, const char *value) {
  if (toml_has_value(key, value)) {
    printf("smelt: %s already has %s\n", key, value);
    return 1;
  }

  FILE *fp = fopen("smelt.toml", "r");
  if (!fp)
    return 0;
  char content[65536] = {0};
  fread(content, 1, sizeof(content) - 1, fp);
  fclose(fp);

  char search[256];
  snprintf(search, sizeof(search), "%s", key);
  char *pos = strstr(content, search);

  if (pos) {
    char *bracket = strchr(pos, ']');
    if (!bracket)
      return 0;

    char newcontent[65536] = {0};
    size_t before = bracket - content;
    strncpy(newcontent, content, before);
    snprintf(newcontent + before, sizeof(newcontent) - before, ", \"%s\"%s",
             value, bracket);

    fp = fopen("smelt.toml", "w");
    if (!fp)
      return 0;
    fputs(newcontent, fp);
    fclose(fp);
  } else {
    fp = fopen("smelt.toml", "a");
    if (!fp)
      return 0;
    fprintf(fp, "\n# added by smelt add\n%s = [\"%s\"]\n", key, value);
    fclose(fp);
  }

  printf("smelt: added %s to %s\n", value, key);
  return 1;
}

static int ends_with_c(const char *name) {
  size_t len = strlen(name);
  return len > 2 && name[len - 2] == '.' && name[len - 1] == 'c';
}

int dep_fetch(const char *url, const char **files, int file_count) {
  char name[MAX_NAME];
  repo_name(name, sizeof(name), url);

  char cache[MAX_PATH];
  cache_path(cache, sizeof(cache), name);

  if (!dir_exists(cache)) {
    ensure_dirs(cache);
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "git clone --depth=1 %s %s", url, cache);
    printf("smelt: %s\n", cmd);
    if (system(cmd) != 0) {
      fprintf(stderr, "smelt: git clone failed\n");
      return 0;
    }
  } else {
    printf("smelt: using cached %s\n", cache);
  }

  mkdir("vendor", 0755);

  for (int i = 0; i < file_count; i++) {
    char src[MAX_PATH * 2];
    snprintf(src, sizeof(src), "%s/%s", cache, files[i]);

    const char *fname = base_name(files[i]);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "vendor/%s", fname);

    if (!copy_file(src, dst))
      return 0;
    printf("smelt: copied %s -> %s\n", files[i], dst);
  }

  for (int i = 0; i < file_count; i++) {
    const char *fname = base_name(files[i]);
    char vendor_path[MAX_PATH];
    snprintf(vendor_path, sizeof(vendor_path), "vendor/%s", fname);

    if (ends_with_c(fname)) {
      toml_append_array("extra_sources", vendor_path);
    }
  }
  toml_append_array("include_dirs", "vendor");

  return 1;
}
