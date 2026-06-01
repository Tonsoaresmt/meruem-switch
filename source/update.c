#include "update.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "net.h"

static void copy_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    snprintf(dst, cap, "%s", src ? src : "");
}

static int ends_with_nro(const char *name) {
    size_t len;
    const char *suffix = ".nro";
    int i;
    if (!name) return 0;
    len = strlen(name);
    if (len < 4) return 0;
    name += len - 4;
    for (i = 0; i < 4; i++) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

static int read_version_part(const char **pp) {
    int value = 0;
    const char *p = *pp;
    while (*p && !isdigit((unsigned char)*p)) p++;
    while (isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    while (*p == '.') p++;
    *pp = p;
    return value;
}

static int version_cmp(const char *a, const char *b) {
    const char *pa = a ? a : "";
    const char *pb = b ? b : "";
    int i;
    for (i = 0; i < 4; i++) {
        int va = read_version_part(&pa);
        int vb = read_version_part(&pb);
        if (va != vb) return (va > vb) ? 1 : -1;
        if (!*pa && !*pb) break;
    }
    return 0;
}

static int has_repo_config(void) {
    return UPDATE_REPO_OWNER[0] && UPDATE_REPO_NAME[0];
}

void update_resolve_target_path(const char *argv0, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (argv0 && argv0[0] && strstr(argv0, ".nro")) {
        copy_text(out, cap, argv0);
        return;
    }
    copy_text(out, cap, "sdmc:/switch/Meruem.nro");
}

int update_check(struct update_info *info) {
    struct membuf resp = {0};
    char url[256];
    const char *net_err = NULL;
    long http_code = 0;
    cJSON *root = NULL;
    cJSON *assets = NULL;
    cJSON *asset = NULL;
    int result = UPDATE_CHECK_ERROR;

    if (info) memset(info, 0, sizeof(*info));
    if (!has_repo_config()) {
        if (info) copy_text(info->message, sizeof(info->message), "Repositorio de release ainda nao configurado.");
        return UPDATE_CHECK_DISABLED;
    }

    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/releases/latest",
             UPDATE_REPO_OWNER, UPDATE_REPO_NAME);
    http_code = net_request(url, "GET", NULL, NULL, &resp, &net_err);
    if (info) info->http_code = http_code;
    if (http_code != 200 || !resp.data) {
        if (info) {
            if (net_err && net_err[0]) {
                snprintf(info->message, sizeof(info->message), "GitHub falhou: %s (%ld).", net_err, http_code);
            } else {
                snprintf(info->message, sizeof(info->message), "GitHub respondeu HTTP %ld.", http_code);
            }
        }
        result = UPDATE_CHECK_ERROR;
        goto done;
    }

    root = cJSON_Parse(resp.data);
    if (!root) {
        if (info) copy_text(info->message, sizeof(info->message), "Resposta invalida do GitHub Releases.");
        result = UPDATE_CHECK_ERROR;
        goto done;
    }

    {
        cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
        if (!cJSON_IsString(tag) || !tag->valuestring || !tag->valuestring[0]) {
            if (info) copy_text(info->message, sizeof(info->message), "Release sem tag_name.");
            result = UPDATE_CHECK_ERROR;
            goto done;
        }
        if (info) copy_text(info->latest_version, sizeof(info->latest_version), tag->valuestring);
        if (version_cmp(tag->valuestring, APP_VERSION_STR) <= 0) {
            if (info) snprintf(info->message, sizeof(info->message), "GitHub latest: %s.", tag->valuestring);
            result = UPDATE_CHECK_UP_TO_DATE;
            goto done;
        }
    }

    assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        cJSON *dl = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        if (!cJSON_IsString(name) || !name->valuestring) continue;
        if (!cJSON_IsString(dl) || !dl->valuestring) continue;
        if (!ends_with_nro(name->valuestring)) continue;
        if (info) {
            copy_text(info->asset_name, sizeof(info->asset_name), name->valuestring);
            copy_text(info->download_url, sizeof(info->download_url), dl->valuestring);
        }
        result = UPDATE_CHECK_AVAILABLE;
        goto done;
    }

    if (info) copy_text(info->message, sizeof(info->message), "Nenhum asset .nro encontrado na release.");
    result = UPDATE_CHECK_ERROR;

done:
    if (root) cJSON_Delete(root);
    membuf_free(&resp);
    return result;
}

int update_apply(const struct update_info *info, const char *target_path, char *err, size_t errcap) {
    char tmp[640];
    char bak[640];
    long code;

    if (err && errcap) err[0] = '\0';
    if (!info || !info->download_url[0]) {
        if (err && errcap) copy_text(err, errcap, "URL do asset nao disponivel.");
        return -1;
    }
    if (!target_path || !target_path[0]) {
        if (err && errcap) copy_text(err, errcap, "Caminho do .nro atual nao identificado.");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s.download", target_path);
    snprintf(bak, sizeof(bak), "%s.bak", target_path);
    remove(tmp);
    remove(bak);

    code = net_download_file(info->download_url, NULL, tmp, NULL);
    if (code != 200) {
        if (err && errcap) snprintf(err, errcap, "Download falhou (HTTP %ld).", code);
        remove(tmp);
        return -1;
    }

    if (rename(target_path, bak) != 0 && errno != ENOENT) {
        if (err && errcap) snprintf(err, errcap, "Nao consegui preparar a troca do .nro (%d).", errno);
        remove(tmp);
        return -1;
    }

    if (rename(tmp, target_path) != 0) {
        int rename_err = errno;
        rename(bak, target_path);
        if (err && errcap) snprintf(err, errcap, "Nao consegui gravar o novo .nro (%d).", rename_err);
        remove(tmp);
        return -1;
    }

    remove(bak);
    return 0;
}
