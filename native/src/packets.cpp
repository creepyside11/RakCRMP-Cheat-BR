// Sync packet fields, offsets taken from the wire format:
//
//   PLAYER_SYNC   0x1E : id(1) lr(2) ud(2) keys(2) pos(12) quat(16) hp(1) armour(1)
//                        ... vel(12) @41
//   VEHICLE_SYNC  0x22 : id(1) vehId(2) lr(2) ud(2) keys(2) quat(16) pos(12) vel(12)
//                        veh_health(f32) @49  hp(1) @53  armour(1) @54
//   PASSENGER_SYNC 0x05: id(1) vehId(2) bits(1) bits(1) hp(1) @5 armour(1) @6
#include "rc.h"
#include "packets.h"
#include <string.h>

namespace rcpkt {

static const int PLAYER_MIN   = 37;
static const int PLAYER_VEL   = 53;   // 41 + 12
static const int VEHICLE_MIN  = 55;

static float g_off[3] = {0, 0, 0};
static float g_prev[3] = {0, 0, 0};
static bool  g_havePrev = false;
static bool  g_wasSpeed = false;

static inline float rd_f(const char* p) { float v; memcpy(&v, p, 4); return v; }
static inline void  wr_f(char* p, float v) { memcpy(p, &v, 4); }

void resetMotion() {
    g_off[0] = g_off[1] = g_off[2] = 0;
    g_havePrev = false;
    g_wasSpeed = false;
}

static bool plausible_pos(float x, float y, float z) {
    if (!(x == x) || !(y == y) || !(z == z)) return false;
    if (x > 1e5f || x < -1e5f) return false;
    if (y > 1e5f || y < -1e5f) return false;
    if (z > 1e5f || z < -1e5f) return false;
    return true;
}

void passengerSync(char* d, int len) {
    if (len < 7) return;
    if (!g_cfg.immortal && !g_cfg.fastHeal) return;
    d[5] = 100;
    d[6] = 100;
    g_net.hpPatched++;
}

void playerSync(char* d, int len) {
    if (len < PLAYER_MIN) return;

    const float px = rd_f(d + 7), py = rd_f(d + 11), pz = rd_f(d + 15);
    const int hp = (unsigned char)d[35];
    const int ap = (unsigned char)d[36];

    // Loose sanity: if these bytes can't be health/armour the layout is not
    // what we expect — leave the packet untouched instead of corrupting it.
    if (!plausible_pos(px, py, pz) || hp > 100 || ap > 100) return;

    g_net.lastPos[0] = px;
    g_net.lastPos[1] = py;
    g_net.lastPos[2] = pz;
    g_net.lastHp = (float)hp;

    if (g_cfg.immortal || g_cfg.fastHeal) {
        d[35] = 100;
        d[36] = 100;
        g_net.hpPatched++;
    }

    float nx = px, ny = py, nz = pz;
    bool zeroVel = false;

    if (g_cfg.tpPulse > 0) {
        nx = g_cfg.tpPos[0];
        ny = g_cfg.tpPos[1];
        nz = g_cfg.tpPos[2];
        if (g_cfg.tpPulse > 0) g_cfg.tpPulse--;
        g_off[0] = g_off[1] = g_off[2] = 0;
        zeroVel = true;
    } else if (g_cfg.speedHack && g_cfg.speedMul > 1.02f) {
        if (g_havePrev) {
            const float k = g_cfg.speedMul - 1.f;
            g_off[0] += (px - g_prev[0]) * k;
            g_off[1] += (py - g_prev[1]) * k;
            g_off[2] += (pz - g_prev[2]) * k;
        }
        nx += g_off[0];
        ny += g_off[1];
        nz += g_off[2];
        g_wasSpeed = true;
    } else if (g_wasSpeed) {
        g_off[0] = g_off[1] = g_off[2] = 0;
        g_wasSpeed = false;
    }

    g_prev[0] = px; g_prev[1] = py; g_prev[2] = pz;
    g_havePrev = true;

    if (nx != px || ny != py || nz != pz) {
        wr_f(d + 7, nx);
        wr_f(d + 11, ny);
        wr_f(d + 15, nz);
    }
    if (zeroVel && len >= PLAYER_VEL) {
        wr_f(d + 41, 0.f);
        wr_f(d + 45, 0.f);
        wr_f(d + 49, 0.f);
    }
}

void vehicleSync(char* d, int len) {
    if (len < VEHICLE_MIN) return;

    if (g_cfg.vehGod) {
        wr_f(d + 49, 1000.f);
        d[53] = 100;
        d[54] = 100;
    }
    if (g_cfg.vehFixPulse > 0) {
        wr_f(d + 49, 1000.f);
        d[53] = 100;
        g_cfg.vehFixPulse--;
    }
    if (g_cfg.vehTpPulse > 0) {
        wr_f(d + 25, g_cfg.vehTpPos[0]);
        wr_f(d + 29, g_cfg.vehTpPos[1]);
        wr_f(d + 33, g_cfg.vehTpPos[2]);
        wr_f(d + 37, 0.f);
        wr_f(d + 41, 0.f);
        wr_f(d + 45, 0.f);
        g_cfg.vehTpPulse--;
    } else if (g_cfg.vehSpeed && g_cfg.vehSpeedMul > 1.02f) {
        wr_f(d + 37, rd_f(d + 37) * g_cfg.vehSpeedMul);
        wr_f(d + 41, rd_f(d + 41) * g_cfg.vehSpeedMul);
        wr_f(d + 45, rd_f(d + 45) * g_cfg.vehSpeedMul);
    }
}

} // namespace rcpkt
