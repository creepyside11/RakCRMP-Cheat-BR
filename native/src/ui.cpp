// Overlay UI: floating red "C" button + grey liquid-glass menu.
#include "rc.h"
#include <vector>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

namespace rcui {

enum { IT_TOGGLE = 0, IT_SLIDER = 1, IT_BUTTON = 2, IT_INFO = 3, IT_GAP = 4 };
enum {
    ACT_NONE = 0,
    ACT_SAVE_POINT = 1,
    ACT_TP_POINT = 2,
    ACT_VEH_FIX = 3,
    ACT_SAVE_POINT_VEH = 4,
    ACT_VEH_TP = 5,
};

struct Item {
    int  kind;
    const char* label;
    bool*  b;
    float* f;
    float  mn, mx;
    int    action;
};

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static float W = 1, H = 1, S = 1;
static float btnNX = 0.905f, btnNY = 0.34f;
static bool  btnDrag = false, btnMoved = false;
static float dragOffX = 0, dragOffY = 0;
static double pressT = 0;
static bool  menuOpen = false;
static float anim = 0.f;
static int   tab = 0;
static int   activeSlider = -1;
static bool  captured = false;
static double lastFrame = 0;

static const char* TAB_NAMES[4] = { "Персонаж", "Транспорт", "Боты", "Отладка" };

static void fmt(char* out, size_t n, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    vsnprintf(out, n, f, ap);
    va_end(ap);
}

static std::vector<Item> items() {
    std::vector<Item> v;
    if (tab == 0) {
        v.push_back({IT_TOGGLE, "Бессмертие",        &g_cfg.immortal,  nullptr, 0, 0, 0});
        v.push_back({IT_TOGGLE, "Авто-лечение",      &g_cfg.fastHeal,  nullptr, 0, 0, 0});
        v.push_back({IT_SLIDER, "Скорость бега",     nullptr, &g_cfg.speedMul, 1.0f, 4.0f, 0});
        v.push_back({IT_GAP,    nullptr, nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_BUTTON, "Запомнить точку",   nullptr, nullptr, 0, 0, ACT_SAVE_POINT});
        v.push_back({IT_BUTTON, "Телепорт на точку", nullptr, nullptr, 0, 0, ACT_TP_POINT});
    } else if (tab == 1) {
        v.push_back({IT_TOGGLE, "Бессмертие авто",     &g_cfg.vehGod, nullptr, 0, 0, 0});
        v.push_back({IT_SLIDER, "Скорость авто",       nullptr, &g_cfg.vehSpeedMul, 1.0f, 4.0f, 0});
        v.push_back({IT_BUTTON, "Ремонт авто",         nullptr, nullptr, 0, 0, ACT_VEH_FIX});
        v.push_back({IT_BUTTON, "Запомнить точку",     nullptr, nullptr, 0, 0, ACT_SAVE_POINT_VEH});
        v.push_back({IT_BUTTON, "Телепорт авто",       nullptr, nullptr, 0, 0, ACT_VEH_TP});
    } else if (tab == 2) {
        v.push_back({IT_TOGGLE, "Дядя Слава", &g_cfg.slava, nullptr, 0, 0, 0});
        v.push_back({IT_TOGGLE, "Авто-диалоги", &g_cfg.autoDialog, nullptr, 0, 0, 0});
        v.push_back({IT_TOGGLE, "Авто-звонки", &g_cfg.autoCall, nullptr, 0, 0, 0});
        v.push_back({IT_TOGGLE, "Мини-игры", &g_cfg.autoMini, nullptr, 0, 0, 0});
        v.push_back({IT_GAP, nullptr, nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "quest", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "dialog", nullptr, nullptr, 0, 0, 0});
    } else {
        v.push_back({IT_INFO, "hooks", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "tx", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "rx", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "json", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "last", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "pos", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "touch", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "log0", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "log1", nullptr, nullptr, 0, 0, 0});
        v.push_back({IT_INFO, "log2", nullptr, nullptr, 0, 0, 0});
    }
    return v;
}

struct Geo {
    float pw, ph, px, py;
    float tabX, tabY, tabW, tabH;
    float cx, cy, cw;          // content origin / width
    float rowH;
};

static Geo geo() {
    Geo g;
    g.pw = W * 0.84f; if (g.pw > 780 * S) g.pw = 780 * S;
    g.ph = H * 0.74f; if (g.ph > 500 * S) g.ph = 500 * S;
    g.px = (W - g.pw) * 0.5f;
    g.py = (H - g.ph) * 0.5f;
    g.tabX = g.px + 16 * S;
    g.tabY = g.py + 84 * S;
    g.tabW = 168 * S;
    g.tabH = 40 * S;
    g.cx = g.px + 200 * S;
    g.cw = g.pw - 220 * S;
    g.rowH = 46 * S;
    return g;
}

static float btnRadius() { return 31 * S; }

// ------------------------------------------------------------------ draw
static void drawSwitch(float x, float y, float w, float h, bool on, float a) {
    RCColor off = rc_rgb(0x3A3D45, 0.9f * a);
    RCColor onC = rc_rgb(0xE23B36, a);
    rcgl::rect(x, y, w, h, h * 0.5f, on ? onC : off);
    float kr = h * 0.5f - 3 * S;
    float kx = on ? (x + w - h * 0.5f) : (x + h * 0.5f);
    rcgl::circle(kx, y + h * 0.5f, kr, RCColor(1, 1, 1, 0.96f * a));
}

static void drawSlider(const Item& it, float x, float y, float w, float a) {
    float t = (it.f && it.mx > it.mn) ? (*it.f - it.mn) / (it.mx - it.mn) : 0;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float h = 6 * S;
    float cy = y + h * 0.5f;
    rcgl::rect(x, cy, w, h, h * 0.5f, rc_rgb(0x2E3138, 0.95f * a));
    rcgl::rect(x, cy, w * t, h, h * 0.5f, rc_rgb(0xE23B36, a));
    rcgl::circle(x + w * t, cy, 8 * S, RCColor(1, 1, 1, a));
    char buf[32];
    fmt(buf, sizeof(buf), "%.1f", it.f ? *it.f : 0.f);
    rcgl::text(x + w + 12 * S, y - 6 * S, buf, 17 * S, rc_rgb(0xC9CDD4, a));
}

static void drawButton(const char* label, float x, float y, float w, float h, float a) {
    rcgl::rect(x, y, w, h, 12 * S, rc_rgb(0x33363D, 0.92f * a));
    rcgl::rect(x, y, w, 1.5f * S, 12 * S, rc_rgb(0x5A5F69, 0.55f * a));
    float tw = rcgl::textWidth(label, 17 * S);
    rcgl::text(x + (w - tw) * 0.5f, y + (h - 17 * S) * 0.5f - 1 * S, label, 17 * S, rc_rgb(0xF0F1F4, a));
}

static void drawInfo(const char* key, const Geo& g, float y, float a, int index) {
    char buf[192];
    if (!strcmp(key, "quest")) {
        fmt(buf, sizeof(buf), "Квест: %s", g_net.slavaStage ? "в процессе" : "нет");
    } else if (!strcmp(key, "dialog")) {
        fmt(buf, sizeof(buf), "Диалоги: %s", g_cfg.autoDialog ? "авто" : "выкл");
    } else if (!strcmp(key, "hooks")) {
        fmt(buf, sizeof(buf), "Хуки: %d ок / %d провал", g_net.hooksOk, g_net.hooksFail);
    } else if (!strcmp(key, "tx")) {
        fmt(buf, sizeof(buf), "Отправлено: %d пакетов, sync %d", g_net.txTotal, g_net.syncOut);
    } else if (!strcmp(key, "rx")) {
        fmt(buf, sizeof(buf), "Принято: %d пакетов, JSON %d", g_net.rxTotal, g_net.jsonIn);
    } else if (!strcmp(key, "json")) {
        fmt(buf, sizeof(buf), "JSON исх: %d, HP-патчей: %d", g_net.jsonOut, g_net.hpPatched);
    } else if (!strcmp(key, "last")) {
        fmt(buf, sizeof(buf), "last tx id 0x%02X/%d, rx 0x%02X/%d", g_net.lastTxId, g_net.lastTxLen,
            g_net.lastRxId, g_net.lastRxLen);
    } else if (!strcmp(key, "pos")) {
        fmt(buf, sizeof(buf), "pos %.1f %.1f %.1f  hp %.0f", g_net.lastPos[0], g_net.lastPos[1],
            g_net.lastPos[2], g_net.lastHp);
    } else if (!strcmp(key, "touch")) {
        fmt(buf, sizeof(buf), "touch %.0f %.0f act %d", g_net.touchX, g_net.touchY, g_net.touchAct);
    } else {
        int idx = key[3] - '0';
        const char* l = rc_log_line(idx);
        fmt(buf, sizeof(buf), "%s", l ? l : "");
    }
    (void)index;
    rcgl::text(g.cx, y, buf, 15 * S, rc_rgb(0x9AA0AA, a));
}

void draw(int w, int h, float dt) {
    pthread_mutex_lock(&g_mtx);
    W = (float)(w > 0 ? w : 1);
    H = (float)(h > 0 ? h : 1);
    S = H / 720.f;
    if (S < 0.75f) S = 0.75f;

    double now = rc_now();
    if (lastFrame == 0) lastFrame = now;
    float dtf = (float)(now - lastFrame);
    lastFrame = now;
    if (dtf > 0.1f) dtf = 0.1f;
    (void)dt;

    float target = menuOpen ? 1.f : 0.f;
    float speed = 7.5f;
    if (anim < target) anim += dtf * speed; else if (anim > target) anim -= dtf * speed;
    if (anim < 0.f) anim = 0.f;
    if (anim > 1.f) anim = 1.f;

    const float a = anim;
    Geo g = geo();

    // --- floating button
    float bx = btnNX * W, by = btnNY * H;
    float br = btnRadius();
    if (g_cfg.showButton) {
        rcgl::shadow(bx - br - 10 * S, by - br - 8 * S, (br + 10 * S) * 2, (br + 10 * S) * 2,
                     br + 8 * S, 14 * S, RCColor(1, 0.1f, 0.1f, 0.28f));
        rcgl::circle(bx, by, br, rc_rgb(0xD2322D, 0.98f));
        rcgl::circle(bx, by - br * 0.22f, br * 0.82f, rc_rgb(0xE8483F, 0.75f));
        rcgl::circle(bx, by, br * 0.94f, RCColor(0, 0, 0, 0.001f));
        float tw = rcgl::textWidth("C", 34 * S);
        rcgl::text(bx - tw * 0.5f, by - 24 * S, "C", 34 * S, RCColor(1, 1, 1, 0.98f));
    }

    // --- menu
    if (anim > 0.01f) {
        float sc = 0.94f + 0.06f * anim;
        float px = g.px + g.pw * (1 - sc) * 0.5f;
        float py = g.py + g.ph * (1 - sc) * 0.5f;

        rcgl::shadow(px - 24 * S, py - 18 * S, g.pw + 48 * S, g.ph + 48 * S, 30 * S, 34 * S,
                     RCColor(0, 0, 0, 0.35f * a));
        rcgl::glass(px, py, g.pw, g.ph, 24 * S, RCColor(0.16f, 0.17f, 0.19f, 0.62f));
        rcgl::border(px, py, g.pw, g.ph, 24 * S, 1.0f * S, RCColor(1, 1, 1, 0.12f * a));

        // header
        rcgl::text(px + 22 * S, py + 16 * S, "RakCRMP Cheat", 24 * S, rc_rgb(0xF4F5F7, a));
        float vw = rcgl::textWidth(RC_VERSION, 14 * S);
        rcgl::text(px + g.pw - 52 * S - vw, py + 25 * S, RC_VERSION, 14 * S, rc_rgb(0x8E949E, a));
        rcgl::rect(px + g.pw - 38 * S, py + 16 * S, 24 * S, 24 * S, 8 * S, rc_rgb(0x3A3D45, 0.85f * a));
        rcgl::text(px + g.pw - 31 * S, py + 19 * S, "x", 17 * S, rc_rgb(0xD8DBE0, a));

        // tabs
        for (int i = 0; i < 4; i++) {
            float ty = py + 84 * S + i * 46 * S;
            bool active = (i == tab);
            if (active)
                rcgl::rect(px + 14 * S, ty, 172 * S, 40 * S, 12 * S, rc_rgb(0xE23B36, 0.92f * a));
            rcgl::text(px + 28 * S, ty + 10 * S, TAB_NAMES[i], 18 * S,
                       active ? RCColor(1, 1, 1, a) : rc_rgb(0xA8AEB8, a));
        }

        // content
        std::vector<Item> its = items();
        float y = py + 86 * S;
        for (size_t i = 0; i < its.size(); i++) {
            const Item& it = its[i];
            if (it.kind == IT_GAP) { y += 18 * S; continue; }
            if (it.kind == IT_INFO) { drawInfo(it.label, g, y + 2 * S, a, (int)i); y += 26 * S; continue; }
            if (it.kind == IT_TOGGLE) {
                rcgl::text(g.cx, y + 8 * S, it.label, 18 * S, rc_rgb(0xE6E8EC, a));
                drawSwitch(px + g.pw - 78 * S, y + 6 * S, 56 * S, 28 * S, it.b && *it.b, a);
            } else if (it.kind == IT_SLIDER) {
                rcgl::text(g.cx, y + 8 * S, it.label, 18 * S, rc_rgb(0xE6E8EC, a));
                drawSlider(it, g.cx + g.cw * 0.42f, y + 12 * S, g.cw * 0.44f, a);
            } else if (it.kind == IT_BUTTON) {
                drawButton(it.label, g.cx, y + 2 * S, g.cw * 0.62f, 34 * S, a);
            }
            y += g.rowH;
        }
    }

    pthread_mutex_unlock(&g_mtx);
}

// ------------------------------------------------------------------ input
static void applyAction(int act) {
    switch (act) {
        case ACT_SAVE_POINT:
            memcpy(g_cfg.tpPos, (const void*)g_net.lastPos, sizeof(g_cfg.tpPos));
            rc_log("точка сохранена: %.1f %.1f %.1f", g_cfg.tpPos[0], g_cfg.tpPos[1], g_cfg.tpPos[2]);
            break;
        case ACT_TP_POINT:
            g_cfg.tpPulse = 8;
            rc_log("телепорт на точку");
            break;
        case ACT_VEH_FIX:
            g_cfg.vehFixPulse = 8;
            rc_log("ремонт авто");
            break;
        case ACT_SAVE_POINT_VEH:
            memcpy(g_cfg.vehTpPos, (const void*)g_net.lastPos, sizeof(g_cfg.vehTpPos));
            rc_log("точка авто сохранена");
            break;
        case ACT_VEH_TP:
            g_cfg.vehTpPulse = 8;
            rc_log("телепорт авто");
            break;
        default: break;
    }
}

bool onInput(const Input& in) {
    pthread_mutex_lock(&g_mtx);
    W = W > 1 ? W : 1;
    S = H / 720.f; if (S < 0.75f) S = 0.75f;

    bool consumed = false;
    float bx = btnNX * W, by = btnNY * H, br = btnRadius() + 10 * S;

    if (in.action == 0) {   // down
        pressT = rc_now();
        btnMoved = false;
        float dx = in.x - bx, dy = in.y - by;
        if (g_cfg.showButton && (dx * dx + dy * dy) <= br * br) {
            btnDrag = true;
            dragOffX = dx; dragOffY = dy;
            consumed = true;
        } else if (menuOpen && anim > 0.5f) {
            Geo g = geo();
            // close button
            if (in.x > g.px + g.pw - 44 * S && in.x < g.px + g.pw - 8 * S &&
                in.y > g.py + 10 * S && in.y < g.py + 44 * S) {
                menuOpen = false;
                consumed = true;
            } else if (in.x > g.px && in.x < g.px + g.pw && in.y > g.py && in.y < g.py + g.ph) {
                consumed = true;
                for (int i = 0; i < 4; i++) {
                    float ty = g.py + 84 * S + i * 46 * S;
                    if (in.y >= ty && in.y <= ty + 40 * S && in.x < g.px + 190 * S) {
                        tab = i;
                        activeSlider = -1;
                        pthread_mutex_unlock(&g_mtx);
                        return true;
                    }
                }
                std::vector<Item> its = items();
                float y = g.py + 86 * S;
                for (size_t i = 0; i < its.size(); i++) {
                    const Item& it = its[i];
                    if (it.kind == IT_GAP) { y += 18 * S; continue; }
                    if (it.kind == IT_INFO) { y += 26 * S; continue; }
                    if (in.y >= y - 6 * S && in.y <= y + g.rowH - 6 * S) {
                        if (it.kind == IT_TOGGLE && it.b) {
                            *it.b = !*it.b;
                            rc_log("toggle %s = %d", it.label, (int)*it.b);
                        } else if (it.kind == IT_SLIDER) {
                            activeSlider = (int)i;
                        } else if (it.kind == IT_BUTTON) {
                            applyAction(it.action);
                        }
                        break;
                    }
                    y += g.rowH;
                }
            }
        }
    } else if (in.action == 2) {   // move
        if (btnDrag) {
            float nx = (in.x - dragOffX) / W;
            float ny = (in.y - dragOffY) / H;
            if (nx < 0.05f) nx = 0.05f; if (nx > 0.95f) nx = 0.95f;
            if (ny < 0.05f) ny = 0.05f; if (ny > 0.95f) ny = 0.95f;
            if (fabsf(nx - btnNX) * W > 4 * S || fabsf(ny - btnNY) * H > 4 * S) btnMoved = true;
            btnNX = nx; btnNY = ny;
            consumed = true;
        } else if (activeSlider >= 0 && menuOpen) {
            Geo g = geo();
            std::vector<Item> its = items();
            if (activeSlider < (int)its.size()) {
                Item& it = its[activeSlider];
                float x0 = g.cx + g.cw * 0.42f;
                float x1 = x0 + g.cw * 0.44f;
                float t = (in.x - x0) / (x1 - x0);
                if (t < 0) t = 0; if (t > 1) t = 1;
                if (it.f) *it.f = it.mn + (it.mx - it.mn) * t;
            }
            consumed = true;
        }
    } else if (in.action == 1) {   // up
        if (btnDrag) {
            btnDrag = false;
            if (!btnMoved && rc_now() - pressT < 0.45) {
                menuOpen = !menuOpen;
                rc_log("меню %s", menuOpen ? "открыто" : "закрыто");
            }
            consumed = true;
        }
        if (activeSlider >= 0) { activeSlider = -1; consumed = true; }
    }

    if (menuOpen && consumed) {
        // keep game from reacting under the menu
    }
    pthread_mutex_unlock(&g_mtx);
    return consumed;
}

bool wantsCapture() { return menuOpen || anim > 0.05f; }

void setScreen(int w, int h) {
    pthread_mutex_lock(&g_mtx);
    W = (float)(w > 0 ? w : 1);
    H = (float)(h > 0 ? h : 1);
    S = H / 720.f; if (S < 0.75f) S = 0.75f;
    pthread_mutex_unlock(&g_mtx);
}

} // namespace rcui
