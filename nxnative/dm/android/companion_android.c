// The companion on a second screen (kalasyan fork): devices with two screens (the AYN Thor's bottom one) show a page of their own there while the game
// plays on the main one. Off unless env.txt sets DM_COMPANION=1. The app has no Java code, so it is all JNI on framework classes: DisplayManager's displays,
// the first other than the default one (a presentation display first) gets an android.app.Presentation holding a SurfaceView, and a thread of its own
// draws into that view's surface with the CPU (ANativeWindow_lock) twice a second. No SurfaceHolder.Callback (that needs a Java class): the thread looks at
// Surface.isValid() each time and takes the window again when the surface was remade. The window is not focusable, so a controller's keys stay the game's
// after a touch on the second screen. Hidden while the app is in the background (onPause), shown again on onResume.
// For now the page is a test card (the screen, the game's frame rate, the time played): the map and the player's state come later.
#include "dm.h"
#include "font5x7.h"
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

__attribute__((weak)) uint64_t dm_frames_shown(void) { return 0; }   // gfx_vk_present.c: the game's frames presented
static JavaVM *g_vm;
static jobject g_pres, g_holder;   // global references: the Presentation and its SurfaceView's SurfaceHolder
static int g_disp_id = -1;
static _Atomic int g_hidden;
static int jx(JNIEnv *E) { if (!(*E)->ExceptionCheck(E)) return 0; (*E)->ExceptionDescribe(E); (*E)->ExceptionClear(E); return 1; }
static int64_t page_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec; }

// ---- the page: RGBA_8888 pixels (0xAABBGGRR) ----
typedef struct { uint32_t *px; int w, h, stride; } page;
static void fill(page *p, int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > p->w) x1 = p->w; if (y1 > p->h) y1 = p->h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) p->px[(size_t)y * p->stride + x] = c;
}
static void text(page *p, int x, int y, int s, uint32_t c, const char *t) {   // s = the size of one font dot in pixels
    for (; *t; t++, x += 6 * s) {
        unsigned ch = (unsigned char)*t; if (ch < 32 || ch > 126) ch = '?';
        for (int col = 0; col < 5; col++) for (int row = 0; row < 7; row++) if (FONT5X7[ch - 32][col] >> row & 1) fill(p, x + col * s, y + row * s, x + (col + 1) * s, y + (row + 1) * s, c);
    }
}
static void draw(page *p, int fps, int64_t secs) {
    fill(p, 0, 0, p->w, p->h, 0xff201010);
    int s = p->h / 70; if (s < 2) s = 2; int m = 3 * s, line = 11 * s;
    fill(p, 0, 0, p->w, 2 * m + 7 * s * 2, 0xff5a2a1a);
    text(p, m, m, 2 * s, 0xffffffff, "NX2A COMPANION");
    char b[96]; int y = 2 * m + 14 * s + line;
    snprintf(b, sizeof b, "Pantalla %d: %dx%d", g_disp_id, p->w, p->h); text(p, m, y, s, 0xffd0d0d0, b); y += line;
    if (fps >= 0) snprintf(b, sizeof b, "Juego: %d FPS", fps); else snprintf(b, sizeof b, "Juego: --"); text(p, m, y, s, 0xff80ff80, b); y += line;
    snprintf(b, sizeof b, "Tiempo: %02d:%02d:%02d", (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60)); text(p, m, y, s, 0xffd0d0d0, b); y += 2 * line;
    text(p, m, y, s, 0xff808080, "Aqui ira el mapa");
}

static void *companion_thread(void *a) {
    (void)a; JNIEnv *E; pthread_setname_np(pthread_self(), "companion");
    if ((*g_vm)->AttachCurrentThread(g_vm, &E, NULL) != JNI_OK) { LOG("[RED] [companion] cannot attach to the Java VM"); return NULL; }
    jclass cH = (*E)->FindClass(E, "android/view/SurfaceHolder"), cS = (*E)->FindClass(E, "android/view/Surface");
    jmethodID mSurf = cH ? (*E)->GetMethodID(E, cH, "getSurface", "()Landroid/view/Surface;") : NULL, mValid = cS ? (*E)->GetMethodID(E, cS, "isValid", "()Z") : NULL;
    if (jx(E) || !mSurf || !mValid) { LOG("[RED] [companion] SurfaceHolder methods missing"); return NULL; }
    ANativeWindow *win = NULL; int64_t t0 = page_ns(), tf = t0; uint64_t f0 = dm_frames_shown(); int fps = -1, logged = 0;
    for (;; usleep(500000)) {
        int64_t t = page_ns(); uint64_t f = dm_frames_shown();
        if (t - tf >= 1000000000) { fps = (int)((f - f0) * 1000000000 / (uint64_t)(t - tf)); f0 = f; tf = t; }
        if (atomic_load(&g_hidden)) continue;
        jobject surf = (*E)->CallObjectMethod(E, g_holder, mSurf); if (jx(E)) surf = NULL;
        jboolean ok = surf ? (*E)->CallBooleanMethod(E, surf, mValid) : 0; if (jx(E)) ok = 0;
        if (!ok) { if (win) { ANativeWindow_release(win); win = NULL; } if (surf) (*E)->DeleteLocalRef(E, surf); continue; }
        if (!win && (win = ANativeWindow_fromSurface(E, surf))) ANativeWindow_setBuffersGeometry(win, 0, 0, WINDOW_FORMAT_RGBA_8888);
        (*E)->DeleteLocalRef(E, surf);
        ANativeWindow_Buffer b;
        if (!win || ANativeWindow_lock(win, &b, NULL)) { if (win) { ANativeWindow_release(win); win = NULL; } continue; }   // the surface went: take the new one next time
        if (!logged++) LOG("[companion] drawing on display %d: %dx%d", g_disp_id, b.width, b.height);
        page p = {b.bits, b.width, b.height, b.stride}; draw(&p, fps, (t - t0) / 1000000000);
        ANativeWindow_unlockAndPost(win);
    }
    return NULL;
}

// On the main thread (ANativeActivity_onCreate): finds the second screen and shows the Presentation on it
void dm_companion_start(ANativeActivity *act) {
    const char *on = getenv("DM_COMPANION"); if (!on || atoi(on) == 0 || g_pres) return;
    JNIEnv *E = act->env; g_vm = act->vm;
    jclass cCtx = (*E)->FindClass(E, "android/content/Context"), cDM = (*E)->FindClass(E, "android/hardware/display/DisplayManager"), cD = (*E)->FindClass(E, "android/view/Display");
    jmethodID mSys = cCtx ? (*E)->GetMethodID(E, cCtx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") : NULL;
    jmethodID mAll = cDM ? (*E)->GetMethodID(E, cDM, "getDisplays", "()[Landroid/view/Display;") : NULL;
    jmethodID mId = cD ? (*E)->GetMethodID(E, cD, "getDisplayId", "()I") : NULL, mFl = cD ? (*E)->GetMethodID(E, cD, "getFlags", "()I") : NULL, mName = cD ? (*E)->GetMethodID(E, cD, "getName", "()Ljava/lang/String;") : NULL;
    if (jx(E) || !mSys || !mAll || !mId || !mFl || !mName) { LOG("[RED] [companion] DisplayManager methods missing"); return; }
    jobject dm = (*E)->CallObjectMethod(E, act->clazz, mSys, (*E)->NewStringUTF(E, "display"));
    jobjectArray all = !jx(E) && dm ? (*E)->CallObjectMethod(E, dm, mAll) : NULL;
    if (jx(E) || !all) { LOG("[RED] [companion] no display list"); return; }
    jobject pick = NULL; int pick_pres = 0; jsize n = (*E)->GetArrayLength(E, all);
    for (jsize i = 0; i < n; i++) {
        jobject d = (*E)->GetObjectArrayElement(E, all, i); int id = (*E)->CallIntMethod(E, d, mId), fl = (*E)->CallIntMethod(E, d, mFl);
        jstring js = (*E)->CallObjectMethod(E, d, mName); const char *nm = js ? (*E)->GetStringUTFChars(E, js, NULL) : NULL;
        LOG("[companion] display %d \"%s\" flags 0x%x", id, nm ? nm : "?", fl);
        if (nm) (*E)->ReleaseStringUTFChars(E, js, nm);
        int pres = (fl & 8) != 0;   // Display.FLAG_PRESENTATION
        if (id != 0 && (!pick || (pres && !pick_pres))) { pick = d; pick_pres = pres; g_disp_id = id; }
    }
    if (!pick) { LOG("[companion] no second screen: nothing to show"); return; }
    jclass cP = (*E)->FindClass(E, "android/app/Presentation"), cDlg = (*E)->FindClass(E, "android/app/Dialog"), cSV = (*E)->FindClass(E, "android/view/SurfaceView");
    jclass cW = (*E)->FindClass(E, "android/view/Window");
    jmethodID pNew = cP ? (*E)->GetMethodID(E, cP, "<init>", "(Landroid/content/Context;Landroid/view/Display;)V") : NULL;
    jmethodID dCtx = cDlg ? (*E)->GetMethodID(E, cDlg, "getContext", "()Landroid/content/Context;") : NULL, dSet = cDlg ? (*E)->GetMethodID(E, cDlg, "setContentView", "(Landroid/view/View;)V") : NULL;
    jmethodID dShow = cDlg ? (*E)->GetMethodID(E, cDlg, "show", "()V") : NULL, dWin = cDlg ? (*E)->GetMethodID(E, cDlg, "getWindow", "()Landroid/view/Window;") : NULL;
    jmethodID wAdd = cW ? (*E)->GetMethodID(E, cW, "addFlags", "(I)V") : NULL;
    jmethodID svNew = cSV ? (*E)->GetMethodID(E, cSV, "<init>", "(Landroid/content/Context;)V") : NULL, svH = cSV ? (*E)->GetMethodID(E, cSV, "getHolder", "()Landroid/view/SurfaceHolder;") : NULL;
    if (jx(E) || !pNew || !dCtx || !dSet || !dShow || !dWin || !wAdd || !svNew || !svH) { LOG("[RED] [companion] Presentation methods missing"); return; }
    jobject pres = (*E)->NewObject(E, cP, pNew, act->clazz, pick); if (jx(E) || !pres) { LOG("[RED] [companion] cannot make the Presentation"); return; }
    jobject ctx = (*E)->CallObjectMethod(E, pres, dCtx), sv = !jx(E) && ctx ? (*E)->NewObject(E, cSV, svNew, ctx) : NULL;
    if (jx(E) || !sv) { LOG("[RED] [companion] cannot make the SurfaceView"); return; }
    (*E)->CallVoidMethod(E, pres, dSet, sv);
    jobject w = !jx(E) ? (*E)->CallObjectMethod(E, pres, dWin) : NULL;
    if (!jx(E) && w) { (*E)->CallVoidMethod(E, w, wAdd, 0x8 | 0x80); jx(E); }   // FLAG_NOT_FOCUSABLE | FLAG_KEEP_SCREEN_ON
    (*E)->CallVoidMethod(E, pres, dShow);
    if (jx(E)) { LOG("[RED] [companion] the Presentation did not show on display %d", g_disp_id); return; }
    jobject h = (*E)->CallObjectMethod(E, sv, svH); if (jx(E) || !h) { LOG("[RED] [companion] no SurfaceHolder"); return; }
    g_pres = (*E)->NewGlobalRef(E, pres); g_holder = (*E)->NewGlobalRef(E, h);
    LOG("[companion] shown on display %d%s", g_disp_id, pick_pres ? " (a presentation display)" : "");
    pthread_t t; if (!pthread_create(&t, NULL, companion_thread, NULL)) pthread_detach(t);
}
// On the main thread (onPause / onResume): the page goes with the game
void dm_companion_show(ANativeActivity *act, int on) {
    if (!g_pres) return;
    JNIEnv *E = act->env; jclass c = (*E)->FindClass(E, "android/app/Dialog"); jmethodID m = c ? (*E)->GetMethodID(E, c, on ? "show" : "hide", "()V") : NULL;
    if (jx(E) || !m) return;
    atomic_store(&g_hidden, !on); (*E)->CallVoidMethod(E, g_pres, m); jx(E);
}
