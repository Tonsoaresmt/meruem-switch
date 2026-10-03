#include "other_apps.h"
#include "update.h"
#include "cacert_bin.h"
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

struct transfer {
    char *json;
    size_t used;
    FILE *file;
    uint32_t limit;
    other_app_progress progress;
    void *userdata;
    int cancelled;
};

static size_t receive(char *data, size_t size, size_t count, void *userdata) {
    struct transfer *t = userdata;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    if (t->used > t->limit || bytes > t->limit - t->used) return 0;
    if (t->file) {
        if (fwrite(data, 1, bytes, t->file) != bytes) return 0;
    } else {
        memcpy(t->json + t->used, data, bytes);
        t->json[t->used + bytes] = 0;
    }
    t->used += bytes;
    return bytes;
}

static int tick(void *userdata, curl_off_t total, curl_off_t now, curl_off_t up, curl_off_t uploaded) {
    struct transfer *t = userdata;
    (void)up; (void)uploaded; (void)total;
    if (now > t->limit) return 1;
    if (t->progress && t->progress((uint32_t)now, t->file ? t->limit : 0, t->userdata)) {
        t->cancelled = 1;
        return 1;
    }
    return 0;
}

static int request(const char *url, struct transfer *t, char *err, size_t errcap) {
    const char *ca = NPLAY_SD_ROOT "/.meruem-apps-ca.pem";
    FILE *f = fopen(ca, "wb");
    if (!f) { snprintf(err, errcap, "Nao consegui preparar o certificado no SD."); return -1; }
    int ca_ok = fwrite(cacert_bin, 1, cacert_bin_size, f) == cacert_bin_size;
    if (fclose(f)) ca_ok = 0;
    if (!ca_ok) { snprintf(err, errcap, "Falha gravando certificado no SD."); return -1; }
    CURL *curl = curl_easy_init();
    if (!curl) { snprintf(err, errcap, "Nao consegui iniciar o download."); return -1; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Meruem-Switch/" APP_VERSION_STR);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, t->file ? 300L : 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, tick);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, t);
    CURLcode result = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (t->cancelled) return 1;
    if (result != CURLE_OK || code != 200) {
        snprintf(err, errcap, "Falha de rede/SD (HTTP %ld, %s). Tente novamente.", code, curl_easy_strerror(result));
        return -1;
    }
    return 0;
}

int other_app_check(struct other_app_release *out, other_app_progress progress,
                    void *userdata, char *err, size_t errcap) {
    struct transfer t = {0};
    t.limit = 512 * 1024;
    t.progress = progress; t.userdata = userdata;
    t.json = malloc(t.limit + 1);
    if (!t.json) { snprintf(err, errcap, "Sem memoria para consultar a versao."); return -1; }
    t.json[0] = 0;
    mkdir(NPLAY_SD_ROOT, 0777);
    int rc = request(NPLAY_RELEASE_API, &t, err, errcap);
    if (!rc && !other_app_parse_release(t.json, out)) {
        snprintf(err, errcap, "Release sem Nplay.nro verificado. Tente novamente depois.");
        rc = -1;
    }
    free(t.json);
    return rc;
}

static int hash_matches(const char *path, const char *expected) {
    unsigned char buf[32768], hash[32];
    char hex[65];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int ok = mbedtls_sha256_starts_ret(&ctx, 0) == 0;
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), f)) > 0)
        ok = mbedtls_sha256_update_ret(&ctx, buf, n) == 0;
    if (ferror(f)) ok = 0;
    fclose(f);
    if (ok) ok = mbedtls_sha256_finish_ret(&ctx, hash) == 0;
    mbedtls_sha256_free(&ctx);
    if (!ok) return 0;
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
    return !strcmp(hex, expected);
}

int other_app_install(const struct other_app_release *release,
                     other_app_progress progress, void *userdata, char *err, size_t errcap) {
    const char *tmp = NPLAY_SD_ROOT "/Nplay/.Nplay.download";
    const char *bak = NPLAY_SD_ROOT "/Nplay/Nplay.nro.bak";
    struct transfer t = {0};
    if (!release || release->size < 128 || release->size > NPLAY_MAX_BYTES) return -1;
    mkdir(NPLAY_SD_ROOT, 0777);
    if (mkdir(NPLAY_SD_ROOT "/Nplay", 0777) && errno != EEXIST) {
        snprintf(err, errcap, "Nao consegui criar a pasta do Nplay no SD."); return -1;
    }
    /* Recover an interrupted previous replacement before starting a new one. */
    struct stat st;
    if (stat(NPLAY_TARGET, &st) && errno == ENOENT && !stat(bak, &st) && rename(bak, NPLAY_TARGET)) {
        snprintf(err, errcap, "Nao consegui restaurar o aplicativo anterior."); return -1;
    }
    t.file = fopen(tmp, "wb");
    if (!t.file) { snprintf(err, errcap, "Nao consegui gravar no SD. Verifique espaco livre."); return -1; }
    t.limit = release->size; t.progress = progress; t.userdata = userdata;
    int rc = request(release->url, &t, err, errcap);
    if (fclose(t.file) && !rc) { snprintf(err, errcap, "Falha ao concluir a gravacao no SD."); rc = -1; }
    if (!rc && (!other_app_valid_nro(tmp, release->size) || !hash_matches(tmp, release->sha256))) {
        snprintf(err, errcap, "Arquivo incompleto ou diferente do original. Tente novamente."); rc = -1;
    }
    if (rc) { remove(tmp); return rc; }
    int had_old = !stat(NPLAY_TARGET, &st);
    if (had_old) {
        if (remove(bak) && errno != ENOENT) goto fail;
        if (rename(NPLAY_TARGET, bak)) goto fail;
    }
    if (rename(tmp, NPLAY_TARGET)) {
        if (had_old) rename(bak, NPLAY_TARGET);
        goto fail;
    }
    /* Keep one backup, including through interrupted SD writes. */
    return 0;
fail:
    remove(tmp);
    snprintf(err, errcap, "Falha ao instalar no SD. O arquivo anterior foi preservado.");
    return -1;
}
