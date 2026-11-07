// RakCRMP Cheat — entry point, frame hook, touch hook.
#include "rc.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>

// ---------------------------------------------------------------- log ring
static pthread_mutex_t g_logMtx = PTHREAD_MUTEX_INITIALIZER;
static char  g_logs[64][160];
static int   g_logHead = 0;
static int   g_logCount = 0;

void rc_log(const char* fmt, ...) {
    char tmp[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp) - 1, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, RC_TAG, "%s", tmp);
    pthread_mutex_lock(&g_logMtx);
    snprintf(g_logs[g_logHead], sizeof(g_logs[0]), "%s", tmp);
    g_logHead = (g_logHead + 1) % 64;
    if (g_logCount < 64) g_logCount++;
    pthread_mutex_unlock(&g_logMtx);
}

const char* rc_log_line(int idx) {
    if (idx < 0 || idx >= g_logCount) return "";
    int i = (g_logHead - 1 - idx + 128) % 64;
    return g_logs[i];
}

int rc_log_count() { return g_logCount; }

// ---------------------------------------------------------------- state
static JavaVM*  g_vm = nullptr;
static jclass   g_jniLib = nullptr;
static jmethodID g_sendJson = nullptr;

static EGLBoolean (*origSwap)(EGLDisplay, EGLSurface) = nullptr;
static rchook::Hook hSwap, hTouch;
static int   g_frameCount = 0;

static void* g_gameHandle = nullptr;

static void jni_multiTouch(JNIEnv*, jobject, jint, jint, jint, jint, jint, jint, jint, jint);
typedef void (*TouchFn)(JNIEnv*, jobject, jint, jint, jint, jint, jint, jint, jint, jint);
static TouchFn origTouch = nullptr;

// ---------------------------------------------------------------- frame hook
static EGLBoolean det_swap(EGLDisplay dpy, EGLSurface surf) {
    static bool inited = false;
    static bool failed = false;
    if (!inited && !failed) {
        inited = true;
        if (!rcgl::init()) { failed = true; rc_log("rcgl::init failed"); }
    }
    if (inited && !failed) {
        EGLint w = 0, h = 0;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        if (w > 0 && h > 0) {
            rcui::setScreen(w, h);
            rcgl::beginFrame(w, h);
            if (rcui::wantsCapture()) rcgl::captureScreen(w, h);
            rcui::draw(w, h, 0.016f);
            rcgl::endFrame();
            g_frameCount++;
        }
    }
    return origSwap(dpy, surf);
}

// ---------------------------------------------------------------- touch hook
static void map_pair(float& x, float& y) {
    float sw = rcui::screenW(), sh = rcui::screenH();
    if (sw < 2 || sh < 2) return;
    if (x >= -0.001f && x <= 1.001f && y >= -0.001f && y <= 1.001f) { x *= sw; y *= sh; }
}

static void jni_multiTouch(JNIEnv* env, jobject thiz, jint a0, jint a1, jint a2, jint a3,
                            jint a4, jint a5, jint a6, jint a7) {
    int action = (int)a0 & 0xff;
    int mapped = 2;
    if (action == 0 || action == 5) mapped = 0;
    else if (action == 1 || action == 6 || action == 3) mapped = 1;
    else mapped = 2;

    jint raw[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    for (int i = 0; i < 8; i++) g_net.rawTouch[i] = (int)raw[i];
    g_net.touchX = (float)a2;
    g_net.touchY = (float)a3;
    g_net.touchAct = action;

    rcui::Input in;
    in.x = (float)a2;  in.y = (float)a3;
    in.x2 = (float)a4; in.y2 = (float)a5;
    in.x3 = (float)a6; in.y3 = (float)a7;
    map_pair(in.x, in.y);
    map_pair(in.x2, in.y2);
    map_pair(in.x3, in.y3);
    in.action = mapped;
    in.id = (int)a1;

    if (rcui::onInput(in)) {
        // consumed by overlay — the game must not see it
        return;
    }
    if (origTouch) origTouch(env, thiz, a0, a1, a2, a3, a4, a5, a6, a7);
}

// ---------------------------------------------------------------- init
static void* tick_thread(void*) {
    for (;;) {
        rcnet::tick(rc_now());
        usleep(50000);
    }
    return nullptr;
}

static void* init_thread(void*) {
    // libblackrussia-client.so загружается сразу после нас через System.loadLibrary
    void* game = nullptr;
    for (int i = 0; i < 40 && !game; i++) {
        game = dlopen("libblackrussia-client.so", RTLD_NOW | RTLD_NOLOAD);
        if (!game) usleep(25000);
    }
    if (!game) game = dlopen("libblackrussia-client.so", RTLD_NOW);
    g_gameHandle = game;
    rc_log("RakCRMP Cheat %s старт (game handle %p)", RC_VERSION, game);

    rcnet::install();

    // touch
    if (game) {
        // 0x1902270 = Java_..._multiTouchEvent in the raknet build of the base APK
        void* pTouch = rchook::sym_or_offset(
            game, "Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent", 0x1902270);
        if (pTouch && rchook::install(hTouch, pTouch, (void*)&jni_multiTouch)) {
            origTouch = (TouchFn)hTouch.tramp;
            g_net.hooksOk++;
            rc_log("хук multiTouchEvent ok");
        } else {
            g_net.hooksFail++;
            rc_log("хук multiTouchEvent провал (%p)", pTouch);
        }
    }

    // frame
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (egl) {
        void* pSwap = dlsym(egl, "eglSwapBuffers");
        if (pSwap && rchook::install(hSwap, pSwap, (void*)&det_swap)) {
            origSwap = (EGLBoolean(*)(EGLDisplay, EGLSurface))hSwap.tramp;
            g_net.hooksOk++;
            rc_log("хук eglSwapBuffers ok");
        } else {
            g_net.hooksFail++;
            rc_log("хук eglSwapBuffers провал");
        }
    } else {
        g_net.hooksFail++;
        rc_log("libEGL.so не открылась");
    }

    pthread_t th;
    pthread_create(&th, nullptr, tick_thread, nullptr);
    pthread_detach(th);
    return nullptr;
}

extern "C" jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    jclass cls = env->FindClass("com/blackhub/bronline/game/core/JNILib");
    jmethodID deliver = nullptr;
    jmethodID sendJson = nullptr;
    if (cls) {
        g_jniLib = (jclass)env->NewGlobalRef(cls);
        deliver = env->GetStaticMethodID(cls, "deliverNetworkEvents", "(I[B)V");
        sendJson = env->GetStaticMethodID(cls, "sendJsonData", "(I[B)V");
        rc_log("JNI: deliver=%p sendJson=%p", (void*)deliver, (void*)sendJson);
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
        rc_log("JNILib не найден");
    }
    rcnet::setJni(vm, g_jniLib, deliver, sendJson);

    pthread_t th;
    pthread_create(&th, nullptr, init_thread, nullptr);
    pthread_detach(th);
    return JNI_VERSION_1_6;
}
