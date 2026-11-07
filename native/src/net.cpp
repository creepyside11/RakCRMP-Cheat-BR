// Network layer: RakNet hooks + feature engine.
#include "rc.h"
#include "packets.h"
#include <dlfcn.h>
#include <stdint.h>
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
static jmethodID g_midDeliver = nullptr;
static jmethodID g_midSendJson = nullptr;

static pthread_mutex_t g_pend = PTHREAD_MUTEX_INITIALIZER;
struct Pending { int iid; char json[512]; double at; };
static std::vector<Pending> g_queue;

// ---------------------------------------------------------------- helpers
static inline float rd_f(const char* p) { float v; memcpy(&v, p, 4); return v; }
static inline void wr_f(char* p, float v) { memcpy(p, &v, 4); }

void setJni(JavaVM* vm, jclass cls, jmethodID deliver, jmethodID sendJson) {
    g_vm = vm;
    g_jniLib = cls;
    g_midDeliver = deliver;
    g_midSendJson = sendJson;
}

static void sendJson(int iid, const char* json) {
    jmethodID mid = g_midDeliver ? g_midDeliver : g_midSendJson;
    if (!g_vm || !g_jniLib || !mid) { rc_log("sendJson: JNI не готов"); return; }
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
        env->CallStaticVoidMethod(g_jniLib, mid, (jint)iid, arr);
        if (env->ExceptionCheck()) { env->ExceptionClear(); rc_log("sendJson: Java-исключение"); }
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

static const char* find_key(const char* js, const char* key) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(js, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "\"%s\" :", key);
        p = strstr(js, pat);
    }
    if (!p) return nullptr;
    p += strlen(pat);
    while (*p == ' ') p++;
    return p;
}

static int json_int_key(const char* js, const char* key, int def) {
    const char* p = find_key(js, key);
    if (!p) return def;
    if (*p != '-' && (*p < '0' || *p > '9')) return def;
    return atoi(p);
}

static bool json_str_key(const char* js, const char* key, char* out, int n) {
    const char* p = find_key(js, key);
    if (!p || *p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < n - 1) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

static int json_int_array(const char* js, const char* key, int* out, int max) {
    const char* p = find_key(js, key);
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        if (*p == '-' || (*p >= '0' && *p <= '9')) {
            out[n++] = atoi(p);
            while (*p == '-' || (*p >= '0' && *p <= '9')) p++;
        } else p++;
    }
    return n;
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
        char q[160];
        if (json_str_key(js, "mq", q, sizeof(q))) {
            if (strcmp(q, g_net.questText) != 0) {
                snprintf(g_net.questText, sizeof(g_net.questText), "%s", q);
                g_net.slavaStage = 1;
                rc_log("задание: %s", q);
            }
        }
    } else if (iid == 22) {
        if (!g_cfg.giftAuto) return;
        int secs = json_int_key(js, "p", -1);
        if (secs > 0) {
            queueJson(22, "{\"t\":2}", 0.7);
            rc_log("бесплатная награда: забираю");
        }
    } else if (iid == 73) {
        if (!g_cfg.giftAuto) return;
        int ct = json_int_key(js, "t", 0);
        if (ct == 1) {
            const char* cc = find_key(js, "cc");
            int cid = 0;
            if (cc) {
                const char* idp = strstr(cc, "\"id\":");
                if (idp) cid = atoi(idp + 5);
            }
            if (cid > 0) {
                char resp[96];
                snprintf(resp, sizeof(resp), "{\"t\":2,\"cs\":%d,\"type\":1}", cid);
                queueJson(73, resp, 0.9);
                rc_log("кейс %d: открываю", cid);
            }
        } else if (ct == 2) {
            int ids[32];
            int n = json_int_array(js, "pr", ids, 32);
            if (n > 0) {
                char resp[256];
                int off = snprintf(resp, sizeof(resp), "{\"t\":3,\"bt1\":[");
                for (int i = 0; i < n && off < (int)sizeof(resp) - 16; i++)
                    off += snprintf(resp + off, sizeof(resp) - off, "%s%d", i ? "," : "", ids[i]);
                snprintf(resp + off, sizeof(resp) - off, "]}");
                queueJson(73, resp, 0.8);
                rc_log("призы: забираю %d", n);
            } else {
                queueJson(73, "{\"t\":4,\"d\":1}", 0.6);
            }
        }
    } else if (iid == 74) {
        if (!g_cfg.giftAuto) return;
        int id = json_int_key(js, "id", -1);
        if (id > 0) {
            char resp[96];
            snprintf(resp, sizeof(resp), "{\"t\":4,\"id\":%d,\"s\":1}", id);
            queueJson(74, resp, 0.5);
            rc_log("награда %d: забираю", id);
        } else {
            queueJson(74, "{\"c\":1}", 0.4);
        }
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
        if (id == 0x1E) { rcpkt::playerSync(buf, len); g_net.syncOut++; }
        else if (id == 0x22) rcpkt::vehicleSync(buf, len);
        else rcpkt::passengerSync(buf, len);
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
// Offsets inside libblackrussia-client.so of the raknet build shipped with the
// base APK. Used only when dlsym cannot resolve the mangled name.
static const uintptr_t OFF_SEND = 0x1a51278;
static const uintptr_t OFF_PROC = 0x1a52978;

static void* open_game() {
    void* h = dlopen("libblackrussia-client.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libblackrussia-client.so", RTLD_NOW);
    return h;
}

void install() {
    void* game = open_game();
    if (!game) { rc_log("не нашла libblackrussia-client.so"); g_net.hooksFail++; return; }
    rc_log("libblackrussia-client.so: %p", game);

    void* pSend = rchook::sym_or_offset(game, "_ZN9RakClient4SendEPKci14PacketPriority17PacketReliabilityc", OFF_SEND);
    void* pProc = rchook::sym_or_offset(game, "_ZN9RakClient20ProcessNetworkPacketEjtPKci", OFF_PROC);
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

