// RakCRMP Cheat — common definitions
// Target: com.br.top (RakCRMP / Black Russia launcher), arm64-v8a
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <jni.h>

#define RC_TAG "RakCRMPCheat"
#define RC_VERSION "1.0.3"

// ---------------------------------------------------------------- logging
void  rc_log(const char* fmt, ...);
const char* rc_log_line(int idx);   // idx 0 = newest
int         rc_log_count();

// ---------------------------------------------------------------- time
static inline double rc_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------- GL (own decls: NDK headers not needed)
typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef int            GLsizei;
typedef float          GLfloat;
typedef float          GLclampf;
typedef char           GLchar;
typedef intptr_t       GLintptr;
typedef ptrdiff_t      GLsizeiptr;

typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;

#define GL_TRIANGLES            0x0004
#define GL_FLOAT                0x1406
#define GL_UNSIGNED_BYTE        0x1401
#define GL_TRUE                 1
#define GL_FALSE                0
#define GL_ARRAY_BUFFER         0x8892
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_TEXTURE_2D           0x0DE1
#define GL_TEXTURE0             0x84C0
#define GL_RGBA                 0x1908
#define GL_RGBA8                0x8058
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_LINEAR               0x2601
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_BLEND                0x0BE2
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_ONE                  1
#define GL_DEPTH_TEST           0x0B71
#define GL_CULL_FACE            0x0B44
#define GL_SCISSOR_TEST         0x0C11
#define GL_FRAMEBUFFER          0x8D40
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#define GL_VIEWPORT             0x0BA2
#define GL_COLOR_BUFFER_BIT     0x4000
#define GL_DEPTH_BUFFER_BIT     0x0100
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define EGL_WIDTH               0x3057
#define EGL_HEIGHT              0x3056

extern "C" {
GLuint  glCreateShader(GLenum);
void    glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
void    glCompileShader(GLuint);
void    glGetShaderiv(GLuint, GLenum, GLint*);
void    glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
void    glDeleteShader(GLuint);
GLuint  glCreateProgram(void);
void    glAttachShader(GLuint, GLuint);
void    glLinkProgram(GLuint);
void    glGetProgramiv(GLuint, GLenum, GLint*);
void    glUseProgram(GLuint);
GLint   glGetUniformLocation(GLuint, const GLchar*);
void    glUniform1i(GLint, GLint);
void    glUniform2f(GLint, GLfloat, GLfloat);
void    glGenBuffers(GLsizei, GLuint*);
void    glBindBuffer(GLenum, GLuint);
void    glBufferData(GLenum, GLsizeiptr, const void*, GLenum);
void    glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void*);
void    glGenVertexArrays(GLsizei, GLuint*);
void    glBindVertexArray(GLuint);
void    glEnableVertexAttribArray(GLuint);
void    glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
void    glDrawArrays(GLenum, GLint, GLsizei);
void    glGenTextures(GLsizei, GLuint*);
void    glBindTexture(GLenum, GLuint);
void    glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void    glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
void    glTexParameteri(GLenum, GLenum, GLint);
void    glActiveTexture(GLenum);
void    glPixelStorei(GLenum, GLint);
void    glCopyTexSubImage2D(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
void    glEnable(GLenum);
void    glDisable(GLenum);
void    glBlendFunc(GLenum, GLenum);
void    glViewport(GLint, GLint, GLsizei, GLsizei);
void    glGetIntegerv(GLenum, GLint*);
void    glDeleteTextures(GLsizei, const GLuint*);
EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint*);
void    glUniform1f(GLint, GLfloat);
}

// ---------------------------------------------------------------- font atlas (generated in CI)
struct RCGlyph { uint32_t cp; int16_t x, y, w, h, adv, bx, by; };
extern const unsigned char rc_atlas_px[];
extern const unsigned int  rc_atlas_w;
extern const unsigned int  rc_atlas_h;
extern const RCGlyph      rc_glyphs[];
extern const unsigned int  rc_glyphs_count;
extern const int           rc_font_size;

// ---------------------------------------------------------------- colour
struct RCColor {
    float r, g, b, a;
    RCColor() : r(1), g(1), b(1), a(1) {}
    RCColor(float R, float G, float B, float A) : r(R), g(G), b(B), a(A) {}
    RCColor withA(float A) const { return RCColor(r, g, b, A); }
};
static inline RCColor rc_rgb(uint32_t hex, float a = 1.f) {
    return RCColor(((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, a);
}

// ---------------------------------------------------------------- config
struct RcConfig {
    bool  immortal      = false;
    bool  fastHeal      = false;
    bool  speedHack     = false;
    float speedMul      = 1.8f;
    bool  tpActive      = false;
    float tpPos[3]      = {0, 0, 0};
    bool  vehGod        = false;
    bool  vehSpeed      = false;
    float vehSpeedMul   = 1.8f;
    bool  vehTp         = false;
    float vehTpPos[3]   = {0, 0, 0};
    bool  slava         = false;
    bool  autoDialog    = true;
    bool  autoCall      = true;
    bool  autoMini      = true;
    bool  giftAuto      = true;
    bool  autoCaptcha   = false;
    bool  showButton    = true;
    // одноразовые действия (ставит UI, сбрасывает сетевой слой)
    int   tpPulse       = 0;
    int   vehFixPulse   = 0;
    int   vehTpPulse    = 0;
};
extern RcConfig g_cfg;

// ---------------------------------------------------------------- net stats
struct RcNetStats {
    int   txTotal    = 0;
    int   rxTotal    = 0;
    int   jsonOut    = 0;
    int   jsonIn     = 0;
    int   syncOut    = 0;
    int   hpPatched  = 0;
    int   lastTxId   = -1;
    int   lastTxLen  = 0;
    int   lastRxId   = -1;
    int   lastRxLen  = 0;
    int   hooksOk    = 0;
    int   hooksFail  = 0;
    int   slavaStage = 0;
    float lastHp     = 0;
    float touchX     = 0;
    float touchY     = 0;
    int   touchAct   = -1;
    int   rawTouch[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    char  questText[160] = {0};
    float lastPos[3] = {0, 0, 0};
};
extern RcNetStats g_net;

// ---------------------------------------------------------------- hook engine
namespace rchook {
struct Hook {
    void*     target    = nullptr;
    void*     tramp     = nullptr;
    uint32_t  saved[4]  = {0, 0, 0, 0};
    bool      installed = false;
};
bool  install(Hook& h, void* target, void* detour);
void* resolve(void* handle, const char* sym);
uintptr_t image_base(const char* soname);
void* sym_or_offset(void* handle, const char* name, uintptr_t offset);
}

// ---------------------------------------------------------------- render
namespace rcgl {
bool  init();
void  beginFrame(int w, int h);
void  endFrame();
void  captureScreen(int w, int h);
void  rect(float x, float y, float w, float h, float radius, RCColor c);
void  circle(float cx, float cy, float r, RCColor c);
void  shadow(float x, float y, float w, float h, float radius, float soft, RCColor c);
void  glass(float x, float y, float w, float h, float radius, RCColor c);
void  border(float x, float y, float w, float h, float radius, float thick, RCColor c);
void  text(float x, float y, const char* s, float size, RCColor c);
float textWidth(const char* s, float size);
float lineHeight(float size);
}

// ---------------------------------------------------------------- ui
namespace rcui {
struct Input {
    float x, y;      // primary pointer
    float x2, y2;    // second pointer
    float x3, y3;    // third pointer
    int   action;    // 0 = down, 1 = up, 2 = move
    int   id;
};
bool  onInput(const Input& in);
void  draw(int w, int h, float dt);
bool  wantsCapture();
void  setScreen(int w, int h);
float screenW();
float screenH();
}

// ---------------------------------------------------------------- net
namespace rcnet {
void install();
void setJni(JavaVM* vm, jclass cls, jmethodID deliver, jmethodID sendJson);
void tick(double now);
}
