// Network layer: RakNet hooks + feature engine.
#include "rc.h"
#include <dlfcn.h>
#include <string.h>
#include <pthread.h>
#include <vector>

RcConfig   g_cfg;
RcNetStats g_net;

namespace rcnet {

typedef int  (*SendFn)(void*, const char*, int, int, int, char);
typedef void (*ProcFn)(void*, unsigned int, unsigned short, const char*, int);

static rchook::Hook hSend, hProc;
static SendFn origSend = nullptr;
static ProcFn origProc = nullptr;

static JavaVM* g_vm = nullptr;
static jclass  g_jniLib = nullptr;
static jmethodID g_midSendJson = nullptr;

static pthread_mutex_t g_pend = PTHREAD_MUTEX_INITIALIZER;
struct Pending { int iid; char json[512]; double at; };
static std::vector<Pending> g_queue;

// ---------------------------------------------------------------- helpers
static inline float rd_f(const char* p) { float v; memcpy(&v, p, 4); return v; }
static inline void wr_f(char* p, float v) { memcpy(p, &v, 4); }

void setJni(JavaVM* vm, jclass cls, jmethodID mid) {
    g_vm = vm;
    g_jniLib = cls;
    g_midSendJson = mid;
}

static void sendJson(int iid, const char* json) {
    if (!g_vm || !g_jniLib || !g_midSendJson) { rc_log("sendJson: JNI не готов"); return; }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    const int n = (int)strlen(json);
    jbyteArray arr = env->NewByteArray(n);
    if (arr) {
        env->SetByteArrayRegion(arr, 0, n, (const jbyte*)(json));
        env->CallStaticVoidMethod(g_jniLib, g_midSendJson, (jint)iid, arr);
        env->DeleteLocalRef(arr);
        g_net.jsonOut++;
    }
    if (attached) g_vm->DetachCurrentThread();
}

static void queueJson(int iid, const char* json, double delay) {
    pthread_mutex_lock(&g_pend);
    Pending p;
    p.iid = iid;
    snprintf(p.json, sizeof(p.json), "%s", json);
    p.at = rc_now() + delay;
    g_queue.push_back(p);
    pthread_mutex_unlock(&g_pend);
}

// ---------------------------------------------------------------- tiny json
static void lower_ru(char* s) {
    for (char* p = s; *p; ) {
        unsigned char c = (unsigned char)p[0];
        if (c == 0xD0 && (unsigned char)p[1] >= 0x90 && (unsigned char)p[1] <= 0xAF) {
            p[1] = (char)((unsigned char)p[1] + 0x20);
            p += 2;
        } else if (c == 0xD0 && (unsigned char)p[1] == 0x81) {
            p[1] = (char)0x91; p += 2;
        } else if (c >= 'A' && c <= 'Z') { p[0] = (char)(c + 32); p++; }
        else p++;
    }
}

struct Btn { char text[96]; int key; };

static int parse_buttons(const char* js, Btn* out, int max) {
    const char* b = strstr(js, "\"b\":");
    if (!b) b = strstr(js, "\"b\" :");
    if (!b) return 0;
    const char* lb = strchr(b, '[');
    if (!lb) return 0;
    int count = 0;
    const char* p = lb;
    while ((p = strchr(p, '{')) && count < max) {
        const char* e = strchr(p, '}');
        if (!e) break;
        char obj[320];
        int len = (int)(e - p);
        if (len > (int)sizeof(obj) - 1) len = (int)sizeof(obj) - 1;
        memcpy(obj, p, len);
        obj[len] = 0;

        char text[96] = {0};
        int  key = -1;
        const char* bnp = strstr(obj, "\"bn\":");
        if (bnp) {
            const char* q = strchr(bnp, '"');
            if (q) { const char* q2 = strchr(q + 1, '"'); if (q2) { int l = (int)(q2 - q - 1); if (l > 95) l = 95; memcpy(text, q + 1, l); text[l] = 0; } }
        }
        const char* bkp = strstr(obj, "\"bk\":");
        if (bkp) key = atoi(bkp + 5);
        if (key >= 0) {
            snprintf(out[count].text, sizeof(out[count].text), "%s", text);
            out[count].key = key;
            count++;
        }
        p = e + 1;
        const char* rb = strchr(p, ']');
        const char* nb = strchr(p, '{');
        if (!nb || (rb && rb < nb)) break;
    }
    return count;
}

static const char* PRIO[] = {
    "далее", "продолжить", "ок", "хорошо", "понял", "готов", "да", "взять",
    "забрать", "согласен", "создать", "оформить", "купить", "оплатить",
    "завершить", "сдать", "ответить", "принять", nullptr
};
static const char* CLOSING[] = { "отказаться", "нет", "закрыть", "выйти", "отмена", "уйти", nullptr };

static int choose_button(Btn* b, int n) {
    int best = -1, bestScore = -1;
    for (int i = 0; i < n; i++) {
        char low[96];
        snprintf(low, sizeof(low), "%s", b[i].text);
        lower_ru(low);
        bool closing = false;
        for (int c = 0; CLOSING[c]; c++) if (strstr(low, CLOSING[c])) closing = true;
        int score = 0;
        for (int p = 0; PRIO[p]; p++) if (strstr(low, PRIO[p])) { score = 100 - p; break; }
        if (closing) score -= 60;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0 && n > 0) best = n - 1;
    return best;
}

// ---------------------------------------------------------------- incoming
static char g_lastDialog[96];
static double g_lastDialogAt = 0;

static void onJsonIn(int iid, const char* js, int len) {
    g_net.jsonIn++;
    if (iid == 63) {
        char copy[1024];
        int l = len < (int)sizeof(copy) - 1 ? len : (int)sizeof(copy) - 1;
        memcpy(copy, js, l); copy[l] = 0;
        if (!g_cfg.autoDialog && !g_cfg.slava) return;
        if (strcmp(copy, g_lastDialog) == 0 && rc_now() - g_lastDialogAt < 2.0) return;
        snprintf(g_lastDialog, sizeof(g_lastDialog), "%s", copy);
        g_lastDialogAt = rc_now();

        Btn b[10];
        int n = parse_buttons(copy, b, 10);
        if (n > 0) {
            int pick = choose_button(b, n);
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"bk\":%d}", b[pick].key);
            queueJson(63, resp, 0.35 + (rand() % 250) / 1000.0);
            rc_log("диалог: %d кнопок, жму '%s'", n, b[pick].text);
        }
    } else if (iid == 65) {
        if (!g_cfg.autoCall && !g_cfg.slava) return;
        int t = 2;
        const char* tp = strstr(js, "\"t\":");
        if (tp) t = atoi(tp + 4);
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"t\":%d,\"b\":1}", t);
        queueJson(65, resp, 0.5);
        rc_log("звонок: принят (t=%d)", t);
    } else if (iid == 31) {
        if (!g_cfg.autoMini && !g_cfg.slava) return;
        queueJson(31, "{\"s\":1}", 0.12);
    } else if (iid == 39) {
        g_net.slavaStage = 1;
        rc_log("квест: %d байт", len);
    } else if (iid == 13) {
        int t = 0, s = 0;
        const char* tp = strstr(js, "\"t\":");
        if (tp) t = atoi(tp + 4);
        const char* sp = strstr(js, "\"s\":");
        if (sp) s = atoi(sp + 4);
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"c\":13,\"t\":%d,\"s\":%d,\"b\":1}", t, s);
        queueJson(13, resp, 0.3);
    }
}

static int scan_rx(const char* d, int len) {
    for (int i = 8; i < len - 2; i++) {
        if (d[i] != '{') continue;
        if (d[i - 7] != 0x1B) continue;
        unsigned short iid = (unsigned char)d[i - 6] | ((unsigned char)d[i - 5] << 8);
        unsigned int  sz  = (unsigned char)d[i - 4] | ((unsigned char)d[i - 3] << 8) |
                            ((unsigned char)d[i - 2] << 16) | ((unsigned char)d[i - 1] << 24);
        (void)sz;
        int avail = len - i;
        int n = 0;
        while (n < avail && d[i + n] && n < 4000) n++;
        if (n < 2) continue;
        g_net.lastRxId = iid;
        g_net.lastRxLen = n;
        onJsonIn(iid, d + i, n);
        return 1;
    }
    return 0;
}

static void det_proc(void* self, unsigned int addr, unsigned short port, const char* data, int len) {
    g_net.rxTotal++;
    if (data && len > 8) {
        if (!scan_rx(data, len) && len > 1) g_net.lastRxId = (unsigned char)data[0];
    }
    origProc(self, addr, port, data, len);
}

// ---------------------------------------------------------------- outgoing
static float g_off[3] = {0, 0, 0};
static float g_prev[3] = {0, 0, 0};
static bool  g_havePrev = false;
static bool  g_wasSpeed = false;

static void apply_player_sync(char* d, int len) {
    if (len < 37) return;
    float px = rd_f(d + 7), py = rd_f(d + 11), pz = rd_f(d + 15);
    g_net.lastPos[0] = px; g_net.lastPos[1] = py; g_net.lastPos[2] = pz;
    g_net.lastHp = (float)(unsigned char)d[35];

    if (g_cfg.immortal || g_cfg.fastHeal) {
        d[35] = 100;
        d[36] = 100;
        g_net.hpPatched++;
    }
    float nx = px, ny = py, nz = pz;
    if (g_cfg.tpPulse > 0) {
        nx = g_cfg.tpPos[0]; ny = g_cfg.tpPos[1]; nz = g_cfg.tpPos[2];
        g_cfg.tpPulse--;
        g_off[0] = g_off[1] = g_off[2] = 0;
        wr_f(d + 41, 0); wr_f(d + 45, 0); wr_f(d + 49, 0);
    } else if (g_cfg.speedHack && g_cfg.speedMul > 1.02f) {
        if (g_havePrev) {
            g_off[0] += (px - g_prev[0]) * (g_cfg.speedMul - 1.f);
            g_off[1] += (py - g_prev[1]) * (g_cfg.speedMul - 1.f);
            g_off[2] += (pz - g_prev[2]) * (g_cfg.speedMul - 1.f);
        }
        nx += g_off[0]; ny += g_off[1]; nz += g_off[2];
        g_wasSpeed = true;
    } else if (g_wasSpeed) {
        g_off[0] = g_off[1] = g_off[2] = 0;
        g_wasSpeed = false;
    }
    g_prev[0] = px; g_prev[1] = py; g_prev[2] = pz;
    g_havePrev = true;
    if (nx != px || ny != py || nz != pz) {
        wr_f(d + 7, nx); wr_f(d + 11, ny); wr_f(d + 15, nz);
    }
}

static void apply_vehicle_sync(char* d, int len) {
    if (len < 55) return;
    if (g_cfg.vehGod) {
        wr_f(d + 49, 1000.f);
        d[53] = 100;
        d[54] = 100;
    }
    if (g_cfg.vehFixPulse > 0) {
        wr_f(d + 49, 1000.f);
        g_cfg.vehFixPulse--;
    }
    if (g_cfg.vehTpPulse > 0) {
        wr_f(d + 25, g_cfg.vehTpPos[0]);
        wr_f(d + 29, g_cfg.vehTpPos[1]);
        wr_f(d + 33, g_cfg.vehTpPos[2]);
        wr_f(d + 37, 0); wr_f(d + 41, 0); wr_f(d + 45, 0);
        g_cfg.vehTpPulse--;
    } else if (g_cfg.vehSpeed && g_cfg.vehSpeedMul > 1.02f) {
        float vx = rd_f(d + 37), vy = rd_f(d + 41), vz = rd_f(d + 45);
        wr_f(d + 37, vx * g_cfg.vehSpeedMul);
        wr_f(d + 41, vy * g_cfg.vehSpeedMul);
        wr_f(d + 45, vz * g_cfg.vehSpeedMul);
    }
}

static int det_send(void* self, const char* data, int len, int prio, int rel, char ch) {
    g_net.txTotal++;
    if (!data || len <= 0) return origSend(self, data, len, prio, rel, ch);

    const int id = (unsigned char)data[0];
    g_net.lastTxId = id;
    g_net.lastTxLen = len;

    static thread_local char buf[4096];
    bool touch = false;
    int  useLen = len;

    if (len < (int)sizeof(buf) && (id == 0x1E || id == 0x22 || id == 0x05)) {
        memcpy(buf, data, len);
        if (id == 0x1E) { apply_player_sync(buf, len); g_net.syncOut++; }
        else if (id == 0x22) apply_vehicle_sync(buf, len);
        else if (len > 7) { if (g_cfg.immortal) { buf[5] = 100; buf[6] = 100; } }
        touch = memcmp(buf, data, len) != 0;
        data = buf;
        useLen = len;
    } else if (id == 0x1B) {
        g_net.jsonOut++;
    }
    (void)touch;
    return origSend(self, data, useLen, prio, rel, ch);
}

// ---------------------------------------------------------------- install
static void* open_game() {
    void* h = dlopen("libblackrussia-client.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libblackrussia-client.so", RTLD_NOW);
    return h;
}

void install() {
    void* game = open_game();
    if (!game) { rc_log("не нашла libblackrussia-client.so"); g_net.hooksFail++; return; }
    rc_log("libblackrussia-client.so: %p", game);

    void* pSend = dlsym(game, "_ZN9RakClient4SendEPKci14PacketPriority17PacketReliabilityc");
    void* pProc = dlsym(game, "_ZN9RakClient20ProcessNetworkPacketEjtPKci");
    if (!pSend) rc_log("RakClient::Send не найден");
    if (!pProc) rc_log("RakClient::ProcessNetworkPacket не найден");

    if (pSend && rchook::install(hSend, pSend, (void*)&det_send)) {
        origSend = (SendFn)hSend.tramp;
        g_net.hooksOk++;
        rc_log("хук Send ok");
    } else if (pSend) g_net.hooksFail++;

    if (pProc && rchook::install(hProc, pProc, (void*)&det_proc)) {
        origProc = (ProcFn)hProc.tramp;
        g_net.hooksOk++;
        rc_log("хук ProcessNetworkPacket ok");
    } else if (pProc) g_net.hooksFail++;

    if (!origSend || !origProc) rc_log("часть хуков не встала — функции ограничены");
}

void tick(double now) {
    (void)now;
    pthread_mutex_lock(&g_pend);
    for (size_t i = 0; i < g_queue.size(); ) {
        if (g_queue[i].at <= rc_now()) {
            sendJson(g_queue[i].iid, g_queue[i].json);
            g_queue.erase(g_queue.begin() + i);
        } else i++;
    }
    pthread_mutex_unlock(&g_pend);
}

} // namespace rcnet

// re-export JNI registration for main.cpp
void rcnet_set_jni(JavaVM* vm, jclass cls, jmethodID mid) { rcnet::setJni(vm, cls, mid); }
