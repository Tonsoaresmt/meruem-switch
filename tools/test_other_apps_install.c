#include "other_apps.h"
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#undef curl_easy_setopt
#undef curl_easy_getinfo

typedef size_t (*writer)(char *, size_t, size_t, void *);
typedef int (*ticker)(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t);
struct mock { writer write; ticker tick; void *data, *progress; const char *url; int tls_peer, tls_host; };
static int failure, hash_bad, cancel;
static int write_fail_at, close_fail_at, writes, closes, activate_fail, rollback_fail, backup_fail;
static int cancel_phase = -1;
static CURLcode transport_error;
static int init_fail, metadata_bad;
static long response;
static unsigned char binary[256];

size_t mock_fwrite(const void *data, size_t size, size_t count, FILE *f) {
    if (++writes == write_fail_at) { errno = ENOSPC; return fwrite(data, size, count / 2, f); }
    return fwrite(data, size, count, f);
}
int mock_fclose(FILE *f) {
    int rc = fclose(f);
    if (++closes == close_fail_at) { errno = ENOSPC; return EOF; }
    return rc;
}
int mock_rename(const char *src, const char *dest) {
    if ((activate_fail && strstr(src, ".Nplay.download")) ||
        (rollback_fail && strstr(src, "Nplay.nro.bak"))) { errno = EIO; return -1; }
    return rename(src, dest);
}
int mock_remove(const char *path) {
    if (backup_fail && strstr(path, "Nplay.nro.bak")) { errno = EACCES; return -1; }
    return remove(path);
}

CURL *curl_easy_init(void) { return init_fail ? NULL : (CURL *)calloc(1, sizeof(struct mock)); }
void curl_easy_cleanup(CURL *c) { free(c); }
const char *curl_easy_strerror(CURLcode c) { (void)c; return "mock transport"; }
CURLcode curl_easy_setopt(CURL *c, CURLoption option, ...) {
    struct mock *m = (struct mock *)c;
    va_list ap; va_start(ap, option);
    switch (option) {
        case CURLOPT_URL: m->url = va_arg(ap, const char *); break;
        case CURLOPT_WRITEFUNCTION: m->write = va_arg(ap, writer); break;
        case CURLOPT_WRITEDATA: m->data = va_arg(ap, void *); break;
        case CURLOPT_XFERINFOFUNCTION: m->tick = va_arg(ap, ticker); break;
        case CURLOPT_XFERINFODATA: m->progress = va_arg(ap, void *); break;
        case CURLOPT_SSL_VERIFYPEER: m->tls_peer = (int)va_arg(ap, long); break;
        case CURLOPT_SSL_VERIFYHOST: m->tls_host = (int)va_arg(ap, long); break;
        case CURLOPT_HTTPHEADER: assert(!"No account headers allowed"); break;
        case CURLOPT_PROTOCOLS: case CURLOPT_REDIR_PROTOCOLS: assert(va_arg(ap, long) == CURLPROTO_HTTPS); break;
        case CURLOPT_MAXREDIRS: assert(va_arg(ap, long) == 5); break;
        case CURLOPT_CONNECTTIMEOUT: assert(va_arg(ap, long) == 12); break;
        case CURLOPT_TIMEOUT: { long timeout=va_arg(ap,long); assert(timeout==30 || timeout==300); break; }
        default: break;
    }
    va_end(ap); return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *c, CURLINFO info, ...) {
    (void)c; assert(info == CURLINFO_RESPONSE_CODE);
    va_list ap; va_start(ap, info); *va_arg(ap, long *) = response; va_end(ap);
    return CURLE_OK;
}
CURLcode curl_easy_perform(CURL *c) {
    struct mock *m = (struct mock *)c;
    assert(m->tls_peer == 1 && m->tls_host == 2);
    if (transport_error) return transport_error;
    if (m->tick(m->progress, 256, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
    if (!strcmp(m->url, NPLAY_RELEASE_API)) {
        if (metadata_bad) {
            char *data = metadata_bad == 2 ? calloc(512 * 1024 + 2, 1) : NULL;
            if (data) {
                size_t n=m->write(data, 1, 512 * 1024 + 1, m->data); free(data);
                return n ? CURLE_OK : CURLE_WRITE_ERROR;
            }
            return m->write("<html>error</html>", 1, 18, m->data) == 18 ? CURLE_OK : CURLE_WRITE_ERROR;
        }
        char json[1024];
        snprintf(json, sizeof(json), "{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"Nplay.nro\",\"size\":256,\"browser_download_url\":\"https://github.com/Tonsoaresmt/nplay-switch/releases/download/v1/Nplay.nro\",\"digest\":\"sha256:%064d\"}]}", 0);
        return m->write(json, 1, strlen(json), m->data) == strlen(json) ? CURLE_OK : CURLE_WRITE_ERROR;
    }
    size_t n = failure == 1 ? 128 : 256;
    if (m->write((char *)binary, 1, n, m->data) != n) return CURLE_WRITE_ERROR;
    if (failure == 2) return CURLE_RECV_ERROR;
    if (failure == 3 && m->write((char *)binary, 1, 1, m->data) != 1) return CURLE_WRITE_ERROR;
    return CURLE_OK;
}
/* SHA implementation is supplied by mbedTLS on Switch. Here the mock exercises
   the installer's acceptance/rejection paths, not cryptographic correctness. */
void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
void mbedtls_sha256_free(mbedtls_sha256_context *c) { (void)c; }
int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c, int is224) { (void)c; assert(!is224); return 0; }
int mbedtls_sha256_update_ret(mbedtls_sha256_context *c, const unsigned char *d, size_t n) { (void)c; (void)d; (void)n; return 0; }
int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c, unsigned char out[32]) { (void)c; memset(out, hash_bad ? 1 : 0, 32); return 0; }

static int progress(enum other_app_phase phase, uint32_t done, uint32_t total, void *u) {
    (void)done; (void)total; (void)u; return cancel || (int)phase == cancel_phase;
}
static void old_file(void) {
    FILE *f = fopen(NPLAY_TARGET, "wb"); assert(f); assert(fwrite("old", 1, 3, f) == 3); assert(!fclose(f));
}
static void assert_old(void) {
    char buf[4] = {0}; FILE *f = fopen(NPLAY_TARGET, "rb"); assert(f); assert(fread(buf, 1, 3, f) == 3); fclose(f); assert(!strcmp(buf, "old"));
    assert(!fopen(NPLAY_SD_ROOT "/Nplay/.Nplay.download", "rb"));
}

static void preserve_on_fault(struct other_app_release *r, char *err, size_t cap) {
    writes = closes = 0;
    assert(other_app_install(r, progress, NULL, err, cap) == -1);
    assert_old();
}

int main(void) {
    mkdir(NPLAY_SD_ROOT, 0777); mkdir(NPLAY_SD_ROOT "/Nplay", 0777);
    memcpy(binary + 16, "NRO0", 4); binary[24] = 128;
    struct other_app_release r;
    char err[256]; response = 200;
    assert(other_app_check(&r, progress, NULL, err, sizeof(err)) == 0);
    old_file();
    for (failure = 1; failure <= 3; failure++) {
        assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1); assert_old();
    }
    failure = 0; hash_bad = 1;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1); assert_old();
    hash_bad = 0; response = 503;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1); assert_old();
    response = 200; cancel = 1;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == 1); assert_old();
    assert(other_app_check(&r, progress, NULL, err, sizeof(err)) == 1);
    cancel = 0;
    assert(other_app_check(&r, progress, NULL, err, sizeof(err)) == 0);
    cancel_phase = APP_VERIFY;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == 1); assert_old();
    cancel_phase = APP_INSTALL;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == 1); assert_old();
    cancel_phase = -1;
    write_fail_at = 2; preserve_on_fault(&r, err, sizeof(err)); write_fail_at = 0;
    close_fail_at = 2; preserve_on_fault(&r, err, sizeof(err)); close_fail_at = 0;
    activate_fail = 1; preserve_on_fault(&r, err, sizeof(err)); activate_fail = 0;
    backup_fail = 1; preserve_on_fault(&r, err, sizeof(err)); backup_fail = 0;
    activate_fail = rollback_fail = 1;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1);
    assert(strstr(err, "Nplay.nro.bak"));
    FILE *backup = fopen(NPLAY_SD_ROOT "/Nplay/Nplay.nro.bak", "rb"); assert(backup); fclose(backup);
    activate_fail = rollback_fail = 0; failure = 2;
    preserve_on_fault(&r, err, sizeof(err)); failure = 0;
    struct other_app_release invalid = r;
    strcpy(invalid.url, "https://evil.example/Nplay.nro");
    preserve_on_fault(&invalid, err, sizeof(err));
    invalid = r; strcpy(invalid.sha256, "bad");
    preserve_on_fault(&invalid, err, sizeof(err));
    assert(other_app_install(NULL, progress, NULL, err, sizeof(err)) == -1);
    CURLcode network_errors[] = {CURLE_COULDNT_RESOLVE_HOST, CURLE_COULDNT_CONNECT,
        CURLE_OPERATION_TIMEDOUT, CURLE_SSL_CONNECT_ERROR, CURLE_PEER_FAILED_VERIFICATION, CURLE_TOO_MANY_REDIRECTS};
    for (unsigned i=0; i<sizeof(network_errors)/sizeof(*network_errors); i++) {
        transport_error=network_errors[i]; preserve_on_fault(&r,err,sizeof(err));
        struct other_app_release checked = r;
        assert(other_app_check(&checked,progress,NULL,err,sizeof(err))==-1 && checked.size==0);
    }
    transport_error=CURLE_OK;
    long http_errors[] = {403,404,429,503};
    for (unsigned i=0; i<sizeof(http_errors)/sizeof(*http_errors); i++) {
        response=http_errors[i]; preserve_on_fault(&r,err,sizeof(err));
        struct other_app_release checked = r;
        assert(other_app_check(&checked,progress,NULL,err,sizeof(err))==-1 && checked.size==0);
    }
    response=200;
    init_fail=1; preserve_on_fault(&r,err,sizeof(err)); init_fail=0;
    struct other_app_release checked = r;
    metadata_bad=1; assert(other_app_check(&checked,progress,NULL,err,sizeof(err))==-1);
    metadata_bad=2; assert(other_app_check(&checked,progress,NULL,err,sizeof(err))==-1);
    metadata_bad=0;
    binary[16] = 'X';
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1); assert_old();
    binary[16] = 'N';
    assert(!rename(NPLAY_TARGET, NPLAY_SD_ROOT "/Nplay/Nplay.nro.bak"));
    failure = 2;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1); assert_old();
    failure = 0;
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == 0);
    assert(other_app_valid_nro(NPLAY_TARGET, 256));
    FILE *f = fopen(NPLAY_SD_ROOT "/Nplay/Nplay.nro.bak", "rb"); assert(f); fclose(f);
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == 0);
    assert(other_app_valid_nro(NPLAY_TARGET, 256));
    remove(NPLAY_TARGET); remove(NPLAY_SD_ROOT "/Nplay/Nplay.nro.bak");
    mkdir(NPLAY_TARGET, 0777);
    assert(other_app_install(&r, progress, NULL, err, sizeof(err)) == -1);
    struct stat st; assert(!stat(NPLAY_TARGET, &st) && S_ISDIR(st.st_mode));
    assert(!rmdir(NPLAY_TARGET));
    puts("PASS: DNS/connect/timeout/TLS/redirect errors; HTTP 403/404/429/503; invalid/oversized metadata; partial/oversized NRO; checksum; cancellation in all phases; disk write/close failures; rollback/recovery; invalid destination; reinstall");
    return 0;
}
