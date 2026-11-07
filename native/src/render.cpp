// GLES3 immediate-mode overlay renderer.
#include "rc.h"
#include "font_atlas.h"
#include <vector>

namespace rcgl {

static const char* VS_SOLID = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aLocal;
layout(location=2) in vec2 aHalf;
layout(location=3) in float aRad;
layout(location=4) in vec4 aCol;
layout(location=5) in float aMode;
uniform vec2 uScreen;
out vec2 vLocal; out vec2 vHalf; out float vRad; out vec4 vCol; out float vMode;
void main(){
  vLocal = aLocal; vHalf = aHalf; vRad = aRad; vCol = aCol; vMode = aMode;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

static const char* FS_SOLID = R"(#version 300 es
precision mediump float;
in vec2 vLocal; in vec2 vHalf; in float vRad; in vec4 vCol; in float vMode;
uniform sampler2D uTex; uniform vec2 uScreen; uniform float uSoft;
out vec4 o;
float sdBox(vec2 p, vec2 h, float r){
  vec2 q = abs(p) - (h - vec2(r));
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main(){
  float d = sdBox(vLocal, vHalf, vRad);
  if (vMode < 0.5) {
    float a = clamp(0.5 - d, 0.0, 1.0);
    o = vec4(vCol.rgb, vCol.a * a);
  } else if (vMode < 1.5) {
    float a = 1.0 - clamp(d / max(uSoft, 1.0) + 0.5, 0.0, 1.0);
    o = vec4(vCol.rgb, vCol.a * a * a);
  } else {
    vec2 uv = gl_FragCoord.xy / uScreen;
    float r = max(uSoft, 1.0);
    vec3 c = texture(uTex, uv).rgb * 0.20;
    c += texture(uTex, uv + vec2( r, 0.0) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2(-r, 0.0) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2(0.0,  r) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2(0.0, -r) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2( r * 0.6,  r * 0.6) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2(-r * 0.6,  r * 0.6) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2( r * 0.6, -r * 0.6) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2(-r * 0.6, -r * 0.6) / uScreen).rgb * 0.06;
    c += texture(uTex, uv + vec2( r * 1.6, 0.0) / uScreen).rgb * 0.03;
    c += texture(uTex, uv + vec2(-r * 1.6, 0.0) / uScreen).rgb * 0.03;
    c += texture(uTex, uv + vec2(0.0,  r * 1.6) / uScreen).rgb * 0.03;
    c += texture(uTex, uv + vec2(0.0, -r * 1.6) / uScreen).rgb * 0.03;
    float a = clamp(0.5 - d, 0.0, 1.0);
    o = vec4(mix(c, vCol.rgb, clamp(vCol.a, 0.0, 1.0)), a);
  }
})";

static const char* VS_TEXT = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vCol;
void main(){
  vUV = aUV; vCol = aCol;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

static const char* FS_TEXT = R"(#version 300 es
precision mediump float;
in vec2 vUV; in vec4 vCol;
uniform sampler2D uTex;
out vec4 o;
void main(){
  float a = texture(uTex, vUV).a;
  o = vec4(vCol.rgb, vCol.a * a);
})";


// ---------------------------------------------------------------- ES 2.0 path
// Some launchers create an ES 2.0 context. A 300 es shader fails to compile
// there and the overlay would silently render nothing, so keep a full ES2 copy.
static const char* VS_SOLID_100 = R"(#version 100
attribute vec2 aPos;
attribute vec2 aLocal;
attribute vec2 aHalf;
attribute float aRad;
attribute vec4 aCol;
attribute float aMode;
uniform vec2 uScreen;
varying vec2 vLocal; varying vec2 vHalf; varying float vRad; varying vec4 vCol; varying float vMode;
void main(){
  vLocal = aLocal; vHalf = aHalf; vRad = aRad; vCol = aCol; vMode = aMode;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

static const char* FS_SOLID_100 = R"(#version 100
precision mediump float;
varying vec2 vLocal; varying vec2 vHalf; varying float vRad; varying vec4 vCol; varying float vMode;
uniform sampler2D uTex; uniform vec2 uScreen; uniform float uSoft;
float sdBox(vec2 p, vec2 h, float r){
  vec2 q = abs(p) - (h - vec2(r));
  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}
void main(){
  float d = sdBox(vLocal, vHalf, vRad);
  if (vMode < 0.5) {
    float a = clamp(0.5 - d, 0.0, 1.0);
    gl_FragColor = vec4(vCol.rgb, vCol.a * a);
  } else if (vMode < 1.5) {
    float a = 1.0 - clamp(d / max(uSoft, 1.0) + 0.5, 0.0, 1.0);
    gl_FragColor = vec4(vCol.rgb, vCol.a * a * a);
  } else {
    vec2 uv = gl_FragCoord.xy / uScreen;
    float r = max(uSoft, 1.0);
    vec3 c = texture2D(uTex, uv).rgb * 0.20;
    c += texture2D(uTex, uv + vec2( r, 0.0) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2(-r, 0.0) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2(0.0,  r) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2(0.0, -r) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2( r * 0.6,  r * 0.6) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2(-r * 0.6,  r * 0.6) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2( r * 0.6, -r * 0.6) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2(-r * 0.6, -r * 0.6) / uScreen).rgb * 0.06;
    c += texture2D(uTex, uv + vec2( r * 1.6, 0.0) / uScreen).rgb * 0.03;
    c += texture2D(uTex, uv + vec2(-r * 1.6, 0.0) / uScreen).rgb * 0.03;
    c += texture2D(uTex, uv + vec2(0.0,  r * 1.6) / uScreen).rgb * 0.03;
    c += texture2D(uTex, uv + vec2(0.0, -r * 1.6) / uScreen).rgb * 0.03;
    float a = clamp(0.5 - d, 0.0, 1.0);
    gl_FragColor = vec4(mix(c, vCol.rgb, clamp(vCol.a, 0.0, 1.0)), a);
  }
})";

static const char* VS_TEXT_100 = R"(#version 100
attribute vec2 aPos;
attribute vec2 aUV;
attribute vec4 aCol;
uniform vec2 uScreen;
varying vec2 vUV; varying vec4 vCol;
void main(){
  vUV = aUV; vCol = aCol;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

static const char* FS_TEXT_100 = R"(#version 100
precision mediump float;
varying vec2 vUV; varying vec4 vCol;
uniform sampler2D uTex;
void main(){
  float a = texture2D(uTex, vUV).a;
  gl_FragColor = vec4(vCol.rgb, vCol.a * a);
})";

static const char* ATTR_SOLID[] = {"aPos", "aLocal", "aHalf", "aRad", "aCol", "aMode"};
static const char* ATTR_TEXT[]  = {"aPos", "aUV", "aCol"};

static void flush();

static GLuint progSolid = 0, progText = 0;
static GLuint uSolidScreen = -1, uSolidTex = -1, uSolidSoft = -1;
static GLuint uTextScreen = -1, uTextTex = -1;
static GLuint vboSolid = 0, vboText = 0, vaoSolid = 0, vaoText = 0;
static GLuint texAtlas = 0, texScreen = 0;
static int   texScreenW = 0, texScreenH = 0;
static int   screenW = 1, screenH = 1;
static bool  ready = false;
static bool  es3 = true;      // ES 3.0 context (shaders + VAOs available)

static std::vector<float> vSolid, vText;
static int  batchMode = -1;   // 0 = solid, 1 = text
static float curSoft = 6.0f;

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        rc_log("shader error: %s", log);
        return 0;
    }
    return s;
}

static GLuint link(const char* vs, const char* fs, const char** attrs, int nattr) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    if (!es3) {
        // ES2 has no layout(location=), so bind before linking
        for (int i = 0; i < nattr; i++) glBindAttribLocation(p, (GLuint)i, attrs[i]);
    }
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { rc_log("link error"); return 0; }
    return p;
}

bool init() {
    if (ready) return true;

    const char* ver = (const char*)glGetString(GL_VERSION);
    es3 = true;
    if (ver) {
        // "OpenGL ES 3.2 v1.r32..." / "OpenGL ES 2.0 ..."
        int major = 0;
        const char* p = ver;
        while (*p && !(*p >= '0' && *p <= '9')) p++;
        if (*p) major = *p - '0';
        es3 = major >= 3;
        rc_log("гласы: %s -> ветка ES%d", ver, es3 ? 3 : 2);
    }
    progSolid = es3 ? link(VS_SOLID, FS_SOLID, nullptr, 0)
                    : link(VS_SOLID_100, FS_SOLID_100, ATTR_SOLID, 6);
    progText  = es3 ? link(VS_TEXT, FS_TEXT, nullptr, 0)
                    : link(VS_TEXT_100, FS_TEXT_100, ATTR_TEXT, 3);
    if (!progSolid || !progText) return false;

    uSolidScreen = glGetUniformLocation(progSolid, "uScreen");
    uSolidTex    = glGetUniformLocation(progSolid, "uTex");
    uSolidSoft   = glGetUniformLocation(progSolid, "uSoft");
    uTextScreen  = glGetUniformLocation(progText, "uScreen");
    uTextTex     = glGetUniformLocation(progText, "uTex");

    if (es3) glGenVertexArrays(1, &vaoSolid);
    glGenBuffers(1, &vboSolid);
    if (es3) glBindVertexArray(vaoSolid);
    glBindBuffer(GL_ARRAY_BUFFER, vboSolid);
    glBufferData(GL_ARRAY_BUFFER, 1 << 21, nullptr, GL_DYNAMIC_DRAW);

    if (es3) glGenVertexArrays(1, &vaoText);
    glGenBuffers(1, &vboText);
    if (es3) glBindVertexArray(vaoText);
    glBindBuffer(GL_ARRAY_BUFFER, vboText);
    glBufferData(GL_ARRAY_BUFFER, 1 << 20, nullptr, GL_DYNAMIC_DRAW);

    glGenTextures(1, &texAtlas);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)rc_atlas_w, (GLsizei)rc_atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rc_atlas_px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ready = true;
    rc_log("рендер готов: атлас %ux%u, шрифт %d, ES%d", rc_atlas_w, rc_atlas_h,
           rc_font_size, es3 ? 3 : 2);
    return true;
}

void beginFrame(int w, int h) {
    screenW = w > 0 ? w : 1;
    screenH = h > 0 ? h : 1;
    vSolid.clear();
    vText.clear();
    batchMode = -1;
    glViewport(0, 0, screenW, screenH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void captureScreen(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (texScreen == 0 || w != texScreenW || h != texScreenH) {
        if (texScreen) glDeleteTextures(1, &texScreen);
        glGenTextures(1, &texScreen);
        glBindTexture(GL_TEXTURE_2D, texScreen);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        texScreenW = w; texScreenH = h;
    }
    flush();
    glBindTexture(GL_TEXTURE_2D, texScreen);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
}

static void flushSolid() {
    if (vSolid.empty()) return;
    glUseProgram(progSolid);
    glUniform2f(uSolidScreen, (float)screenW, (float)screenH);
    glUniform1i(uSolidTex, 0);
    glUniform1f(uSolidSoft, curSoft);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texScreen);
    if (es3) glBindVertexArray(vaoSolid);
    glBindBuffer(GL_ARRAY_BUFFER, vboSolid);
    size_t bytes = vSolid.size() * sizeof(float);
    if (bytes > (1u << 21)) bytes = 1u << 21;
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vSolid.data());
    const GLsizei stride = 12 * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * 4));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(4 * 4));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * 4));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(7 * 4));
    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)(11 * 4));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(bytes / (12 * sizeof(float))));
    vSolid.clear();
}

static void flushText() {
    if (vText.empty()) return;
    glUseProgram(progText);
    glUniform2f(uTextScreen, (float)screenW, (float)screenH);
    glUniform1i(uTextTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    if (es3) glBindVertexArray(vaoText);
    glBindBuffer(GL_ARRAY_BUFFER, vboText);
    size_t bytes = vText.size() * sizeof(float);
    if (bytes > (1u << 20)) bytes = 1u << 20;
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vText.data());
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * 4));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * 4));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(bytes / (8 * sizeof(float))));
    vText.clear();
}

void flush() {
    if (batchMode == 1) { flushText(); }
    else if (batchMode == 0) { flushSolid(); }
    batchMode = -1;
}

void endFrame() {
    flush();
    if (es3) glBindVertexArray(0);
    glUseProgram(0);
}

bool isES3() { return es3; }

static void quadSolid(float x, float y, float w, float h, float radius, RCColor c, int mode) {
    if (!ready) return;
    if (batchMode == 1) flushText();
    batchMode = 0;
    const float hx = w * 0.5f, hy = h * 0.5f;
    const float cx = x + hx, cy = y + hy;
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    const float lx0 = -hx, ly0 = -hy, lx1 = hx, ly1 = hy;
    const float v[6][12] = {
        {x0, y0, lx0, ly0, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
        {x1, y0, lx1, ly0, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
        {x1, y1, lx1, ly1, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
        {x0, y0, lx0, ly0, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
        {x1, y1, lx1, ly1, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
        {x0, y1, lx0, ly1, hx, hy, radius, c.r, c.g, c.b, c.a, (float)mode},
    };
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 12; k++) vSolid.push_back(v[i][k]);
}

void rect(float x, float y, float w, float h, float radius, RCColor c) {
    quadSolid(x, y, w, h, radius, c, 0);
}

void circle(float cx, float cy, float r, RCColor c) {
    quadSolid(cx - r, cy - r, r * 2, r * 2, r, c, 0);
}

void shadow(float x, float y, float w, float h, float radius, float soft, RCColor c) {
    float saved = curSoft;
    curSoft = soft;
    quadSolid(x, y, w, h, radius, c, 1);
    curSoft = saved;
}

void glass(float x, float y, float w, float h, float radius, RCColor c) {
    float saved = curSoft;
    curSoft = 7.0f;
    quadSolid(x, y, w, h, radius, c, 2);
    curSoft = saved;
}

void border(float x, float y, float w, float h, float radius, float thick, RCColor c) {
    // draw four thin rects forming a rounded outline is overkill: draw two rounded rects
    rect(x, y, w, h, radius, RCColor(c.r, c.g, c.b, c.a * 0.55f));
    rect(x + thick, y + thick, w - thick * 2, h - thick * 2, radius > thick ? radius - thick : 0,
         RCColor(c.r, c.g, c.b, 0.0f));
}

static const RCGlyph* find_glyph(uint32_t cp) {
    for (unsigned i = 0; i < rc_glyphs_count; i++)
        if (rc_glyphs[i].cp == cp) return &rc_glyphs[i];
    return nullptr;
}

static void quadText(float x, float y, float w, float h, float u0, float v0, float u1, float v1, RCColor c) {
    if (batchMode == 0) flushSolid();
    batchMode = 1;
    const float v[6][8] = {
        {x, y, u0, v0, c.r, c.g, c.b, c.a},
        {x + w, y, u1, v0, c.r, c.g, c.b, c.a},
        {x + w, y + h, u1, v1, c.r, c.g, c.b, c.a},
        {x, y, u0, v0, c.r, c.g, c.b, c.a},
        {x + w, y + h, u1, v1, c.r, c.g, c.b, c.a},
        {x, y + h, u0, v1, c.r, c.g, c.b, c.a},
    };
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 8; k++) vText.push_back(v[i][k]);
}

void text(float x, float y, const char* s, float size, RCColor c) {
    if (!ready || !s) return;
    const float scale = size / (float)rc_font_size;
    float cx = x;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xC0) {
            if ((cp & 0xE0) == 0xC0) { uint32_t b1 = *p++; cp = ((cp & 0x1F) << 6) | (b1 & 0x3F); }
            else if ((cp & 0xF0) == 0xE0) { uint32_t b1 = *p++, b2 = *p++; cp = ((cp & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F); }
            else if ((cp & 0xF8) == 0xF0) { uint32_t b1 = *p++, b2 = *p++, b3 = *p++; cp = ((cp & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F); }
        }
        if (cp == '\n') { continue; }
        const RCGlyph* g = find_glyph(cp);
        if (!g) g = find_glyph('?');
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            const float u0 = (float)g->x / (float)rc_atlas_w;
            const float v0 = (float)g->y / (float)rc_atlas_h;
            const float u1 = (float)(g->x + g->w) / (float)rc_atlas_w;
            const float v1 = (float)(g->y + g->h) / (float)rc_atlas_h;
            quadText(cx + g->bx * scale, y + g->by * scale, g->w * scale, g->h * scale, u0, v0, u1, v1, c);
        }
        cx += g->adv * scale;
    }
}

float textWidth(const char* s, float size) {
    const float scale = size / (float)rc_font_size;
    float w = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xC0) {
            if ((cp & 0xE0) == 0xC0) { uint32_t b1 = *p++; cp = ((cp & 0x1F) << 6) | (b1 & 0x3F); }
            else if ((cp & 0xF0) == 0xE0) { uint32_t b1 = *p++, b2 = *p++; cp = ((cp & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F); }
            else if ((cp & 0xF8) == 0xF0) { uint32_t b1 = *p++, b2 = *p++, b3 = *p++; cp = ((cp & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F); }
        }
        const RCGlyph* g = find_glyph(cp);
        if (!g) g = find_glyph('?');
        if (g) w += g->adv * scale;
    }
    return w;
}

float lineHeight(float size) { return size * 1.22f; }

} // namespace rcgl
