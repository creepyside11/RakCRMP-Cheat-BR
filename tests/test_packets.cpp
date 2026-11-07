// Offsets check for the sync-packet patcher. Build & run: tools/run_tests.sh
#include "rc.h"
#include "packets.h"
#include <cstdio>
#include <cstring>
#include <cmath>

RcConfig   g_cfg;
RcNetStats g_net;

void rc_log(const char*, ...) {}
const char* rc_log_line(int) { return ""; }
int rc_log_count() { return 0; }

static int fails = 0;
static void check(bool ok, const char* what) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
    else printf("  ok   %s\n", what);
}

static void putf(char* p, float v) { memcpy(p, &v, 4); }
static float getf(const char* p) { float v; memcpy(&v, p, 4); return v; }

static int buildPlayer(char* d, float x, float y, float z, int hp, int ap, int len = 53) {
    memset(d, 0, 128);
    d[0] = 0x1E;
    putf(d + 7, x); putf(d + 11, y); putf(d + 15, z);
    d[35] = (char)hp;
    d[36] = (char)ap;
    return len;
}

static int buildVehicle(char* d, float x, float y, float z, float vx, float vy, float vz, float vehHp, int hp, int ap) {
    memset(d, 0, 128);
    d[0] = 0x22;
    putf(d + 25, x); putf(d + 29, y); putf(d + 33, z);
    putf(d + 37, vx); putf(d + 41, vy); putf(d + 45, vz);
    putf(d + 49, vehHp);
    d[53] = (char)hp;
    d[54] = (char)ap;
    return 64;
}

int main() {
    char d[128];
    int len;

    printf("player sync\n");
    g_cfg = RcConfig();
    g_net = RcNetStats();
    rcpkt::resetMotion();

    len = buildPlayer(d, 100.f, 200.f, 10.f, 42, 7);
    rcpkt::playerSync(d, len);
    check(getf(d + 7) == 100.f && getf(d + 11) == 200.f && getf(d + 15) == 10.f, "no feature -> position untouched");
    check(d[35] == 42 && d[36] == 7, "no feature -> hp untouched");
    check(g_net.lastPos[0] == 100.f && g_net.lastHp == 42.f, "stats filled");

    g_cfg.immortal = true;
    len = buildPlayer(d, 100.f, 200.f, 10.f, 12, 0);
    rcpkt::playerSync(d, len);
    check(d[35] == 100 && d[36] == 100, "immortal -> hp/armour 100");
    check(g_net.hpPatched == 1, "hp patch counted");

    g_cfg.immortal = false;
    g_cfg.fastHeal = true;
    len = buildPlayer(d, 1.f, 2.f, 3.f, 1, 0);
    rcpkt::playerSync(d, len);
    check(d[35] == 100, "fast heals");
    g_cfg.fastHeal = false;

    // layout guard
    len = buildPlayer(d, 1.f, 2.f, 3.f, 12, 0);
    d[35] = (char)200;
    g_cfg.immortal = true;
    rcpkt::playerSync(d, len);
    check(d[35] == (char)200, "implausible hp byte -> packet untouched");
    g_cfg.immortal = false;

    len = buildPlayer(d, 1.f, 2.f, 3.f, 12, 0);
    putf(d + 7, NAN);
    g_cfg.immortal = true;
    rcpkt::playerSync(d, len);
    check(d[35] == 12, "nan position -> packet untouched");
    g_cfg.immortal = false;
    rcpkt::resetMotion();

    printf("teleport\n");
    g_cfg.tpPos[0] = 500.f; g_cfg.tpPos[1] = 600.f; g_cfg.tpPos[2] = 20.f;
    g_cfg.tpPulse = 2;
    len = buildPlayer(d, 1.f, 2.f, 3.f, 50, 0);
    rcpkt::playerSync(d, len);
    check(getf(d + 7) == 500.f && getf(d + 11) == 600.f && getf(d + 15) == 20.f, "teleport moved position");
    check(getf(d + 41) == 0.f && getf(d + 45) == 0.f && getf(d + 49) == 0.f, "teleport zeroed velocity");
    check(g_cfg.tpPulse == 1, "pulse decremented");
    check(g_net.lastPos[0] == 1.f, "stats keep the real position");
    // short packet must not be written past the end
    {
        char small[40];
        memset(small, 0, sizeof(small));
        small[0] = 0x1E;
        putf(small + 7, 1.f); putf(small + 11, 2.f); putf(small + 15, 3.f);
        rcpkt::playerSync(small, 37);
        check(getf(small + 7) == 500.f, "short packet: position still written");
    }
    g_cfg.tpPulse = 0;

    printf("speed\n");
    rcpkt::resetMotion();
    g_cfg.speedHack = true;
    g_cfg.speedMul = 2.0f;
    len = buildPlayer(d, 0.f, 0.f, 0.f, 50, 0);
    rcpkt::playerSync(d, len);
    check(getf(d + 7) == 0.f, "first packet has no offset");
    len = buildPlayer(d, 10.f, 0.f, 0.f, 50, 0);
    rcpkt::playerSync(d, len);
    check(std::fabs(getf(d + 7) - 20.f) < 0.001f, "second packet offset by delta*(mul-1)");
    g_cfg.speedHack = false;
    len = buildPlayer(d, 20.f, 0.f, 0.f, 50, 0);
    rcpkt::playerSync(d, len);
    check(getf(d + 7) == 20.f, "turning the hack off drops the offset");

    printf("vehicle sync\n");
    g_cfg = RcConfig();
    g_net = RcNetStats();
    len = buildVehicle(d, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 900.f, 100, 100);
    rcpkt::vehicleSync(d, len);
    check(getf(d + 49) == 900.f, "no feature -> vehicle untouched");

    g_cfg.vehGod = true;
    len = buildVehicle(d, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 120.f, 10, 0);
    rcpkt::vehicleSync(d, len);
    check(getf(d + 49) == 1000.f && d[53] == 100 && d[54] == 100, "vehicle god");
    g_cfg.vehGod = false;

    g_cfg.vehFixPulse = 1;
    len = buildVehicle(d, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 120.f, 10, 0);
    rcpkt::vehicleSync(d, len);
    check(getf(d + 49) == 1000.f && g_cfg.vehFixPulse == 0, "repair pulse");

    g_cfg.vehTpPos[0] = 77.f; g_cfg.vehTpPos[1] = 88.f; g_cfg.vehTpPos[2] = 9.f;
    g_cfg.vehTpPulse = 1;
    len = buildVehicle(d, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 900.f, 100, 100);
    rcpkt::vehicleSync(d, len);
    check(getf(d + 25) == 77.f && getf(d + 29) == 88.f && getf(d + 33) == 9.f, "vehicle teleport");
    check(getf(d + 37) == 0.f && getf(d + 45) == 0.f, "vehicle teleport zeroes velocity");

    g_cfg.vehSpeed = true;
    g_cfg.vehSpeedMul = 2.5f;
    len = buildVehicle(d, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 900.f, 100, 100);
    rcpkt::vehicleSync(d, len);
    check(getf(d + 37) == 10.f && getf(d + 41) == 12.5f && getf(d + 45) == 15.f, "vehicle speed multiplier");

    printf("passenger sync\n");
    g_cfg = RcConfig();
    memset(d, 0, 128);
    d[0] = 0x05; d[5] = 30; d[6] = 0;
    g_cfg.immortal = true;
    rcpkt::passengerSync(d, 24);
    check(d[5] == 100 && d[6] == 100, "passenger hp patched");
    memset(d, 0, 128);
    d[0] = 0x05; d[5] = 30; d[6] = 0;
    g_cfg.immortal = false;
    rcpkt::passengerSync(d, 24);
    check(d[5] == 30, "passenger untouched when off");

    printf(fails ? "\nFAILED %d\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
