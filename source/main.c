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
#include <math.h>
#include <sys/stat.h>
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
#define READER_ZOOM_MIN 1.0f
#define READER_ZOOM_MAX 4.0f
#define READER_PINCH_DEADZONE 0.12f
#define READER_OVERLAY_MS 2200
#define NEXT_CHAPTER_MS 5000
#define AREA_COUNT 2
#define OFFLINE_DIR "sdmc:/switch/Meruem/offline"
#define OFFLINE_COUNT_CACHE_MAX 128

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

typedef enum { SC_SERIES, SC_FAVORITES, SC_SETTINGS, SC_CONTINUE, SC_CHAPTERS, SC_READER } Screen;

static SDL_Renderer *gRen = NULL;
static SDL_Texture  *gCanvas = NULL;
static int g_portrait = 1;

static char *g_token = NULL;
static char g_server[256] = {0};
static char g_username[96] = {0};
static int g_offline_mode = 0;
static const char *AREAS[AREA_COUNT] = { "manga", "comics" };
static int areaIdx = 0;
static char g_search[96] = {0};
static int catViewMode = 0; // 0 = capas, 1 = lista compacta

static Screen screen = SC_SERIES;
static int catalogFavorites = 0;
static int catalogLoadFailed = 0;

static int profileLoaded = 0;
static int profileFailed = 0;
static char profileTier[32] = "";
static char profileAccess[128] = "";
static char profileChapters[128] = "";
static char profileBooks[128] = "";
static char profileCounts[160] = "";

static cJSON *g_cat = NULL;
static int catPage = 0, catTotal = 1, catCount = 0, catSel = 0, catScroll = 0;

static cJSON *g_ser = NULL;
static int chapCount = 0, chapSel = 0, chapScroll = 0;
static char curSeriesTitle[256] = {0};
static char curSeriesId[96] = {0};
static char curSeriesCover[640] = {0};
static int chapReversed = 0;              // 1 = mostrar do mais novo pro mais antigo
static Screen g_chapters_back = SC_SERIES;
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
static int curChapIndex = -1;
static float rd_zoom = READER_ZOOM_MIN;
static float rd_pan_x = 0.0f, rd_pan_y = 0.0f;
static Uint32 reader_overlay_until = 0;
static int next_prompt_cancelled = 0;
static Uint32 next_prompt_started = 0;

typedef struct {
    char id[96];
    char url[640];
    char token[512];
    SDL_Texture *tex;
    SDL_Thread *thread;
    unsigned char *data;
    size_t len;
    volatile int ready;
    volatile int loading;
    int failed;
} CoverCacheEntry;

static CoverCacheEntry g_cover_cache[COVER_CACHE_MAX];
static int g_cover_started_this_frame = 0;

typedef struct {
    char id[96];
    int pages;
    int count;
} OfflineCountEntry;

static OfflineCountEntry g_offline_counts[OFFLINE_COUNT_CACHE_MAX];

// estado do toque
static int  fingerDown = 0;
static int  downLX = 0, downLY = 0, lastLY = 0, movedMax = 0;
static float dragAccum = 0.0f;

typedef struct {
    int active;
    SDL_FingerID id;
    int x, y;
    int startX, startY;
} TouchSlot;

static TouchSlot readerTouches[2];
static int readerSwipeMoved = 0;
static int readerPinching = 0;
static int readerPinchLive = 0;
static float readerPinchBaseDist = 0.0f;
static float readerPinchBaseZoom = 1.0f;

static void enter_reader(int idx);
static void load_catalog(void);
static void reader_reset_view(void);
static void reader_start_next_prompt(void);
static void reader_show_overlay(void);
static void reader_clamp_pan(void);
static void reader_open_next_chapter_now(void);
static int reader_has_next_chapter(void);
static void offline_download_current_chapter(void);

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
// ---- grade de capas (tela de series) ----
static int grid_cols(void) { return g_portrait ? 3 : 5; }
static int grid_gap(void) { return 14; }
static int grid_card_w(void) { int cols = grid_cols(); return (LW() - grid_gap() * (cols + 1)) / cols; }
static int grid_cover_h(void) { return (int)(grid_card_w() * 1.45f); }
static int grid_cell_h(void) { return grid_cover_h() + 42; }
static int grid_visible_rows(void) {
    int v = (LH() - LIST_Y - FOOTER_H - 6) / grid_cell_h();
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
static cJSON *chap_at_raw(int i) { return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"), i); }
static cJSON *chap_at(int i) {
    if (chapReversed && chapCount > 0) return chap_at_raw(chapCount - 1 - i);
    return chap_at_raw(i);
}

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
static Btn btn_favorites(void) { Btn b = { 282, 8, 140, TB - 14, "Favoritos" }; return b; }
static Btn btn_catalog_home(void) { Btn b = { 282, 8, 140, TB - 14, "Biblioteca" }; return b; }
static Btn btn_settings(void) { Btn b = { 430, 8, 120, TB - 14, "Conta" }; return b; }
static Btn btn_library(void)  { Btn b = { 6, 8, 160, TB - 14, "Biblioteca" }; return b; }
static Btn btn_area(void)   { Btn b = { LW()/2 - 170, LH() - 50, 110, 40, "Area" };  return b; }
static Btn btn_search(void) { Btn b = { LW()/2 - 50, LH() - 50, 120, 40, "Buscar" }; return b; }
static Btn btn_view_mode(void) { Btn b = { LW()/2 + 80, LH() - 50, 120, 40, catViewMode ? "Capas" : "Lista" }; return b; }
static Btn btn_prev(void)   { Btn b = { 6, LH() - 50, 110, 40, "< Pag" };  return b; }
static Btn btn_next(void)   { Btn b = { LW() - 116, LH() - 50, 110, 40, "Pag >" }; return b; }
static Btn btn_up(void)     { Btn b = { LW() - 76, TB + 8, 68, 64, "/\\" }; return b; }
static Btn btn_down(void)   { Btn b = { LW() - 76, LH() - FOOTER_H - 76, 68, 64, "\\/" }; return b; }
static Btn btn_chap_order(void) { Btn b = { LW() - 312, 8, 170, TB - 14, chapReversed ? "Mais antigos" : "Mais novos" }; return b; }
static Btn btn_next_open(void) { Btn b = { LW()/2 - 210, LH()/2 + 44, 190, 46, "Abrir agora" }; return b; }
static Btn btn_next_cancel(void) { Btn b = { LW()/2 + 20, LH()/2 + 44, 170, 46, "Cancelar" }; return b; }
static Btn btn_switch_account(void) { Btn b = { 30, LH() - FOOTER_H - 72, 230, 50, "Trocar conta" }; return b; }
static Btn btn_offline(void) { Btn b = { LW() - 286, 8, 146, TB - 14, "Offline" }; return b; }

static int is_catalog_screen(void) {
    return screen == SC_SERIES || screen == SC_FAVORITES;
}

static void return_to_library(void) {
    catalogFavorites = 0;
    catPage = 0;
    catSel = 0;
    catScroll = 0;
    load_catalog();
    screen = SC_SERIES;
}

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

static void draw_scrollbar_units(int scroll, int count, int vis, int unitH) {
    if (count <= vis) return;
    int trackH = vis * unitH - 8;
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

static void draw_scrollbar(int scroll, int count, int vis) {
    draw_scrollbar_units(scroll, count, vis, ROW_H);
}

static void draw_more_hint(int scroll, int count, int vis) {
    if (count <= vis) return;
    const char *hint = scroll <= 0 ? "Mais abaixo v" : (scroll + vis >= count ? "^ Mais acima" : "^ Mais  v");
    int tw = 0, th = 0;
    SDL_Texture *t = text_make(gRen, hint, COL_HEAD, 0, &tw, &th);
    if (!t) return;
    int x = LW() - tw - 24;
    int y = LH() - FOOTER_H - th - 8;
    SDL_SetRenderDrawColor(gRen, 10, 12, 19, 205);
    SDL_Rect bg = { x - 10, y - 6, tw + 20, th + 12 };
    SDL_RenderFillRect(gRen, &bg);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 130);
    SDL_RenderDrawRect(gRen, &bg);
    SDL_Rect d = { x, y, tw, th };
    SDL_RenderCopy(gRen, t, NULL, &d);
    SDL_DestroyTexture(t);
}

static void draw_selected_card_focus(int x, int y, int w, int h) {
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 42);
    SDL_Rect glow = { x - 8, y - 8, w + 16, h + 54 };
    SDL_RenderFillRect(gRen, &glow);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 255);
    for (int i = 0; i < 4; i++) {
        SDL_Rect b = { x - 4 - i, y - 4 - i, w + 8 + i * 2, h + 8 + i * 2 };
        SDL_RenderDrawRect(gRen, &b);
    }
    SDL_SetRenderDrawColor(gRen, 10, 12, 19, 224);
    SDL_Rect chip = { x + 10, y + h - 36, 92, 28 };
    SDL_RenderFillRect(gRen, &chip);
    SDL_SetRenderDrawColor(gRen, 250, 215, 120, 190);
    SDL_RenderDrawRect(gRen, &chip);
    text_draw(gRen, "A Abrir", chip.x + 12, chip.y + 5, COL_HEAD, 0);
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
    long c = net_request_timeout(url, "GET", NULL, token, &r, NULL, 4L, 8L);
    membuf_free(&r);
    return c == 200;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void offline_safe_id(const char *id, char *out, size_t cap) {
    size_t j = 0;
    if (!out || cap == 0) return;
    if (!id) id = "";
    for (size_t i = 0; id[i] && j + 1 < cap; i++) {
        char c = id[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        out[j++] = ok ? c : '_';
    }
    out[j] = '\0';
}

static void offline_chapter_dir(const char *bookId, char *out, size_t cap) {
    char safe[128];
    offline_safe_id(bookId, safe, sizeof(safe));
    snprintf(out, cap, "%s/%s", OFFLINE_DIR, safe[0] ? safe : "unknown");
}

static void offline_page_path(const char *bookId, int page, char *out, size_t cap) {
    char dir[256];
    offline_chapter_dir(bookId, dir, sizeof(dir));
    snprintf(out, cap, "%s/%04d.img", dir, page);
}

static int offline_ensure_dirs(const char *bookId) {
    char dir[256];
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Meruem", 0777);
    mkdir(OFFLINE_DIR, 0777);
    offline_chapter_dir(bookId, dir, sizeof(dir));
    mkdir(dir, 0777);
    return 1;
}

static void offline_count_invalidate(const char *bookId) {
    if (!bookId || !bookId[0]) return;
    for (int i = 0; i < OFFLINE_COUNT_CACHE_MAX; i++) {
        if (g_offline_counts[i].id[0] && strcmp(g_offline_counts[i].id, bookId) == 0) {
            g_offline_counts[i].id[0] = '\0';
            g_offline_counts[i].pages = 0;
            g_offline_counts[i].count = 0;
        }
    }
}

static int offline_count_pages_scan(const char *bookId, int pages) {
    int n = 0;
    char path[320];
    if (!bookId || !bookId[0] || pages <= 0) return 0;
    for (int i = 1; i <= pages; i++) {
        offline_page_path(bookId, i, path, sizeof(path));
        if (file_exists(path)) n++;
    }
    return n;
}

static int offline_count_pages(const char *bookId, int pages) {
    int empty = -1;
    if (!bookId || !bookId[0] || pages <= 0) return 0;
    for (int i = 0; i < OFFLINE_COUNT_CACHE_MAX; i++) {
        if (g_offline_counts[i].id[0] && strcmp(g_offline_counts[i].id, bookId) == 0 &&
            g_offline_counts[i].pages == pages) {
            return g_offline_counts[i].count;
        }
        if (empty < 0 && !g_offline_counts[i].id[0]) empty = i;
    }
    if (empty < 0) empty = 0;
    snprintf(g_offline_counts[empty].id, sizeof(g_offline_counts[empty].id), "%s", bookId);
    g_offline_counts[empty].pages = pages;
    g_offline_counts[empty].count = offline_count_pages_scan(bookId, pages);
    return g_offline_counts[empty].count;
}

static SDL_Texture *texture_from_rw(SDL_RWops *rw) {
    SDL_Texture *tex = NULL;
    if (!rw) return NULL;
    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(gRen, surf);
        if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        SDL_FreeSurface(surf);
    }
    return tex;
}

static SDL_Texture *load_page(const char *baseUrl, int page) {
    char local[320];
    if (curBookId[0]) {
        offline_page_path(curBookId, page, local, sizeof(local));
        if (file_exists(local)) {
            SDL_Texture *localTex = texture_from_rw(SDL_RWFromFile(local, "rb"));
            if (localTex) return localTex;
        }
        if (g_offline_mode) return NULL;
    }
    char url[560];
    snprintf(url, sizeof(url), "%s%d", baseUrl, page);
    struct membuf buf = {0};
    long code = net_request_timeout(url, "GET", NULL, g_token, &buf, NULL, 8L, 25L);
    SDL_Texture *tex = NULL;
    if (code == 200 && buf.data && buf.len > 0) {
        tex = texture_from_rw(SDL_RWFromConstMem(buf.data, (int)buf.len));
    }
    membuf_free(&buf);
    return tex;
}

static void cover_cache_clear(void) {
    for (int i = 0; i < COVER_CACHE_MAX; i++) {
        if (g_cover_cache[i].thread) {
            SDL_WaitThread(g_cover_cache[i].thread, NULL);
            g_cover_cache[i].thread = NULL;
        }
        if (g_cover_cache[i].tex) SDL_DestroyTexture(g_cover_cache[i].tex);
        free(g_cover_cache[i].data);
        g_cover_cache[i].tex = NULL;
        g_cover_cache[i].data = NULL;
        g_cover_cache[i].len = 0;
        g_cover_cache[i].ready = 0;
        g_cover_cache[i].loading = 0;
        g_cover_cache[i].failed = 0;
        g_cover_cache[i].id[0] = '\0';
        g_cover_cache[i].url[0] = '\0';
        g_cover_cache[i].token[0] = '\0';
    }
}

static int cover_download_thread(void *arg) {
    CoverCacheEntry *entry = (CoverCacheEntry *)arg;
    struct membuf buf = {0};
    long code = net_request_timeout(entry->url, "GET", NULL, entry->token, &buf, NULL, 6L, 18L);
    if (code == 200 && buf.data && buf.len > 0) {
        entry->data = (unsigned char *)buf.data;
        entry->len = buf.len;
        buf.data = NULL;
        buf.len = 0;
    } else {
        entry->failed = 1;
    }
    membuf_free(&buf);
    entry->loading = 0;
    entry->ready = 1;
    return 0;
}

static void cover_cache_pump(void) {
    for (int i = 0; i < COVER_CACHE_MAX; i++) {
        CoverCacheEntry *e = &g_cover_cache[i];
        if (!e->ready) continue;
        if (e->thread) {
            SDL_WaitThread(e->thread, NULL);
            e->thread = NULL;
        }
        e->ready = 0;
        if (e->data && e->len > 0) {
            SDL_RWops *rw = SDL_RWFromConstMem(e->data, (int)e->len);
            SDL_Surface *surf = IMG_Load_RW(rw, 1);
            if (surf) {
                e->tex = SDL_CreateTextureFromSurface(gRen, surf);
                if (e->tex) SDL_SetTextureScaleMode(e->tex, SDL_ScaleModeLinear);
                SDL_FreeSurface(surf);
            }
            free(e->data);
            e->data = NULL;
            e->len = 0;
            if (!e->tex) e->failed = 1;
        }
    }
}

static SDL_Texture *cover_texture_for_key(const char *id, const char *cover) {
    int empty = -1;
    if (g_offline_mode) return NULL;
    if (!id[0] || !cover[0]) return NULL;
    for (int i = 0; i < COVER_CACHE_MAX; i++) {
        if (g_cover_cache[i].id[0] && strcmp(g_cover_cache[i].id, id) == 0) {
            if (g_cover_cache[i].failed || g_cover_cache[i].loading) return NULL;
            return g_cover_cache[i].tex;
        }
        if (empty < 0 && !g_cover_cache[i].id[0]) empty = i;
    }
    if (empty < 0 || g_cover_started_this_frame) return NULL;
    g_cover_started_this_frame = 1;
    snprintf(g_cover_cache[empty].id, sizeof(g_cover_cache[empty].id), "%s", id);
    if (strncmp(cover, "http://", 7) == 0 || strncmp(cover, "https://", 8) == 0) {
        snprintf(g_cover_cache[empty].url, sizeof(g_cover_cache[empty].url), "%s", cover);
    } else {
        snprintf(g_cover_cache[empty].url, sizeof(g_cover_cache[empty].url), "%s%s", g_server, cover);
    }
    snprintf(g_cover_cache[empty].token, sizeof(g_cover_cache[empty].token), "%s", g_token ? g_token : "");
    g_cover_cache[empty].loading = 1;
    g_cover_cache[empty].thread = SDL_CreateThread(cover_download_thread, "cover", &g_cover_cache[empty]);
    if (!g_cover_cache[empty].thread) {
        g_cover_cache[empty].loading = 0;
        g_cover_cache[empty].failed = 1;
    }
    return NULL;
}

static SDL_Texture *series_cover_texture(cJSON *series) {
    return cover_texture_for_key(json_str(series, "id", ""), json_str(series, "cover", ""));
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

static void draw_modal_box(const char *title, const char *l1, const char *l2,
                           const char *l3, SDL_Color accent, const char *hint) {
    int w = LW() - 64;
    int h = 390;
    int x, y;
    if (w > 640) w = 640;
    x = (LW() - w) / 2;
    y = (LH() - h) / 2 - 24;
    if (y < 86) y = 86;

    begin_frame();
    draw_background();
    SDL_SetRenderDrawColor(gRen, 8, 10, 17, 150);
    SDL_Rect shade = { 0, 0, LW(), LH() };
    SDL_RenderFillRect(gRen, &shade);

    SDL_SetRenderDrawColor(gRen, 10, 13, 22, 210);
    SDL_Rect shadow = { x + 6, y + 8, w, h };
    SDL_RenderFillRect(gRen, &shadow);

    SDL_SetRenderDrawColor(gRen, 20, 27, 42, 246);
    SDL_Rect box = { x, y, w, h };
    SDL_RenderFillRect(gRen, &box);
    SDL_SetRenderDrawColor(gRen, accent.r, accent.g, accent.b, 255);
    SDL_RenderDrawRect(gRen, &box);
    SDL_Rect stripe = { x, y, w, 7 };
    SDL_RenderFillRect(gRen, &stripe);

    text_draw(gRen, title ? title : "Meruem", x + 28, y + 34, COL_HEAD, 1);
    if (l1) text_draw(gRen, l1, x + 28, y + 110, COL_SEL, 0);
    if (l2) text_draw(gRen, l2, x + 28, y + 154, COL_SOFT, 0);
    if (l3) text_draw(gRen, l3, x + 28, y + 204, COL_TEXT, 0);
    if (hint) {
        SDL_SetRenderDrawColor(gRen, 12, 16, 27, 220);
        SDL_Rect hint_box = { x + 20, y + h - 74, w - 40, 46 };
        SDL_RenderFillRect(gRen, &hint_box);
        SDL_SetRenderDrawColor(gRen, 50, 64, 90, 255);
        SDL_RenderDrawRect(gRen, &hint_box);
        text_draw(gRen, hint, hint_box.x + 16, hint_box.y + 11, COL_DIM, 0);
    }
    end_frame();
}

static int modal_wait_loop(const char *title, const char *l1, const char *l2,
                           const char *l3, SDL_Color accent, const char *hint,
                           int allow_cancel) {
    SDL_Event e;
    Uint32 shown_at = SDL_GetTicks();
    while (appletMainLoop()) {
        while (SDL_PollEvent(&e)) {
            int input_ready = (SDL_GetTicks() - shown_at) > 650;
            if (e.type == SDL_QUIT) return 0;
            if (!input_ready) continue;
            if (e.type == SDL_FINGERUP) return 1;
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (allow_cancel && (e.jbutton.button == JOY_PLUS || e.jbutton.button == JOY_B)) return 0;
                if (!allow_cancel && e.jbutton.button == JOY_PLUS) return 0;
                if (e.jbutton.button == JOY_A) return 1;
            }
        }
        draw_modal_box(title, l1, l2, l3, accent, hint);
        SDL_Delay(16);
    }
    return 0;
}

// mensagem; A ou toque = continuar(1), + = sair(0)
static int message_screen(const char *l1, const char *l2) {
    SDL_Color red = { 220, 72, 72, 255 };
    return modal_wait_loop("Meruem", l1, l2, NULL, red, "Toque ou A = continuar    + = sair", 0);
}
static int success_screen(const char *l1, const char *l2) {
    SDL_Color green = { 78, 190, 132, 255 };
    return modal_wait_loop("Meruem", l1, l2, NULL, green, "Toque ou A = continuar", 0);
}
static int info_screen(const char *l1, const char *l2) {
    SDL_Color blue = { 96, 154, 232, 255 };
    return modal_wait_loop("Meruem", l1, l2, NULL, blue, "Toque ou A = continuar", 0);
}
// A/toque = sim; B/+ = nao
static int confirm_screen(const char *l1, const char *l2, const char *l3) {
    SDL_Color gold = { 238, 187, 92, 255 };
    return modal_wait_loop("Atualizacao Meruem", l1, l2, l3, gold, "A/toque = atualizar    B/+ = depois", 1);
}

static int offline_cancel_requested(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 1;
        if (e.type == SDL_JOYBUTTONDOWN && (e.jbutton.button == JOY_B || e.jbutton.button == JOY_PLUS)) return 1;
    }
    return 0;
}

static void offline_progress_screen(int page, int pages, int done, int failed) {
    char l1[160];
    char l2[160];
    char l3[160];
    SDL_Color blue = { 96, 154, 232, 255 };
    snprintf(l1, sizeof(l1), "Baixando capitulo %s", curChapLabel[0] ? curChapLabel : "");
    snprintf(l2, sizeof(l2), "Pagina %d/%d", page, pages);
    snprintf(l3, sizeof(l3), "Salvas: %d    Falhas: %d", done, failed);
    draw_modal_box("Leitura offline", l1, l2, l3, blue, "B/+ = cancelar apos a pagina atual");
}

static void offline_download_current_chapter(void) {
    char line[180];
    int existing;
    int saved = 0;
    int failed = 0;
    int cancelled = 0;
    if (!curBookId[0] || !pageBase[0] || pageCount < 1) {
        message_screen("Nao consegui identificar o capitulo.", "Abra o capitulo novamente e tente baixar.");
        return;
    }
    existing = offline_count_pages(curBookId, pageCount);
    snprintf(line, sizeof(line), "%d/%d paginas ja estao no SD.", existing, pageCount);
    {
        SDL_Color gold = { 238, 187, 92, 255 };
        if (!modal_wait_loop("Leitura offline", "Baixar este capitulo para o SD?", line,
                             "Depois ele abre mesmo sem internet.", gold,
                             "A/toque = baixar    B/+ = depois", 1)) {
            reader_show_overlay();
            return;
        }
    }
    offline_ensure_dirs(curBookId);
    for (int p = 1; p <= pageCount; p++) {
        char path[320];
        char url[560];
        offline_page_path(curBookId, p, path, sizeof(path));
        if (file_exists(path)) {
            saved++;
            continue;
        }
        if (offline_cancel_requested()) { cancelled = 1; break; }
        offline_progress_screen(p, pageCount, saved, failed);
        snprintf(url, sizeof(url), "%s%d", pageBase, p);
        long code = net_download_file_timeout(url, g_token, path, NULL, 8L, 45L);
        if (code == 200) saved++;
        else {
            remove(path);
            failed++;
        }
    }

    offline_count_invalidate(curBookId);
    {
        SDL_Texture *newTex = load_page(pageBase, curPage);
        if (newTex) {
            if (pageTex) SDL_DestroyTexture(pageTex);
            pageTex = newTex;
        }
    }
    reader_show_overlay();
    if (cancelled) {
        snprintf(line, sizeof(line), "Salvas: %d/%d. Voce pode continuar depois.", offline_count_pages(curBookId, pageCount), pageCount);
        info_screen("Download offline cancelado.", line);
    } else if (failed > 0) {
        snprintf(line, sizeof(line), "Salvas: %d/%d. Tente de novo para completar.", offline_count_pages(curBookId, pageCount), pageCount);
        message_screen("Algumas paginas falharam.", line);
    } else {
        snprintf(line, sizeof(line), "%d paginas salvas no SD.", offline_count_pages(curBookId, pageCount));
        success_screen("Capitulo pronto para ler offline.", line);
    }
    reader_show_overlay();
}

// Tela de boas-vindas / login. Retorna: 0 = sair, 1 = entrar, 2 = trocar conta.
static int login_welcome_screen(int hasUser, const char *user) {
    SDL_Event e;
    Uint32 shown = SDL_GetTicks();
    while (appletMainLoop()) {
        int bw = LW() - 56;
        int bx = 28, by = 120;
        int bh = LH() - 240;
        if (bh > 560) bh = 560;
        Btn enter = { bx + 28, by + bh - 150, bw - 56, 58, hasUser ? "Entrar" : "Ja tenho conta - Entrar" };
        Btn swap  = { bx + 28, by + bh - 82,  bw - 56, 48, "Trocar conta" };

        while (SDL_PollEvent(&e)) {
            int ready = (SDL_GetTicks() - shown) > 400;
            if (e.type == SDL_QUIT) return 0;
            if (!ready) continue;
            if (e.type == SDL_FINGERUP) {
                int lx, ly;
                screen_to_logical(e.tfinger.x, e.tfinger.y, &lx, &ly);
                if (btn_hit(enter, lx, ly)) return 1;
                if (hasUser && btn_hit(swap, lx, ly)) return 2;
            }
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (e.jbutton.button == JOY_A) return 1;
                if (e.jbutton.button == JOY_PLUS || e.jbutton.button == JOY_B) return 0;
                if (hasUser && e.jbutton.button == JOY_Y) return 2;
            }
        }

        begin_frame();
        draw_background();
        SDL_SetRenderDrawColor(gRen, 22, 30, 46, 240);
        SDL_Rect box = { bx, by, bw, bh };
        SDL_RenderFillRect(gRen, &box);
        SDL_SetRenderDrawColor(gRen, 76, 96, 130, 255);
        SDL_RenderDrawRect(gRen, &box);
        SDL_SetRenderDrawColor(gRen, 250, 215, 120, 255);
        SDL_Rect stripe = { bx, by, bw, 7 };
        SDL_RenderFillRect(gRen, &stripe);

        text_draw(gRen, "Meruem", bx + 28, by + 30, COL_HEAD, 1);
        if (hasUser) {
            char line[160];
            text_draw(gRen, "Bem-vindo de volta!", bx + 28, by + 96, COL_TEXT, 0);
            snprintf(line, sizeof(line), "Conta: %s", user);
            text_draw(gRen, line, bx + 28, by + 140, COL_SEL, 0);
            text_draw(gRen, "Toque em Entrar e digite sua senha.", bx + 28, by + 186, COL_SOFT, 0);
        } else {
            text_draw(gRen, "Bem-vindo! Para ler no Switch:", bx + 28, by + 96, COL_TEXT, 0);
            text_draw(gRen, "1) No celular/PC, crie sua conta em:", bx + 28, by + 142, COL_SOFT, 0);
            text_draw(gRen, g_server[0] ? g_server : DEFAULT_SERVER, bx + 28, by + 178, COL_SEL, 0);
            text_draw(gRen, "2) Volte aqui e toque em Entrar.", bx + 28, by + 224, COL_SOFT, 0);
        }
        btn_draw(enter);
        if (hasUser) btn_draw(swap);
        text_draw(gRen, hasUser ? "A: entrar    Y: trocar conta    B/+: sair"
                                : "A/toque: entrar    B/+: sair",
                  bx + 28, by + bh - 26, COL_DIM, 0);
        end_frame();
        SDL_Delay(16);
    }
    return 0;
}

static int authenticate(void) {
    char tok[512];
    if (store_load_token(tok, sizeof(tok))) {
        present_color(20, 20, 40);
        if (token_is_valid(tok)) {
            g_offline_mode = 0;
            g_token = strdup(tok);
            return 1;
        }
        g_offline_mode = 1;
        g_token = strdup(tok);
        info_screen("Servidor indisponivel.", "Abrindo leituras salvas no modo offline.");
        return 1;
    }
    int hasUser = store_load_user(g_username, sizeof(g_username));
    while (appletMainLoop()) {
        int act = login_welcome_screen(hasUser, g_username);
        if (act == 0) return 0;
        if (act == 2) { store_clear_user(); g_username[0] = '\0'; hasUser = 0; continue; }

        char user[96] = {0}, pass[96] = {0};
        if (hasUser) {
            snprintf(user, sizeof(user), "%s", g_username);
        } else {
            int ru = prompt_text("Usuario ou e-mail da conta Meruem", user, sizeof(user), 0);
            if (ru != 0) continue;   // cancelou/vazio -> volta ao welcome
        }
        int rp = prompt_text("Senha da conta Meruem", pass, sizeof(pass), 1);
        if (rp != 0) continue;

        present_color(20, 20, 40);
        g_token = login_request(user, pass);
        if (g_token) {
            store_save_token(g_token);
            snprintf(g_username, sizeof(g_username), "%s", user);
            store_save_user(g_username);
            return 1;
        }
        message_screen("Login falhou. Verifique usuario/senha.",
                       hasUser ? "Use 'Trocar conta' se o usuario estiver errado." : "Se ainda nao tem conta, crie no site primeiro.");
        hasUser = store_load_user(g_username, sizeof(g_username));
    }
    return 0;
}

static void write_update_log(int rc, const struct update_info *info) {
    FILE *f = fopen("sdmc:/switch/Meruem/update.log", "wb");
    if (!f) return;
    fprintf(f, "current=%s\n", APP_VERSION_STR);
    fprintf(f, "target=%s\n", g_self_path);
    fprintf(f, "result=%d\n", rc);
    if (info) {
        fprintf(f, "http=%ld\n", info->http_code);
        fprintf(f, "latest=%s\n", info->latest_version);
        fprintf(f, "asset=%s\n", info->asset_name);
        fprintf(f, "message=%s\n", info->message);
    }
    fclose(f);
}

static int maybe_install_update(void) {
    struct update_info info;
    char line1[200];
    char line2[600];
    char err[256];
    char seen[40] = {0};
    int rc;

    present_color(20, 20, 40);
    {
        SDL_Color blue = { 96, 154, 232, 255 };
        draw_modal_box("Meruem", "Verificando atualizacoes...",
                       "Versao atual: " APP_VERSION_STR, NULL, blue, NULL);
    }
    SDL_Delay(450);
    rc = update_check(&info);
    write_update_log(rc, &info);
    // Atualizado, erro de rede ou sem repo configurado: segue sem incomodar.
    if (rc != UPDATE_CHECK_AVAILABLE) return 0;

    // Ja perguntamos (instalou ou adiou) sobre ESTA versao? Nao perturba de novo.
    store_load_update_seen(seen, sizeof(seen));
    if (seen[0] && strcmp(seen, info.latest_version) == 0) return 0;

    snprintf(line1, sizeof(line1), "Nova versao: %s   (atual %s)", info.latest_version, APP_VERSION_STR);
    snprintf(line2, sizeof(line2), "Instala em: %s", g_self_path[0] ? g_self_path : "(padrao)");
    if (!confirm_screen(line1, line2, "Baixar e trocar o .nro agora?")) {
        store_save_update_seen(info.latest_version);   // "depois": nao pergunta de novo nesta versao
        return 0;
    }

    present_color(20, 20, 40);
    if (update_apply(&info, g_self_path, err, sizeof(err)) != 0) {
        message_screen("Falha ao instalar.", err[0] ? err : info.message);
        return 0;   // nao marca: permite tentar de novo no proximo boot
    }
    store_save_update_seen(info.latest_version);
    success_screen("Atualizacao instalada!", "Feche e abra o Meruem novamente.");
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
    cover_cache_clear();   // libera as capas da pagina anterior
    catalogFavorites = 0;
    catalogLoadFailed = 0;
    if (g_cat) { cJSON_Delete(g_cat); g_cat = NULL; }
    if (g_offline_mode) {
        catCount = 0;
        catTotal = 1;
        catSel = 0;
        catScroll = 0;
        catalogLoadFailed = 1;
        return;
    }
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

static void load_favorites(void) {
    present_color(20, 20, 40);
    cover_cache_clear();
    catalogFavorites = 1;
    catalogLoadFailed = 0;
    if (g_cat) { cJSON_Delete(g_cat); g_cat = NULL; }
    if (g_offline_mode) {
        catCount = 0;
        catTotal = 1;
        catSel = 0;
        catScroll = 0;
        catalogLoadFailed = 1;
        return;
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/switch/favorites?size=%d&page=%d", g_server, PAGE_SIZE, catPage);
    struct membuf r = {0};
    long code = net_request(url, "GET", NULL, g_token, &r, NULL);
    if (code == 200 && r.data) g_cat = cJSON_Parse(r.data);
    else catalogLoadFailed = 1;
    membuf_free(&r);
    catCount = 0; catTotal = 1; catScroll = 0;
    if (g_cat) {
        catTotal = json_int(g_cat, "totalPages", 1);
        catCount = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_cat, "series"));
    }
    if (catSel >= catCount) catSel = catCount > 0 ? catCount - 1 : 0;
    if (catSel < 0) catSel = 0;
}

static void load_profile(void) {
    profileLoaded = 0;
    profileFailed = 0;
    profileTier[0] = '\0';
    profileAccess[0] = '\0';
    profileChapters[0] = '\0';
    profileBooks[0] = '\0';
    profileCounts[0] = '\0';

    if (g_offline_mode) {
        profileFailed = 1;
        return;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s/switch/me", g_server);
    struct membuf r = {0};
    long code = net_request_timeout(url, "GET", NULL, g_token, &r, NULL, 6L, 14L);
    if (code != 200 || !r.data) {
        profileFailed = 1;
        membuf_free(&r);
        return;
    }

    cJSON *root = cJSON_Parse(r.data);
    membuf_free(&r);
    if (!root) {
        profileFailed = 1;
        return;
    }

    cJSON *user = cJSON_GetObjectItemCaseSensitive(root, "user");
    cJSON *limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
    cJSON *chapters = cJSON_GetObjectItemCaseSensitive(limits, "chapters");
    cJSON *counts = cJSON_GetObjectItemCaseSensitive(root, "counts");
    const char *tier = json_str(user, "tier", "free");
    snprintf(profileTier, sizeof(profileTier), "%s", tier);

    int hasFullAccess = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(limits, "hasFullAccess"));
    snprintf(profileAccess, sizeof(profileAccess), "%s",
             hasFullAccess ? "Acesso liberado: assinatura/admin sem limite diario." : "Plano Free: leitura com limite diario.");

    if (hasFullAccess) {
        snprintf(profileChapters, sizeof(profileChapters), "Capitulos: ilimitado");
        snprintf(profileBooks, sizeof(profileBooks), "Livros: ocultos nesta versao do Switch.");
    } else {
        int used = json_int(chapters, "usedToday", 0);
        int remaining = json_int(chapters, "remainingToday", 0);
        int limit = json_int(chapters, "limit", 3);
        snprintf(profileChapters, sizeof(profileChapters), "Capitulos hoje: %d/%d usados, %d restantes", used, limit, remaining);
        snprintf(profileBooks, sizeof(profileBooks), "Livros: ocultos nesta versao do Switch.");
    }

    snprintf(profileCounts, sizeof(profileCounts), "Favoritos %d  Prateleira %d  Continuar %d",
             json_int(counts, "favorites", 0),
             json_int(counts, "libraryPins", 0) + json_int(counts, "shelves", 0),
             json_int(counts, "continueReading", 0));

    profileLoaded = 1;
    cJSON_Delete(root);
}

static void clamp_catalog_scroll_to_selection(void) {
    if (catViewMode == 0) {
        int gc = grid_cols();
        int selRow = gc > 0 ? catSel / gc : 0;
        int visR = grid_visible_rows();
        int rows = (catCount + gc - 1) / gc;
        int maxRow = rows - visR;
        if (maxRow < 0) maxRow = 0;
        if (selRow < catScroll) catScroll = selRow;
        if (selRow >= catScroll + visR) catScroll = selRow - visR + 1;
        if (catScroll > maxRow) catScroll = maxRow;
    } else {
        int vis = visible_rows();
        int maxs = catCount - vis;
        if (maxs < 0) maxs = 0;
        if (catSel < catScroll) catScroll = catSel;
        if (catSel >= catScroll + vis) catScroll = catSel - vis + 1;
        if (catScroll > maxs) catScroll = maxs;
    }
    if (catScroll < 0) catScroll = 0;
}

static void toggle_catalog_view(void) {
    catViewMode = !catViewMode;
    clamp_catalog_scroll_to_selection();
}

static void enter_series(int idx) {
    cJSON *s = cat_series_at(idx);
    if (!s) return;
    const char *id = json_str(s, "id", "");
    if (!id[0]) return;
    snprintf(curSeriesId, sizeof(curSeriesId), "%s", id);
    snprintf(curSeriesTitle, sizeof(curSeriesTitle), "%s", json_str(s, "title", "Serie"));
    const char *cover = json_str(s, "cover", "");
    if (cover[0] && (strncmp(cover, "http://", 7) == 0 || strncmp(cover, "https://", 8) == 0)) {
        snprintf(curSeriesCover, sizeof(curSeriesCover), "%s", cover);
    } else if (cover[0]) {
        snprintf(curSeriesCover, sizeof(curSeriesCover), "%s%s", g_server, cover);
    } else {
        curSeriesCover[0] = '\0';
    }
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
    g_chapters_back = screen;
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
    // O indice real no array (pra next-chapter funcionar certo):
    curChapIndex = (chapReversed && chapCount > 0) ? (chapCount - 1 - idx) : idx;
    curPage = store_get_progress(curBookId);
    if (curPage > pageCount) curPage = pageCount;
    if (curPage < 1) curPage = 1;
    {
        SDL_Texture *newTex = load_page(pageBase, curPage);
        if (pageTex) SDL_DestroyTexture(pageTex);
        pageTex = newTex;
    }
    reader_reset_view();
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    reader_start_next_prompt();
    g_reader_back = SC_CHAPTERS;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    screen = SC_READER;
}

// Retoma a leitura a partir de um registro salvo (tela Continuar).
static void enter_reader_from_record(const char *bookId) {
    char sid[96] = {0}, st[256] = {0}, cl[64] = {0}, pb[512] = {0}, cv[640] = {0};
    int page = 1, pages = 1;
    if (!store_entry(bookId, sid, sizeof(sid), st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), cv, sizeof(cv), &page, &pages)) return;
    if (!pb[0]) return;
    snprintf(curBookId, sizeof(curBookId), "%s", bookId);
    snprintf(curSeriesId, sizeof(curSeriesId), "%s", sid);
    snprintf(curSeriesTitle, sizeof(curSeriesTitle), "%s", st);
    snprintf(curSeriesCover, sizeof(curSeriesCover), "%s", cv);
    snprintf(curChapLabel, sizeof(curChapLabel), "%s", cl);
    snprintf(pageBase, sizeof(pageBase), "%s", pb);
    pageCount = pages < 1 ? 1 : pages;
    curPage = page; if (curPage > pageCount) curPage = pageCount; if (curPage < 1) curPage = 1;
    curChapIndex = -1;
    if (sid[0] && !g_offline_mode) {
        if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
        char url[512];
        snprintf(url, sizeof(url), "%s/switch/series/%s", g_server, sid);
        struct membuf r = {0};
        long code = net_request(url, "GET", NULL, g_token, &r, NULL);
        if (code == 200 && r.data) g_ser = cJSON_Parse(r.data);
        membuf_free(&r);
        chapCount = 0; chapSel = 0; chapScroll = 0;
        if (g_ser) {
            chapCount = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"));
            for (int i = 0; i < chapCount; i++) {
                cJSON *c = chap_at_raw(i);
                if (c && strcmp(json_str(c, "id", ""), bookId) == 0) {
                    curChapIndex = i;
                    chapSel = i;
                    break;
                }
            }
        }
    }
    {
        SDL_Texture *newTex = load_page(pageBase, curPage);
        if (pageTex) SDL_DestroyTexture(pageTex);
        pageTex = newTex;
    }
    reader_reset_view();
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    reader_start_next_prompt();
    g_reader_back = SC_CONTINUE;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    screen = SC_READER;
}

static void load_continue(void) {
    contN = store_recent(contIds, 60);
    if (contSel >= contN) contSel = contN > 0 ? contN - 1 : 0;
    if (contSel < 0) contSel = 0;
    contScroll = 0;
}
static void reader_show_overlay(void) {
    reader_overlay_until = SDL_GetTicks() + READER_OVERLAY_MS;
}

static void reader_reset_touch(void) {
    memset(readerTouches, 0, sizeof(readerTouches));
    readerSwipeMoved = 0;
    readerPinching = 0;
    readerPinchLive = 0;
    readerPinchBaseDist = 0.0f;
    readerPinchBaseZoom = rd_zoom;
}

static void reader_clamp_pan(void) {
    int tw = 0, th = 0;
    if (!pageTex) { rd_pan_x = rd_pan_y = 0.0f; return; }
    SDL_QueryTexture(pageTex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0 || rd_zoom <= READER_ZOOM_MIN + 0.01f) {
        rd_pan_x = rd_pan_y = 0.0f;
        return;
    }
    float fit = (float)LW() / tw;
    float fy = (float)LH() / th;
    if (fy < fit) fit = fy;
    float w = tw * fit * rd_zoom;
    float h = th * fit * rd_zoom;
    float maxX = w > LW() ? (w - LW()) * 0.5f : 0.0f;
    float maxY = h > LH() ? (h - LH()) * 0.5f : 0.0f;
    if (rd_pan_x < -maxX) rd_pan_x = -maxX;
    if (rd_pan_x >  maxX) rd_pan_x =  maxX;
    if (rd_pan_y < -maxY) rd_pan_y = -maxY;
    if (rd_pan_y >  maxY) rd_pan_y =  maxY;
}

static void reader_reset_view(void) {
    rd_zoom = READER_ZOOM_MIN;
    rd_pan_x = rd_pan_y = 0.0f;
    reader_reset_touch();
    reader_show_overlay();
}

static void reader_page_rect(SDL_Rect *dst) {
    int tw = 0, th = 0;
    dst->x = dst->y = 0; dst->w = LW(); dst->h = LH();
    if (!pageTex) return;
    SDL_QueryTexture(pageTex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float scale = (float)LW() / tw;
    float sh = (float)LH() / th;
    if (sh < scale) scale = sh;
    int w = (int)(tw * scale * rd_zoom);
    int h = (int)(th * scale * rd_zoom);
    dst->w = w; dst->h = h;
    dst->x = (LW() - w) / 2 + (int)rd_pan_x;
    dst->y = (LH() - h) / 2 + (int)rd_pan_y;
}

static int reader_has_next_chapter(void) {
    // curChapIndex e o indice REAL (nao invertido) no array JSON
    return g_ser && curChapIndex >= 0 && curChapIndex + 1 < chapCount;
}
// Abre o proximo capitulo usando indice REAL no array (nao invertido)
static cJSON *chap_at_real(int i) { return chap_at_raw(i); }

static void reader_start_next_prompt(void) {
    if (curPage == pageCount && reader_has_next_chapter() && !next_prompt_cancelled && next_prompt_started == 0) {
        next_prompt_started = SDL_GetTicks();
    }
}

static void reader_open_page(int n, int atBottom) {
    (void)atBottom;
    if (n < 1) n = 1;
    if (n > pageCount) n = pageCount;
    if (pageTex && n == curPage) { reader_start_next_prompt(); return; }
    curPage = n;
    // Nao escurece: mantem a imagem anterior visivel ate a nova chegar (evita flash).
    SDL_Texture *newTex = load_page(pageBase, curPage);
    if (pageTex) SDL_DestroyTexture(pageTex);
    pageTex = newTex;
    reader_reset_view();
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    reader_start_next_prompt();
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
}
// dir > 0 = avancar; dir < 0 = voltar.
static void reader_advance(int dir) {
    reader_open_page(curPage + (dir > 0 ? 1 : -1), 0);
}

static void reader_open_next_chapter_now(void) {
    if (!reader_has_next_chapter()) return;
    int nextReal = curChapIndex + 1;
    cJSON *c = chap_at_real(nextReal);
    if (!c) return;
    const char *pb = json_str(c, "pageBase", "");
    if (!pb[0]) return;
    snprintf(pageBase, sizeof(pageBase), "%s%s", g_server, pb);
    pageCount = json_int(c, "pages", 1);
    if (pageCount < 1) pageCount = 1;
    snprintf(curChapLabel, sizeof(curChapLabel), "#%s", json_str(c, "number", "?"));
    snprintf(curBookId, sizeof(curBookId), "%s", json_str(c, "id", ""));
    curChapIndex = nextReal;
    curPage = 1;
    SDL_Texture *newTex = load_page(pageBase, curPage);
    if (pageTex) SDL_DestroyTexture(pageTex);
    pageTex = newTex;
    reader_reset_view();
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    reader_start_next_prompt();
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
}

static void reader_tick(void) {
    if (screen != SC_READER || !next_prompt_started || next_prompt_cancelled) return;
    if (SDL_GetTicks() - next_prompt_started >= NEXT_CHAPTER_MS) reader_open_next_chapter_now();
}

// ---------------- render ----------------
static void draw_topbar_reserved(const char *title, Btn left, int rightX) {
    SDL_SetRenderDrawColor(gRen, 16, 20, 31, 245);
    SDL_Rect bar = { 0, 0, LW(), TB };
    SDL_RenderFillRect(gRen, &bar);
    SDL_SetRenderDrawColor(gRen, 54, 70, 96, 255);
    SDL_RenderDrawLine(gRen, 0, TB - 1, LW(), TB - 1);
    btn_draw(left);
    btn_draw(btn_rotate());
    if (title) {
        int maxW = rightX - (left.x + left.w + 24);
        if (maxW < 60) maxW = 60;
        char trunc[200];
        snprintf(trunc, sizeof(trunc), "%s", title);
        // Trunca antes de invadir os botoes da direita.
        int est = (int)strlen(trunc) * 10;
        if (est > maxW) {
            int max = (maxW / 10) - 3;
            if (max < 1) max = 1;
            if (max < (int)sizeof(trunc) - 4) {
                trunc[max] = '.';
                trunc[max + 1] = '.';
                trunc[max + 2] = '.';
                trunc[max + 3] = '\0';
            }
        }
        text_draw(gRen, trunc, left.x + left.w + 16, 12, COL_HEAD, 0);
    }
}

static void draw_topbar(const char *title, Btn left) {
    draw_topbar_reserved(title, left, btn_rotate().x);
}

static void render_series(void) {
    draw_background();
    g_cover_started_this_frame = 0;
    cover_cache_pump();
    char hd[80];
    const char *catTitle = catalogFavorites ? "Favoritos" : AREAS[areaIdx];
    int shownStart = catCount > 0 ? catSel + 1 : 0;
    int shownEnd = shownStart;
    snprintf(hd, sizeof(hd), "%s", catTitle);
    SDL_SetRenderDrawColor(gRen, 16, 20, 31, 245);
    SDL_Rect tbar = { 0, 0, LW(), TB };
    SDL_RenderFillRect(gRen, &tbar);
    SDL_SetRenderDrawColor(gRen, 54, 70, 96, 255);
    SDL_RenderDrawLine(gRen, 0, TB - 1, LW(), TB - 1);
    btn_draw(btn_exit());
    btn_draw(btn_continue());
    btn_draw(catalogFavorites ? btn_catalog_home() : btn_favorites());
    btn_draw(btn_settings());
    btn_draw(btn_rotate());

    if (catCount == 0) {
        if (catalogLoadFailed) draw_empty_state("Favoritos ainda nao conectados", "Atualize o Meruem web para liberar /switch/favorites.");
        else if (catalogFavorites) draw_empty_state("Nenhum favorito ainda", "Favorite obras no site Meruem. Depois elas aparecem aqui no Switch.");
        else draw_empty_state("Nada encontrado", "Tente outra busca ou troque a area.");
    } else if (catViewMode == 0) {
        int cols = grid_cols(), gap = grid_gap();
        int cardW = grid_card_w(), coverH = grid_cover_h(), cellH = grid_cell_h();
        int visR = grid_visible_rows();
        int rows = (catCount + cols - 1) / cols;
        for (int r = 0; r < visR; r++) {
            for (int c = 0; c < cols; c++) {
                int idx = (catScroll + r) * cols + c;
                if (idx >= catCount) continue;
                int x = gap + c * (cardW + gap);
                int y = LIST_Y + 4 + r * cellH;
                cJSON *s = cat_series_at(idx);
                const char *title = json_str(s, "title", "(sem titulo)");
                if (idx == catSel) draw_selected_card_focus(x, y, cardW, coverH);
                SDL_Texture *cover = series_cover_texture(s);
                if (cover) draw_cover_texture(cover, x, y, cardW, coverH);
                else       draw_cover_placeholder(x, y, cardW, coverH, title);
                char t[40];
                snprintf(t, sizeof(t), "%.16s", title);
                text_draw(gRen, t, x, y + coverH + 5, idx == catSel ? COL_SEL : COL_SOFT, 0);
            }
        }
        shownStart = catScroll * cols + 1;
        shownEnd = (catScroll + visR) * cols;
        if (shownEnd > catCount) shownEnd = catCount;
        draw_scrollbar_units(catScroll, rows, visR, cellH);
        draw_more_hint(catScroll, rows, visR);
    } else {
        int vis = visible_rows();
        for (int i = 0; i < vis; i++) {
            int idx = catScroll + i;
            if (idx >= catCount) break;
            int y = LIST_Y + i * ROW_H;
            cJSON *s = cat_series_at(idx);
            const char *title = json_str(s, "title", "(sem titulo)");
            draw_row_shell(y, idx == catSel);
            char row[320];
            snprintf(row, sizeof(row), "%.58s", title);
            text_draw(gRen, row, 24, y + 22, idx == catSel ? COL_SEL : COL_TEXT, 0);
        }
        shownStart = catScroll + 1;
        shownEnd = catScroll + vis;
        if (shownEnd > catCount) shownEnd = catCount;
        draw_scrollbar(catScroll, catCount, vis);
        draw_more_hint(catScroll, catCount, vis);
    }
    draw_footer(catalogFavorites ? "B/Biblioteca: voltar    Favoritos vem do site Meruem    X: alternar visual" : NULL);
    btn_draw(btn_prev()); btn_draw(btn_area()); btn_draw(btn_search()); btn_draw(btn_view_mode()); btn_draw(btn_next());
}

static void render_settings(void) {
    draw_background();
    draw_topbar("Conta e ajustes", btn_library());
    SDL_SetRenderDrawColor(gRen, 22, 30, 46, 232);
    SDL_Rect box = { 28, LIST_Y + 12, LW() - 56, 430 };
    SDL_RenderFillRect(gRen, &box);
    SDL_SetRenderDrawColor(gRen, 92, 152, 76, 180);
    SDL_RenderDrawRect(gRen, &box);

    char line[300];
    text_draw(gRen, "Perfil Meruem", box.x + 24, box.y + 24, COL_HEAD, 1);
    snprintf(line, sizeof(line), "Conta logada: %s", g_username[0] ? g_username : "(nao identificada)");
    text_draw(gRen, line, box.x + 24, box.y + 78, COL_SEL, 0);
    snprintf(line, sizeof(line), "Servidor: %.52s", g_server[0] ? g_server : DEFAULT_SERVER);
    text_draw(gRen, line, box.x + 24, box.y + 118, COL_SOFT, 0);
    snprintf(line, sizeof(line), "Versao do app: %s", APP_VERSION_STR);
    text_draw(gRen, line, box.x + 24, box.y + 154, COL_DIM, 0);
    text_draw(gRen, "Assinatura e limite diario:", box.x + 24, box.y + 196, COL_HEAD, 0);
    if (profileLoaded) {
        snprintf(line, sizeof(line), "Plano: %s", profileTier[0] ? profileTier : "free");
        text_draw(gRen, line, box.x + 24, box.y + 236, COL_SEL, 0);
        text_draw(gRen, profileAccess, box.x + 24, box.y + 272, COL_SOFT, 0);
        text_draw(gRen, profileChapters, box.x + 24, box.y + 308, COL_DIM, 0);
        text_draw(gRen, profileBooks, box.x + 24, box.y + 344, COL_DIM, 0);
        text_draw(gRen, profileCounts, box.x + 24, box.y + 380, COL_DIM, 0);
    } else if (profileFailed) {
        text_draw(gRen, "Nao foi possivel sincronizar /switch/me agora.", box.x + 24, box.y + 236, COL_DIM, 0);
        text_draw(gRen, "Atualize o Meruem web no servidor e tente abrir esta tela novamente.", box.x + 24, box.y + 276, COL_DIM, 0);
    } else {
        text_draw(gRen, "Sincronizando conta com o Meruem web...", box.x + 24, box.y + 236, COL_DIM, 0);
    }
    btn_draw(btn_switch_account());
    draw_footer("A/toque: acao    B/Biblioteca: voltar    ZL/ZR: girar");
}

static void render_chapters(void) {
    draw_background();
    char hd[200];
    snprintf(hd, sizeof(hd), "%.20s (%d)", curSeriesTitle, chapCount);
    Btn orderBtn = btn_chap_order();
    draw_topbar_reserved(hd, btn_back(), orderBtn.x);
    btn_draw(orderBtn);

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
        const char *bid = json_str(c, "id", "");
        int prog = store_get_progress(bid);
        int pages = json_int(c, "pages", 0);
        int off = offline_count_pages(bid, pages);
        char row[320];
        char meta[160];
        snprintf(row, sizeof(row), "#%s  %.40s", num, tt[0] ? tt : "Capitulo");
        if (off > 0) snprintf(meta, sizeof(meta), "%d paginas  offline %d/%d%s", pages, off, pages, prog > 1 ? "  em andamento" : "");
        else         snprintf(meta, sizeof(meta), "%d paginas%s", pages, prog > 1 ? "  em andamento" : "");
        text_draw(gRen, row, 24, y + 8, idx == chapSel ? COL_SEL : COL_TEXT, 0);
        text_draw(gRen, meta, 24, y + 34, COL_SOFT, 0);
    }
    draw_scrollbar(chapScroll, chapCount, vis);
    draw_footer("A/toque: ler    B: voltar    L/R: pular lista    ZL/ZR: girar");
    btn_draw(btn_up()); btn_draw(btn_down());
}

static void render_continue(void) {
    draw_background();
    g_cover_started_this_frame = 0;
    cover_cache_pump();
    draw_topbar(g_offline_mode ? "Continuar lendo (offline)" : "Continuar lendo", btn_library());
    int vis = visible_rows();
    if (contN == 0) {
        draw_empty_state(g_offline_mode ? "Nada salvo offline" : "Nada por aqui ainda",
                         g_offline_mode ? "Baixe capitulos antes de sair da internet." : "Abra a Biblioteca e escolha um capitulo.");
    }
    for (int i = 0; i < vis; i++) {
        int idx = contScroll + i;
        if (idx >= contN) break;
        int y = LIST_Y + i * ROW_H;
        draw_row_shell(y, idx == contSel);
        char st[256] = {0}, cl[64] = {0}, sid[96] = {0}, pb[512] = {0}, cv[640] = {0};
        int page = 1, pages = 1;
        store_entry(contIds[idx], sid, sizeof(sid), st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), cv, sizeof(cv), &page, &pages);
        char row[360];
        char meta[160];
        int pct = pages > 0 ? (page * 100) / pages : 0;
        int off = offline_count_pages(contIds[idx], pages);
        if (pct > 100) pct = 100;
        snprintf(row, sizeof(row), "%.34s", st[0] ? st : "(serie)");
        if (off > 0) snprintf(meta, sizeof(meta), "%s  pagina %d/%d  offline %d/%d", cl, page, pages, off, pages);
        else         snprintf(meta, sizeof(meta), "%s  pagina %d/%d  %d%%", cl, page, pages, pct);
        SDL_Texture *cover = cover_texture_for_key(contIds[idx], cv);
        if (cover) draw_cover_texture(cover, 22, y + 7, 42, 60);
        else       draw_cover_placeholder(22, y + 7, 42, 60, st[0] ? st : "?");
        text_draw(gRen, row, 78, y + 9, idx == contSel ? COL_SEL : COL_TEXT, 0);
        text_draw(gRen, meta, 78, y + 38, COL_SOFT, 0);
    }
    draw_scrollbar(contScroll, contN, vis);
    draw_footer("A/toque: continuar    B/Biblioteca: acervo    ZL/ZR: girar");
}

static void render_reader(void) {
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderClear(gRen);
    if (pageTex) {
        SDL_Rect dst;
        reader_page_rect(&dst);
        SDL_RenderCopy(gRen, pageTex, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(gRen, 20, 24, 36, 255);
        SDL_Rect r = { LW()/2 - 250, LH()/2 - 70, 500, 140 };
        SDL_RenderFillRect(gRen, &r);
        SDL_SetRenderDrawColor(gRen, 238, 187, 92, 220);
        SDL_RenderDrawRect(gRen, &r);
        text_draw(gRen, g_offline_mode ? "Pagina nao salva offline" : "Nao consegui carregar a pagina",
                  r.x + 28, r.y + 34, COL_HEAD, 1);
        text_draw(gRen, g_offline_mode ? "Baixe o capitulo antes de sair da internet." : "Tente avancar/voltar ou recarregar depois.",
                  r.x + 28, r.y + 86, COL_SOFT, 0);
    }
    int overlay = SDL_GetTicks() < reader_overlay_until;
    if (overlay) {
        SDL_SetRenderDrawColor(gRen, 0, 0, 0, 175);
        SDL_Rect bar = { 0, 0, LW(), TB };
        SDL_RenderFillRect(gRen, &bar);
        btn_draw(btn_back());
        btn_draw(btn_offline());
        btn_draw(btn_rotate());
        char pc[160];
        int maxPcW = btn_offline().x - (btn_back().x + btn_back().w + 24);
        snprintf(pc, sizeof(pc), "%s  %d/%d", curChapLabel, curPage, pageCount);
        text_draw(gRen, pc, btn_back().x + btn_back().w + 14, 12, COL_SEL, 0);
        (void)maxPcW;
        {
            int off = offline_count_pages(curBookId, pageCount);
            char st[80];
            snprintf(st, sizeof(st), "X: offline  %d/%d no SD", off, pageCount);
            text_draw(gRen, st, 18, LH() - 48, off == pageCount ? COL_HEAD : COL_DIM, 0);
        }
    }
    if (next_prompt_started && !next_prompt_cancelled && reader_has_next_chapter()) {
        Uint32 elapsed = SDL_GetTicks() - next_prompt_started;
        int left = (int)((NEXT_CHAPTER_MS - (elapsed < NEXT_CHAPTER_MS ? elapsed : NEXT_CHAPTER_MS) + 999) / 1000);
        SDL_SetRenderDrawColor(gRen, 0, 0, 0, 205);
        SDL_Rect box = { LW()/2 - 280, LH()/2 - 72, 560, 172 };
        SDL_RenderFillRect(gRen, &box);
        SDL_SetRenderDrawColor(gRen, 250, 215, 120, 230);
        SDL_RenderDrawRect(gRen, &box);
        char msg[160];
        snprintf(msg, sizeof(msg), "Proximo capitulo em %ds", left);
        text_draw(gRen, msg, box.x + 32, box.y + 24, COL_HEAD, 1);
        cJSON *next = chap_at_real(curChapIndex + 1);
        snprintf(msg, sizeof(msg), "Capitulo #%s", json_str(next, "number", "?"));
        text_draw(gRen, msg, box.x + 32, box.y + 62, COL_SEL, 0);
        btn_draw(btn_next_open());
        btn_draw(btn_next_cancel());
    }
    if (pageCount > 1) {
        int barW = LW() - 36;
        int filled = (barW * curPage) / pageCount;
        SDL_SetRenderDrawColor(gRen, 26, 30, 44, 210);
        SDL_Rect bg = { 18, LH() - 14, barW, 6 };
        SDL_RenderFillRect(gRen, &bg);
        SDL_SetRenderDrawColor(gRen, 250, 215, 120, 240);
        SDL_Rect fg = { 18, LH() - 14, filled, 6 };
        SDL_RenderFillRect(gRen, &fg);
    }
}

// ---------------- toque: dispatch de tap ----------------
static int *running_ptr = NULL;
static void handle_tap(int lx, int ly) {
    // botoes comuns no topo
    if (screen != SC_READER && btn_hit(btn_rotate(), lx, ly)) { toggle_orientation(); return; }

    if (is_catalog_screen()) {
        if (btn_hit(btn_exit(), lx, ly)) { if (running_ptr) *running_ptr = 0; return; }
        if (btn_hit(btn_continue(), lx, ly)) { load_continue(); screen = SC_CONTINUE; return; }
        if (catalogFavorites && btn_hit(btn_catalog_home(), lx, ly)) { return_to_library(); return; }
        if (!catalogFavorites && btn_hit(btn_favorites(), lx, ly)) { catPage = 0; catSel = 0; load_favorites(); screen = SC_FAVORITES; return; }
        if (btn_hit(btn_settings(), lx, ly)) { load_profile(); screen = SC_SETTINGS; return; }
        if (btn_hit(btn_prev(), lx, ly)) { if (catPage > 0) { catPage--; catSel = 0; if (catalogFavorites) load_favorites(); else load_catalog(); } return; }
        if (btn_hit(btn_next(), lx, ly)) { if (catPage < catTotal - 1) { catPage++; catSel = 0; if (catalogFavorites) load_favorites(); else load_catalog(); } return; }
        if (btn_hit(btn_area(), lx, ly)) { areaIdx = (areaIdx + 1) % AREA_COUNT; catPage = 0; catSel = 0; g_search[0] = '\0'; load_catalog(); screen = SC_SERIES; return; }
        if (btn_hit(btn_view_mode(), lx, ly)) { toggle_catalog_view(); return; }
        if (btn_hit(btn_search(), lx, ly)) {
            char term[96] = {0};
            int rs = prompt_text("Buscar serie (vazio = limpar)", term, sizeof(term), 0);
            if (rs != -1) { snprintf(g_search, sizeof(g_search), "%s", rs == 0 ? term : ""); catPage = 0; catSel = 0; load_catalog(); }
            return;
        }
        if (catViewMode == 0) {
            int cols = grid_cols(), gap = grid_gap(), cardW = grid_card_w(), cellH = grid_cell_h();
            if (ly >= LIST_Y && ly < LH() - FOOTER_H) {
                int r = (ly - LIST_Y - 4) / cellH;
                int c = (lx - gap) / (cardW + gap);
                if (r >= 0 && c >= 0 && c < cols) {
                    int idx = (catScroll + r) * cols + c;
                    if (idx >= 0 && idx < catCount) { catSel = idx; enter_series(idx); }
                }
            }
        } else if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = catScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < catCount) { catSel = idx; enter_series(idx); }
        }
    } else if (screen == SC_SETTINGS) {
        if (btn_hit(btn_library(), lx, ly)) { screen = catalogFavorites ? SC_FAVORITES : SC_SERIES; return; }
        if (btn_hit(btn_switch_account(), lx, ly)) {
            store_clear_token();
            store_clear_user();
            if (g_token) { free(g_token); g_token = NULL; }
            g_username[0] = '\0';
            if (authenticate()) { catPage = 0; catSel = 0; load_catalog(); screen = SC_SERIES; }
            return;
        }
    } else if (screen == SC_CHAPTERS) {
        if (btn_hit(btn_back(), lx, ly)) { screen = g_chapters_back; return; }
        if (btn_hit(btn_chap_order(), lx, ly)) { chapReversed = !chapReversed; chapSel = 0; chapScroll = 0; return; }
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
        if (next_prompt_started && !next_prompt_cancelled && reader_has_next_chapter()) {
            if (btn_hit(btn_next_open(), lx, ly)) { reader_open_next_chapter_now(); return; }
            if (btn_hit(btn_next_cancel(), lx, ly)) { next_prompt_cancelled = 1; next_prompt_started = 0; reader_show_overlay(); return; }
        }
        int overlay = SDL_GetTicks() < reader_overlay_until;
        if (overlay && ly < TB) {
            if (btn_hit(btn_back(), lx, ly)) { if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; } screen = g_reader_back; return; }
            if (btn_hit(btn_offline(), lx, ly)) { offline_download_current_chapter(); return; }
            if (btn_hit(btn_rotate(), lx, ly)) { toggle_orientation(); reader_clamp_pan(); reader_show_overlay(); return; }
            return;
        }
        if (rd_zoom <= READER_ZOOM_MIN + 0.01f) {
            if (lx < LW() * 34 / 100) { reader_advance(-1); return; }
            if (lx > LW() * 66 / 100) { reader_advance(1); return; }
        }
        reader_show_overlay();
    }
}

static int reader_touch_count(void) {
    int n = 0;
    for (int i = 0; i < 2; i++) if (readerTouches[i].active) n++;
    return n;
}

static int reader_touch_find(SDL_FingerID id) {
    for (int i = 0; i < 2; i++) if (readerTouches[i].active && readerTouches[i].id == id) return i;
    return -1;
}

static int reader_touch_free_slot(void) {
    for (int i = 0; i < 2; i++) if (!readerTouches[i].active) return i;
    return -1;
}

static float reader_touch_dist(void) {
    float dx = (float)(readerTouches[0].x - readerTouches[1].x);
    float dy = (float)(readerTouches[0].y - readerTouches[1].y);
    return SDL_sqrtf(dx * dx + dy * dy);
}

static void reader_touch_begin(SDL_FingerID id, float nx, float ny) {
    int lx, ly;
    screen_to_logical(nx, ny, &lx, &ly);
    int slot = reader_touch_free_slot();
    if (slot < 0) {
        reader_reset_touch();
        slot = 0;
    }
    readerTouches[slot].active = 1;
    readerTouches[slot].id = id;
    readerTouches[slot].x = readerTouches[slot].startX = lx;
    readerTouches[slot].y = readerTouches[slot].startY = ly;
    readerSwipeMoved = 0;
    if (reader_touch_count() == 2) {
        readerPinching = 1;
        readerPinchLive = 0;
        readerPinchBaseDist = reader_touch_dist();
        readerPinchBaseZoom = rd_zoom;
    }
}

static void reader_touch_move(SDL_FingerID id, float nx, float ny) {
    int slot = reader_touch_find(id);
    if (slot < 0) return;
    int lx, ly;
    screen_to_logical(nx, ny, &lx, &ly);
    int dx = lx - readerTouches[slot].x;
    int dy = ly - readerTouches[slot].y;
    readerTouches[slot].x = lx;
    readerTouches[slot].y = ly;
    int totalMove = abs(lx - readerTouches[slot].startX) + abs(ly - readerTouches[slot].startY);
    if (totalMove > readerSwipeMoved) readerSwipeMoved = totalMove;

    if (reader_touch_count() == 2 && readerPinching) {
        float dist = reader_touch_dist();
        if (readerPinchBaseDist < 8.0f) readerPinchBaseDist = dist;
        float ratio = readerPinchBaseDist > 0.0f ? dist / readerPinchBaseDist : 1.0f;
        if (readerPinchLive || ratio < 1.0f - READER_PINCH_DEADZONE || ratio > 1.0f + READER_PINCH_DEADZONE) {
            readerPinchLive = 1;
            rd_zoom = readerPinchBaseZoom * ratio;
            if (rd_zoom < READER_ZOOM_MIN) rd_zoom = READER_ZOOM_MIN;
            if (rd_zoom > READER_ZOOM_MAX) rd_zoom = READER_ZOOM_MAX;
            reader_clamp_pan();
            reader_show_overlay();
        }
        return;
    }

    if (reader_touch_count() == 1 && rd_zoom > READER_ZOOM_MIN + 0.01f) {
        rd_pan_x += dx;
        rd_pan_y += dy;
        reader_clamp_pan();
    }
}

static void reader_touch_end(SDL_FingerID id, float nx, float ny) {
    int slot = reader_touch_find(id);
    if (slot < 0) return;
    int lx, ly;
    screen_to_logical(nx, ny, &lx, &ly);
    int dx = lx - readerTouches[slot].startX;
    int dy = ly - readerTouches[slot].startY;
    int wasPinching = readerPinching || readerPinchLive || reader_touch_count() > 1;
    readerTouches[slot].active = 0;

    if (reader_touch_count() == 0) {
        if (!wasPinching) {
            int adx = abs(dx), ady = abs(dy);
            if (rd_zoom <= READER_ZOOM_MIN + 0.01f && adx > 70 && adx > ady + 30) {
                reader_advance(dx < 0 ? 1 : -1);
            } else if (readerSwipeMoved <= TAP_THRESH) {
                handle_tap(lx, ly);
            }
        }
        reader_reset_touch();
    } else {
        for (int i = 0; i < 2; i++) {
            if (readerTouches[i].active) {
                readerTouches[i].startX = readerTouches[i].x;
                readerTouches[i].startY = readerTouches[i].y;
            }
        }
        readerPinching = 0;
        readerPinchLive = 0;
        readerSwipeMoved = 0;
    }
}

// arrasto vertical nas listas -> rola
static void scroll_current_view(int delta) {
    if (delta == 0) return;
    if (is_catalog_screen() && catViewMode == 0) {
        int cols = grid_cols();
        int maxRow = (catCount + cols - 1) / cols - grid_visible_rows();
        if (maxRow < 0) maxRow = 0;
        catScroll += delta;
        if (catScroll < 0) catScroll = 0;
        if (catScroll > maxRow) catScroll = maxRow;
        return;
    }
    int *scroll = NULL, count = 0, vis = visible_rows();
    if (is_catalog_screen()) { scroll = &catScroll; count = catCount; }
    else if (screen == SC_CONTINUE) { scroll = &contScroll; count = contN; }
    else if (screen == SC_CHAPTERS) { scroll = &chapScroll; count = chapCount; }
    else return;
    int maxs = count - vis;
    if (maxs < 0) maxs = 0;
    *scroll += delta;
    if (*scroll < 0) *scroll = 0;
    if (*scroll > maxs) *scroll = maxs;
}

static void handle_swipe_release(int dx, int dy) {
    int adx = abs(dx), ady = abs(dy);
    if (ady < 34 || ady < adx) return;
    int steps = 1;
    if (ady > 210) steps = 3;
    else if (ady > 110) steps = 2;
    scroll_current_view(dy < 0 ? steps : -steps);
}

static void handle_drag(int curLY) {
    if (screen == SC_READER) {
        return;
    }
    if (is_catalog_screen() && catViewMode == 0) {   // grade: rola por linhas de cards
        int cellH = grid_cell_h();
        int cols = grid_cols();
        int maxRow = (catCount + cols - 1) / cols - grid_visible_rows();
        if (maxRow < 0) maxRow = 0;
        dragAccum += (float)(curLY - lastLY);
        while (dragAccum >= cellH) { catScroll--; dragAccum -= cellH; }
        while (dragAccum <= -cellH) { catScroll++; dragAccum += cellH; }
        if (catScroll < 0) catScroll = 0;
        if (catScroll > maxRow) catScroll = maxRow;
        return;
    }
    int *scroll = NULL, count = 0;
    if (is_catalog_screen()) { scroll = &catScroll; count = catCount; }
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
        if (maybe_install_update()) goto cleanup;
        if (configure_server() && authenticate()) {
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
                        if (screen == SC_READER) {
                            reader_touch_begin(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
                        } else {
                            screen_to_logical(e.tfinger.x, e.tfinger.y, &downLX, &downLY);
                            lastLY = downLY; movedMax = 0; dragAccum = 0; fingerDown = 1;
                        }
                    } else if (e.type == SDL_FINGERMOTION) {
                        if (screen == SC_READER) {
                            reader_touch_move(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
                        } else if (fingerDown) {
                            int cx, cy; screen_to_logical(e.tfinger.x, e.tfinger.y, &cx, &cy);
                            int d = abs(cx - downLX) + abs(cy - downLY);
                            if (d > movedMax) movedMax = d;
                            if (movedMax > TAP_THRESH) handle_drag(cy);
                            lastLY = cy;
                        }
                    } else if (e.type == SDL_FINGERUP) {
                        if (screen == SC_READER) {
                            reader_touch_end(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
                        } else {
                            if (fingerDown) {
                                int ux, uy;
                                screen_to_logical(e.tfinger.x, e.tfinger.y, &ux, &uy);
                                if (movedMax <= TAP_THRESH) handle_tap(downLX, downLY);
                                else handle_swipe_release(ux - downLX, uy - downLY);
                            }
                            fingerDown = 0;
                        }
                    } else if (e.type == SDL_JOYBUTTONDOWN) {
                        int b = e.jbutton.button;
                        if (b == JOY_PLUS) { running = 0; break; }
                        if (b == JOY_ZL || b == JOY_ZR) { toggle_orientation(); continue; }

                        if (is_catalog_screen()) {
                            int gc = grid_cols();
                            if (catViewMode == 0 && b == JOY_UP) { if (catSel - gc >= 0) catSel -= gc; }
                            else if (catViewMode == 0 && b == JOY_DOWN) { if (catSel + gc < catCount) catSel += gc; }
                            else if (catViewMode == 0 && b == JOY_DLEFT) { if (catSel > 0) catSel--; }
                            else if (catViewMode == 0 && b == JOY_DRIGHT) { if (catSel < catCount - 1) catSel++; }
                            else if (catViewMode == 1 && b == JOY_UP && catSel > 0) catSel--;
                            else if (catViewMode == 1 && b == JOY_DOWN && catSel < catCount - 1) catSel++;
                            else if (catViewMode == 1 && b == JOY_DLEFT) { catSel -= visible_rows(); if (catSel < 0) catSel = 0; }
                            else if (catViewMode == 1 && b == JOY_DRIGHT) { catSel += visible_rows(); if (catSel > catCount - 1) catSel = catCount - 1; }
                            else if (b == JOY_L) { if (catPage > 0) { catPage--; catSel = 0; if (catalogFavorites) load_favorites(); else load_catalog(); } }
                            else if (b == JOY_R) { if (catPage < catTotal - 1) { catPage++; catSel = 0; if (catalogFavorites) load_favorites(); else load_catalog(); } }
                            else if (b == JOY_Y) { areaIdx = (areaIdx + 1) % AREA_COUNT; catPage = 0; catSel = 0; g_search[0] = '\0'; load_catalog(); }
                            else if (b == JOY_X) { toggle_catalog_view(); }
                            else if (b == JOY_MINUS) { store_clear_token(); if (g_token) { free(g_token); g_token = NULL; } if (!authenticate()) { running = 0; break; } catPage = 0; catSel = 0; load_catalog(); }
                            else if (b == JOY_B) { if (catalogFavorites) return_to_library(); else { load_continue(); screen = SC_CONTINUE; } }
                            else if (b == JOY_A && catCount > 0) enter_series(catSel);
                            clamp_catalog_scroll_to_selection();
                        } else if (screen == SC_CHAPTERS) {
                            if (b == JOY_UP && chapSel > 0) chapSel--;
                            else if (b == JOY_DOWN && chapSel < chapCount - 1) chapSel++;
                            else if (b == JOY_L) { chapSel -= visible_rows(); if (chapSel < 0) chapSel = 0; }
                            else if (b == JOY_R) { chapSel += visible_rows(); if (chapSel > chapCount - 1) chapSel = chapCount - 1; }
                            else if (b == JOY_X || b == JOY_Y) { chapReversed = !chapReversed; chapSel = 0; chapScroll = 0; }
                            else if (b == JOY_A && chapCount > 0) enter_reader(chapSel);
                            else if (b == JOY_B) screen = g_chapters_back;
                            if (chapSel < chapScroll) chapScroll = chapSel;
                            if (chapSel >= chapScroll + visible_rows()) chapScroll = chapSel - visible_rows() + 1;
                        } else if (screen == SC_CONTINUE) {
                            if (b == JOY_UP && contSel > 0) contSel--;
                            else if (b == JOY_DOWN && contSel < contN - 1) contSel++;
                            else if (b == JOY_A && contN > 0) enter_reader_from_record(contIds[contSel]);
                            else if (b == JOY_B) screen = SC_SERIES;
                            if (contSel < contScroll) contScroll = contSel;
                            if (contSel >= contScroll + visible_rows()) contScroll = contSel - visible_rows() + 1;
                        } else if (screen == SC_SETTINGS) {
                            if (b == JOY_A) {
                                store_clear_token();
                                store_clear_user();
                                if (g_token) { free(g_token); g_token = NULL; }
                                g_username[0] = '\0';
                                if (authenticate()) { catPage = 0; catSel = 0; load_catalog(); screen = SC_SERIES; }
                            } else if (b == JOY_B) screen = catalogFavorites ? SC_FAVORITES : SC_SERIES;
                        } else {
                            if (b == JOY_A && next_prompt_started && !next_prompt_cancelled && reader_has_next_chapter()) reader_open_next_chapter_now();
                            else if (b == JOY_X && next_prompt_started) { next_prompt_cancelled = 1; next_prompt_started = 0; reader_show_overlay(); }
                            else if (b == JOY_X) offline_download_current_chapter();
                            else if (b == JOY_R || b == JOY_DRIGHT || b == JOY_DOWN || b == JOY_A) reader_advance(1);
                            else if (b == JOY_L || b == JOY_DLEFT || b == JOY_UP) reader_advance(-1);
                            else if (b == JOY_B) { if (pageTex) { SDL_DestroyTexture(pageTex); pageTex = NULL; } screen = g_reader_back; }
                        }
                    }
                }

                reader_tick();
                begin_frame();
                if (is_catalog_screen()) render_series();
                else if (screen == SC_CONTINUE) render_continue();
                else if (screen == SC_SETTINGS) render_settings();
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
