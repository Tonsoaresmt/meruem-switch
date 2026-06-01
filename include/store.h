// store.h - token de login + progresso/historico de leitura no SD.
#pragma once
#include <stddef.h>

void store_init(void);

int  store_load_token(char *out, size_t cap);
void store_save_token(const char *token);
void store_clear_token(void);

int  store_load_server(char *out, size_t cap);
void store_save_server(const char *url);

// pagina salva de um capitulo (>=1) ou 1 se nao houver.
int  store_get_progress(const char *bookId);

// registra/atualiza a leitura (pagina atual + metadados p/ "continuar lendo").
void store_record(const char *bookId, int page, const char *seriesId,
                  const char *seriesTitle, const char *chapLabel,
                  const char *pageBase, int pages);

// preenche ids[] com ate `max` capitulos lidos recentemente (mais recente 1o).
// Retorna a quantidade.
int  store_recent(char ids[][96], int max);

// le os campos salvos de um capitulo. Retorna 1 se existe.
int  store_entry(const char *bookId,
                 char *seriesId, size_t sidCap,
                 char *seriesTitle, size_t stCap,
                 char *chapLabel, size_t clCap,
                 char *pageBase, size_t pbCap,
                 int *page, int *pages);

void store_flush(void);
