#include "other_apps.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *valid = "{\"tag_name\":\"v0.12.38\",\"assets\":[{\"name\":\"Nplay.nro\",\"size\":256,\"browser_download_url\":\"https://github.com/Tonsoaresmt/nplay-switch/releases/download/v0.12.38/Nplay.nro\",\"digest\":\"sha256:3c7eaf7ee989298b5fa610513e452b3ad9bdbecee5d18e2619646d2ce7cf3b9e\"}]}";

static void reject_field(const char *key, cJSON *value) {
    struct other_app_release r;
    cJSON *root = cJSON_Parse(valid);
    cJSON *asset = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "assets"), 0);
    cJSON_ReplaceItemInObjectCaseSensitive(asset, key, value);
    char *json = cJSON_PrintUnformatted(root);
    assert(!other_app_parse_release(json, &r));
    assert(r.size == 0 && !r.url[0]);
    cJSON_free(json); cJSON_Delete(root);
}

static void write_nro(unsigned char *bytes, size_t n) {
    FILE *f = fopen("test-nplay.nro", "wb");
    assert(f && fwrite(bytes, 1, n, f) == n);
    assert(fclose(f) == 0);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        char json[512*1024+1]; size_t n=fread(json,1,sizeof(json)-1,f); json[n]=0; fclose(f);
        struct other_app_release r;
        assert(other_app_parse_release(json,&r));
        printf("PASS: live anonymous GitHub release accepted: %s, %u bytes\n",r.version,r.size);
        return 0;
    }
    if (argc == 3) {
        assert(other_app_valid_nro(argv[1], (uint32_t)strtoul(argv[2], NULL, 10)));
        puts("PASS: real release NRO structure and size");
        return 0;
    }
    struct other_app_release r;
    assert(other_app_parse_release(valid, &r));
    assert(!strcmp(r.version, "v0.12.38") && r.size == 256);
    assert(strlen(r.sha256) == 64);
    assert(!other_app_parse_release("{}", &r));
    assert(!other_app_parse_release("not json", &r));
    char trailing[1024]; snprintf(trailing,sizeof(trailing),"%s garbage",valid);
    assert(!other_app_parse_release(trailing,&r));
    assert(!other_app_parse_release("{\"tag_name\":\"v1\",\"assets\":{}}", &r));
    reject_field("name", cJSON_CreateString("Meruem.nro"));
    reject_field("browser_download_url", cJSON_CreateString("https://evil.example/Nplay.nro"));
    reject_field("browser_download_url", cJSON_CreateString("https://github.com/Tonsoaresmt/nplay-switch/releases/download/v1/Nplay.nro"));
    reject_field("browser_download_url", cJSON_CreateString("http://github.com/Tonsoaresmt/nplay-switch/releases/download/v0.12.38/Nplay.nro"));
    reject_field("digest", cJSON_CreateString("sha256:bad"));
    reject_field("digest", cJSON_CreateNull());
    reject_field("size", cJSON_CreateNumber(0));
    reject_field("size", cJSON_CreateNumber(NPLAY_MAX_BYTES + 1.0));
    reject_field("size", cJSON_CreateNumber(256.5));
    cJSON *root = cJSON_Parse(valid);
    cJSON_AddBoolToObject(root, "prerelease", 1);
    char *json = cJSON_PrintUnformatted(root);
    assert(!other_app_parse_release(json, &r));
    cJSON_free(json); cJSON_Delete(root);
    root = cJSON_Parse(valid);
    cJSON_AddBoolToObject(root, "draft", 1);
    json = cJSON_PrintUnformatted(root);
    assert(!other_app_parse_release(json, &r));
    cJSON_free(json); cJSON_Delete(root);

    unsigned char nro[256] = {0};
    memcpy(nro + 16, "NRO0", 4);
    nro[24] = 128;
    nro[32] = 32; nro[36] = 64;
    write_nro(nro, sizeof(nro));
    assert(other_app_valid_nro("test-nplay.nro", 256));
    assert(!other_app_valid_nro("test-nplay.nro", 255));
    nro[36] = 255;
    write_nro(nro, sizeof(nro));
    assert(!other_app_valid_nro("test-nplay.nro", 256));
    nro[36] = 64; nro[16] = 'X';
    write_nro(nro, sizeof(nro));
    assert(!other_app_valid_nro("test-nplay.nro", 256));
    write_nro(nro, 32);
    assert(!other_app_valid_nro("test-nplay.nro", 256));
    remove("test-nplay.nro");
    puts("PASS: release identity, HTTPS, digest, size limits, draft/prerelease, truncated/corrupt NRO");
    return 0;
}
