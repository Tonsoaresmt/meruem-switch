// Meruem - Leitor de CBZ para Nintendo Switch
//
// Etapa 1 de toque/retrato: orientacao RETRATO por padrao (render rotacionado),
// botao GIRAR na tela, e controle por TOQUE (tocar laterais vira pagina, tocar
// em linhas abre, botoes na tela pra voltar/girar). Os botoes do controle
// continuam funcionando (essenciais no modo dock, que nao tem toque).
//
// === PARAFUSOS DE CALIBRACAO (ajustar depois do 1o teste no hardware) ===
//   ROT_ANGLE: 90.0 ou 270.0  -> se a imagem aparecer de cabeca pra baixo, troque.
//   O mapeamento do toque acompanha o ROT_ANGLE automaticamente (screen_to_logical).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#include <switch.h>
#include "net.h"
#include "cJSON.h"
#include "text.h"
#include "store.h"
#include "update.h"

#define WIN_W 1280
#define WIN_H 720
#define ROT_ANGLE 90.0     // <-- calibrar: 90.0 ou 270.0
#define DEFAULT_SERVER "https://meruem.tonserverlocal.uk"

#define TB        58       // altura da barra de topo
#define LIST_Y    76
#define ROW_H     74
#define FOOTER_H  58
#define PAGE_SIZE 40
#define TAP_THRESH 24      // movimento (px logicos) abaixo disso = toque, acima = arrasto
#define COVER_CACHE_MAX 48

#define JOY_A      0
#define JOY_B      1
#define JOY_X      2
#define JOY_Y      3
#define JOY_L      6
#define JOY_R      7
#define JOY_PLUS   10
#define JOY_MINUS  11
#define JOY_DLEFT  12
#define JOY_UP     13
#define JOY_DRIGHT 14
#define JOY_DOWN   15
#define JOY_ZL     8
#define JOY_ZR     9

typedef enum { SC_SERIES, SC_CONTINUE, SC_CHAPTERS, SC_READER } Screen;

static SDL_Renderer *gRen = NULL;
static SDL_Texture  *gCanvas = NULL;
static int g_portrait = 1;

static char *g_token = NULL;
static char g_server[256] = {0};
static const char *AREAS[3] = { "manga", "comics", "books" };
static int areaIdx = 0;
static char g_search[96] = {0};

static Screen screen = SC_SERIES;

static cJSON *g_cat = NULL;
static int catPage = 0, catTotal = 1, catCount = 0, catSel = 0, catScroll = 0;

static cJSON *g_ser = NULL;
static int chapCount = 0, chapSel = 0, chapScroll = 0;
static char curSeriesTitle[256] = {0};
static char curSeriesId[96] = {0};
static Screen g_reader_back = SC_CHAPTERS;   // pra onde o leitor volta no B

// "Continuar lendo"
static char contIds[60][96];
static int contN = 0, contSel = 0, contScroll = 0;

static char pageBase[512] = {0};
static char curChapLabel[64] = {0};
static char curBookId[96] = {0};
static int pageCount = 1, curPage = 1;
static SDL_Texture *pageTex = NULL;
static char g_self_path[512] = {0};

typedef struct {
    char id[96];
    SDL_Texture *tex;
    int failed;
} CoverCacheEntry;

static CoverCacheEntry g_cover_cache[COVER_CACHE_MAX];
static int g_cover_loaded_this_frame = 0;

// estado do toque
static int  fingerDown = 0;
static int  downLX = 0, downLY = 0, lastLY = 0, movedMax = 0;
static float dragAccum = 0.0f;

static const SDL_Color COL_TEXT = { 220, 220, 228, 255 };
static const SDL_Color COL_SEL  = { 255, 255, 255, 255 };
static const SDL_Color COL_HEAD = { 250, 215, 120, 255 };
static const SDL_Color COL_DIM  = { 150, 150, 162, 255 };
static const SDL_Color COL_SOFT = { 178, 196, 222, 255 };

typedef struct { int x, y, w, h; const char *label; } Btn;

// ---------------- dimensoes logicas ----------------
static int LW(void) { return g_portrait ? 720 : 1280; }
static int LH(void) { return g_portrait ? 1280 : 720; }
static int visible_rows(void) {
    int v = (LH() - LIST_Y - FOOTER_H - 12) / ROW_H;
    return v < 1 ? 1 : v;
}

// ---------------- JSON ----------------
static int json_int(cJSON *o, const char *k, int fb) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(it) ? it->valueint : fb;
}
static const char *json_str(cJSON *o, const char *k, const char *fb) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : fb;
}
static cJSON *cat_series_at(int i) { return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(g_cat, "series"), i); }
static cJSON *chap_at(int i) { return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"), i); }

// ---------------- canvas / frame / rotacao ----------------
static void ensure_canvas(void) {
    SDL_SetRenderTarget(gRen, NULL);
    if (gCanvas) { SDL_DestroyTexture(gCanvas); gCanvas = NULL; }
    gCanvas = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, LW(), LH());
    SDL_SetTextureBlendMode(gCanvas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(gCanvas, SDL_ScaleModeLinear);
}
static void begin_frame(void) { SDL_SetRenderTarget(gRen, gCanvas); }
static void end_frame(void) {
    SDL_SetRenderTarget(gRen, NULL);
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderClear(gRen);
    if (g_portrait) {
        SDL_Rect dst = { (WIN_W - 720) / 2, (WIN_H - 1280) / 2, 720, 1280 };
        SDL_RenderCopyEx(gRen, gCanvas, NULL, &dst, ROT_ANGLE, NULL, SDL_FLIP_NONE);
    } else {
        SDL_RenderCopy(gRen, gCanvas, NULL, NULL);
    }
    SDL_RenderPresent(gRen);
}
static void present_color(Uint8 r, Uint8 g, Uint8 b) {
    SDL_SetRenderTarget(gRen, NULL);
    SDL_SetRenderDrawColor(gRen, r, g, b, 255);
    SDL_RenderClear(gRen);
    SDL_RenderPresent(gRen);
}
static void toggle_orientation(void) {
    g_portrait = !g_portrait;
    ensure_canvas();
    if (catScroll > catCount) catScroll = 0;
    if (chapScroll > chapCount) chapScroll = 0;
}

// converte toque (normalizado) -> coordenadas logicas do canvas
static void screen_to_logical(float nx, float ny, int *lx, int *ly) {
    int px = (int)(nx * WIN_W), py = (int)(ny * WIN_H);
    if (!g_portrait) { *lx = px; *ly = py; return; }
    if (ROT_ANGLE == 90.0) { *lx = py;         *ly = 1280 - px; }
    else                   { *lx = 720 - py;   *ly = px;        }
}

// ---------------- desenho de botoes ----------------
static void btn_draw(Btn b) {
    SDL_SetRenderDrawColor(gRen, 12, 16, 26, 160);
    SDL_Rect sh = { b.x + 2, b.y + 3, b.w, b.h };
    SDL_RenderFillRect(gRen, &sh);
    SDL_SetRenderDrawColor(gRen, 40, 62, 96, 255);
    SDL_Rect r = { b.x, b.y, b.w, b.h };
    SDL_RenderFillRect(gRen, &r);
    SDL_SetRenderDrawColor(gRen, 84, 116, 160, 255);
    SDL_RenderDrawRect(gRen, &r);
    int tw = 0, th = 0;
    SDL_Texture *t = text_make(gRen, b.label, COL_SEL, 0, &tw, &th);
    if (t) {
        SDL_Rect d = { b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2, tw, th };
        SDL_RenderCopy(gRen, t, NULL, &d);
        SDL_DestroyTexture(t);
    }
}
static int btn_hit(Btn b, int lx, int ly) {
    return lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h;
}
static Btn btn_back(void)   { Btn b = { 6, 8, 150, TB - 14, "< Voltar" }; return b; }
static Btn btn_exit(void)   { Btn b = { 6, 8, 110, TB - 14, "Sair" };     return b; }
static Btn btn_rotate(void) { Btn b = { LW() - 130, 8, 124, TB - 14, "Girar" }; return b; }
static Btn btn_continue(void) { Btn b = { 124, 8, 150, TB - 14, "Continuar" }; return b; }
static Btn btn_library(void)  { Btn b = { 6, 8, 160, TB - 14, "Biblioteca" }; return b; }
static Btn btn_area(void)   { Btn b = { LW()/2 - 170, LH() - 50, 110, 40, "Area" };  return b; }
static Btn btn_search(void) { Btn b = { LW()/2 - 50, LH() - 50, 120, 40, "Buscar" }; return b; }
static Btn btn_prev(void)   { Btn b = { 6, LH() - 50, 110, 40, "< Pag" };  return b; }
static Btn btn_next(void)   { Btn b = { LW() - 116, LH() - 50, 110, 40, "Pag >" }; return b; }
static Btn btn_up(void)     { Btn b = { LW() - 76, TB + 8, 68, 64, "/\\" }; return b; }
static Btn btn_down(void)   { Btn b = { LW() - 76, LH() - FOOTER_H - 76, 68, 64, "\\/" }; return b; }

static void draw_background(void) {
    SDL_SetRenderDrawColor(gRen, 13, 16, 26, 255);
    SDL_RenderClear(gRen);
    SDL_SetRenderDrawColor(gRen, 20, 28, 43, 255);
    SDL_Rect top = { 0, 0, LW(), LH() / 3 };
    SDL_RenderFillRect(gRen, &top);
    SDL_SetRenderDrawColor(gRen, 18, 22, 34, 255);
    SDL_Rect mid = { 0, LH() / 3, LW(), LH() / 3 };
    SDL_RenderFillRect(gRen, &mid);
    SDL_SetRenderDrawColor(gRen, 11, 13, 20, 255);
    SDL_Rect bot = { 0, (LH() * 2) / 3, LW(), LH() / 3 + 2 };
    SDL_RenderFillRect(gRen, &bot);
    SDL_SetRenderDrawColor(gRen, 238, 187, 92, 34);
    SDL_Rect glow = { 0, 0, LW(), 5 };
    SDL_RenderFillRect(gRen, &glow);
}

static void draw_footer(const char *hint) {
    SDL_SetRenderDrawColor(gRen, 10, 12, 19, 232);
    SDL_Rect r = { 0, LH() - FOOTER_H, LW(), FOOTER_H };
    SDL_RenderFillRect(gRen, &r);
    SDL_SetRenderDrawColor(gRen, 52, 67, 92, 255);
    SDL_RenderDrawLine(gRen, 0, r.y, LW(), r.y);
    if (hint) text_draw(gRen, hint, 18, LH() - 41, COL_DIM, 0);
}

static void draw_empty_state(const char *title, const char *subtitle) {
    SDL_SetRenderDrawColor(gRen, 25, 32, 48, 220);
    SDL_Rect box = { 18, LIST_Y + 16, LW() - 36, 150 };
    SDL_RenderFillRect(gRen, &box);
    SDL_SetRenderDrawColor(gRen, 68, 84, 112, 255);
    SDL_RenderDrawRect(gRen, &box);
    if (title) text_draw(gRen, title, box.x + 24, box.y + 32, COL_SEL, 1);
    if (subtitle) text_draw(gRen, subtitle, box.x + 24, box.y + 88, COL_DIM, 0);
}

static void draw_row_shell(int y, int selected) {
    SDL_SetRenderDrawColor(gRen, selected ? 47 : 24, selected ? 74 : 31, selected ? 116 : 47, 242);
    SDL_Rect row = { 12, y, LW() - 24, ROW_H - 6 };
    SDL_RenderFillRect(gRen, &row);
    SDL_SetRenderDrawColor(gRen, selected ? 248 : 52, selected ? 199 : 64, selected ? 91 : 84, 255);
    SDL_RenderDrawRect(gRen, &row);
    if (selected) {
        SDL_SetRenderDrawColor(gRen, 250, 215, 120, 255);
        SDL_Rect accent = { row.x, row.y, 5, row.h };
        SDL_RenderFillRect(gRen, &accent);
    }
}

static void draw_cover_placeholder(int x, int y, int w, int h, const char *title) {
    SDL_SetRenderDrawColor(gRen, 31, 38, 54, 255);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(gRen, &r);
    SDL_SetRenderDrawColor(gRen, 79, 96, 125, 255);
    SDL_RenderDrawRect(gRen, &r);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 160);
    SDL_Rect mark = { x + w / 2 - 9, y + h / 2 - 13, 18, 26 };
    SDL_RenderDrawRect(gRen, &mark);
    if (title && title[0]) {
        char letter[2] = { title[0], 0 };
        text_draw(gRen, letter, x + 8, y + h - 28, COL_DIM, 0);
    }
}

static void draw_cover_texture(SDL_Texture *tex, int x, int y, int w, int h) {
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float scale = (float)w / tw;
    float sh = (float)h / th;
    if (sh > scale) scale = sh;
    int sw = (int)(w / scale);
    int sheight = (int)(h / scale);
    if (sw > tw) sw = tw;
    if (sheight > th) sheight = th;
    SDL_Rect src = { (tw - sw) / 2, (th - sheight) / 2, sw, sheight };
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(gRen, tex, &src, &dst);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 90);
    SDL_RenderDrawRect(gRen, &dst);
}

static void draw_scrollbar(int scroll, int count, int vis) {
    if (count <= vis) return;
    int trackH = vis * ROW_H - 8;
    int trackY = LIST_Y + 2;
    int trackX = LW() - 10;
    int thumbH = (trackH * vis) / count;
    int maxScroll = count - vis;
    int thumbY;
    if (thumbH < 24) thumbH = 24;
    thumbY = trackY + ((trackH - thumbH) * scroll) / (maxScroll > 0 ? maxScroll : 1);
    SDL_SetRenderDrawColor(gRen, 66, 74, 92, 130);
    SDL_Rect track = { trackX, trackY, 4, trackH };
    SDL_RenderFillRect(gRen, &track);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 220);
    SDL_Rect thumb = { trackX - 1, thumbY, 6, thumbH };
    SDL_RenderFillRect(gRen, &thumb);
}

// ---------------- rede ----------------
static char *login_request(const char *user, const char *pass) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "username", user);
    cJSON_AddStringToObject(o, "password", pass);
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!body) return NULL;
    struct membuf resp = {0};
    char url[512];
    snprintf(url, sizeof(url), "%s/auth/login", g_server);
    long code = net_request(url, "POST", body, NULL, &resp, NULL);
    free(body);
    char *token = NULL;
    if (code == 200 && resp.data) {
        cJSON *root = cJSON_Parse(resp.data);
        if (root) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "token");
            if (cJSON_IsString(t) && t->valuestring) token = strdup(t->valuestring);
            cJSON_Delete(root);
        }
    }
    membuf_free(&resp);
    return token;
}
static int token_is_valid(const char *token) {
    struct membuf r = {0};
    char url[512];
    snprintf(url, sizeof(url), "%s/switch/ping", g_server);
    long c = net_request(url, "GET", NULL, token, &r, NULL);
    membuf_free(&r);
    return c == 200;
}
static SDL_Texture *load_page(const char *baseUrl, int page) {
    char url[560];
    snprintf(url, sizeof(url), "%s%d", baseUrl, page);
    struct membuf buf = {0};
    long code = net_request(url, "GET", NULL, g_token, &buf, NULL);
    SDL_Texture *tex = NULL;
    if (code == 200 && buf.data && buf.len > 0) {
        SDL_RWops *rw = SDL_RWFromConstMem(buf.data, (int)buf.len);
        SDL_Surface *surf = IMG_Load_RW(rw, 1);
        if (surf) {
            tex = SDL_CreateTextureFromSurface(gRen, surf);
            if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            SDL_FreeSurface(surf);
        }
    }
    membuf_free(&buf);
    return tex;
}

static void cover_cache_clear(void) {
    for (int i = 0; i < COVER_CACHE_MAX; i++) {
        if (g_cover_cache[i].tex) SDL_DestroyTexture(g_cover_cache[i].tex);
        g_cover_cache[i].tex = NULL;
        g_cover_cache[i].failed = 0;
        g_cover_cache[i].id[0] = '\0';
    }
}

static SDL_Texture *load_image_url(const char *url) {
    struct membuf buf = {0};
    SDL_Texture *tex = NULL;
    long code = net_request(url, "GET", NULL, g_token, &buf, NULL);
    if (code == 200 && buf.data && buf.len > 0) {
        SDL_RWops *rw = SDL_RWFromConstMem(buf.data, (int)buf.len);
        SDL_Surface *surf = IMG_Load_RW(rw, 1);
        if (surf) {
            tex = SDL_CreateTextureFromSurface(gRen, surf);
            if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            SDL_FreeSurface(surf);
        }
    }
    membuf_free(&buf);
    return tex;
}

static SDL_Texture *series_cover_texture(cJSON *series) {
    const char *id = json_str(series, "id", "");
    const char *cover = json_str(series, "cover", "");
    int empty = -1;
    if (!id[0] || !cover[0]) return NULL;
    for (int i = 0; i < COVER_CACHE_MAX; i++) {
        if (g_cover_cache[i].id[0] && strcmp(g_cover_cache[i].id, id) == 0) {
            if (g_cover_cache[i].failed) return NULL;
            return g_cover_cache[i].tex;
        }
        if (empty < 0 && !g_cover_cache[i].id[0]) empty = i;
    }
    if (empty < 0 || g_cover_loaded_this_frame) return NULL;
    g_cover_loaded_this_frame = 1;
    snprintf(g_cover_cache[empty].id, sizeof(g_cover_cache[empty].id), "%s", id);
    char url[640];
    if (strncmp(cover, "http://", 7) == 0 || strncmp(cover, "https://", 8) == 0) {
        snprintf(url, sizeof(url), "%s", cover);
    } else {
        snprintf(url, sizeof(url), "%s%s", g_server, cover);
    }
    g_cover_cache[empty].tex = load_image_url(url);
    g_cover_cache[empty].failed = g_cover_cache[empty].tex ? 0 : 1;
    return g_cover_cache[empty].tex;
}

// ---------------- teclado / login ----------------
static int prompt_text(const char *guide, char *out, size_t cap, int password) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return -1;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, (u32)(cap - 1));
    if (password) swkbdConfigSetPasswordFlag(&kbd, 1);
    out[0] = '\0';
    Result rc = swkbdShow(&kbd, out, cap);
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return -1;
    return out[0] ? 0 : -2;
}
// mensagem; A ou toque = continuar(1), + = sair(0)
static int message_screen(const char *l1, const char *l2) {
    SDL_Event e;
    while (appletMainLoop()) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
            if (e.type == SDL_FINGERUP) return 1;
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (e.jbutton.button == JOY_PLUS) return 0;
                if (e.jbutton.button == JOY_A) return 1;
            }
        }
        begin_frame();
        SDL_SetRenderDrawColor(gRen, 40, 14, 14, 255);
        SDL_RenderClear(gRen);
        text_draw(gRen, "Meruem", 40, 200, COL_HEAD, 1);
        if (l1) text_draw(gRen, l1, 40, 260, COL_SEL, 0);
        if (l2) text_draw(gRen, l2, 40, 300, COL_DIM, 0);
        text_draw(gRen, "Toque ou A = continuar    + = sair", 40, 360, COL_DIM, 0);
        end_frame();
        SDL_Delay(16);
    }
    return 0;
}
// A/toque = sim; B/+ = nao
static int confirm_screen(const char *l1, const char *l2, const char *l3) {
    SDL_Event e;
    while (appletMainLoop()) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return 0;
            if (e.type == SDL_FINGERUP) return 1;
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (e.jbutton.button == JOY_PLUS || e.jbutton.button == JOY_B) return 0;
                if (e.jbutton.button == JOY_A) return 1;
            }
        }
        begin_frame();
        SDL_SetRenderDrawColor(gRen, 16, 20, 32, 255);
        SDL_RenderClear(gRen);
        text_draw(gRen, "Meruem", 40, 180, COL_HEAD, 1);
        if (l1) text_draw(gRen, l1, 40, 250, COL_SEL, 0);
        if (l2) text_draw(gRen, l2, 40, 290, COL_DIM, 0);
        if (l3) text_draw(gRen, l3, 40, 350, COL_TEXT, 0);
        text_draw(gRen, "A ou toque = atualizar    B/+ = depois", 40, 410, COL_DIM, 0);
        end_frame();
        SDL_Delay(16);
    }
    return 0;
}
static int authenticate(void) {
    char tok[160];
    if (store_load_token(tok, sizeof(tok))) {
        present_color(20, 20, 40);
        if (token_is_valid(tok)) { g_token = strdup(tok); return 1; }
        store_clear_token();
    }
    while (appletMainLoop()) {
        char user[96] = {0}, pass[96] = {0};
        int ru = prompt_text("Usuario ou e-mail do Meruem", user, sizeof(user), 0);
        if (ru == -1) return 0;
        if (ru == -2) continue;
        int rp = prompt_text("Senha", pass, sizeof(pass), 1);
        if (rp == -1) return 0;
        if (rp == -2) continue;
        present_color(20, 20, 40);
        g_token = login_request(user, pass);
        if (g_token) { store_save_token(g_token); return 1; }
        if (!message_screen("Login falhou. Verifique usuario/senha.", NULL)) return 0;
    }
    return 0;
}

static int maybe_install_update(void) {
    struct update_info info;
    char line1[160];
    char line2[200];
    char err[256];
    int rc;

    present_color(20, 20, 40);
    rc = update_check(&info);
    if (rc == UPDATE_CHECK_DISABLED || rc == UPDATE_CHECK_UP_TO_DATE) return 0;
    if (rc == UPDATE_CHECK_ERROR) return 0;

    snprintf(line1, sizeof(line1), "Nova versao encontrada: %s", info.latest_version);
    snprintf(line2, sizeof(line2), "Atual: %s    Asset: %s", APP_VERSION_STR,
             info.asset_name[0] ? info.asset_name : "(sem nome)");
    if (!confirm_screen(line1, line2, "Baixar e trocar o .nro agora?")) return 0;

    present_color(20, 20, 40);
    if (update_apply(&info, g_self_path, err, sizeof(err)) != 0) {
        message_screen("Falha ao instalar a atualizacao.", err[0] ? err : info.message);
        return 0;
    }

    message_screen("Atualizacao instalada com sucesso.", "Feche e abra o Meruem de novo.");
    return 1;
}

static int configure_server(void) {
    char url[256] = {0};
    if (store_load_server(g_server, sizeof(g_server))) return 1;
    if (DEFAULT_SERVER[0]) {
        snprintf(g_server, sizeof(g_server), "%s", DEFAULT_SERVER);
        store_save_server(g_server);
        return 1;
    }
    while (appletMainLoop()) {
        int r = prompt_text("URL do servidor Meruem (https://...)", url, sizeof(url), 0);
        if (r == -1) return 0;
        if (r == -2) continue;
        if (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) {
            if (!message_screen("URL invalida.", "Use https://seu-dominio")) return 0;
            continue;
        }
        store_save_server(url);
        if (store_load_server(g_server, sizeof(g_server))) return 1;
    }
    return 0;
}

// ---------------- dados ----------------
static void load_catalog(void) {
    present_color(20, 20, 40);
    if (g_cat) { cJSON_Delete(g_cat); g_cat = NULL; }
    cover_cache_clear();
    char url[512];
    int n = snprintf(url, sizeof(url), "%s/switch/catalog?area=%s&size=%d&page=%d",
                     g_server,
                     AREAS[areaIdx], PAGE_SIZE, catPage);
    if (g_search[0] && n > 0 && (size_t)n < sizeof(url)) {
        char enc[200];
        net_urlencode(g_search, enc, sizeof(enc));
        snprintf(url + n, sizeof(url) - n, "&search=%s", enc);
    }
    struct membuf r = {0};
    long code = net_request(url, "GET", NULL, g_token, &r, NULL);
    if (code == 200 && r.data) g_cat = cJSON_Parse(r.data);
    membuf_free(&r);
    catCount = 0; catTotal = 1; catScroll = 0;
    if (g_cat) {
        catTotal = json_int(g_cat, "totalPages", 1);
        catCount = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_cat, "series"));
    }
    if (catSel >= catCount) catSel = catCount > 0 ? catCount - 1 : 0;
    if (catSel < 0) catSel = 0;
}
static void enter_series(int idx) {
    cJSON *s = cat_series_at(idx);
    if (!s) return;
    const char *id = json_str(s, "id", "");
    if (!id[0]) return;
    snprintf(curSeriesId, sizeof(curSeriesId), "%s", id);
    snprintf(curSeriesTitle, sizeof(curSeriesTitle), "%s", json_str(s, "title", "Serie"));
    present_color(20, 20, 40);
    if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
    char url[512];
    snprintf(url, sizeof(url), "%s/switch/series/%s", g_server, id);
    struct membuf r = {0};
    long code = net_request(url, "GET", NULL, g_token, &r, NULL);
    if (code == 200 && r.data) g_ser = cJSON_Parse(r.data);
    membuf_free(&r);
    chapCount = 0; chapSel = 0; chapScroll = 0;
    if (g_ser) chapCount = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"));
    screen = SC_CHAPTERS;
}
static void enter_reader(int idx) {
    cJSON *c = chap_at(idx);
    if (!c) return;
    const char *pb = json_str(c, "pageBase", "");
    if (!pb[0]) return;
    snprintf(pageBase, sizeof(pageBase), "%s%s", g_server, pb);
    pageCount = json_int(c, "pages", 1);
    if (pageCount < 1) pageCount = 1;
    snprintf(curChapLabel, sizeof(curChapLabel), "#%s", json_str(c, "number", "?"));
    snprintf(curBookId, sizeof(curBookId), "%s", json_str(c, "id", ""));
    curPage = store_get_progress(curBookId);
    if (curPage > pageCount) curPage = pageCount;
    if (curPage < 1) curPage = 1;
    present_color(20, 20, 40);
    if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; }
    pageTex = load_page(pageBase, curPage);
    g_reader_back = SC_CHAPTERS;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, pageCount);
    screen = SC_READER;
}

// Retoma a leitura a partir de um registro salvo (tela Continuar).
static void enter_reader_from_record(const char *bookId) {
    char sid[96] = {0}, st[256] = {0}, cl[64] = {0}, pb[512] = {0};
    int page = 1, pages = 1;
    if (!store_entry(bookId, sid, sizeof(sid), st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), &page, &pages)) return;
    if (!pb[0]) return;
    snprintf(curBookId, sizeof(curBookId), "%s", bookId);
    snprintf(curSeriesId, sizeof(curSeriesId), "%s", sid);
    snprintf(curSeriesTitle, sizeof(curSeriesTitle), "%s", st);
    snprintf(curChapLabel, sizeof(curChapLabel), "%s", cl);
    snprintf(pageBase, sizeof(pageBase), "%s", pb);
    pageCount = pages < 1 ? 1 : pages;
    curPage = page; if (curPage > pageCount) curPage = pageCount; if (curPage < 1) curPage = 1;
    present_color(20, 20, 40);
    if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; }
    pageTex = load_page(pageBase, curPage);
    g_reader_back = SC_CONTINUE;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, pageCount);
    screen = SC_READER;
}

static void load_continue(void) {
    contN = store_recent(contIds, 60);
    if (contSel >= contN) contSel = contN > 0 ? contN - 1 : 0;
    if (contSel < 0) contSel = 0;
    contScroll = 0;
}
static void reader_goto(int n) {
    if (n < 1) n = 1;
    if (n > pageCount) n = pageCount;
    if (n == curPage) return;
    curPage = n;
    present_color(20, 20, 40);
    if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; }
    pageTex = load_page(pageBase, curPage);
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, pageCount);
}

// ---------------- render ----------------
static void draw_topbar(const char *title, Btn left) {
    SDL_SetRenderDrawColor(gRen, 16, 20, 31, 245);
    SDL_Rect bar = { 0, 0, LW(), TB };
    SDL_RenderFillRect(gRen, &bar);
    SDL_SetRenderDrawColor(gRen, 54, 70, 96, 255);
    SDL_RenderDrawLine(gRen, 0, TB - 1, LW(), TB - 1);
    btn_draw(left);
    btn_draw(btn_rotate());
    if (title) text_draw(gRen, title, left.x + left.w + 16, 12, COL_HEAD, 0);
}

static void render_series(void) {
    draw_background();
    g_cover_loaded_this_frame = 0;
    char hd[200];
    if (g_search[0]) snprintf(hd, sizeof(hd), "%s  busca: %.18s  pag %d/%d", AREAS[areaIdx], g_search, catPage + 1, catTotal);
    else             snprintf(hd, sizeof(hd), "%s  pag %d/%d", AREAS[areaIdx], catPage + 1, catTotal);
    SDL_SetRenderDrawColor(gRen, 16, 20, 31, 245);
    SDL_Rect tbar = { 0, 0, LW(), TB };
    SDL_RenderFillRect(gRen, &tbar);
    SDL_SetRenderDrawColor(gRen, 54, 70, 96, 255);
    SDL_RenderDrawLine(gRen, 0, TB - 1, LW(), TB - 1);
    btn_draw(btn_exit());
    btn_draw(btn_continue());
    btn_draw(btn_rotate());
    text_draw(gRen, hd, btn_continue().x + btn_continue().w + 12, 12, COL_HEAD, 0);

    int vis = visible_rows();
    if (catCount == 0) draw_empty_state("Nada encontrado", "Tente outra busca ou troque a area.");
    for (int i = 0; i < vis; i++) {
        int idx = catScroll + i;
        if (idx >= catCount) break;
        int y = LIST_Y + i * ROW_H;
        draw_row_shell(y, idx == catSel);
        cJSON *s = cat_series_at(idx);
        char row[300];
        char meta[160];
        snprintf(row, sizeof(row), "%s", json_str(s, "title", "(sem titulo)"));
        snprintf(meta, sizeof(meta), "%d livros", json_int(s, "booksCount", 0));
        SDL_Texture *cover = series_cover_texture(s);
        if (cover) draw_cover_texture(cover, 22, y + 7, 42, 60);
        else       draw_cover_placeholder(22, y + 7, 42, 60, row);
        text_draw(gRen, row, 78, y + 12, idx == catSel ? COL_SEL : COL_TEXT, 0);
        text_draw(gRen, meta, 78, y + 42, COL_SOFT, 0);
    }
    draw_scrollbar(catScroll, catCount, vis);
    draw_footer(NULL);
    btn_draw(btn_prev()); btn_draw(btn_area()); btn_draw(btn_search()); btn_draw(btn_next());
}

static void render_chapters(void) {
    draw_background();
    char hd[200];
    snprintf(hd, sizeof(hd), "%.30s (%d cap)", curSeriesTitle, chapCount);
    draw_topbar(hd, btn_back());

    int vis = visible_rows();
    if (chapCount == 0) draw_empty_state("Sem capitulos", "Esta serie nao retornou capitulos.");
    for (int i = 0; i < vis; i++) {
        int idx = chapScroll + i;
        if (idx >= chapCount) break;
        int y = LIST_Y + i * ROW_H;
        draw_row_shell(y, idx == chapSel);
        cJSON *c = chap_at(idx);
        const char *num = json_str(c, "number", "?");
        const char *tt = json_str(c, "title", "");
        int prog = store_get_progress(json_str(c, "id", ""));
        char row[320];
        char meta[160];
        snprintf(row, sizeof(row), "#%s  %.40s", num, tt[0] ? tt : "Capitulo");
        snprintf(meta, sizeof(meta), "%d paginas%s", json_int(c, "pages", 0), prog > 1 ? "  em andamento" : "");
        text_draw(gRen, row, 24, y + 8, idx == chapSel ? COL_SEL : COL_TEXT, 0);
        text_draw(gRen, meta, 24, y + 34, COL_SOFT, 0);
    }
    draw_scrollbar(chapScroll, chapCount, vis);
    draw_footer("A/toque: ler    B: voltar    L/R: pular lista    ZL/ZR: girar");
    btn_draw(btn_up()); btn_draw(btn_down());
}

static void render_continue(void) {
    draw_background();
    draw_topbar("Continuar lendo", btn_library());
    int vis = visible_rows();
    if (contN == 0) {
        draw_empty_state("Nada por aqui ainda", "Abra a Biblioteca e escolha um capitulo.");
    }
    for (int i = 0; i < vis; i++) {
        int idx = contScroll + i;
        if (idx >= contN) break;
        int y = LIST_Y + i * ROW_H;
        draw_row_shell(y, idx == contSel);
        char st[256] = {0}, cl[64] = {0}, sid[96] = {0}, pb[512] = {0};
        int page = 1, pages = 1;
        store_entry(contIds[idx], sid, sizeof(sid), st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), &page, &pages);
        char row[360];
        char meta[160];
        snprintf(row, sizeof(row), "%.34s", st[0] ? st : "(serie)");
        snprintf(meta, sizeof(meta), "%s  pagina %d/%d", cl, page, pages);
        text_draw(gRen, row, 24, y + 8, idx == contSel ? COL_SEL : COL_TEXT, 0);
        text_draw(gRen, meta, 24, y + 34, COL_SOFT, 0);
    }
    draw_scrollbar(contScroll, contN, vis);
    draw_footer("A/toque: continuar    B/Biblioteca: acervo    ZL/ZR: girar");
}

static void render_reader(void) {
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderClear(gRen);
    if (pageTex) {
        int tw = 0, th = 0;
        SDL_QueryTexture(pageTex, NULL, NULL, &tw, &th);
        if (tw > 0 && th > 0) {
            float scale = (float)LW() / tw;
            float sh = (float)LH() / th;
            if (sh < scale) scale = sh;
            int w = (int)(tw * scale), h = (int)(th * scale);
            SDL_Rect dst = { (LW() - w) / 2, (LH() - h) / 2, w, h };
            SDL_RenderCopy(gRen, pageTex, NULL, &dst);
        }
    } else {
        SDL_SetRenderDrawColor(gRen, 90, 0, 120, 255);
        SDL_Rect r = { LW()/2 - 160, LH()/2 - 40, 320, 80 };
        SDL_RenderFillRect(gRen, &r);
    }
    // barra de topo translucida
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 170);
    SDL_Rect bar = { 0, 0, LW(), TB };
    SDL_RenderFillRect(gRen, &bar);
    btn_draw(btn_back());
    btn_draw(btn_rotate());
    char pc[80];
    snprintf(pc, sizeof(pc), "%s  %d/%d", curChapLabel, curPage, pageCount);
    text_draw(gRen, pc, btn_back().x + btn_back().w + 16, 12, COL_SEL, 0);
    if (pageCount > 1) {
        int barW = LW() - 36;
        int filled = (barW * curPage) / pageCount;
        SDL_SetRenderDrawColor(gRen, 26, 30, 44, 210);
        SDL_Rect bg = { 18, LH() - 22, barW, 7 };
        SDL_RenderFillRect(gRen, &bg);
        SDL_SetRenderDrawColor(gRen, 250, 215, 120, 240);
        SDL_Rect fg = { 18, LH() - 22, filled, 7 };
        SDL_RenderFillRect(gRen, &fg);
    }
    text_draw(gRen, "Toque esquerda/direita para virar", 18, LH() - 52, COL_DIM, 0);
}

// ---------------- toque: dispatch de tap ----------------
static int *running_ptr = NULL;
static void handle_tap(int lx, int ly) {
    // botoes comuns no topo
    if (btn_hit(btn_rotate(), lx, ly)) { toggle_orientation(); return; }

    if (screen == SC_SERIES) {
        if (btn_hit(btn_exit(), lx, ly)) { if (running_ptr) *running_ptr = 0; return; }
        if (btn_hit(btn_continue(), lx, ly)) { load_continue(); screen = SC_CONTINUE; return; }
        if (btn_hit(btn_prev(), lx, ly)) { if (catPage > 0) { catPage--; catSel = 0; load_catalog(); } return; }
        if (btn_hit(btn_next(), lx, ly)) { if (catPage < catTotal - 1) { catPage++; catSel = 0; load_catalog(); } return; }
        if (btn_hit(btn_area(), lx, ly)) { areaIdx = (areaIdx + 1) % 3; catPage = 0; catSel = 0; g_search[0] = '\0'; load_catalog(); return; }
        if (btn_hit(btn_search(), lx, ly)) {
            char term[96] = {0};
            int rs = prompt_text("Buscar serie (vazio = limpar)", term, sizeof(term), 0);
            if (rs != -1) { snprintf(g_search, sizeof(g_search), "%s", rs == 0 ? term : ""); catPage = 0; catSel = 0; load_catalog(); }
            return;
        }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = catScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < catCount) { catSel = idx; enter_series(idx); }
        }
    } else if (screen == SC_CHAPTERS) {
        if (btn_hit(btn_back(), lx, ly)) { screen = SC_SERIES; return; }
        if (btn_hit(btn_up(), lx, ly))   { chapScroll -= visible_rows(); if (chapScroll < 0) chapScroll = 0; return; }
        if (btn_hit(btn_down(), lx, ly)) { chapScroll += visible_rows(); if (chapScroll > chapCount - 1) chapScroll = (chapCount > 0 ? chapCount - 1 : 0); return; }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = chapScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < chapCount) { chapSel = idx; enter_reader(idx); }
        }
    } else if (screen == SC_CONTINUE) {
        if (btn_hit(btn_library(), lx, ly)) { screen = SC_SERIES; return; }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = contScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < contN) { contSel = idx; enter_reader_from_record(contIds[idx]); }
        }
    } else { // SC_READER
        if (ly < TB) {
            if (btn_hit(btn_back(), lx, ly)) { if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; } screen = g_reader_back; return; }
            return;
        }
        if (lx < LW() / 2) reader_goto(curPage - 1);
        else               reader_goto(curPage + 1);
    }
}

// arrasto vertical nas listas -> rola
static void handle_drag(int curLY) {
    int *scroll = NULL, count = 0;
    if (screen == SC_SERIES)         { scroll = &catScroll;  count = catCount; }
    else if (screen == SC_CONTINUE)  { scroll = &contScroll; count = contN; }
    else if (screen == SC_CHAPTERS)  { scroll = &chapScroll; count = chapCount; }
    else return;
    dragAccum += (float)(curLY - lastLY);
    while (dragAccum >= ROW_H)  { (*scroll)--; dragAccum -= ROW_H; }
    while (dragAccum <= -ROW_H) { (*scroll)++; dragAccum += ROW_H; }
    int maxs = count - 1; if (maxs < 0) maxs = 0;
    if (*scroll < 0) *scroll = 0;
    if (*scroll > maxs) *scroll = maxs;
}

int main(int argc, char **argv) {
    update_resolve_target_path((argc > 0 && argv) ? argv[0] : NULL, g_self_path, sizeof(g_self_path));

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    // Filtro linear: ao reduzir paginas grandes de manga, suaviza em vez de
    // serrar (nearest). Melhora muito a leitura do texto. Setar ANTES das texturas.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow("Meruem", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    gRen = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_JoystickOpen(0);

    int text_ok = (text_init() == 0);
    ensure_canvas();
    present_color(20, 20, 40);

    int net_ok = 0, curl_ok = 0;

    if (!text_ok) {
        present_color(150, 0, 0); SDL_Delay(2500);
    } else if (R_FAILED(socketInitializeDefault())) {
        message_screen("Falha ao iniciar a rede do Switch.", NULL);
    } else if ((net_ok = 1) && net_init() != 0) {
        message_screen("Falha ao iniciar o curl.", NULL);
    } else {
        curl_ok = 1;
        store_init();
        if (configure_server() && authenticate()) {
            if (maybe_install_update()) goto cleanup;
            load_catalog();
            load_continue();
            screen = (contN > 0) ? SC_CONTINUE : SC_SERIES;
            int running = 1;
            running_ptr = &running;
            SDL_Event e;
            while (running && appletMainLoop()) {
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) { running = 0; break; }

                    if (e.type == SDL_FINGERDOWN) {
                        screen_to_logical(e.tfinger.x, e.tfinger.y, &downLX, &downLY);
                        lastLY = downLY; movedMax = 0; dragAccum = 0; fingerDown = 1;
                    } else if (e.type == SDL_FINGERMOTION) {
                        if (fingerDown) {
                            int cx, cy; screen_to_logical(e.tfinger.x, e.tfinger.y, &cx, &cy);
                            int d = abs(cx - downLX) + abs(cy - downLY);
                            if (d > movedMax) movedMax = d;
                            if (movedMax > TAP_THRESH) handle_drag(cy);
                            lastLY = cy;
                        }
                    } else if (e.type == SDL_FINGERUP) {
                        if (fingerDown && movedMax <= TAP_THRESH) handle_tap(downLX, downLY);
                        fingerDown = 0;
                    } else if (e.type == SDL_JOYBUTTONDOWN) {
                        int b = e.jbutton.button;
                        if (b == JOY_PLUS) { running = 0; break; }
                        if (b == JOY_ZL || b == JOY_ZR) { toggle_orientation(); continue; }

                        if (screen == SC_SERIES) {
                            if (b == JOY_UP && catSel > 0) catSel--;
                            else if (b == JOY_DOWN && catSel < catCount - 1) catSel++;
                            else if ((b == JOY_L || b == JOY_DLEFT) && catPage > 0) { catPage--; catSel = 0; load_catalog(); }
                            else if ((b == JOY_R || b == JOY_DRIGHT) && catPage < catTotal - 1) { catPage++; catSel = 0; load_catalog(); }
                            else if (b == JOY_Y) { areaIdx = (areaIdx + 1) % 3; catPage = 0; catSel = 0; g_search[0] = '\0'; load_catalog(); }
                            else if (b == JOY_X) {
                                char term[96] = {0};
                                int rs = prompt_text("Buscar serie (vazio = limpar)", term, sizeof(term), 0);
                                if (rs != -1) { snprintf(g_search, sizeof(g_search), "%s", rs == 0 ? term : ""); catPage = 0; catSel = 0; load_catalog(); }
                            }
                            else if (b == JOY_MINUS) { store_clear_token(); if (g_token) { free(g_token); g_token = NULL; } if (!authenticate()) { running = 0; break; } catPage = 0; catSel = 0; load_catalog(); }
                            else if (b == JOY_B) { load_continue(); screen = SC_CONTINUE; }
                            else if (b == JOY_A && catCount > 0) enter_series(catSel);
                            if (catSel < catScroll) catScroll = catSel;
                            if (catSel >= catScroll + visible_rows()) catScroll = catSel - visible_rows() + 1;
                        } else if (screen == SC_CHAPTERS) {
                            if (b == JOY_UP && chapSel > 0) chapSel--;
                            else if (b == JOY_DOWN && chapSel < chapCount - 1) chapSel++;
                            else if (b == JOY_L) { chapSel -= visible_rows(); if (chapSel < 0) chapSel = 0; }
                            else if (b == JOY_R) { chapSel += visible_rows(); if (chapSel > chapCount - 1) chapSel = chapCount - 1; }
                            else if (b == JOY_A && chapCount > 0) enter_reader(chapSel);
                            else if (b == JOY_B) screen = SC_SERIES;
                            if (chapSel < chapScroll) chapScroll = chapSel;
                            if (chapSel >= chapScroll + visible_rows()) chapScroll = chapSel - visible_rows() + 1;
                        } else if (screen == SC_CONTINUE) {
                            if (b == JOY_UP && contSel > 0) contSel--;
                            else if (b == JOY_DOWN && contSel < contN - 1) contSel++;
                            else if (b == JOY_A && contN > 0) enter_reader_from_record(contIds[contSel]);
                            else if (b == JOY_B) screen = SC_SERIES;
                            if (contSel < contScroll) contScroll = contSel;
                            if (contSel >= contScroll + visible_rows()) contScroll = contSel - visible_rows() + 1;
                        } else {
                            if (b == JOY_R || b == JOY_DRIGHT) reader_goto(curPage + 1);
                            else if (b == JOY_L || b == JOY_DLEFT) reader_goto(curPage - 1);
                            else if (b == JOY_B) { if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; } screen = g_reader_back; }
                        }
                    }
                }

                begin_frame();
                if (screen == SC_SERIES) render_series();
                else if (screen == SC_CONTINUE) render_continue();
                else if (screen == SC_CHAPTERS) render_chapters();
                else render_reader();
                end_frame();
                SDL_Delay(16);
            }
            store_flush();
        }
    }

cleanup:
    if (pageTex) SDL_DestroyTexture(pageTex);
    if (g_ser)   cJSON_Delete(g_ser);
    if (g_cat)   cJSON_Delete(g_cat);
    cover_cache_clear();
    if (g_token) free(g_token);
    if (curl_ok) net_exit();
    if (net_ok)  socketExit();
    if (text_ok) text_exit();
    if (gCanvas) SDL_DestroyTexture(gCanvas);
    if (gRen)    SDL_DestroyRenderer(gRen);
    if (win)     SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
