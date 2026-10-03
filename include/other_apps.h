#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef NPLAY_SD_ROOT
#define NPLAY_SD_ROOT "sdmc:/switch"
#endif
#define NPLAY_TARGET NPLAY_SD_ROOT "/Nplay/Nplay.nro"
#define NPLAY_RELEASE_API "https://api.github.com/repos/Tonsoaresmt/nplay-switch/releases/latest"
#define NPLAY_MAX_BYTES (64u * 1024u * 1024u)

struct other_app_release {
    char version[32];
    char url[512];
    char sha256[65];
    uint32_t size;
};
enum other_app_phase { APP_CHECK, APP_DOWNLOAD, APP_VERIFY, APP_INSTALL };
/* Return nonzero to cancel; callbacks run on the calling/UI thread. */
typedef int (*other_app_progress)(enum other_app_phase phase, uint32_t done, uint32_t total, void *userdata);
int other_app_parse_release(const char *json, struct other_app_release *out);
int other_app_valid_nro(const char *path, uint32_t expected_size);
int other_app_check(struct other_app_release *out, other_app_progress progress,
                    void *userdata, char *err, size_t errcap);
/* 0 installed, 1 cancelled, -1 failed. Existing NRO is preserved on failure. */
int other_app_install(const struct other_app_release *release,
                     other_app_progress progress, void *userdata, char *err, size_t errcap);
