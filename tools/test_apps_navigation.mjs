import {readFileSync, writeFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';

const source = readFileSync('source/main.c', 'utf8');
function extract(name) {
  const start = source.indexOf('static ', source.lastIndexOf('\n', source.indexOf(` ${name}(`)));
  assert(start >= 0, name);
  const open = source.indexOf('{', start);
  let depth = 0, quote = '', comment = '';
  for (let i = open; i < source.length; i++) {
    const c = source[i], next = source[i + 1];
    if (comment === '//') { if (c === '\n') comment = ''; continue; }
    if (comment === '/*') { if (c === '*' && next === '/') { comment = ''; i++; } continue; }
    if (quote) { if (c === '\\') i++; else if (c === quote) quote = ''; continue; }
    if (c === '/' && (next === '/' || next === '*')) { comment = c + next; i++; continue; }
    if (c === '"' || c === "'") { quote = c; continue; }
    if (c === '{') depth++;
    if (c === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`Unclosed function ${name}`);
}
assert.match(source, /if \(act == 6\) \{ other_apps_screen\(\); continue; \}/);
assert.match(source, /if \(btn_hit\(btn_other_apps\(\), lx, ly\)\) \{ other_apps_screen\(\); return; \}/);
const prefix = `
#define _GNU_SOURCE
#include "other_apps.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned int Uint32;
typedef struct {int x,y,w,h; const char *label;} Btn;
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct {int type; struct {int button;} jbutton; struct {float x,y;} tfinger;} SDL_Event;
#define SDL_QUIT 1
#define SDL_FINGERUP 2
#define SDL_JOYBUTTONDOWN 3
#define JOY_A 0
#define JOY_B 1
#define JOY_X 2
#define JOY_Y 3
#define JOY_L 6
#define JOY_R 7
#define JOY_PLUS 10
#define JOY_MINUS 11
#define COL_HEAD 0
#define COL_TEXT 0
#define COL_SOFT 0
#define COL_SEL 0
#define COL_DIM 0
static void *gRen;
static int portrait, next_event, polled, loops, checks, installs, check_rc, install_rc;
static Uint32 ticks;
static SDL_Event events[32];
static char *g_token, g_username[96];
static int g_offline_mode, account_calls;
static int store_load_token(char *out,size_t n) {(void)out;(void)n;return 0;}
static int store_load_user(char *out,size_t n) {(void)out;(void)n;return 0;}
static int token_status(const char *s) {(void)s;account_calls++;return 0;}
static void store_clear_token(void) {account_calls++;}
static void store_clear_user(void) {account_calls++;}
static void store_save_token(const char *s) {(void)s;account_calls++;}
static void store_save_user(const char *s) {(void)s;account_calls++;}
static void present_color(int a,int b,int c) {(void)a;(void)b;(void)c;}
static int info_screen(const char *a,const char *b) {(void)a;(void)b;return 1;}
static int message_screen(const char *a,const char *b) {(void)a;(void)b;return 1;}
static int qr_login_screen(void) {account_calls++;return 0;}
static int switch_access_screen(void) {account_calls++;return 0;}
static int prompt_text(const char *s,char *out,size_t n,int secret) {(void)s;(void)out;(void)n;(void)secret;account_calls++;return -1;}
static char *login_request(const char *u,const char *p) {(void)u;(void)p;account_calls++;return NULL;}
static int LW(void) {return portrait ? 720 : 1280;}
static int LH(void) {return portrait ? 1280 : 720;}
static int appletMainLoop(void) {assert(++loops < 200); polled = 0; return 1;}
static Uint32 SDL_GetTicks(void) {ticks += 500; return ticks;}
static void SDL_Delay(int ms) {(void)ms;}
static int SDL_PollEvent(SDL_Event *e) {if (polled) return 0; polled=1; *e=events[next_event++]; assert(next_event<32); return e->type != 0;}
static void screen_to_logical(float x,float y,int *lx,int *ly) {*lx=(int)x; *ly=(int)y;}
static int btn_hit(Btn b,int x,int y) {return x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h;}
static void bounds(int x,int y,int w,int h) {assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=LW() && y+h<=LH());}
static void btn_draw(Btn b) {bounds(b.x,b.y,b.w,b.h);}
static void begin_frame(void) {}
static void end_frame(void) {}
static void draw_background(void) {}
static void draw_footer(const char *s) {(void)s;}
static void text_draw_fit(void *r,const char *s,int x,int y,int w,int color,int bold) {(void)r;(void)s;(void)color;(void)bold;bounds(x,y,w,24);}
static void SDL_SetRenderDrawColor(void *r,int a,int b,int c,int d) {(void)r;(void)a;(void)b;(void)c;(void)d;}
static void SDL_RenderFillRect(void *r,SDL_Rect *b) {(void)r;bounds(b->x,b->y,b->w,b->h);}
static void SDL_RenderDrawRect(void *r,SDL_Rect *b) {(void)r;bounds(b->x,b->y,b->w,b->h);}
int other_app_check(struct other_app_release *r,other_app_progress p,void *u,char *err,size_t cap) {(void)p;(void)u;checks++;snprintf(err,cap,"mock check failure");strcpy(r->version,"v1");r->size=256;return check_rc;}
int other_app_install(const struct other_app_release *r,other_app_progress p,void *u,char *err,size_t cap) {(void)r;(void)p;(void)u;installs++;snprintf(err,cap,"mock install failure");return install_rc;}
`;
const state = source.match(/struct apps_progress_state \{[^}]+\};/)[0];
const functions = ['apps_download_progress','apps_prompt','other_apps_screen','login_welcome_screen','authenticate'].map(extract).join('\n');
const scenarios = `
static SDL_Event joy(int b) {SDL_Event e={0};e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=b;return e;}
static SDL_Event touch(int x,int y) {SDL_Event e={0};e.type=SDL_FINGERUP;e.tfinger.x=x;e.tfinger.y=y;return e;}
static void reset(void) {memset(events,0,sizeof(events));next_event=polled=loops=checks=installs=check_rc=install_rc=account_calls=0;ticks=0;}
int main(void) {
for (portrait=0;portrait<=1;portrait++) {
 reset();events[0]=joy(JOY_MINUS);assert(login_welcome_screen(0,"")==6);
 reset();events[0]=touch(60,240);assert(login_welcome_screen(0,"")==6);
 reset();events[1]=joy(JOY_B);assert(login_welcome_screen(1,"test")==0); /* render/validate bounds */
 reset();events[0]=joy(JOY_A);events[1]=joy(JOY_B);events[2]=joy(JOY_B);other_apps_screen();assert(checks==1 && installs==0);
 reset();events[0]=joy(JOY_A);events[1]=joy(JOY_A);events[2]=joy(JOY_B);events[3]=joy(JOY_B);other_apps_screen();assert(installs==1);
 reset();check_rc=-1;events[0]=joy(JOY_A);events[1]=joy(JOY_B);events[2]=joy(JOY_B);other_apps_screen();assert(checks==1 && installs==0);
 reset();check_rc=1;events[0]=joy(JOY_A);events[1]=joy(JOY_B);other_apps_screen();assert(checks==1 && installs==0);
 reset();install_rc=-1;events[0]=joy(JOY_A);events[1]=joy(JOY_A);events[2]=joy(JOY_B);events[3]=joy(JOY_B);other_apps_screen();assert(installs==1);
 reset();install_rc=1;events[0]=joy(JOY_A);events[1]=joy(JOY_A);events[2]=joy(JOY_B);other_apps_screen();assert(installs==1);
 reset();events[0]=joy(JOY_A);events[1]=touch(10,10);events[2]=touch(60,470);events[3]=joy(JOY_B);other_apps_screen();assert(installs==0);
 reset();events[0]=touch(60,400);events[1]=touch(60,400);events[2]=touch(60,470);events[3]=touch(60,470);other_apps_screen();assert(installs==1);
 reset();events[0]=joy(JOY_A);events[1]=joy(JOY_B);events[2]=joy(JOY_A);events[3]=joy(JOY_A);events[4]=joy(JOY_B);events[5]=joy(JOY_B);other_apps_screen();assert(checks==2 && installs==1);
 reset();events[0]=joy(JOY_B);struct apps_progress_state s={0,{36,380,LW()-72,54,"Cancelar"},APP_CHECK};assert(apps_download_progress(APP_DOWNLOAD,0,256,&s)==1);
 reset();events[0]=touch(60,400);assert(apps_download_progress(APP_VERIFY,128,256,&s)==1);
 reset();assert(!apps_download_progress(APP_CHECK,0,0,&s));assert(!apps_download_progress(APP_INSTALL,256,256,&s));
 reset();events[0]=joy(JOY_MINUS);events[1]=joy(JOY_A);events[2]=joy(JOY_A);events[3]=joy(JOY_B);events[4]=joy(JOY_B);events[5]=joy(JOY_B);assert(authenticate()==0);assert(installs==1 && account_calls==0 && g_token==NULL);
 reset();check_rc=-1;events[0]=joy(JOY_MINUS);events[1]=joy(JOY_A);events[2]=joy(JOY_B);events[3]=joy(JOY_B);events[4]=joy(JOY_B);assert(authenticate()==0);assert(installs==0 && account_calls==0 && g_token==NULL);
}
puts("PASS: 34 navigation simulations using actual UI/auth functions, control/touch, no-login (zero account calls), confirm/back, failure/cancel/retry, portrait/TV bounds");
return 0;
}
`;
writeFileSync('build/test_apps_navigation.c', prefix + state + '\n' + functions + scenarios);
execFileSync('C:/devkitPro/msys2/usr/bin/gcc.exe', ['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','build/test_apps_navigation.c','-o','build/test_apps_navigation.exe'], {stdio:'inherit'});
execFileSync('build/test_apps_navigation.exe', [], {stdio:'inherit'});
