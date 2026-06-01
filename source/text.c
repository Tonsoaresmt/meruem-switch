// text.c - fonte do sistema do Switch (via servico pl) + SDL2_ttf.
#include "text.h"
#include <switch.h>
#include <SDL_ttf.h>

static TTF_Font *g_font     = NULL;   // texto normal (listas)
static TTF_Font *g_font_big = NULL;   // titulos
static int       g_pl_ok    = 0;

int text_init(void) {
    if (TTF_Init() != 0) return -1;
    if (R_FAILED(plInitialize(PlServiceType_User))) return -2;
    g_pl_ok = 1;

    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) return -3;

    // Dois ponteiros RWops sobre a MESMA memoria da fonte (so leitura). A memoria
    // pertence ao servico pl e fica valida ate plExit (chamado em text_exit).
    SDL_RWops *rw1 = SDL_RWFromConstMem(fd.address, (int)fd.size);
    g_font = TTF_OpenFontRW(rw1, 1, 23);
    SDL_RWops *rw2 = SDL_RWFromConstMem(fd.address, (int)fd.size);
    g_font_big = TTF_OpenFontRW(rw2, 1, 31);

    if (!g_font || !g_font_big) return -4;
    return 0;
}

void text_exit(void) {
    if (g_font)     { TTF_CloseFont(g_font);     g_font = NULL; }
    if (g_font_big) { TTF_CloseFont(g_font_big); g_font_big = NULL; }
    if (g_pl_ok)    { plExit(); g_pl_ok = 0; }
    TTF_Quit();
}

SDL_Texture *text_make(SDL_Renderer *ren, const char *utf8, SDL_Color color, int big, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    TTF_Font *f = big ? g_font_big : g_font;
    if (!f || !utf8 || !utf8[0]) return NULL;

    SDL_Surface *s = TTF_RenderUTF8_Blended(f, utf8, color);
    if (!s) return NULL;

    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    if (outW) *outW = s->w;
    if (outH) *outH = s->h;
    SDL_FreeSurface(s);
    return t;
}

int text_draw(SDL_Renderer *ren, const char *utf8, int x, int y, SDL_Color color, int big) {
    int w = 0, h = 0;
    SDL_Texture *t = text_make(ren, utf8, color, big, &w, &h);
    if (t) {
        SDL_Rect dst = { x, y, w, h };
        SDL_RenderCopy(ren, t, NULL, &dst);
        SDL_DestroyTexture(t);
    }
    return w;
}
