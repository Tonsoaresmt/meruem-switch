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
#include <strings.h>
#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <zlib.h>
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
#define PAGE_CACHE_MAX 5
#define READER_ZOOM_MIN 1.0f
#define READER_ZOOM_MAX 4.0f
#define READER_PINCH_DEADZONE 0.12f
#define READER_OVERLAY_MS 2200
#define NEXT_CHAPTER_MS 5000
#define OFFLINE_DIR "sdmc:/switch/Meruem/offline"
#define LOCAL_ROOT_DEFAULT "sdmc:/Mangas"
#define LOCAL_ROOT_LEGACY "sdmc:/switch/Meruem/local"
#define OFFLINE_COUNT_CACHE_MAX 128
#define LOCAL_MAX_ITEMS 220
#define LOCAL_MAX_PAGES 1000
#define LOCAL_PATH_MAX 512
#define CBZ_MAX_IMAGE_BYTES (64u * 1024u * 1024u)

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

typedef enum { SC_SERIES, SC_FAVORITES, SC_SETTINGS, SC_CONTINUE, SC_OFFLINE, SC_LOCAL, SC_LOCAL_PICKER, SC_CHAPTERS, SC_READER } Screen;
typedef enum { AREA_MANGA, AREA_COMICS, AREA_DOWNLOADED, AREA_LOCAL, AREA_COUNT } AreaType;
typedef enum { READER_SRC_REMOTE, READER_SRC_OFFLINE, READER_SRC_LOCAL, READER_SRC_CBZ } ReaderSource;

static SDL_Renderer *gRen = NULL;
static SDL_Texture  *gCanvas = NULL;
static int g_portrait = 1;

static char *g_token = NULL;
static char g_server[256] = {0};
static char g_username[96] = {0};
static int g_offline_mode = 0;
static const char *AREA_KEYS[AREA_COUNT] = { "manga", "comics", "", "" };
static const char *AREA_LABELS[AREA_COUNT] = { "Mangas", "HQ", "Baixados", "Local" };
static int areaIdx = AREA_MANGA;
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

// Gerenciador de capitulos salvos no SD.
static char offlineIds[60][96];
static int offlineN = 0, offlineSel = 0, offlineScroll = 0;
static int offlineSaved[60];
static int offlinePages[60];
static unsigned long long offlineBytes[60];
static unsigned long long g_offlineTotalBytes = 0;   // total ocupado pelo offline no SD
static Screen g_offline_back = SC_CONTINUE;
static Screen g_local_back = SC_CONTINUE;

typedef struct {
    char name[160];
    char path[LOCAL_PATH_MAX];
    int isReadCurrent;
    int isCbz;
    int imageCount;
    int cbzCount;
    int cbzPages;
    int dirCount;
    unsigned long long size;
} LocalItem;

typedef struct {
    char name[256];
    uint32_t method;
    uint32_t compSize;
    uint32_t uncompSize;
    uint32_t localHeaderOffset;
} CbzPageEntry;

static char g_local_root[LOCAL_PATH_MAX] = LOCAL_ROOT_DEFAULT;
static char g_local_cwd[LOCAL_PATH_MAX] = LOCAL_ROOT_DEFAULT;
static char g_picker_cwd[LOCAL_PATH_MAX] = LOCAL_ROOT_DEFAULT;
static LocalItem localItems[LOCAL_MAX_ITEMS];
static int localN = 0, localSel = 0, localScroll = 0;
static int localLoadFailed = 0, localTruncated = 0;
static char localStatus[192] = {0};
static char localPagePaths[LOCAL_MAX_PAGES][LOCAL_PATH_MAX];
static int localPageN = 0;
static CbzPageEntry localCbzPages[LOCAL_MAX_PAGES];
static char localCbzPath[LOCAL_PATH_MAX] = {0};
static int localCbzPageN = 0;

static char pageBase[512] = {0};
static char curChapLabel[64] = {0};
static char curBookId[96] = {0};
static int pageCount = 1, curPage = 1;
static ReaderSource g_reader_source = READER_SRC_REMOTE;
static SDL_Texture *pageTex = NULL;
static int pageTexPage = 0;
static int reader_pending_view_reset = 0;
static char g_self_path[512] = {0};
static int curChapIndex = -1;
static float rd_zoom = READER_ZOOM_MIN;
static float rd_pan_x = 0.0f, rd_pan_y = 0.0f;
static Uint32 rd_lastTapTime = 0;   // p/ detectar toque duplo (zoom)
static int rd_lastTapX = 0, rd_lastTapY = 0;
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
    char bookId[96];
    char baseUrl[512];
    char localPath[LOCAL_PATH_MAX];
    char cbzName[256];
    uint32_t cbzMethod;
    uint32_t cbzCompSize;
    uint32_t cbzUncompSize;
    uint32_t cbzLocalHeaderOffset;
    char token[512];
    int page;
    int source;
    int offlineMode;
    SDL_Texture *tex;
    SDL_Thread *thread;
    unsigned char *data;
    size_t len;
    volatile int ready;
    volatile int loading;
    int failed;
    Uint32 lastUsed;
} PageCacheEntry;

static PageCacheEntry g_page_cache[PAGE_CACHE_MAX];

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
static Uint32 g_last_activity = 0;   // ultima interacao (p/ render preguicoso/bateria)

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
static void load_offline_manager(void);
static void load_local_browser(const char *path);
static void load_local_picker(const char *path);
static void switch_area_to(int nextArea);
static void draw_area_hint_line(void);
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

static void render_copy_clipped(SDL_Texture *tex, const SDL_Rect *dst, int maxW) {
    if (!tex || !dst || maxW <= 0) return;
    if (dst->w > maxW) {
        SDL_Rect clip = { dst->x, dst->y - 2, maxW, dst->h + 4 };
        SDL_RenderSetClipRect(gRen, &clip);
        SDL_RenderCopy(gRen, tex, NULL, dst);
        SDL_RenderSetClipRect(gRen, NULL);
    } else {
        SDL_RenderCopy(gRen, tex, NULL, dst);
    }
}

static int text_draw_fit(SDL_Renderer *ren, const char *utf8, int x, int y,
                         int maxW, SDL_Color color, int big) {
    int w = 0, h = 0;
    if (maxW <= 0) return 0;
    SDL_Texture *t = text_cached(ren, utf8, color, big, &w, &h);
    if (!t) return 0;
    if (w <= maxW) {
        SDL_Rect dst = { x, y, w, h };
        SDL_RenderCopy(gRen, t, NULL, &dst);
        return w;
    }
    // Nao cabe: corta reservando espaco e desenha "..." no fim.
    int ew = 0, eh = 0;
    SDL_Texture *ell = text_cached(ren, "...", color, big, &ew, &eh);
    int clipW = maxW - ew;
    if (!ell || clipW < 10) {                 // estreito demais: so clipa
        SDL_Rect dst = { x, y, w, h };
        render_copy_clipped(t, &dst, maxW);
        return maxW;
    }
    SDL_Rect dst = { x, y, w, h };
    render_copy_clipped(t, &dst, clipW);
    SDL_Rect edst = { x + clipW, y, ew, eh };
    SDL_RenderCopy(gRen, ell, NULL, &edst);
    return maxW;
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
    SDL_Texture *t = text_cached(gRen, b.label, COL_SEL, 0, &tw, &th);
    if (t) {
        int maxW = b.w - 14;
        int dx = tw <= maxW ? b.x + (b.w - tw) / 2 : b.x + 7;
        SDL_Rect d = { dx, b.y + (b.h - th) / 2, tw, th };
        render_copy_clipped(t, &d, maxW);
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
static Btn btn_use_folder(void) { Btn b = { 12, LH() - 50, 210, 40, "Usar esta pasta" }; return b; }
static Btn btn_parent_folder(void) { Btn b = { 232, LH() - 50, 210, 40, "Voltar pasta" }; return b; }
static Btn btn_prev(void)   { Btn b = { 6, LH() - 50, 110, 40, "< Pag" };  return b; }
static Btn btn_next(void)   { Btn b = { LW() - 116, LH() - 50, 110, 40, "Pag >" }; return b; }
static Btn btn_up(void)     { Btn b = { LW() - 76, TB + 8, 68, 64, "/\\" }; return b; }
static Btn btn_down(void)   { Btn b = { LW() - 76, LH() - FOOTER_H - 76, 68, 64, "\\/" }; return b; }
static Btn btn_chap_order(void) { Btn b = { LW() - 312, 8, 170, TB - 14, chapReversed ? "Mais antigos" : "Mais novos" }; return b; }
static Btn btn_download_all(void) {
    int st = store_get_series_offline(curSeriesId);
    const char *lbl = st == 2 ? "Baixada (rebaixar)" : (st == 1 ? "Completar download" : "Baixar tudo (offline)");
    Btn b = { 12, LH() - 50, 304, 40, lbl };
    return b;
}
static Btn btn_next_open(void) { Btn b = { LW()/2 - 210, LH()/2 + 44, 190, 46, "Abrir agora" }; return b; }
static Btn btn_next_cancel(void) { Btn b = { LW()/2 + 20, LH()/2 + 44, 170, 46, "Cancelar" }; return b; }
static Btn btn_switch_account(void) { Btn b = { 30, LH() - FOOTER_H - 72, 230, 50, "Trocar conta" }; return b; }
static Btn btn_pick_local(void) { Btn b = { 270, LH() - FOOTER_H - 72, 230, 50, "Escolher local" }; return b; }
static Btn btn_default_local(void) { Btn b = { 510, LH() - FOOTER_H - 72, 190, 50, "Usar padrao" }; return b; }
static Btn btn_offline(void) { Btn b = { LW() - 286, 8, 146, TB - 14, "Offline" }; return b; }
static Btn btn_downloads(void) { Btn b = { LW() - 286, 8, 146, TB - 14, "Baixados" }; return b; }
static Btn btn_local_top(void) { Btn b = { LW() - 440, 8, 146, TB - 14, "Local" }; return b; }
static Btn btn_delete_offline(void) { Btn b = { LW() - 286, 8, 146, TB - 14, "Apagar" }; return b; }
static Btn btn_delete_all(void) { Btn b = { 12, LH() - 50, 150, 40, "Apagar tudo" }; return b; }

static int is_catalog_screen(void) {
    return screen == SC_SERIES || screen == SC_FAVORITES;
}

static int area_is_online(int area) {
    return area == AREA_MANGA || area == AREA_COMICS;
}

static const char *current_area_label(void) {
    if (areaIdx < 0 || areaIdx >= AREA_COUNT) return "Mangas";
    return AREA_LABELS[areaIdx];
}

static int is_local_id(const char *id) {
    return id && strncmp(id, "local:", 6) == 0;
}

static int is_local_base(const char *pb) {
    return pb && strncmp(pb, "local:", 6) == 0;
}

static int is_cbz_base(const char *pb) {
    return pb && strncmp(pb, "cbz:", 4) == 0;
}

static void return_to_library(void) {
    catalogFavorites = 0;
    catPage = 0;
    catSel = 0;
    catScroll = 0;
    areaIdx = AREA_MANGA;
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
    if (hint) text_draw_fit(gRen, hint, 18, LH() - 41, LW() - 36, COL_DIM, 0);
}

static void draw_empty_state(const char *title, const char *subtitle) {
    SDL_SetRenderDrawColor(gRen, 25, 32, 48, 220);
    SDL_Rect box = { 18, LIST_Y + 16, LW() - 36, 150 };
    SDL_RenderFillRect(gRen, &box);
    SDL_SetRenderDrawColor(gRen, 68, 84, 112, 255);
    SDL_RenderDrawRect(gRen, &box);
    if (title) text_draw_fit(gRen, title, box.x + 24, box.y + 32, box.w - 48, COL_SEL, 1);
    if (subtitle) text_draw_fit(gRen, subtitle, box.x + 24, box.y + 88, box.w - 48, COL_DIM, 0);
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

// Selo de "baixado" no canto da capa/linha. state: 1 = parcial, 2 = inteira.
static void draw_offline_badge(int x, int y, int state) {
    if (state <= 0) return;
    SDL_SetRenderDrawColor(gRen, 8, 10, 16, 235);
    SDL_Rect sh = { x - 2, y - 2, 54, 24 };
    SDL_RenderFillRect(gRen, &sh);
    if (state >= 2) SDL_SetRenderDrawColor(gRen, 70, 190, 110, 255);
    else            SDL_SetRenderDrawColor(gRen, 232, 180, 70, 255);
    SDL_Rect bg = { x, y, 50, 20 };
    SDL_RenderFillRect(gRen, &bg);
    SDL_Color dark = { 8, 10, 16, 255 };
    text_draw(gRen, state >= 2 ? "OFF" : "1/2", x + 6, y + 1, dark, 0);
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
    long code = net_request_timeout(url, "POST", body, NULL, &resp, NULL, 8L, 20L);
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
// Retorna 1 = ok, 0 = token recusado, -1 = servidor/rede indisponivel.
static int token_status(const char *token) {
    struct membuf r = {0};
    char url[512];
    snprintf(url, sizeof(url), "%s/switch/ping", g_server);
    long c = net_request_timeout(url, "GET", NULL, token, &r, NULL, 4L, 8L);
    membuf_free(&r);
    if (c == 200) return 1;
    if (c <= 0 || c >= 500) return -1;
    return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static unsigned long long file_size_bytes(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n > 0 ? (unsigned long long)n : 0;
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

static unsigned long long offline_chapter_size(const char *bookId, int pages) {
    unsigned long long total = 0;
    char path[320];
    if (!bookId || !bookId[0] || pages <= 0) return 0;
    for (int i = 1; i <= pages; i++) {
        offline_page_path(bookId, i, path, sizeof(path));
        total += file_size_bytes(path);
    }
    return total;
}

static int offline_delete_chapter_files(const char *bookId, int pages) {
    int removed = 0;
    char path[320];
    char dir[256];
    if (!bookId || !bookId[0] || pages <= 0) return 0;
    for (int i = 1; i <= pages; i++) {
        offline_page_path(bookId, i, path, sizeof(path));
        if (remove(path) == 0) removed++;
    }
    offline_chapter_dir(bookId, dir, sizeof(dir));
    remove(dir);
    offline_count_invalidate(bookId);
    return removed;
}

// Soma recursiva do tamanho de uma pasta (ou arquivo).
static unsigned long long dir_total_size(const char *path) {
    DIR *d = opendir(path);
    if (!d) return file_size_bytes(path);   // nao e pasta -> trata como arquivo
    unsigned long long total = 0;
    struct dirent *e;
    char child[512];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        total += dir_total_size(child);
    }
    closedir(d);
    return total;
}

// Remove recursivamente uma pasta e todo o conteudo.
static void rmrf(const char *path) {
    DIR *d = opendir(path);
    if (!d) { remove(path); return; }
    struct dirent *e;
    char child[512];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        rmrf(child);
    }
    closedir(d);
    rmdir(path);
}

// Reduz uma vez paginas muito grandes para ~1600px no lado maior, via GPU (bilinear).
// Resultado: menos serrilhado (duas reducoes suaves em vez de uma forte por frame),
// menos VRAM, e ainda nitido com zoom moderado. Capas (pequenas) passam intactas.
#define IMG_MAX_SIDE 1600
static SDL_Texture *downscale_if_huge(SDL_Texture *src) {
    int tw = 0, th = 0;
    if (!src) return src;
    SDL_QueryTexture(src, NULL, NULL, &tw, &th);
    int longSide = tw > th ? tw : th;
    if (tw <= 0 || th <= 0 || longSide <= IMG_MAX_SIDE) return src;
    float s = (float)IMG_MAX_SIDE / (float)longSide;
    int dw = (int)(tw * s), dh = (int)(th * s);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    SDL_Texture *dst = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, dw, dh);
    if (!dst) return src;
    SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
    SDL_Texture *prev = SDL_GetRenderTarget(gRen);   // restaura depois (pode ser o canvas)
    SDL_SetRenderTarget(gRen, dst);
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 0);
    SDL_RenderClear(gRen);
    SDL_RenderCopy(gRen, src, NULL, NULL);
    SDL_SetRenderTarget(gRen, prev);
    SDL_DestroyTexture(src);
    SDL_SetTextureScaleMode(dst, SDL_ScaleModeLinear);
    return dst;
}

static SDL_Texture *texture_from_rw(SDL_RWops *rw) {
    SDL_Texture *tex = NULL;
    if (!rw) return NULL;
    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(gRen, surf);
        SDL_FreeSurface(surf);
        if (tex) {
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            tex = downscale_if_huge(tex);
        }
    }
    return tex;
}

static SDL_Texture *texture_from_mem(const unsigned char *data, size_t len) {
    if (!data || len == 0 || len > 64 * 1024 * 1024) return NULL;
    return texture_from_rw(SDL_RWFromConstMem(data, (int)len));
}

static int read_file_bytes(const char *path, unsigned char **out, size_t *len) {
    FILE *f;
    long n;
    unsigned char *buf;
    size_t rd;
    if (out) *out = NULL;
    if (len) *len = 0;
    if (!path || !out || !len) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    buf = (unsigned char *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return 0;
    }
    rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) {
        free(buf);
        return 0;
    }
    *out = buf;
    *len = rd;
    return 1;
}

static int ascii_tolower_int(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static void copy_trunc(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int contains_ascii_ci(const char *hay, const char *needle) {
    size_t hn, nn;
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    hn = strlen(hay);
    nn = strlen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        while (j < nn && ascii_tolower_int((unsigned char)hay[i + j]) == ascii_tolower_int((unsigned char)needle[j])) j++;
        if (j == nn) return 1;
    }
    return 0;
}

static int natural_cmp_str(const char *a, const char *b) {
    const unsigned char *pa = (const unsigned char *)(a ? a : "");
    const unsigned char *pb = (const unsigned char *)(b ? b : "");
    while (*pa || *pb) {
        if (isdigit(*pa) && isdigit(*pb)) {
            unsigned long va = 0, vb = 0;
            while (*pa == '0') pa++;
            while (*pb == '0') pb++;
            while (isdigit(*pa)) { va = va * 10 + (unsigned long)(*pa - '0'); pa++; }
            while (isdigit(*pb)) { vb = vb * 10 + (unsigned long)(*pb - '0'); pb++; }
            if (va < vb) return -1;
            if (va > vb) return 1;
            continue;
        }
        int ca = ascii_tolower_int(*pa);
        int cb = ascii_tolower_int(*pb);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
        if (*pa) pa++;
        if (*pb) pb++;
    }
    return 0;
}

static const char *path_basename(const char *path) {
    const char *s;
    if (!path || !path[0]) return "";
    s = strrchr(path, '/');
    return s && s[1] ? s + 1 : path;
}

// Mostra o FIM do caminho: ".../penultima/ultima" (ou o caminho todo se curto).
static void path_short(const char *path, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (!path || !path[0]) { out[0] = '\0'; return; }
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;     // ignora barra final
    int slashes = 0;
    size_t i = len;
    while (i > 0) {
        i--;
        if (path[i] == '/' && ++slashes == 2) break;
    }
    if (slashes == 2 && i > 0) snprintf(out, cap, "...%.*s", (int)(len - i), path + i);
    else                       snprintf(out, cap, "%.*s", (int)len, path);
}

static void path_parent(const char *path, char *out, size_t cap) {
    char tmp[LOCAL_PATH_MAX];
    char *s;
    if (!out || cap == 0) return;
    snprintf(tmp, sizeof(tmp), "%s", path && path[0] ? path : "sdmc:/");
    size_t len = strlen(tmp);
    while (len > 6 && tmp[len - 1] == '/') tmp[--len] = '\0';
    s = strrchr(tmp, '/');
    if (!s || s <= tmp + 5) {
        snprintf(out, cap, "sdmc:/");
        return;
    }
    *s = '\0';
    snprintf(out, cap, "%s", tmp);
}

static int path_join(char *out, size_t cap, const char *base, const char *name) {
    int n;
    if (!out || cap == 0 || !base || !name) return 0;
    n = snprintf(out, cap, "%s%s%s", base, (base[0] && base[strlen(base) - 1] == '/') ? "" : "/", name);
    return n > 0 && (size_t)n < cap;
}

static int path_is_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}

static int is_image_file_name(const char *name) {
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
           !strcasecmp(dot, ".png") || !strcasecmp(dot, ".webp") ||
           !strcasecmp(dot, ".bmp");
}

static int is_cbz_file_name(const char *name) {
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".cbz") || !strcasecmp(dot, ".zip");
}

static unsigned long fnv1a_path(const char *s) {
    unsigned long h = 2166136261u;
    if (!s) s = "";
    while (*s) {
        unsigned char c = (unsigned char)ascii_tolower_int((unsigned char)*s++);
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

static void local_make_book_id(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "local:%08lx", fnv1a_path(path));
}

static int local_item_cmp(const void *a, const void *b) {
    const LocalItem *ia = (const LocalItem *)a;
    const LocalItem *ib = (const LocalItem *)b;
    if (ia->isReadCurrent != ib->isReadCurrent) return ib->isReadCurrent - ia->isReadCurrent;
    return natural_cmp_str(ia->name, ib->name);
}

static int local_page_cmp(const void *a, const void *b) {
    const char *pa = (const char *)a;
    const char *pb = (const char *)b;
    return natural_cmp_str(path_basename(pa), path_basename(pb));
}

static uint16_t zip_rd16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t zip_rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int cbz_page_cmp(const void *a, const void *b) {
    const CbzPageEntry *pa = (const CbzPageEntry *)a;
    const CbzPageEntry *pb = (const CbzPageEntry *)b;
    return natural_cmp_str(pa->name, pb->name);
}

static int cbz_scan_pages(const char *path, CbzPageEntry *out, int maxOut, int *truncated) {
    FILE *f = NULL;
    unsigned char *tail = NULL;
    long fileSize, tailSize, tailStart;
    unsigned char *eocd = NULL;
    int count = 0;
    if (truncated) *truncated = 0;
    if (!path || !path[0]) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    if (fileSize < 22) {
        fclose(f);
        return 0;
    }
    tailSize = fileSize > 66000 ? 66000 : fileSize;
    tailStart = fileSize - tailSize;
    tail = (unsigned char *)malloc((size_t)tailSize);
    if (!tail) {
        fclose(f);
        return 0;
    }
    fseek(f, tailStart, SEEK_SET);
    if (fread(tail, 1, (size_t)tailSize, f) != (size_t)tailSize) {
        free(tail);
        fclose(f);
        return 0;
    }
    for (long i = tailSize - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocd = tail + i;
            break;
        }
    }
    if (!eocd) {
        free(tail);
        fclose(f);
        return 0;
    }

    uint16_t totalEntries = zip_rd16(eocd + 10);
    uint32_t cdOffset = zip_rd32(eocd + 16);
    if (totalEntries == 0xffffu || cdOffset == 0xffffffffu || cdOffset >= (uint32_t)fileSize) {
        free(tail);
        fclose(f);
        return 0; // ZIP64 ainda fica fora da v1 para manter previsivel no Switch.
    }
    free(tail);
    tail = NULL;

    if (fseek(f, (long)cdOffset, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    for (uint16_t i = 0; i < totalEntries; i++) {
        unsigned char h[46];
        char name[512];
        if (fread(h, 1, sizeof(h), f) != sizeof(h)) break;
        if (zip_rd32(h) != 0x02014b50u) break;
        uint16_t flags = zip_rd16(h + 8);
        uint16_t method = zip_rd16(h + 10);
        uint32_t compSize = zip_rd32(h + 20);
        uint32_t uncompSize = zip_rd32(h + 24);
        uint16_t nameLen = zip_rd16(h + 28);
        uint16_t extraLen = zip_rd16(h + 30);
        uint16_t commentLen = zip_rd16(h + 32);
        uint32_t localOffset = zip_rd32(h + 42);
        if (nameLen == 0 || nameLen >= sizeof(name)) {
            fseek(f, (long)nameLen + extraLen + commentLen, SEEK_CUR);
            continue;
        }
        if (fread(name, 1, nameLen, f) != nameLen) break;
        name[nameLen] = '\0';
        fseek(f, (long)extraLen + commentLen, SEEK_CUR);
        if (name[nameLen - 1] == '/' || !is_image_file_name(name)) continue;
        if ((flags & 1) != 0) continue;
        if (!(method == 0 || method == 8)) continue;
        if (compSize == 0 || uncompSize == 0 || compSize > CBZ_MAX_IMAGE_BYTES ||
            uncompSize > CBZ_MAX_IMAGE_BYTES || localOffset >= (uint32_t)fileSize) {
            continue;
        }
        if (maxOut > 0 && count >= maxOut) {
            if (truncated) *truncated = 1;
            continue;
        }
        if (out) {
            copy_trunc(out[count].name, sizeof(out[count].name), name);
            out[count].method = method;
            out[count].compSize = compSize;
            out[count].uncompSize = uncompSize;
            out[count].localHeaderOffset = localOffset;
        }
        count++;
    }
    fclose(f);
    if (out && count > 1) qsort(out, count, sizeof(CbzPageEntry), cbz_page_cmp);
    return count;
}

static int cbz_count_pages_file(const char *path) {
    int truncated = 0;
    return cbz_scan_pages(path, NULL, LOCAL_MAX_PAGES, &truncated);
}

static int cbz_extract_page_data(const char *path, const CbzPageEntry *ent, unsigned char **out, size_t *len) {
    FILE *f;
    unsigned char h[30];
    unsigned char *comp = NULL;
    unsigned char *buf = NULL;
    uint16_t nameLen, extraLen;
    if (out) *out = NULL;
    if (len) *len = 0;
    if (!path || !ent || !out || !len) return 0;
    if (ent->compSize == 0 || ent->uncompSize == 0 ||
        ent->compSize > CBZ_MAX_IMAGE_BYTES || ent->uncompSize > CBZ_MAX_IMAGE_BYTES) {
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, (long)ent->localHeaderOffset, SEEK_SET) != 0 ||
        fread(h, 1, sizeof(h), f) != sizeof(h) ||
        zip_rd32(h) != 0x04034b50u) {
        fclose(f);
        return 0;
    }
    nameLen = zip_rd16(h + 26);
    extraLen = zip_rd16(h + 28);
    if (fseek(f, (long)nameLen + extraLen, SEEK_CUR) != 0) {
        fclose(f);
        return 0;
    }
    comp = (unsigned char *)malloc(ent->compSize);
    if (!comp) {
        fclose(f);
        return 0;
    }
    if (fread(comp, 1, ent->compSize, f) != ent->compSize) {
        free(comp);
        fclose(f);
        return 0;
    }
    fclose(f);

    if (ent->method == 0) {
        *out = comp;
        *len = ent->compSize;
        return 1;
    }
    if (ent->method != 8) {
        free(comp);
        return 0;
    }

    buf = (unsigned char *)malloc(ent->uncompSize);
    if (!buf) {
        free(comp);
        return 0;
    }
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = comp;
    zs.avail_in = (uInt)ent->compSize;
    zs.next_out = buf;
    zs.avail_out = (uInt)ent->uncompSize;
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        free(comp);
        free(buf);
        return 0;
    }
    int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    free(comp);
    if (ret != Z_STREAM_END) {
        free(buf);
        return 0;
    }
    *out = buf;
    *len = (size_t)zs.total_out;
    return *len > 0;
}

static void cbz_display_name(const char *pathOrName, char *out, size_t cap) {
    const char *base = path_basename(pathOrName);
    if (!out || cap == 0) return;
    copy_trunc(out, cap, base && base[0] ? base : "CBZ");
    char *dot = strrchr(out, '.');
    if (dot && is_cbz_file_name(dot)) *dot = '\0';
}

static int local_count_cbz_in_dir(const char *path, unsigned long long *bytes) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int count = 0;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    if (bytes) *bytes = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!is_cbz_file_name(e->d_name)) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (path_is_dir(child)) continue;
        if (count >= LOCAL_MAX_ITEMS) {
            localTruncated = 1;
            break;
        }
        count++;
        if (bytes) *bytes += file_size_bytes(child);
    }
    closedir(d);
    return count;
}

static int local_dir_has_direct_content(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!is_image_file_name(e->d_name) && !is_cbz_file_name(e->d_name)) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (!path_is_dir(child)) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

static int local_dir_has_visible_content_depth(const char *path, int depth) {
    DIR *d;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    int scanned = 0;
    if (local_dir_has_direct_content(path)) return 1;
    if (depth <= 0) return 0;
    d = opendir(path);
    if (!d) return 0;
    while ((e = readdir(d)) != NULL && scanned < LOCAL_MAX_ITEMS) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (!path_is_dir(child)) continue;
        scanned++;
        if (local_dir_has_visible_content_depth(child, depth - 1)) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

static int local_root_has_visible_content(const char *path) {
    return local_dir_has_visible_content_depth(path, 2);
}

static int local_count_images_in_dir(const char *path, unsigned long long *bytes) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int count = 0;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    if (bytes) *bytes = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!is_image_file_name(e->d_name)) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (path_is_dir(child)) continue;
        if (count >= LOCAL_MAX_PAGES) {
            localTruncated = 1;
            break;
        }
        count++;
        if (bytes) *bytes += file_size_bytes(child);
    }
    closedir(d);
    return count;
}

static int local_count_dirs_in_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (path_is_dir(child)) count++;
    }
    closedir(d);
    return count;
}

static int local_load_pages_from_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    localPageN = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!is_image_file_name(e->d_name)) continue;
        if (!path_join(child, sizeof(child), path, e->d_name)) continue;
        if (path_is_dir(child)) continue;
        if (localPageN >= LOCAL_MAX_PAGES) {
            localTruncated = 1;
            break;
        }
        snprintf(localPagePaths[localPageN], sizeof(localPagePaths[localPageN]), "%s", child);
        localPageN++;
    }
    closedir(d);
    qsort(localPagePaths, localPageN, sizeof(localPagePaths[0]), local_page_cmp);
    return localPageN;
}

static void local_root_load_config(void) {
    if (!store_load_local_root(g_local_root, sizeof(g_local_root))) {
        snprintf(g_local_root, sizeof(g_local_root), "%s", LOCAL_ROOT_DEFAULT);
        store_save_local_root(g_local_root);
    }
    if (strcmp(g_local_root, LOCAL_ROOT_LEGACY) == 0 && !local_root_has_visible_content(g_local_root)) {
        snprintf(g_local_root, sizeof(g_local_root), "%s", LOCAL_ROOT_DEFAULT);
        store_save_local_root(g_local_root);
    }
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Meruem", 0777);
    mkdir(LOCAL_ROOT_DEFAULT, 0777);
    if (strcmp(g_local_root, LOCAL_ROOT_LEGACY) == 0) mkdir(LOCAL_ROOT_LEGACY, 0777);
    snprintf(g_local_cwd, sizeof(g_local_cwd), "%s", g_local_root);
}

static void local_root_save(const char *path) {
    if (!path || !path[0]) return;
    snprintf(g_local_root, sizeof(g_local_root), "%s", path);
    snprintf(g_local_cwd, sizeof(g_local_cwd), "%s", path);
    store_save_local_root(g_local_root);
}

static void load_local_browser(const char *path) {
    DIR *d;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    unsigned long long hereBytes = 0;
    unsigned long long hereCbzBytes = 0;
    int hereImages, hereCbz;
    localN = 0;
    localSel = 0;
    localScroll = 0;
    localLoadFailed = 0;
    localTruncated = 0;
    localStatus[0] = '\0';
    if (path && path[0]) snprintf(g_local_cwd, sizeof(g_local_cwd), "%s", path);
    d = opendir(g_local_cwd);
    if (!d) {
        localLoadFailed = 1;
        snprintf(localStatus, sizeof(localStatus), "Pasta nao encontrada: %.120s", g_local_cwd);
        return;
    }
    hereImages = local_count_images_in_dir(g_local_cwd, &hereBytes);
    hereCbz = local_count_cbz_in_dir(g_local_cwd, &hereCbzBytes);
    if (hereImages > 0 && contains_ascii_ci(path_basename(g_local_cwd), g_search)) {
        LocalItem *it = &localItems[localN++];
        memset(it, 0, sizeof(*it));
        snprintf(it->name, sizeof(it->name), "Ler imagens desta pasta");
        snprintf(it->path, sizeof(it->path), "%s", g_local_cwd);
        it->isReadCurrent = 1;
        it->isCbz = 0;
        it->imageCount = hereImages;
        it->cbzCount = hereCbz > 0 ? hereCbz : 0;
        it->cbzPages = 0;
        it->dirCount = local_count_dirs_in_dir(g_local_cwd);
        it->size = hereBytes;
    }
    while ((e = readdir(d)) != NULL) {
        unsigned long long imageBytes = 0;
        unsigned long long cbzBytes = 0;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!path_join(child, sizeof(child), g_local_cwd, e->d_name)) {
            localTruncated = 1;
            continue;
        }
        if (localN >= LOCAL_MAX_ITEMS) {
            localTruncated = 1;
            break;
        }
        if (path_is_dir(child)) {
            if (!contains_ascii_ci(e->d_name, g_search)) continue;
            LocalItem *it = &localItems[localN++];
            memset(it, 0, sizeof(*it));
            copy_trunc(it->name, sizeof(it->name), e->d_name);
            snprintf(it->path, sizeof(it->path), "%s", child);
            it->isReadCurrent = 0;
            it->isCbz = 0;
            it->imageCount = local_count_images_in_dir(child, &imageBytes);
            if (it->imageCount < 0) it->imageCount = 0;
            it->cbzCount = local_count_cbz_in_dir(child, &cbzBytes);
            if (it->cbzCount < 0) it->cbzCount = 0;
            it->cbzPages = 0;
            it->dirCount = local_count_dirs_in_dir(child);
            it->size = imageBytes + cbzBytes;
        } else if (is_cbz_file_name(e->d_name)) {
            char display[160];
            cbz_display_name(e->d_name, display, sizeof(display));
            if (!contains_ascii_ci(display, g_search) && !contains_ascii_ci(e->d_name, g_search)) continue;
            LocalItem *it = &localItems[localN++];
            memset(it, 0, sizeof(*it));
            copy_trunc(it->name, sizeof(it->name), display);
            snprintf(it->path, sizeof(it->path), "%s", child);
            it->isReadCurrent = 0;
            it->isCbz = 1;
            it->imageCount = 0;
            it->cbzCount = 1;
            it->cbzPages = cbz_count_pages_file(child);
            it->dirCount = 0;
            it->size = file_size_bytes(child);
        }
    }
    closedir(d);
    qsort(localItems, localN, sizeof(LocalItem), local_item_cmp);
    if (localN == 0) snprintf(localStatus, sizeof(localStatus), g_search[0] ? "Nada bateu com a busca nesta pasta." : "Pasta vazia. Use CBZ ou pastas com imagens.");
    else if (localTruncated) snprintf(localStatus, sizeof(localStatus), "Lista limitada. Use uma pasta menor se faltar algo.");
}

static void load_local_picker(const char *path) {
    DIR *d;
    struct dirent *e;
    char child[LOCAL_PATH_MAX];
    localN = 0;
    localSel = 0;
    localScroll = 0;
    localLoadFailed = 0;
    localTruncated = 0;
    if (path && path[0]) snprintf(g_picker_cwd, sizeof(g_picker_cwd), "%s", path);
    d = opendir(g_picker_cwd);
    if (!d) {
        localLoadFailed = 1;
        snprintf(localStatus, sizeof(localStatus), "Nao consegui abrir: %.120s", g_picker_cwd);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!path_join(child, sizeof(child), g_picker_cwd, e->d_name)) {
            localTruncated = 1;
            continue;
        }
        if (!path_is_dir(child)) continue;
        if (localN >= LOCAL_MAX_ITEMS) {
            localTruncated = 1;
            break;
        }
        LocalItem *it = &localItems[localN++];
        memset(it, 0, sizeof(*it));
        copy_trunc(it->name, sizeof(it->name), e->d_name);
        snprintf(it->path, sizeof(it->path), "%s", child);
        it->isReadCurrent = 0;
        it->isCbz = 0;
        it->imageCount = local_count_images_in_dir(child, &it->size);
        if (it->imageCount < 0) it->imageCount = 0;
        unsigned long long cbzBytes = 0;
        it->cbzCount = local_count_cbz_in_dir(child, &cbzBytes);
        if (it->cbzCount < 0) it->cbzCount = 0;
        it->size += cbzBytes;
        it->dirCount = local_count_dirs_in_dir(child);
    }
    closedir(d);
    qsort(localItems, localN, sizeof(LocalItem), local_item_cmp);
    if (localTruncated) snprintf(localStatus, sizeof(localStatus), "Lista limitada. Use uma pasta menor.");
    else localStatus[0] = '\0';
}

static int page_cache_matches(PageCacheEntry *e, const char *bookId, const char *baseUrl, int page) {
    return e && e->bookId[0] && e->page == page &&
           e->source == (int)g_reader_source &&
           strcmp(e->bookId, bookId ? bookId : "") == 0 &&
           strcmp(e->baseUrl, baseUrl ? baseUrl : "") == 0;
}

static PageCacheEntry *page_cache_find(const char *bookId, const char *baseUrl, int page) {
    for (int i = 0; i < PAGE_CACHE_MAX; i++) {
        if (page_cache_matches(&g_page_cache[i], bookId, baseUrl, page)) return &g_page_cache[i];
    }
    return NULL;
}

static PageCacheEntry *page_cache_current(void) {
    return page_cache_find(curBookId, pageBase, curPage);
}

static void page_cache_reset_entry(PageCacheEntry *e) {
    if (!e) return;
    if (e->tex == pageTex) {
        pageTex = NULL;
        pageTexPage = 0;
    }
    if (e->tex) SDL_DestroyTexture(e->tex);
    free(e->data);
    memset(e, 0, sizeof(*e));
}

static int page_download_thread(void *arg) {
    PageCacheEntry *entry = (PageCacheEntry *)arg;
    char local[320];
    unsigned char *data = NULL;
    size_t len = 0;

    if (entry->source == READER_SRC_LOCAL) {
        if (entry->localPath[0] && read_file_bytes(entry->localPath, &data, &len)) {
            entry->data = data;
            entry->len = len;
        } else {
            entry->failed = 1;
        }
        entry->loading = 0;
        entry->ready = 1;
        return 0;
    }

    if (entry->source == READER_SRC_CBZ) {
        CbzPageEntry ent;
        memset(&ent, 0, sizeof(ent));
        snprintf(ent.name, sizeof(ent.name), "%s", entry->cbzName);
        ent.method = entry->cbzMethod;
        ent.compSize = entry->cbzCompSize;
        ent.uncompSize = entry->cbzUncompSize;
        ent.localHeaderOffset = entry->cbzLocalHeaderOffset;
        if (entry->localPath[0] && cbz_extract_page_data(entry->localPath, &ent, &data, &len)) {
            entry->data = data;
            entry->len = len;
        } else {
            entry->failed = 1;
        }
        entry->loading = 0;
        entry->ready = 1;
        return 0;
    }

    if (entry->bookId[0] && !is_local_id(entry->bookId)) {
        offline_page_path(entry->bookId, entry->page, local, sizeof(local));
        if (read_file_bytes(local, &data, &len)) {
            entry->data = data;
            entry->len = len;
            entry->loading = 0;
            entry->ready = 1;
            return 0;
        }
        if (entry->offlineMode || entry->source == READER_SRC_OFFLINE) {
            entry->failed = 1;
            entry->loading = 0;
            entry->ready = 1;
            return 0;
        }
    }

    if (entry->baseUrl[0]) {
        char url[560];
        struct membuf buf = {0};
        snprintf(url, sizeof(url), "%s%d", entry->baseUrl, entry->page);
        long code = net_request_timeout(url, "GET", NULL, entry->token, &buf, NULL, 5L, 22L);
        if (code == 200 && buf.data && buf.len > 0) {
            entry->data = (unsigned char *)buf.data;
            entry->len = buf.len;
            buf.data = NULL;
            buf.len = 0;
        } else {
            entry->failed = 1;
        }
        membuf_free(&buf);
    } else {
        entry->failed = 1;
    }

    entry->loading = 0;
    entry->ready = 1;
    return 0;
}

static PageCacheEntry *page_cache_slot_for(const char *bookId, const char *baseUrl, int page) {
    PageCacheEntry *existing = page_cache_find(bookId, baseUrl, page);
    PageCacheEntry *empty = NULL;
    PageCacheEntry *victim = NULL;
    Uint32 oldest = 0xffffffffu;
    if (existing) return existing;
    for (int i = 0; i < PAGE_CACHE_MAX; i++) {
        PageCacheEntry *e = &g_page_cache[i];
        if (!e->bookId[0]) {
            empty = e;
            break;
        }
        if (!e->loading && !e->thread && e->lastUsed < oldest) {
            if (page_cache_matches(e, curBookId, pageBase, curPage)) continue;
            oldest = e->lastUsed;
            victim = e;
        }
    }
    if (empty) return empty;
    if (victim) {
        page_cache_reset_entry(victim);
        return victim;
    }
    return NULL;
}

static void page_cache_request(int page) {
    if (page < 1 || page > pageCount || !curBookId[0] || !pageBase[0]) return;
    PageCacheEntry *e = page_cache_slot_for(curBookId, pageBase, page);
    if (!e || e->loading || e->tex || e->failed) {
        if (e) e->lastUsed = SDL_GetTicks();
        return;
    }
    snprintf(e->bookId, sizeof(e->bookId), "%s", curBookId);
    snprintf(e->baseUrl, sizeof(e->baseUrl), "%s", pageBase);
    e->localPath[0] = '\0';
    e->cbzName[0] = '\0';
    e->cbzMethod = 0;
    e->cbzCompSize = 0;
    e->cbzUncompSize = 0;
    e->cbzLocalHeaderOffset = 0;
    if (g_reader_source == READER_SRC_LOCAL && page >= 1 && page <= localPageN) {
        snprintf(e->localPath, sizeof(e->localPath), "%s", localPagePaths[page - 1]);
    } else if (g_reader_source == READER_SRC_CBZ && page >= 1 && page <= localCbzPageN) {
        const CbzPageEntry *src = &localCbzPages[page - 1];
        snprintf(e->localPath, sizeof(e->localPath), "%s", localCbzPath);
        snprintf(e->cbzName, sizeof(e->cbzName), "%s", src->name);
        e->cbzMethod = src->method;
        e->cbzCompSize = src->compSize;
        e->cbzUncompSize = src->uncompSize;
        e->cbzLocalHeaderOffset = src->localHeaderOffset;
    }
    snprintf(e->token, sizeof(e->token), "%s", g_token ? g_token : "");
    e->page = page;
    e->source = g_reader_source;
    e->offlineMode = g_offline_mode || g_reader_source == READER_SRC_OFFLINE;
    e->loading = 1;
    e->ready = 0;
    e->failed = 0;
    e->lastUsed = SDL_GetTicks();
    e->thread = SDL_CreateThread(page_download_thread, "page", e);
    if (!e->thread) {
        e->loading = 0;
        e->failed = 1;
    }
}

static void page_cache_pump(void) {
    for (int i = 0; i < PAGE_CACHE_MAX; i++) {
        PageCacheEntry *e = &g_page_cache[i];
        if (!e->ready) continue;
        if (e->thread) {
            SDL_WaitThread(e->thread, NULL);
            e->thread = NULL;
        }
        e->ready = 0;
        if (e->data && e->len > 0) {
            e->tex = texture_from_mem(e->data, e->len);
            free(e->data);
            e->data = NULL;
            e->len = 0;
            if (!e->tex) e->failed = 1;
        }
    }
}

static int page_cache_apply_current(void) {
    PageCacheEntry *e = page_cache_current();
    if (!e || !e->tex) return 0;
    e->lastUsed = SDL_GetTicks();
    if (pageTex != e->tex || pageTexPage != curPage) {
        pageTex = e->tex;
        pageTexPage = curPage;
        if (reader_pending_view_reset) {
            reader_pending_view_reset = 0;
            reader_reset_view();
        }
        reader_start_next_prompt();
    }
    return 1;
}

static int page_cache_current_failed(void) {
    PageCacheEntry *e = page_cache_current();
    return e && e->failed && !e->loading && !e->tex;
}

static int page_cache_current_loading(void) {
    PageCacheEntry *e = page_cache_current();
    return e && (e->loading || e->ready);
}

static void page_cache_prefetch_around(void) {
    page_cache_request(curPage);
    if (curPage < pageCount) page_cache_request(curPage + 1);
    if (curPage > 1) page_cache_request(curPage - 1);
}

static void page_cache_clear(void) {
    for (int i = 0; i < PAGE_CACHE_MAX; i++) {
        if (g_page_cache[i].thread) {
            SDL_WaitThread(g_page_cache[i].thread, NULL);
            g_page_cache[i].thread = NULL;
        }
        page_cache_reset_entry(&g_page_cache[i]);
    }
    pageTex = NULL;
    pageTexPage = 0;
}

static void page_cache_clear_book(const char *bookId) {
    if (!bookId || !bookId[0]) return;
    for (int i = 0; i < PAGE_CACHE_MAX; i++) {
        PageCacheEntry *e = &g_page_cache[i];
        if (!e->bookId[0] || strcmp(e->bookId, bookId) != 0) continue;
        if (e->thread) continue;
        page_cache_reset_entry(e);
    }
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
    if (empty < 0) {
        for (int i = 0; i < COVER_CACHE_MAX; i++) {
            if (!g_cover_cache[i].loading && !g_cover_cache[i].thread) {
                if (g_cover_cache[i].tex) SDL_DestroyTexture(g_cover_cache[i].tex);
                free(g_cover_cache[i].data);
                memset(&g_cover_cache[i], 0, sizeof(g_cover_cache[i]));
                empty = i;
                break;
            }
        }
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

    text_draw_fit(gRen, title ? title : "Meruem", x + 28, y + 34, w - 56, COL_HEAD, 1);
    if (l1) text_draw_fit(gRen, l1, x + 28, y + 110, w - 56, COL_SEL, 0);
    if (l2) text_draw_fit(gRen, l2, x + 28, y + 154, w - 56, COL_SOFT, 0);
    if (l3) text_draw_fit(gRen, l3, x + 28, y + 204, w - 56, COL_TEXT, 0);
    if (hint) {
        SDL_SetRenderDrawColor(gRen, 12, 16, 27, 220);
        SDL_Rect hint_box = { x + 20, y + h - 74, w - 40, 46 };
        SDL_RenderFillRect(gRen, &hint_box);
        SDL_SetRenderDrawColor(gRen, 50, 64, 90, 255);
        SDL_RenderDrawRect(gRen, &hint_box);
        text_draw_fit(gRen, hint, hint_box.x + 16, hint_box.y + 11, hint_box.w - 32, COL_DIM, 0);
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

// ---------------- download offline em paralelo ----------------
#define DL_WORKERS 4

typedef struct {
    char base[512];
    char bookId[96];
    int  pages;
} DlChap;

typedef struct {
    DlChap *chaps;
    int nChaps;
    int *chapStart;          // chapStart[i] = paginas acumuladas antes do cap i; [nChaps] = total
    int total;
    char token[512];
    SDL_atomic_t next;       // proxima pagina (global) a baixar
    SDL_atomic_t saved;
    SDL_atomic_t failed;
    volatile int cancel;
} DlPool;

static int dl_worker(void *arg) {
    DlPool *p = (DlPool *)arg;
    for (;;) {
        if (p->cancel) break;
        int j = SDL_AtomicAdd(&p->next, 1);
        if (j >= p->total) break;
        int c = 0;
        while (c + 1 < p->nChaps && p->chapStart[c + 1] <= j) c++;
        int page = j - p->chapStart[c] + 1;
        DlChap *dc = &p->chaps[c];
        char path[320], url[600];
        offline_page_path(dc->bookId, page, path, sizeof(path));
        if (file_exists(path)) { SDL_AtomicAdd(&p->saved, 1); continue; }
        snprintf(url, sizeof(url), "%s%d", dc->base, page);
        long code = net_download_file_timeout(url, p->token, path, NULL, 8L, 45L);
        if (code == 200) SDL_AtomicAdd(&p->saved, 1);
        else { remove(path); SDL_AtomicAdd(&p->failed, 1); }
    }
    return 0;
}

// Baixa em PARALELO todas as paginas dos capitulos dados. Mostra progresso e
// permite pausar (B/+). Retorna paginas que falharam, ou -1 se cancelado.
static int offline_download_jobs(DlChap *chaps, int nChaps, const char *noun) {
    if (!chaps || nChaps <= 0) return 0;
    int *chapStart = (int *)malloc(sizeof(int) * (nChaps + 1));
    if (!chapStart) return 0;
    int total = 0;
    for (int i = 0; i < nChaps; i++) {
        chapStart[i] = total;
        total += chaps[i].pages > 0 ? chaps[i].pages : 0;
        offline_ensure_dirs(chaps[i].bookId);
    }
    chapStart[nChaps] = total;
    if (total <= 0) { free(chapStart); return 0; }

    DlPool pool;
    memset(&pool, 0, sizeof(pool));
    pool.chaps = chaps; pool.nChaps = nChaps; pool.chapStart = chapStart; pool.total = total;
    snprintf(pool.token, sizeof(pool.token), "%s", g_token ? g_token : "");
    SDL_AtomicSet(&pool.next, 0);
    SDL_AtomicSet(&pool.saved, 0);
    SDL_AtomicSet(&pool.failed, 0);

    SDL_Thread *th[DL_WORKERS];
    for (int i = 0; i < DL_WORKERS; i++) th[i] = SDL_CreateThread(dl_worker, "dl", &pool);

    int cancelled = 0;
    for (;;) {
        int saved = SDL_AtomicGet(&pool.saved);
        int failed = SDL_AtomicGet(&pool.failed);
        int done = saved + failed;
        int started = SDL_AtomicGet(&pool.next);
        if (started > total) started = total;
        {
            char l1[160], l2[160], l3[160];
            SDL_Color blue = { 96, 154, 232, 255 };
            int pct = total > 0 ? (done * 100) / total : 0;
            snprintf(l1, sizeof(l1), "Baixando %s (%d em paralelo)", noun ? noun : "paginas", DL_WORKERS);
            snprintf(l2, sizeof(l2), "%d de %d paginas  (%d%%)", done, total, pct);
            snprintf(l3, sizeof(l3), "Salvas: %d    Falhas: %d", saved, failed);
            draw_modal_box("Leitura offline", l1, l2, l3, blue,
                           cancelled ? "Finalizando downloads em andamento..." : "B/+ = pausar");
        }
        if (!cancelled && offline_cancel_requested()) { pool.cancel = 1; cancelled = 1; }
        if (done >= total) break;
        if (cancelled && (started - done) <= 0) break;
        SDL_Delay(40);
    }
    for (int i = 0; i < DL_WORKERS; i++) if (th[i]) SDL_WaitThread(th[i], NULL);

    int failedTotal = SDL_AtomicGet(&pool.failed);
    free(chapStart);
    return cancelled ? -1 : failedTotal;
}

// Recalcula o estado offline da serie atual (g_ser) e grava no store (0/1/2).
static void recompute_series_offline(void) {
    if (!g_ser || !curSeriesId[0]) return;
    int n = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"));
    int total = 0, saved = 0, any = 0;
    for (int i = 0; i < n; i++) {
        cJSON *c = chap_at_raw(i);
        const char *bid = json_str(c, "id", "");
        int pages = json_int(c, "pages", 0);
        if (!bid[0] || pages <= 0) continue;
        any = 1; total += pages; saved += offline_count_pages(bid, pages);
    }
    int state = 0;
    if (any && saved > 0) state = (saved >= total) ? 2 : 1;
    store_set_series_offline(curSeriesId, state);
}

static void offline_download_current_chapter(void) {
    char line[180];
    if (g_reader_source != READER_SRC_REMOTE) {
        info_screen("Esta leitura ja esta no SD.", "Nao precisa baixar de novo.");
        reader_show_overlay();
        return;
    }
    if (!curBookId[0] || !pageBase[0] || pageCount < 1) {
        message_screen("Nao consegui identificar o capitulo.", "Abra o capitulo novamente e tente baixar.");
        return;
    }
    int existing = offline_count_pages(curBookId, pageCount);
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
    DlChap chap;
    memset(&chap, 0, sizeof(chap));
    snprintf(chap.base, sizeof(chap.base), "%s", pageBase);
    snprintf(chap.bookId, sizeof(chap.bookId), "%s", curBookId);
    chap.pages = pageCount;
    int failed = offline_download_jobs(&chap, 1, "capitulo");

    offline_count_invalidate(curBookId);
    {
        PageCacheEntry *e = page_cache_current();
        if (e && e->failed) page_cache_reset_entry(e);
        page_cache_request(curPage);
        page_cache_apply_current();
        page_cache_prefetch_around();
    }
    recompute_series_offline();
    reader_show_overlay();
    int saved = offline_count_pages(curBookId, pageCount);
    if (failed == -1) {
        snprintf(line, sizeof(line), "Salvas: %d/%d. Voce pode continuar depois.", saved, pageCount);
        info_screen("Download offline pausado.", line);
    } else if (failed > 0) {
        snprintf(line, sizeof(line), "Salvas: %d/%d. Tente de novo para completar.", saved, pageCount);
        SDL_Color gold = { 238, 187, 92, 255 };
        if (modal_wait_loop("Algumas paginas falharam.", line, "Quer tentar baixar so as faltantes agora?",
                            NULL, gold, "A/toque = tentar de novo    B/+ = depois", 1)) {
            offline_download_current_chapter();
            return;
        }
    } else {
        snprintf(line, sizeof(line), "%d paginas salvas no SD.", saved);
        success_screen("Capitulo pronto para ler offline.", line);
    }
    reader_show_overlay();
}

// Baixa TODOS os capitulos da serie aberta (g_ser) para o SD.
static void offline_download_all_chapters(void) {
    if (!g_ser || chapCount <= 0) {
        message_screen("Nenhum capitulo para baixar.", "Abra a serie novamente.");
        return;
    }
    int totalPages = 0, alreadySaved = 0, withPages = 0;
    for (int i = 0; i < chapCount; i++) {
        cJSON *c = chap_at_raw(i);
        int pages = json_int(c, "pages", 0);
        const char *bid = json_str(c, "id", "");
        if (pages > 0 && bid[0]) {
            totalPages += pages;
            alreadySaved += offline_count_pages(bid, pages);
            withPages++;
        }
    }
    if (totalPages <= 0) {
        message_screen("Esta serie nao tem paginas de imagem.", "Pode ser PDF/EPUB (nao suportado no Switch).");
        return;
    }
    {
        char l1[160], l2[160];
        SDL_Color gold = { 238, 187, 92, 255 };
        snprintf(l1, sizeof(l1), "Baixar a serie inteira? %d capitulos.", withPages);
        snprintf(l2, sizeof(l2), "%d paginas no total. %d ja no SD.", totalPages, alreadySaved);
        if (!modal_wait_loop("Baixar serie offline", l1, l2,
                             "Pode demorar. Da pra pausar com B/+.", gold,
                             "A/toque = baixar tudo    B/+ = cancelar", 1)) {
            return;
        }
    }

    DlChap *chaps = (DlChap *)malloc(sizeof(DlChap) * chapCount);
    if (!chaps) { message_screen("Sem memoria para preparar o download.", NULL); return; }
    int n = 0;
    for (int i = 0; i < chapCount; i++) {
        cJSON *c = chap_at_raw(i);
        const char *pb = json_str(c, "pageBase", "");
        const char *bid = json_str(c, "id", "");
        int pages = json_int(c, "pages", 0);
        if (!pb[0] || !bid[0] || pages <= 0) continue;
        memset(&chaps[n], 0, sizeof(DlChap));
        snprintf(chaps[n].base, sizeof(chaps[n].base), "%s%s", g_server, pb);
        snprintf(chaps[n].bookId, sizeof(chaps[n].bookId), "%s", bid);
        chaps[n].pages = pages;
        n++;
    }

    int failed = offline_download_jobs(chaps, n, "serie");

    int savedTotal = 0;
    for (int i = 0; i < n; i++) {
        offline_count_invalidate(chaps[i].bookId);
        savedTotal += offline_count_pages(chaps[i].bookId, chaps[i].pages);
    }
    free(chaps);
    recompute_series_offline();

    {
        char res[200];
        if (failed == -1) {
            snprintf(res, sizeof(res), "%d/%d paginas salvas. Continue depois.", savedTotal, totalPages);
            info_screen("Download da serie pausado.", res);
        } else if (failed > 0) {
            SDL_Color gold = { 238, 187, 92, 255 };
            snprintf(res, sizeof(res), "%d/%d salvas, %d falharam.", savedTotal, totalPages, failed);
            if (modal_wait_loop("Algumas paginas falharam.", res, "Tentar baixar so as faltantes agora?",
                                NULL, gold, "A/toque = tentar de novo    B/+ = depois", 1)) {
                offline_download_all_chapters();
                return;
            }
        } else {
            snprintf(res, sizeof(res), "%d paginas salvas. Serie pronta offline!", savedTotal);
            success_screen("Serie baixada!", res);
        }
    }
}

// Tela de boas-vindas / login. Retorna: 0 = sair, 1 = entrar, 2 = trocar conta, 3 = offline/local.
static int login_welcome_screen(int hasUser, const char *user) {
    SDL_Event e;
    Uint32 shown = SDL_GetTicks();
    while (appletMainLoop()) {
        int bw = LW() - 56;
        int bx = 28, by = 120;
        int bh = LH() - 240;
        if (bh > 560) bh = 560;
        Btn enter = { bx + 28, by + bh - 190, bw - 56, 54, hasUser ? "Entrar" : "Ja tenho conta - Entrar" };
        Btn offline = { bx + 28, by + bh - 126, bw - 56, 48, "Ler offline / local" };
        Btn swap  = { bx + 28, by + bh - 70,  bw - 56, 46, "Trocar conta" };

        while (SDL_PollEvent(&e)) {
            int ready = (SDL_GetTicks() - shown) > 400;
            if (e.type == SDL_QUIT) return 0;
            if (!ready) continue;
            if (e.type == SDL_FINGERUP) {
                int lx, ly;
                screen_to_logical(e.tfinger.x, e.tfinger.y, &lx, &ly);
                if (btn_hit(enter, lx, ly)) return 1;
                if (btn_hit(offline, lx, ly)) return 3;
                if (hasUser && btn_hit(swap, lx, ly)) return 2;
            }
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (e.jbutton.button == JOY_A) return 1;
                if (e.jbutton.button == JOY_PLUS || e.jbutton.button == JOY_B) return 0;
                if (e.jbutton.button == JOY_X) return 3;
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

        text_draw_fit(gRen, "Meruem", bx + 28, by + 30, bw - 56, COL_HEAD, 1);
        if (hasUser) {
            char line[160];
            text_draw_fit(gRen, "Bem-vindo de volta!", bx + 28, by + 96, bw - 56, COL_TEXT, 0);
            snprintf(line, sizeof(line), "Conta: %s", user);
            text_draw_fit(gRen, line, bx + 28, by + 140, bw - 56, COL_SEL, 0);
            text_draw_fit(gRen, "Entrar: Mangas/HQ. Offline/Local: sem rede.", bx + 28, by + 186, bw - 56, COL_SOFT, 0);
        } else {
            text_draw_fit(gRen, "Bem-vindo! Para ler no Switch:", bx + 28, by + 96, bw - 56, COL_TEXT, 0);
            text_draw_fit(gRen, "1) Crie sua conta no Meruem:", bx + 28, by + 142, bw - 56, COL_SOFT, 0);
            text_draw_fit(gRen, g_server[0] ? g_server : DEFAULT_SERVER, bx + 28, by + 178, bw - 56, COL_SEL, 0);
            text_draw_fit(gRen, "2) Volte aqui e toque em Entrar.", bx + 28, by + 218, bw - 56, COL_SOFT, 0);
            text_draw_fit(gRen, "Offline/Local le arquivos no SD.", bx + 28, by + 260, bw - 56, COL_DIM, 0);
        }
        btn_draw(enter);
        btn_draw(offline);
        if (hasUser) btn_draw(swap);
        text_draw_fit(gRen, hasUser ? "A entrar  X offline/local  Y trocar  B/+ sair"
                                    : "A entrar  X offline/local  B/+ sair",
                      bx + 28, by + bh - 26, bw - 56, COL_DIM, 0);
        end_frame();
        SDL_Delay(16);
    }
    return 0;
}

static int authenticate(void) {
    char tok[512];
    if (store_load_token(tok, sizeof(tok))) {
        present_color(20, 20, 40);
        int tok_state = token_status(tok);
        if (tok_state == 1) {
            g_offline_mode = 0;
            g_token = strdup(tok);
            return 1;
        }
        if (tok_state < 0) {
            g_offline_mode = 1;
            g_token = strdup(tok);
            info_screen("Servidor indisponivel.", "Abrindo leituras salvas no modo offline.");
            return 1;
        }
        store_clear_token();
        message_screen("Sessao expirada.", "Entre novamente com sua conta Meruem.");
    }
    int hasUser = store_load_user(g_username, sizeof(g_username));
    while (appletMainLoop()) {
        int act = login_welcome_screen(hasUser, g_username);
        if (act == 0) return 0;
        if (act == 2) { store_clear_user(); g_username[0] = '\0'; hasUser = 0; continue; }
        if (act == 3) { g_offline_mode = 1; return 1; }

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
            g_offline_mode = 0;
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
        fprintf(f, "notes=%s\n", info->release_notes);
        fprintf(f, "message=%s\n", info->message);
    }
    fclose(f);
}

static int maybe_install_update(void) {
    struct update_info info;
    char line1[200];
    char line2[600];
    char line3[220];
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
    if (info.release_notes[0]) snprintf(line3, sizeof(line3), "Mudancas: %.180s", info.release_notes);
    else snprintf(line3, sizeof(line3), "Baixar e trocar o .nro agora?");
    if (!confirm_screen(line1, line2, line3)) {
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

static int ensure_online_session(void) {
    if (!g_offline_mode && g_token) return 1;
    if (!info_screen("Area online precisa de login.", "Entre com sua conta Meruem para acessar Mangas/HQ.")) return 0;
    if (!authenticate()) return 0;
    return (!g_offline_mode && g_token);
}

static void switch_area_to(int nextArea) {
    if (nextArea < 0) nextArea = 0;
    areaIdx = nextArea % AREA_COUNT;
    catalogFavorites = 0;
    catPage = 0;
    catSel = 0;
    catScroll = 0;
    offlineSel = 0;
    offlineScroll = 0;
    localSel = 0;
    localScroll = 0;
    if (area_is_online(areaIdx)) {
        if (!ensure_online_session()) {
            areaIdx = AREA_DOWNLOADED;
            g_offline_back = SC_SERIES;
            load_offline_manager();
            screen = SC_OFFLINE;
            return;
        }
        g_offline_mode = 0;
        load_catalog();
        screen = SC_SERIES;
    } else if (areaIdx == AREA_DOWNLOADED) {
        g_offline_back = SC_SERIES;
        load_offline_manager();
        screen = SC_OFFLINE;
    } else {
        g_local_back = SC_SERIES;
        load_local_browser(g_local_root);
        screen = SC_LOCAL;
    }
}

static void switch_area_next(void) {
    for (int i = 1; i <= AREA_COUNT; i++) {
        int next = (areaIdx + i) % AREA_COUNT;
        if (next == AREA_LOCAL && !local_root_has_visible_content(g_local_root)) continue;
        switch_area_to(next);
        return;
    }
    switch_area_to(AREA_MANGA);
}

// ---------------- dados ----------------
static void load_catalog(void) {
    present_color(20, 20, 40);
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
                     AREA_KEYS[areaIdx], PAGE_SIZE, catPage);
    if (g_search[0] && n > 0 && (size_t)n < sizeof(url)) {
        char enc[200];
        net_urlencode(g_search, enc, sizeof(enc));
        snprintf(url + n, sizeof(url) - n, "&search=%s", enc);
    }
    struct membuf r = {0};
    long code = net_request_timeout(url, "GET", NULL, g_token, &r, NULL, 8L, 20L);
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
    long code = net_request_timeout(url, "GET", NULL, g_token, &r, NULL, 8L, 20L);
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
    long code = net_request_timeout(url, "GET", NULL, g_token, &r, NULL, 8L, 20L);
    if (code == 200 && r.data) g_ser = cJSON_Parse(r.data);
    membuf_free(&r);
    chapCount = 0; chapSel = 0; chapScroll = 0;
    if (g_ser) chapCount = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(g_ser, "chapters"));
    recompute_series_offline();
    g_chapters_back = screen;
    screen = SC_CHAPTERS;
}

static void enter_local_reader_from_folder(const char *folder, const char *seriesTitle,
                                           const char *chapLabel, Screen back) {
    char id[96];
    char parent[LOCAL_PATH_MAX];
    const char *folderName;
    if (!folder || !folder[0]) return;
    if (!path_is_dir(folder)) {
        message_screen("Pasta local nao encontrada.", "Volte em Conta e escolha outra pasta.");
        return;
    }
    if (strlen(folder) + 6 >= sizeof(pageBase)) {
        message_screen("Caminho local longo demais.", "Escolha uma pasta mais curta no SD.");
        return;
    }
    localTruncated = 0;
    localCbzPageN = 0;
    localCbzPath[0] = '\0';
    int pages = local_load_pages_from_dir(folder);
    if (pages <= 0) {
        message_screen("Esta pasta nao tem imagens.", "A v1 do Local le JPG, PNG, WEBP e BMP em pastas.");
        return;
    }
    local_make_book_id(folder, id, sizeof(id));
    path_parent(folder, parent, sizeof(parent));
    folderName = path_basename(folder);
    snprintf(curBookId, sizeof(curBookId), "%s", id);
    snprintf(curSeriesId, sizeof(curSeriesId), "local");
    copy_trunc(curSeriesTitle, sizeof(curSeriesTitle),
               (seriesTitle && seriesTitle[0]) ? seriesTitle : (path_basename(parent)[0] ? path_basename(parent) : "Local"));
    copy_trunc(curChapLabel, sizeof(curChapLabel),
               (chapLabel && chapLabel[0]) ? chapLabel : (folderName[0] ? folderName : "Pasta local"));
    curSeriesCover[0] = '\0';
    snprintf(pageBase, sizeof(pageBase), "local:%s", folder);
    pageCount = pages;
    curPage = store_get_progress(curBookId);
    if (curPage > pageCount) curPage = pageCount;
    if (curPage < 1) curPage = 1;
    if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
    chapCount = 0;
    curChapIndex = -1;
    g_reader_source = READER_SRC_LOCAL;
    pageTex = NULL;
    pageTexPage = 0;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    g_reader_back = back;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    reader_show_overlay();
    screen = SC_READER;
    if (localTruncated) {
        info_screen("Pasta grande demais.", "Mostrei ate 1000 paginas para evitar travamentos.");
        reader_show_overlay();
    }
}

static void enter_local_reader_from_cbz(const char *path, const char *seriesTitle,
                                        const char *chapLabel, Screen back) {
    char id[96];
    char parent[LOCAL_PATH_MAX];
    char display[160];
    if (!path || !path[0]) return;
    if (!file_exists(path) || path_is_dir(path)) {
        message_screen("CBZ local nao encontrado.", "Volte em Local e escolha outro arquivo.");
        return;
    }
    if (strlen(path) + 4 >= sizeof(pageBase)) {
        message_screen("Caminho local longo demais.", "Escolha uma pasta mais curta no SD.");
        return;
    }
    localTruncated = 0;
    localPageN = 0;
    localCbzPageN = cbz_scan_pages(path, localCbzPages, LOCAL_MAX_PAGES, &localTruncated);
    if (localCbzPageN <= 0) {
        message_screen("CBZ sem paginas validas.", "Use CBZ com JPG, PNG, WEBP ou BMP.");
        return;
    }
    snprintf(localCbzPath, sizeof(localCbzPath), "%s", path);
    cbz_display_name(path, display, sizeof(display));
    local_make_book_id(path, id, sizeof(id));
    path_parent(path, parent, sizeof(parent));
    snprintf(curBookId, sizeof(curBookId), "%s", id);
    snprintf(curSeriesId, sizeof(curSeriesId), "local");
    copy_trunc(curSeriesTitle, sizeof(curSeriesTitle),
               (seriesTitle && seriesTitle[0]) ? seriesTitle : (path_basename(parent)[0] ? path_basename(parent) : "Local"));
    copy_trunc(curChapLabel, sizeof(curChapLabel),
               (chapLabel && chapLabel[0]) ? chapLabel : (display[0] ? display : "CBZ local"));
    curSeriesCover[0] = '\0';
    snprintf(pageBase, sizeof(pageBase), "cbz:%s", path);
    pageCount = localCbzPageN;
    curPage = store_get_progress(curBookId);
    if (curPage > pageCount) curPage = pageCount;
    if (curPage < 1) curPage = 1;
    if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
    chapCount = 0;
    curChapIndex = -1;
    g_reader_source = READER_SRC_CBZ;
    pageTex = NULL;
    pageTexPage = 0;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    g_reader_back = back;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    reader_show_overlay();
    screen = SC_READER;
    if (localTruncated) {
        info_screen("CBZ grande demais.", "Mostrei ate 1000 paginas para evitar travamentos.");
        reader_show_overlay();
    }
}

static void enter_reader(int idx) {
    cJSON *c = chap_at(idx);
    if (!c) return;
    const char *pb = json_str(c, "pageBase", "");
    if (!pb[0]) return;
    localPageN = 0;
    g_reader_source = g_offline_mode ? READER_SRC_OFFLINE : READER_SRC_REMOTE;
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
    pageTex = NULL;
    pageTexPage = 0;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    g_reader_back = SC_CHAPTERS;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    reader_show_overlay();
    screen = SC_READER;
}

// Retoma a leitura a partir de um registro salvo (tela Continuar).
static void enter_reader_from_record_source(const char *bookId, ReaderSource source, Screen back) {
    char sid[96] = {0}, st[256] = {0}, cl[64] = {0}, pb[512] = {0}, cv[640] = {0};
    int page = 1, pages = 1;
    if (!store_entry(bookId, sid, sizeof(sid), st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), cv, sizeof(cv), &page, &pages)) return;
    if (!pb[0]) return;
    if (is_cbz_base(pb)) {
        enter_local_reader_from_cbz(pb + 4, st, cl, back);
        return;
    }
    if (is_local_base(pb)) {
        const char *folder = is_local_base(pb) ? pb + 6 : "";
        enter_local_reader_from_folder(folder, st, cl, back);
        return;
    }
    if (is_local_id(bookId)) return;
    snprintf(curBookId, sizeof(curBookId), "%s", bookId);
    snprintf(curSeriesId, sizeof(curSeriesId), "%s", sid);
    snprintf(curSeriesTitle, sizeof(curSeriesTitle), "%s", st);
    snprintf(curSeriesCover, sizeof(curSeriesCover), "%s", cv);
    snprintf(curChapLabel, sizeof(curChapLabel), "%s", cl);
    snprintf(pageBase, sizeof(pageBase), "%s", pb);
    pageCount = pages < 1 ? 1 : pages;
    curPage = page; if (curPage > pageCount) curPage = pageCount; if (curPage < 1) curPage = 1;
    curChapIndex = -1;
    localPageN = 0;
    g_reader_source = (source == READER_SRC_OFFLINE || g_offline_mode) ? READER_SRC_OFFLINE : READER_SRC_REMOTE;
    if (sid[0] && g_reader_source == READER_SRC_REMOTE) {
        if (g_ser) { cJSON_Delete(g_ser); g_ser = NULL; }
        char url[512];
        snprintf(url, sizeof(url), "%s/switch/series/%s", g_server, sid);
        struct membuf r = {0};
        long code = net_request_timeout(url, "GET", NULL, g_token, &r, NULL, 8L, 20L);
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
    pageTex = NULL;
    pageTexPage = 0;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    g_reader_back = back;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    reader_show_overlay();
    screen = SC_READER;
}

static void enter_reader_from_record(const char *bookId) {
    enter_reader_from_record_source(bookId, g_offline_mode ? READER_SRC_OFFLINE : READER_SRC_REMOTE, SC_CONTINUE);
}

static void load_continue(void) {
    contN = store_recent(contIds, 60);
    {
        int out = 0;
        for (int i = 0; i < contN; i++) {
            int pages = 1;
            char pb[512] = {0};
            int keep = 1;
            store_entry(contIds[i], NULL, 0, NULL, 0, NULL, 0, pb, sizeof(pb), NULL, 0, NULL, &pages);
            if (is_cbz_base(pb)) {
                const char *path = pb + 4;
                keep = path[0] && file_exists(path) && !path_is_dir(path);
            } else if (is_local_id(contIds[i]) || is_local_base(pb)) {
                const char *folder = is_local_base(pb) ? pb + 6 : "";
                keep = folder[0] && path_is_dir(folder);
            } else if (g_offline_mode && offline_count_pages(contIds[i], pages) <= 0) {
                keep = 0;
            }
            if (!keep) continue;
            if (out != i) {
                memcpy(contIds[out], contIds[i], sizeof(contIds[out]));
                contIds[out][sizeof(contIds[out]) - 1] = '\0';
            }
            out++;
        }
        contN = out;
    }
    if (contSel >= contN) contSel = contN > 0 ? contN - 1 : 0;
    if (contSel < 0) contSel = 0;
    contScroll = 0;
}

static void format_bytes(unsigned long long bytes, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        snprintf(out, cap, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, cap, "%llu B", bytes);
    }
}

static void load_offline_manager(void) {
    char ids[60][96];
    int n = store_recent(ids, 60);
    offlineN = 0;
    for (int i = 0; i < n && offlineN < 60; i++) {
        int pages = 1;
        int saved;
        char st[256] = {0}, cl[64] = {0}, pb[512] = {0};
        store_entry(ids[i], NULL, 0, st, sizeof(st), cl, sizeof(cl), pb, sizeof(pb), NULL, 0, NULL, &pages);
        if (is_local_id(ids[i]) || is_local_base(pb) || is_cbz_base(pb)) continue;
        if (g_search[0] && !contains_ascii_ci(st, g_search) && !contains_ascii_ci(cl, g_search)) continue;
        if (pages < 1) pages = 1;
        saved = offline_count_pages(ids[i], pages);
        if (saved <= 0) continue;
        memcpy(offlineIds[offlineN], ids[i], sizeof(offlineIds[offlineN]));
        offlineIds[offlineN][sizeof(offlineIds[offlineN]) - 1] = '\0';
        offlinePages[offlineN] = pages;
        offlineSaved[offlineN] = saved;
        offlineBytes[offlineN] = offline_chapter_size(ids[i], pages);
        offlineN++;
    }
    g_offlineTotalBytes = dir_total_size(OFFLINE_DIR);   // total real no SD (inclui caps nao listados)
    if (offlineSel >= offlineN) offlineSel = offlineN > 0 ? offlineN - 1 : 0;
    if (offlineSel < 0) offlineSel = 0;
    offlineScroll = 0;
}

static void offline_delete_selected(void) {
    if (offlineN <= 0 || offlineSel < 0 || offlineSel >= offlineN) return;
    char st[256] = {0}, cl[64] = {0};
    char line[180];
    store_entry(offlineIds[offlineSel], NULL, 0, st, sizeof(st), cl, sizeof(cl),
                NULL, 0, NULL, 0, NULL, NULL);
    snprintf(line, sizeof(line), "%.28s  %s", st[0] ? st : "(serie)", cl);
    SDL_Color red = { 238, 92, 92, 255 };
    if (!modal_wait_loop("Apagar offline?", line, "Remove as paginas salvas deste capitulo.",
                         "Seu progresso de leitura fica guardado.", red,
                         "A/toque = apagar    B/+ = manter", 1)) {
        return;
    }
    int removed = offline_delete_chapter_files(offlineIds[offlineSel], offlinePages[offlineSel]);
    page_cache_clear_book(offlineIds[offlineSel]);
    load_offline_manager();
    snprintf(line, sizeof(line), "%d paginas removidas do SD.", removed);
    success_screen("Capitulo offline apagado.", line);
}

static void offline_delete_all(void) {
    char line[120], sz[40];
    SDL_Color red = { 238, 92, 92, 255 };
    format_bytes(g_offlineTotalBytes, sz, sizeof(sz));
    snprintf(line, sizeof(line), "Remove TODO o offline (%s) do SD.", sz);
    if (!modal_wait_loop("Apagar tudo offline?", line, "Seu progresso de leitura fica guardado.",
                         "Nao da pra desfazer.", red, "A/toque = apagar tudo    B/+ = manter", 1)) {
        return;
    }
    rmrf(OFFLINE_DIR);
    mkdir(OFFLINE_DIR, 0777);
    memset(g_offline_counts, 0, sizeof(g_offline_counts));   // zera o cache de contagem
    store_clear_series_offline_all();                        // tira os selos do catalogo
    load_offline_manager();
    success_screen("Offline apagado.", "Todo o conteudo offline foi removido do SD.");
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
    if (!pageTex || pageTexPage != curPage) return;
    if (curPage == pageCount && reader_has_next_chapter() && !next_prompt_cancelled && next_prompt_started == 0) {
        next_prompt_started = SDL_GetTicks();
    }
}

static void reader_open_page(int n, int atBottom) {
    (void)atBottom;
    if (n < 1) n = 1;
    if (n > pageCount) n = pageCount;
    if (pageTex && pageTexPage == curPage && n == curPage) {
        page_cache_prefetch_around();
        reader_start_next_prompt();
        return;
    }
    curPage = n;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    if (pageTexPage != curPage) reader_show_overlay();
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
    pageTex = NULL;
    pageTexPage = 0;
    reader_pending_view_reset = 1;
    next_prompt_started = 0;
    next_prompt_cancelled = 0;
    store_record(curBookId, curPage, curSeriesId, curSeriesTitle, curChapLabel, pageBase, curSeriesCover, pageCount);
    page_cache_request(curPage);
    page_cache_apply_current();
    page_cache_prefetch_around();
    reader_show_overlay();
}

static void reader_tick(void) {
    if (screen != SC_READER) return;
    page_cache_pump();
    page_cache_apply_current();
    page_cache_prefetch_around();
    if (!next_prompt_started || next_prompt_cancelled) return;
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
        text_draw_fit(gRen, title, left.x + left.w + 16, 12, maxW, COL_HEAD, 0);
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
    const char *catTitle = catalogFavorites ? "Favoritos" : current_area_label();
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
        if (g_offline_mode) draw_empty_state("Biblioteca online indisponivel", "Sem internet, use Continuar lendo para abrir o que ja foi salvo.");
        else if (catalogLoadFailed) draw_empty_state("Favoritos ainda nao conectados", "Atualize o Meruem web para liberar /switch/favorites.");
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
                draw_offline_badge(x + cardW - 52, y + 6, store_get_series_offline(json_str(s, "id", "")));
                char t[40];
                snprintf(t, sizeof(t), "%.16s", title);
                text_draw_fit(gRen, t, x, y + coverH + 5, cardW, idx == catSel ? COL_SEL : COL_SOFT, 0);
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
            snprintf(row, sizeof(row), "%.48s", title);
            text_draw_fit(gRen, row, 24, y + 22, LW() - 116, idx == catSel ? COL_SEL : COL_TEXT, 0);
            draw_offline_badge(LW() - 74, y + 16, store_get_series_offline(json_str(s, "id", "")));
        }
        shownStart = catScroll + 1;
        shownEnd = catScroll + vis;
        if (shownEnd > catCount) shownEnd = catCount;
        draw_scrollbar(catScroll, catCount, vis);
        draw_more_hint(catScroll, catCount, vis);
    }
    if (!catalogFavorites) draw_area_hint_line();
    draw_footer(catalogFavorites ? "B/Biblioteca: voltar    Favoritos vem do site Meruem    X: alternar visual" : NULL);
    btn_draw(btn_prev()); btn_draw(btn_area()); btn_draw(btn_search()); btn_draw(btn_view_mode()); btn_draw(btn_next());
}

static void render_settings(void) {
    draw_background();
    draw_topbar("Conta e ajustes", btn_library());
    SDL_SetRenderDrawColor(gRen, 22, 30, 46, 232);
    SDL_Rect box = { 28, LIST_Y + 12, LW() - 56, 356 };
    SDL_RenderFillRect(gRen, &box);
    SDL_SetRenderDrawColor(gRen, 92, 152, 76, 180);
    SDL_RenderDrawRect(gRen, &box);

    char line[600];
    int boxTextW = box.w - 48;
    text_draw_fit(gRen, "Perfil Meruem", box.x + 24, box.y + 18, boxTextW, COL_HEAD, 1);
    snprintf(line, sizeof(line), "Conta logada: %s", g_username[0] ? g_username : "(nao identificada)");
    text_draw_fit(gRen, line, box.x + 24, box.y + 62, boxTextW, COL_SEL, 0);
    snprintf(line, sizeof(line), "Servidor: %s", g_server[0] ? g_server : DEFAULT_SERVER);
    text_draw_fit(gRen, line, box.x + 24, box.y + 96, boxTextW, COL_SOFT, 0);
    snprintf(line, sizeof(line), "Versao do app: %s", APP_VERSION_STR);
    text_draw_fit(gRen, line, box.x + 24, box.y + 128, boxTextW, COL_DIM, 0);
    text_draw_fit(gRen, "Assinatura e limite diario:", box.x + 24, box.y + 166, boxTextW, COL_HEAD, 0);
    if (profileLoaded) {
        snprintf(line, sizeof(line), "Plano: %s", profileTier[0] ? profileTier : "free");
        text_draw_fit(gRen, line, box.x + 24, box.y + 204, boxTextW, COL_SEL, 0);
        text_draw_fit(gRen, profileAccess, box.x + 24, box.y + 236, boxTextW, COL_SOFT, 0);
        text_draw_fit(gRen, profileChapters, box.x + 24, box.y + 268, boxTextW, COL_DIM, 0);
        text_draw_fit(gRen, profileBooks, box.x + 24, box.y + 300, boxTextW, COL_DIM, 0);
        text_draw_fit(gRen, profileCounts, box.x + 24, box.y + 328, boxTextW, COL_DIM, 0);
    } else if (profileFailed) {
        text_draw_fit(gRen, "Nao foi possivel sincronizar /switch/me agora.", box.x + 24, box.y + 204, boxTextW, COL_DIM, 0);
        text_draw_fit(gRen, "Abra esta tela novamente depois.", box.x + 24, box.y + 238, boxTextW, COL_DIM, 0);
    } else {
        text_draw_fit(gRen, "Sincronizando conta com o Meruem web...", box.x + 24, box.y + 204, boxTextW, COL_DIM, 0);
    }
    SDL_Rect localBox = { 28, box.y + box.h + 12, LW() - 56, g_portrait ? 126 : 92 };
    SDL_SetRenderDrawColor(gRen, 22, 30, 46, 232);
    SDL_RenderFillRect(gRen, &localBox);
    SDL_SetRenderDrawColor(gRen, 96, 154, 232, 180);
    SDL_RenderDrawRect(gRen, &localBox);
    int localTextW = localBox.w - 48;
    text_draw_fit(gRen, "Leitura local", localBox.x + 24, localBox.y + 18, localTextW, COL_HEAD, 1);
    char shortPath[160];
    path_short(g_local_root, shortPath, sizeof(shortPath));
    snprintf(line, sizeof(line), "Pasta: %s", shortPath);
    text_draw_fit(gRen, line, localBox.x + 24, localBox.y + 52, localTextW, COL_SEL, 0);
    if (g_portrait) {
        text_draw_fit(gRen, "Padrao: sdmc:/Mangas", localBox.x + 24, localBox.y + 82, localTextW, COL_SOFT, 0);
        text_draw_fit(gRen, "CBZ ou pastas com imagens.", localBox.x + 24, localBox.y + 106, localTextW, COL_DIM, 0);
    }
    btn_draw(btn_switch_account());
    btn_draw(btn_pick_local());
    btn_draw(btn_default_local());
    draw_footer("A conta  X local  Y padrao  B voltar  ZL/ZR girar");
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
        text_draw_fit(gRen, row, 24, y + 8, LW() - 56, idx == chapSel ? COL_SEL : COL_TEXT, 0);
        text_draw_fit(gRen, meta, 24, y + 34, LW() - 56, COL_SOFT, 0);
    }
    draw_scrollbar(chapScroll, chapCount, vis);
    draw_footer(NULL);
    btn_draw(btn_download_all());
    {
        int soff = store_get_series_offline(curSeriesId);
        const char *hint = soff == 2 ? "Serie ja baixada (X = rebaixar)   B: voltar"
                         : soff == 1 ? "X = completar download   B: voltar"
                                     : "X = baixar tudo offline   B: voltar";
        text_draw_fit(gRen, hint, btn_download_all().x + btn_download_all().w + 16, LH() - 41,
                      LW() - (btn_download_all().x + btn_download_all().w + 32), COL_DIM, 0);
    }
    btn_draw(btn_up()); btn_draw(btn_down());
}

static void render_continue(void) {
    draw_background();
    g_cover_started_this_frame = 0;
    cover_cache_pump();
    Btn downloads = btn_downloads();
    Btn localBtn = btn_local_top();
    draw_topbar_reserved(g_offline_mode ? "Continuar lendo (offline)" : "Continuar lendo", btn_library(), localBtn.x);
    btn_draw(localBtn);
    btn_draw(downloads);
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
        int local = is_local_id(contIds[idx]) || is_local_base(pb) || is_cbz_base(pb);
        int off = local ? 0 : offline_count_pages(contIds[idx], pages);
        if (pct > 100) pct = 100;
        snprintf(row, sizeof(row), "%.34s", st[0] ? st : "(serie)");
        if (local) snprintf(meta, sizeof(meta), "%s  pagina %d/%d  Local", cl, page, pages);
        else if (off > 0) snprintf(meta, sizeof(meta), "%s  pagina %d/%d  offline %d/%d", cl, page, pages, off, pages);
        else         snprintf(meta, sizeof(meta), "%s  pagina %d/%d  %d%%", cl, page, pages, pct);
        SDL_Texture *cover = cover_texture_for_key(contIds[idx], cv);
        if (cover) draw_cover_texture(cover, 22, y + 7, 42, 60);
        else       draw_cover_placeholder(22, y + 7, 42, 60, st[0] ? st : "?");
        text_draw_fit(gRen, row, 78, y + 9, LW() - 100, idx == contSel ? COL_SEL : COL_TEXT, 0);
        text_draw_fit(gRen, meta, 78, y + 38, LW() - 100, COL_SOFT, 0);
    }
    draw_scrollbar(contScroll, contN, vis);
    draw_footer("A/toque: continuar    X: Baixados    Y: Local    B: acervo    ZL/ZR: girar");
}

static void render_offline_manager(void) {
    draw_background();
    Btn del = btn_delete_offline();
    char total[40], hd[80];
    format_bytes(g_offlineTotalBytes, total, sizeof(total));
    snprintf(hd, sizeof(hd), "Baixados  (%s no SD)", total);
    draw_topbar_reserved(hd, btn_back(), del.x);
    btn_draw(del);
    int vis = visible_rows();
    if (offlineN == 0) {
        draw_empty_state("Nada baixado ainda", "Abra um capitulo e use X/Offline para salvar no SD.");
    }
    for (int i = 0; i < vis; i++) {
        int idx = offlineScroll + i;
        if (idx >= offlineN) break;
        int y = LIST_Y + i * ROW_H;
        char st[256] = {0}, cl[64] = {0};
        char row[360];
        char meta[180];
        char size[40];
        int page = 1, pages = offlinePages[idx] > 0 ? offlinePages[idx] : 1;
        store_entry(offlineIds[idx], NULL, 0, st, sizeof(st), cl, sizeof(cl),
                    NULL, 0, NULL, 0, &page, &pages);
        format_bytes(offlineBytes[idx], size, sizeof(size));
        draw_row_shell(y, idx == offlineSel);
        snprintf(row, sizeof(row), "%.34s", st[0] ? st : "(serie)");
        snprintf(meta, sizeof(meta), "%s  offline %d/%d  %s", cl, offlineSaved[idx], pages, size);
        text_draw_fit(gRen, row, 24, y + 9, LW() - 56, idx == offlineSel ? COL_SEL : COL_TEXT, 0);
        text_draw_fit(gRen, meta, 24, y + 38, LW() - 56, COL_SOFT, 0);
    }
    draw_scrollbar(offlineScroll, offlineN, vis);
    draw_area_hint_line();
    draw_footer(NULL);
    btn_draw(btn_delete_all());
    btn_draw(btn_area());
    btn_draw(btn_search());
    text_draw_fit(gRen, "A abrir  X apagar  Y tudo  B voltar",
                  btn_search().x + btn_search().w + 16, LH() - 41,
                  LW() - (btn_search().x + btn_search().w + 32), COL_DIM, 0);
}

static void draw_area_hint_line(void) {
    char line[180];
    snprintf(line, sizeof(line), "Area atual: %s", current_area_label());
    text_draw_fit(gRen, line, 18, LH() - FOOTER_H - 26, LW() - 36, COL_HEAD, 0);
}

static void render_local_browser(void) {
    draw_background();
    char hd[220];
    snprintf(hd, sizeof(hd), "Local: %.38s", path_basename(g_local_cwd));
    draw_topbar(hd, btn_back());
    int vis = visible_rows();
    if (localLoadFailed) {
        draw_empty_state("Pasta local nao encontrada", "Entre em Conta e escolha outra pasta do SD.");
    } else if (localN == 0) {
        draw_empty_state("Nada local por aqui", localStatus[0] ? localStatus : "Use CBZ ou pastas com imagens.");
    }
    for (int i = 0; i < vis; i++) {
        int idx = localScroll + i;
        if (idx >= localN) break;
        int y = LIST_Y + i * ROW_H;
        LocalItem *it = &localItems[idx];
        char row[260], meta[220], size[40];
        draw_row_shell(y, idx == localSel);
        format_bytes(it->size, size, sizeof(size));
        snprintf(row, sizeof(row), "%.46s", it->name);
        if (it->isCbz) snprintf(meta, sizeof(meta), "CBZ  %d paginas  %s  A: ler", it->cbzPages, size);
        else if (it->imageCount > 0 && it->dirCount > 0) snprintf(meta, sizeof(meta), "%d imagens  %d pastas  %s", it->imageCount, it->dirCount, size);
        else if (it->imageCount > 0) snprintf(meta, sizeof(meta), "%d imagens  %s  A: ler", it->imageCount, size);
        else if (it->cbzCount > 0 && it->dirCount > 0) snprintf(meta, sizeof(meta), "%d CBZ  %d pastas  %s", it->cbzCount, it->dirCount, size);
        else if (it->cbzCount > 0) snprintf(meta, sizeof(meta), "%d CBZ  %s", it->cbzCount, size);
        else snprintf(meta, sizeof(meta), "%d pastas", it->dirCount);
        text_draw_fit(gRen, row, 24, y + 9, LW() - 56, idx == localSel ? COL_SEL : COL_TEXT, 0);
        text_draw_fit(gRen, meta, 24, y + 38, LW() - 56, COL_SOFT, 0);
    }
    draw_scrollbar(localScroll, localN, vis);
    draw_more_hint(localScroll, localN, vis);
    if (localStatus[0] && localN > 0) text_draw_fit(gRen, localStatus, 18, LH() - FOOTER_H - 50, LW() - 36, COL_DIM, 0);
    draw_area_hint_line();
    draw_footer(NULL);
    btn_draw(btn_area());
    btn_draw(btn_search());
    text_draw_fit(gRen, "A ler  Y pasta  X local  B voltar", btn_search().x + btn_search().w + 16, LH() - 41,
                  LW() - (btn_search().x + btn_search().w + 32), COL_DIM, 0);
}

static void render_local_picker(void) {
    draw_background();
    char hd[220];
    snprintf(hd, sizeof(hd), "Escolher pasta: %.34s", path_basename(g_picker_cwd));
    draw_topbar(hd, btn_back());
    int vis = visible_rows();
    if (localLoadFailed) {
        draw_empty_state("Nao consegui abrir esta pasta", localStatus[0] ? localStatus : "Use B para voltar.");
    } else if (localN == 0) {
        draw_empty_state("Sem pastas aqui", "Use X ou o botao abaixo para escolher esta pasta.");
    }
    for (int i = 0; i < vis; i++) {
        int idx = localScroll + i;
        if (idx >= localN) break;
        int y = LIST_Y + i * ROW_H;
        LocalItem *it = &localItems[idx];
        char row[260], meta[180], size[40];
        draw_row_shell(y, idx == localSel);
        format_bytes(it->size, size, sizeof(size));
        snprintf(row, sizeof(row), "%.48s", it->name);
        if (it->imageCount > 0 && it->cbzCount > 0) snprintf(meta, sizeof(meta), "%d imagens  %d CBZ  %d pastas", it->imageCount, it->cbzCount, it->dirCount);
        else if (it->imageCount > 0) snprintf(meta, sizeof(meta), "%d imagens  %d pastas  %s", it->imageCount, it->dirCount, size);
        else if (it->cbzCount > 0) snprintf(meta, sizeof(meta), "%d CBZ  %d pastas  %s", it->cbzCount, it->dirCount, size);
        else snprintf(meta, sizeof(meta), "%d pastas", it->dirCount);
        text_draw_fit(gRen, row, 24, y + 9, LW() - 56, idx == localSel ? COL_SEL : COL_TEXT, 0);
        text_draw_fit(gRen, meta, 24, y + 38, LW() - 56, COL_SOFT, 0);
    }
    draw_scrollbar(localScroll, localN, vis);
    draw_more_hint(localScroll, localN, vis);
    if (localStatus[0]) text_draw_fit(gRen, localStatus, 18, LH() - FOOTER_H - 50, LW() - 36, COL_DIM, 0);
    draw_footer("A entrar  X usar esta pasta  B voltar pasta");
    btn_draw(btn_use_folder());
    btn_draw(btn_parent_folder());
}

static void render_reader(void) {
    int currentVisible = pageTex && pageTexPage == curPage;
    int failed = !currentVisible && page_cache_current_failed();
    int waiting = !currentVisible && !failed;
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderClear(gRen);
    if (pageTex) {
        SDL_Rect dst;
        reader_page_rect(&dst);
        SDL_RenderCopy(gRen, pageTex, NULL, &dst);
        if (!currentVisible) {
            SDL_SetRenderDrawColor(gRen, 0, 0, 0, 150);
            SDL_Rect shade = { 0, 0, LW(), LH() };
            SDL_RenderFillRect(gRen, &shade);
        }
    }
    if (!currentVisible) {
        int dots = (SDL_GetTicks() / 350) % 4;
        char title[96];
        char line[180];
        SDL_Rect r = { LW()/2 - 270, LH()/2 - 82, 540, 164 };
        if (!pageTex) {
            SDL_SetRenderDrawColor(gRen, 10, 14, 24, 255);
            SDL_Rect bg = { 0, 0, LW(), LH() };
            SDL_RenderFillRect(gRen, &bg);
        }
        SDL_SetRenderDrawColor(gRen, 20, 24, 36, 238);
        SDL_RenderFillRect(gRen, &r);
        SDL_SetRenderDrawColor(gRen, failed ? 238 : 96, failed ? 92 : 154, failed ? 92 : 232, 235);
        SDL_RenderDrawRect(gRen, &r);
        if (failed) {
            if (g_reader_source == READER_SRC_LOCAL) {
                snprintf(title, sizeof(title), "Arquivo local invalido");
                snprintf(line, sizeof(line), "Imagem corrompida, grande demais ou removida do SD.");
            } else {
                snprintf(title, sizeof(title), "%s", (g_offline_mode || g_reader_source == READER_SRC_OFFLINE) ? "Pagina nao salva offline" : "Nao consegui carregar a pagina");
                snprintf(line, sizeof(line), "%s", (g_offline_mode || g_reader_source == READER_SRC_OFFLINE) ? "Baixe este capitulo antes de sair da internet." : "Tente avancar, voltar ou abrir de novo depois.");
            }
        } else {
            snprintf(title, sizeof(title), "Carregando pagina %d/%d%.*s", curPage, pageCount, dots, "...");
            snprintf(line, sizeof(line), "%s", page_cache_current_loading() ? "Preparando a imagem em segundo plano." : "Aguardando vaga no cache de paginas.");
        }
        text_draw_fit(gRen, title, r.x + 28, r.y + 30, r.w - 56, COL_HEAD, 1);
        text_draw_fit(gRen, line, r.x + 28, r.y + 82, r.w - 56, COL_SOFT, 0);
        if (waiting && pageTex && pageTexPage > 0) {
            char hint[120];
            snprintf(hint, sizeof(hint), "Mantive a pagina %d na tela enquanto isso.", pageTexPage);
            text_draw_fit(gRen, hint, r.x + 28, r.y + 118, r.w - 56, COL_DIM, 0);
        }
    }
    int overlay = SDL_GetTicks() < reader_overlay_until;
    if (overlay) {
        SDL_SetRenderDrawColor(gRen, 0, 0, 0, 175);
        SDL_Rect bar = { 0, 0, LW(), TB };
        SDL_RenderFillRect(gRen, &bar);
        btn_draw(btn_back());
        if (g_reader_source == READER_SRC_REMOTE) btn_draw(btn_offline());
        btn_draw(btn_rotate());
        char pc[160];
        int maxPcW = (g_reader_source == READER_SRC_REMOTE ? btn_offline().x : btn_rotate().x) - (btn_back().x + btn_back().w + 24);
        snprintf(pc, sizeof(pc), "%s  %d/%d", curChapLabel, curPage, pageCount);
        text_draw_fit(gRen, pc, btn_back().x + btn_back().w + 14, 12, maxPcW, COL_SEL, 0);
        (void)maxPcW;
        if (g_reader_source == READER_SRC_REMOTE) {
            int off = offline_count_pages(curBookId, pageCount);
            char st[80];
            snprintf(st, sizeof(st), "X: offline  %d/%d no SD", off, pageCount);
            text_draw_fit(gRen, st, 18, LH() - 48, LW() - 36, off == pageCount ? COL_HEAD : COL_DIM, 0);
        } else {
            text_draw_fit(gRen, "Local: lendo direto do SD", 18, LH() - 48, LW() - 36, COL_DIM, 0);
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
        text_draw_fit(gRen, msg, box.x + 32, box.y + 24, box.w - 64, COL_HEAD, 1);
        cJSON *next = chap_at_real(curChapIndex + 1);
        snprintf(msg, sizeof(msg), "Capitulo #%s", json_str(next, "number", "?"));
        text_draw_fit(gRen, msg, box.x + 32, box.y + 62, box.w - 64, COL_SEL, 0);
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

static int local_can_go_parent(const char *cwd, const char *root) {
    if (!cwd || !root) return 0;
    if (!strcmp(cwd, "sdmc:/")) return 0;
    return strcmp(cwd, root) != 0;
}

static void local_go_parent(void) {
    char parent[LOCAL_PATH_MAX];
    if (!local_can_go_parent(g_local_cwd, g_local_root)) return;
    path_parent(g_local_cwd, parent, sizeof(parent));
    load_local_browser(parent);
}

static void local_open_selected(int enterOnly) {
    if (localN <= 0 || localSel < 0 || localSel >= localN) return;
    LocalItem *it = &localItems[localSel];
    if (it->isCbz) {
        char parent[LOCAL_PATH_MAX];
        path_parent(it->path, parent, sizeof(parent));
        enter_local_reader_from_cbz(it->path, path_basename(parent)[0] ? path_basename(parent) : "Local", it->name, SC_LOCAL);
        return;
    }
    if (!enterOnly && it->imageCount > 0) {
        char parent[LOCAL_PATH_MAX];
        const char *seriesTitle;
        const char *chapLabel = it->isReadCurrent ? path_basename(g_local_cwd) : it->name;
        path_parent(it->path, parent, sizeof(parent));
        seriesTitle = strcmp(parent, g_local_root) == 0 ? path_basename(it->path) : path_basename(parent);
        enter_local_reader_from_folder(it->path, seriesTitle[0] ? seriesTitle : "Local", chapLabel, SC_LOCAL);
        return;
    }
    if (!it->isReadCurrent) load_local_browser(it->path);
}

static void picker_go_parent(void) {
    char parent[LOCAL_PATH_MAX];
    if (!strcmp(g_picker_cwd, "sdmc:/")) {
        load_profile();
        screen = SC_SETTINGS;
        return;
    }
    path_parent(g_picker_cwd, parent, sizeof(parent));
    load_local_picker(parent);
}

static void picker_use_current(void) {
    if (!path_is_dir(g_picker_cwd)) {
        message_screen("Pasta invalida.", "Escolha outra pasta no SD.");
        return;
    }
    local_root_save(g_picker_cwd);
    success_screen("Pasta local definida.", "Abra a area Local para ler.");
    load_local_browser(g_local_root);
    areaIdx = AREA_LOCAL;
    screen = SC_LOCAL;
}

static void picker_enter_selected(void) {
    if (localN <= 0 || localSel < 0 || localSel >= localN) return;
    load_local_picker(localItems[localSel].path);
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
        if (btn_hit(btn_area(), lx, ly)) { g_search[0] = '\0'; switch_area_next(); return; }
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
        if (btn_hit(btn_pick_local(), lx, ly)) {
            snprintf(g_picker_cwd, sizeof(g_picker_cwd), "%s", g_local_root[0] ? g_local_root : "sdmc:/");
            if (!path_is_dir(g_picker_cwd)) snprintf(g_picker_cwd, sizeof(g_picker_cwd), "sdmc:/");
            load_local_picker(g_picker_cwd);
            screen = SC_LOCAL_PICKER;
            return;
        }
        if (btn_hit(btn_default_local(), lx, ly)) {
            mkdir(LOCAL_ROOT_DEFAULT, 0777);
            local_root_save(LOCAL_ROOT_DEFAULT);
            success_screen("Pasta padrao definida.", "Coloque seus CBZ em sdmc:/Mangas.");
            return;
        }
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
        if (btn_hit(btn_download_all(), lx, ly)) { offline_download_all_chapters(); return; }
        if (btn_hit(btn_chap_order(), lx, ly)) { chapReversed = !chapReversed; chapSel = 0; chapScroll = 0; return; }
        if (btn_hit(btn_up(), lx, ly))   { chapScroll -= visible_rows(); if (chapScroll < 0) chapScroll = 0; return; }
        if (btn_hit(btn_down(), lx, ly)) {
            int maxs = chapCount - visible_rows();
            if (maxs < 0) maxs = 0;
            chapScroll += visible_rows();
            if (chapScroll > maxs) chapScroll = maxs;
            return;
        }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = chapScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < chapCount) { chapSel = idx; enter_reader(idx); }
        }
    } else if (screen == SC_CONTINUE) {
        if (btn_hit(btn_library(), lx, ly)) { screen = SC_SERIES; return; }
        if (btn_hit(btn_downloads(), lx, ly)) { areaIdx = AREA_DOWNLOADED; g_offline_back = SC_CONTINUE; load_offline_manager(); screen = SC_OFFLINE; return; }
        if (btn_hit(btn_local_top(), lx, ly)) { areaIdx = AREA_LOCAL; g_local_back = SC_CONTINUE; load_local_browser(g_local_root); screen = SC_LOCAL; return; }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = contScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < contN) { contSel = idx; enter_reader_from_record(contIds[idx]); }
        }
    } else if (screen == SC_OFFLINE) {
        if (btn_hit(btn_back(), lx, ly)) { if (g_offline_back == SC_CONTINUE) load_continue(); screen = g_offline_back; return; }
        if (btn_hit(btn_area(), lx, ly)) { g_search[0] = '\0'; switch_area_next(); return; }
        if (btn_hit(btn_search(), lx, ly)) {
            char term[96] = {0};
            int rs = prompt_text("Buscar nos baixados (vazio = limpar)", term, sizeof(term), 0);
            if (rs != -1) { snprintf(g_search, sizeof(g_search), "%s", rs == 0 ? term : ""); load_offline_manager(); }
            return;
        }
        if (btn_hit(btn_delete_all(), lx, ly)) { offline_delete_all(); return; }
        if (btn_hit(btn_delete_offline(), lx, ly)) { offline_delete_selected(); return; }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = offlineScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < offlineN) { offlineSel = idx; enter_reader_from_record_source(offlineIds[idx], READER_SRC_OFFLINE, SC_OFFLINE); }
        }
    } else if (screen == SC_LOCAL) {
        if (btn_hit(btn_back(), lx, ly)) { if (local_can_go_parent(g_local_cwd, g_local_root)) local_go_parent(); else { if (g_local_back == SC_CONTINUE) load_continue(); screen = g_local_back; } return; }
        if (btn_hit(btn_area(), lx, ly)) { g_search[0] = '\0'; switch_area_next(); return; }
        if (btn_hit(btn_search(), lx, ly)) {
            char term[96] = {0};
            int rs = prompt_text("Buscar nesta pasta local (vazio = limpar)", term, sizeof(term), 0);
            if (rs != -1) { snprintf(g_search, sizeof(g_search), "%s", rs == 0 ? term : ""); load_local_browser(g_local_cwd); }
            return;
        }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = localScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < localN) { localSel = idx; local_open_selected(0); }
        }
    } else if (screen == SC_LOCAL_PICKER) {
        if (btn_hit(btn_back(), lx, ly) || btn_hit(btn_parent_folder(), lx, ly)) { picker_go_parent(); return; }
        if (btn_hit(btn_use_folder(), lx, ly)) { picker_use_current(); return; }
        if (ly >= LIST_Y && ly < LIST_Y + visible_rows() * ROW_H) {
            int idx = localScroll + (ly - LIST_Y) / ROW_H;
            if (idx >= 0 && idx < localN) { localSel = idx; picker_enter_selected(); }
        }
    } else { // SC_READER
        if (next_prompt_started && !next_prompt_cancelled && reader_has_next_chapter()) {
            if (btn_hit(btn_next_open(), lx, ly)) { reader_open_next_chapter_now(); return; }
            if (btn_hit(btn_next_cancel(), lx, ly)) { next_prompt_cancelled = 1; next_prompt_started = 0; reader_show_overlay(); return; }
        }
        int overlay = SDL_GetTicks() < reader_overlay_until;
        if (overlay && ly < TB) {
            if (btn_hit(btn_back(), lx, ly)) { pageTex = NULL; pageTexPage = 0; screen = g_reader_back; return; }
            if (btn_hit(btn_offline(), lx, ly)) { if (g_reader_source == READER_SRC_REMOTE) offline_download_current_chapter(); return; }
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

// Toque duplo: alterna entre 1x e ~2.2x, centralizando no ponto tocado.
static void reader_double_tap_zoom(int lx, int ly) {
    if (rd_zoom > READER_ZOOM_MIN + 0.01f) {
        rd_zoom = READER_ZOOM_MIN;
        rd_pan_x = rd_pan_y = 0.0f;
    } else {
        float target = 2.2f;
        if (target > READER_ZOOM_MAX) target = READER_ZOOM_MAX;
        rd_zoom = target;
        rd_pan_x = (lx - LW() / 2.0f) * (1.0f - target);
        rd_pan_y = (ly - LH() / 2.0f) * (1.0f - target);
    }
    reader_clamp_pan();
    reader_show_overlay();
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
                Uint32 now = SDL_GetTicks();
                int zoomed = rd_zoom > READER_ZOOM_MIN + 0.01f;
                int centerZone = (lx > LW() * 34 / 100 && lx < LW() * 66 / 100);
                int isDouble = (now - rd_lastTapTime < 320) &&
                               (abs(lx - rd_lastTapX) < 80) && (abs(ly - rd_lastTapY) < 80);
                if (isDouble && (zoomed || centerZone)) {
                    rd_lastTapTime = 0;
                    reader_double_tap_zoom(lx, ly);
                } else {
                    rd_lastTapTime = now; rd_lastTapX = lx; rd_lastTapY = ly;
                    handle_tap(lx, ly);
                }
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
    else if (screen == SC_OFFLINE) { scroll = &offlineScroll; count = offlineN; }
    else if (screen == SC_LOCAL || screen == SC_LOCAL_PICKER) { scroll = &localScroll; count = localN; }
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
    else if (screen == SC_OFFLINE)  { scroll = &offlineScroll; count = offlineN; }
    else if (screen == SC_LOCAL || screen == SC_LOCAL_PICKER)  { scroll = &localScroll; count = localN; }
    else if (screen == SC_CHAPTERS)  { scroll = &chapScroll; count = chapCount; }
    else return;
    dragAccum += (float)(curLY - lastLY);
    while (dragAccum >= ROW_H)  { (*scroll)--; dragAccum -= ROW_H; }
    while (dragAccum <= -ROW_H) { (*scroll)++; dragAccum += ROW_H; }
    int maxs = count - visible_rows(); if (maxs < 0) maxs = 0;
    if (*scroll < 0) *scroll = 0;
    if (*scroll > maxs) *scroll = maxs;
}

// Ha alguma capa carregando em segundo plano?
static int covers_busy(void) {
    for (int i = 0; i < COVER_CACHE_MAX; i++)
        if (g_cover_cache[i].loading || g_cover_cache[i].ready) return 1;
    return 0;
}

// Precisa redesenhar (algo se mexendo)? Senao, o loop dorme mais e poupa bateria.
static int app_is_animating(void) {
    Uint32 now = SDL_GetTicks();
    if (now - g_last_activity < 900) return 1;     // acabou de interagir: mantem fluido
    if (covers_busy()) return 1;                   // capas chegando em background
    if (screen == SC_READER) {
        if (!(pageTex && pageTexPage == curPage)) return 1;          // pagina carregando
        if (now < reader_overlay_until) return 1;                    // overlay some sozinho
        if (next_prompt_started && !next_prompt_cancelled) return 1; // contagem do proximo cap
    }
    return 0;
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
        local_root_load_config();
        if (configure_server() && authenticate()) {
            if (!g_offline_mode && maybe_install_update()) goto cleanup;
            load_continue();
            if (g_offline_mode) {
                load_offline_manager();
                if (contN > 0) screen = SC_CONTINUE;
                else if (offlineN > 0) { areaIdx = AREA_DOWNLOADED; g_offline_back = SC_CONTINUE; screen = SC_OFFLINE; }
                else if (local_root_has_visible_content(g_local_root)) {
                    areaIdx = AREA_LOCAL;
                    g_local_back = SC_CONTINUE;
                    load_local_browser(g_local_root);
                    screen = SC_LOCAL;
                } else {
                    screen = SC_CONTINUE;
                }
            } else {
                load_catalog();
                screen = contN > 0 ? SC_CONTINUE : SC_SERIES;
            }
            int running = 1;
            running_ptr = &running;
            SDL_Event e;
            int firstFrame = 1;
            Uint32 lastRender = 0;
            while (running && appletMainLoop()) {
                int hadEvent = 0;
                while (SDL_PollEvent(&e)) {
                    hadEvent = 1;
                    if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION ||
                        e.type == SDL_FINGERUP || e.type == SDL_JOYBUTTONDOWN) g_last_activity = SDL_GetTicks();
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
                            else if (b == JOY_Y) { g_search[0] = '\0'; switch_area_next(); }
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
                            else if (b == JOY_Y) { chapReversed = !chapReversed; chapSel = 0; chapScroll = 0; }
                            else if (b == JOY_X) { offline_download_all_chapters(); }
                            else if (b == JOY_A && chapCount > 0) enter_reader(chapSel);
                            else if (b == JOY_B) screen = g_chapters_back;
                            if (chapSel < chapScroll) chapScroll = chapSel;
                            if (chapSel >= chapScroll + visible_rows()) chapScroll = chapSel - visible_rows() + 1;
                        } else if (screen == SC_CONTINUE) {
                            if (b == JOY_UP && contSel > 0) contSel--;
                            else if (b == JOY_DOWN && contSel < contN - 1) contSel++;
                            else if (b == JOY_A && contN > 0) enter_reader_from_record(contIds[contSel]);
                            else if (b == JOY_X) { areaIdx = AREA_DOWNLOADED; g_offline_back = SC_CONTINUE; load_offline_manager(); screen = SC_OFFLINE; }
                            else if (b == JOY_Y) { areaIdx = AREA_LOCAL; g_local_back = SC_CONTINUE; load_local_browser(g_local_root); screen = SC_LOCAL; }
                            else if (b == JOY_B) screen = SC_SERIES;
                            if (contSel < contScroll) contScroll = contSel;
                            if (contSel >= contScroll + visible_rows()) contScroll = contSel - visible_rows() + 1;
                        } else if (screen == SC_OFFLINE) {
                            if (b == JOY_UP && offlineSel > 0) offlineSel--;
                            else if (b == JOY_DOWN && offlineSel < offlineN - 1) offlineSel++;
                            else if (b == JOY_A && offlineN > 0) enter_reader_from_record_source(offlineIds[offlineSel], READER_SRC_OFFLINE, SC_OFFLINE);
                            else if (b == JOY_X && offlineN > 0) offline_delete_selected();
                            else if (b == JOY_Y) offline_delete_all();
                            else if (b == JOY_L) { offlineSel -= visible_rows(); if (offlineSel < 0) offlineSel = 0; }
                            else if (b == JOY_R) { offlineSel += visible_rows(); if (offlineSel > offlineN - 1) offlineSel = offlineN - 1; }
                            else if (b == JOY_MINUS) { g_search[0] = '\0'; load_offline_manager(); }
                            else if (b == JOY_B) { if (g_offline_back == SC_CONTINUE) load_continue(); screen = g_offline_back; }
                            if (offlineN <= 0) offlineSel = 0;
                            else if (offlineSel > offlineN - 1) offlineSel = offlineN - 1;
                            if (offlineSel < 0) offlineSel = 0;
                            if (offlineSel < offlineScroll) offlineScroll = offlineSel;
                            if (offlineSel >= offlineScroll + visible_rows()) offlineScroll = offlineSel - visible_rows() + 1;
                        } else if (screen == SC_LOCAL) {
                            if (b == JOY_UP && localSel > 0) localSel--;
                            else if (b == JOY_DOWN && localSel < localN - 1) localSel++;
                            else if (b == JOY_L) { localSel -= visible_rows(); if (localSel < 0) localSel = 0; }
                            else if (b == JOY_R) { localSel += visible_rows(); if (localSel > localN - 1) localSel = localN - 1; }
                            else if (b == JOY_A && localN > 0) local_open_selected(0);
                            else if (b == JOY_Y && localN > 0) local_open_selected(1);
                            else if (b == JOY_X) { snprintf(g_picker_cwd, sizeof(g_picker_cwd), "%s", g_local_root); load_local_picker(g_picker_cwd); screen = SC_LOCAL_PICKER; }
                            else if (b == JOY_MINUS) { g_search[0] = '\0'; load_local_browser(g_local_cwd); }
                            else if (b == JOY_B) { if (local_can_go_parent(g_local_cwd, g_local_root)) local_go_parent(); else { if (g_local_back == SC_CONTINUE) load_continue(); screen = g_local_back; } }
                            if (localN <= 0) localSel = 0;
                            else if (localSel > localN - 1) localSel = localN - 1;
                            if (localSel < 0) localSel = 0;
                            if (localSel < localScroll) localScroll = localSel;
                            if (localSel >= localScroll + visible_rows()) localScroll = localSel - visible_rows() + 1;
                        } else if (screen == SC_LOCAL_PICKER) {
                            if (b == JOY_UP && localSel > 0) localSel--;
                            else if (b == JOY_DOWN && localSel < localN - 1) localSel++;
                            else if (b == JOY_L) { localSel -= visible_rows(); if (localSel < 0) localSel = 0; }
                            else if (b == JOY_R) { localSel += visible_rows(); if (localSel > localN - 1) localSel = localN - 1; }
                            else if (b == JOY_A && localN > 0) picker_enter_selected();
                            else if (b == JOY_X) picker_use_current();
                            else if (b == JOY_B) picker_go_parent();
                            if (localN <= 0) localSel = 0;
                            else if (localSel > localN - 1) localSel = localN - 1;
                            if (localSel < 0) localSel = 0;
                            if (localSel < localScroll) localScroll = localSel;
                            if (localSel >= localScroll + visible_rows()) localScroll = localSel - visible_rows() + 1;
                        } else if (screen == SC_SETTINGS) {
                            if (b == JOY_A) {
                                store_clear_token();
                                store_clear_user();
                                if (g_token) { free(g_token); g_token = NULL; }
                                g_username[0] = '\0';
                                if (authenticate()) { catPage = 0; catSel = 0; load_catalog(); screen = SC_SERIES; }
                            } else if (b == JOY_X) {
                                snprintf(g_picker_cwd, sizeof(g_picker_cwd), "%s", g_local_root[0] ? g_local_root : "sdmc:/");
                                if (!path_is_dir(g_picker_cwd)) snprintf(g_picker_cwd, sizeof(g_picker_cwd), "sdmc:/");
                                load_local_picker(g_picker_cwd);
                                screen = SC_LOCAL_PICKER;
                            } else if (b == JOY_Y) {
                                mkdir(LOCAL_ROOT_DEFAULT, 0777);
                                local_root_save(LOCAL_ROOT_DEFAULT);
                                success_screen("Pasta padrao definida.", "Coloque seus CBZ em sdmc:/Mangas.");
                            } else if (b == JOY_B) screen = catalogFavorites ? SC_FAVORITES : SC_SERIES;
                        } else {
                            if (b == JOY_A && next_prompt_started && !next_prompt_cancelled && reader_has_next_chapter()) reader_open_next_chapter_now();
                            else if (b == JOY_X && next_prompt_started) { next_prompt_cancelled = 1; next_prompt_started = 0; reader_show_overlay(); }
                            else if (b == JOY_X && g_reader_source == READER_SRC_REMOTE) offline_download_current_chapter();
                            else if (b == JOY_R || b == JOY_DRIGHT || b == JOY_DOWN || b == JOY_A) reader_advance(1);
                            else if (b == JOY_L || b == JOY_DLEFT || b == JOY_UP) reader_advance(-1);
                            else if (b == JOY_B) { pageTex = NULL; pageTexPage = 0; screen = g_reader_back; }
                        }
                    }
                }

                reader_tick();
                int animating = app_is_animating();
                Uint32 nowMs = SDL_GetTicks();
                if (hadEvent || animating || firstFrame || (nowMs - lastRender >= 1000)) {
                    firstFrame = 0;
                    lastRender = nowMs;
                    begin_frame();
                    if (is_catalog_screen()) render_series();
                    else if (screen == SC_CONTINUE) render_continue();
                    else if (screen == SC_OFFLINE) render_offline_manager();
                    else if (screen == SC_LOCAL) render_local_browser();
                    else if (screen == SC_LOCAL_PICKER) render_local_picker();
                    else if (screen == SC_SETTINGS) render_settings();
                    else if (screen == SC_CHAPTERS) render_chapters();
                    else render_reader();
                    end_frame();
                }
                SDL_Delay(animating ? 16 : 80);   // parado, dorme mais (poupa bateria)
            }
            store_flush();
        }
    }

cleanup:
    page_cache_clear();
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
