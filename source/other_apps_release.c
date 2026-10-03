#include "other_apps.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int other_app_parse_release(const char *json, struct other_app_release *out) {
    const char *prefix = "https://github.com/Tonsoaresmt/nplay-switch/releases/download/";
    cJSON *root = cJSON_ParseWithOpts(json ? json : "", NULL, 1);
    cJSON *asset;
    int ok = 0;
    if (!out) { cJSON_Delete(root); return 0; }
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(root) || cJSON_IsTrue(cJSON_GetObjectItem(root, "draft")) ||
        cJSON_IsTrue(cJSON_GetObjectItem(root, "prerelease"))) goto done;
    cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
    if (!cJSON_IsString(tag) || !tag->valuestring[0] || strlen(tag->valuestring) >= sizeof(out->version)) goto done;
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    if (!cJSON_IsArray(assets)) goto done;
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        cJSON *url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
        cJSON *digest = cJSON_GetObjectItemCaseSensitive(asset, "digest");
        if (!cJSON_IsString(name) || strcmp(name->valuestring, "Nplay.nro")) continue;
        if (!cJSON_IsString(url) || strncmp(url->valuestring, prefix, strlen(prefix)) ||
            strlen(url->valuestring) >= sizeof(out->url)) continue;
        char expected_url[512];
        snprintf(expected_url, sizeof(expected_url), "%s%s/Nplay.nro", prefix, tag->valuestring);
        if (strcmp(url->valuestring, expected_url)) continue;
        if (!cJSON_IsNumber(size) || size->valuedouble < 128 || size->valuedouble > NPLAY_MAX_BYTES ||
            size->valuedouble != (uint32_t)size->valuedouble) continue;
        if (!cJSON_IsString(digest) || strlen(digest->valuestring) != 71 ||
            strncmp(digest->valuestring, "sha256:", 7)) continue;
        int valid = 1;
        for (int i = 0; i < 64; i++) {
            unsigned char c = digest->valuestring[7 + i];
            if (!isxdigit(c)) valid = 0;
            out->sha256[i] = (char)tolower(c);
        }
        if (!valid) continue;
        snprintf(out->version, sizeof(out->version), "%s", tag->valuestring);
        snprintf(out->url, sizeof(out->url), "%s", url->valuestring);
        out->size = (uint32_t)size->valuedouble;
        ok = 1;
        break;
    }
done:
    cJSON_Delete(root);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int other_app_valid_nro(const char *path, uint32_t expected_size) {
    unsigned char header[128];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(header, 1, sizeof(header), f) == sizeof(header) &&
             memcmp(header + 16, "NRO0", 4) == 0;
    if (fseek(f, 0, SEEK_END)) ok = 0;
    long size = ftell(f);
    fclose(f);
    if (!ok || size < 128 || (unsigned long)size != expected_size) return 0;
    uint32_t image_size = le32(header + 24);
    if (image_size < 128 || image_size > expected_size) return 0;
    for (int i = 0; i < 3; i++) {
        uint32_t offset = le32(header + 32 + i * 8);
        uint32_t length = le32(header + 36 + i * 8);
        if (offset > image_size || length > image_size - offset) return 0;
    }
    return 1;
}
