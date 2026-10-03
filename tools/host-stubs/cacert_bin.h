#pragma once
/* Transport tests mock TLS/curl; production embeds the Mozilla CA bundle. */
static const unsigned char cacert_bin[] = "test certificate";
static const unsigned int cacert_bin_size = sizeof(cacert_bin) - 1;
