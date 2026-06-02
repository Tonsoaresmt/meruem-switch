// store.h - token de login + progresso/historico de leitura no SD.
#pragma once
#include <stddef.h>

void store_init(void);

int  store_load_token(char *out, size_t cap);
void store_save_token(const char *token);
void store_clear_token(void);

int  store_load_server(char *out, size_t cap);
void store_save_server(const char *url);

int  store_load_user(char *out, size_t cap);
void store_save_user(const char *user);
void store_clear_user(void);

int  store_load_update_seen(char *out, size_t cap);
void store_save_update_seen(const char *tag);

int  store_load_local_root(char *out, size_t cap);
void store_save_local_root(const char *path);

// Estado offline de uma serie: 0 = nada, 1 = parcial, 2 = baixada inteira.
int  store_get_series_offline(const char *seriesId);
void store_set_series_offline(const char *seriesId, int state);
void store_clear_series_offline_all(void);

// pagina salva de um capitulo (>=1) ou 1 se nao houver.
int  store_get_progress(const char *bookId);

// registra/atualiza a leitura (pagina atual + metadados p/ "continuar lendo").
void store_record(const char *bookId, int page, const char *seriesId,
                  const char *seriesTitle, const char *chapLabel,
                  const char *pageBase, const char *seriesCover, int pages);

// preenche ids[] com ate `max` capitulos lidos recentemente (mais recente 1o).
// Retorna a quantidade.
int  store_recent(char ids[][96], int max);

// le os campos salvos de um capitulo. Retorna 1 se existe.
int  store_entry(const char *bookId,
                 char *seriesId, size_t sidCap,
                 char *seriesTitle, size_t stCap,
                 char *chapLabel, size_t clCap,
                 char *pageBase, size_t pbCap,
                 char *seriesCover, size_t cvCap,
                 int *page, int *pages);

void store_flush(void);
