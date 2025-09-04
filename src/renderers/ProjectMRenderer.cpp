
#include "ProjectMRenderer.h"
#include "../utils/Logging.h"
using namespace juce;

#if defined(HAVE_PROJECTM)
 #if __has_include(<libprojectM/projectM.hpp>)
  #include <libprojectM/projectM.hpp>
  namespace PM = libprojectM;
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM/projectM.hpp>)
  #include <projectM/projectM.hpp>
  namespace PM = projectM;
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM-4/projectM.hpp>)
  #include <projectM-4/projectM.hpp>
  namespace PM = projectM4; // vcpkg can expose this as a distinct namespace; adjust if needed
  #define PM_HAVE_V4 1
 #elif __has_include(<projectM-4/projectM.h>) || defined(PROJECTM4_C_API)
  extern "C" {
    #include <projectM-4/projectM.h>
  }
  #define PM_HAVE_V4_C_API 1
 #else
  #if defined(_MSC_VER)
   #pragma message("HAVE_PROJECTM set, but v4 headers not found")
  #else
   #warning "HAVE_PROJECTM set, but v4 headers not found"
  #endif
 #endif
#endif

static const char* VS_150 = R"(#version 150 core
in vec2 aPos;
in vec3 aCol;
out vec3 vCol;
void main(){ vCol=aCol; gl_Position=vec4(aPos,0.0,1.0); })";
static const char* FS_150 = R"(#version 150 core
in vec3 vCol; out vec4 FragColor;
void main(){ FragColor=vec4(vCol,1.0); })";

ProjectMRenderer::ProjectMRenderer(juce::OpenGLContext& ctx, LockFreeAudioFifo* f, int sr)
    : context(ctx), audioFifo(f), audioSampleRate(sr) {}
ProjectMRenderer::~ProjectMRenderer() = default;

void ProjectMRenderer::setAudioSource(LockFreeAudioFifo* f, int sr)
{
    audioFifo = f;
    audioSampleRate = sr;
}

void ProjectMRenderer::newOpenGLContextCreated()
{
    DBG("[GL] Version: "  + String((const char*) gl::glGetString(gl::GL_VERSION)));
    DBG("[GL] Renderer: " + String((const char*) gl::glGetString(gl::GL_RENDERER)));
    DBG("[GL] Vendor: "   + String((const char*) gl::glGetString(gl::GL_VENDOR)));

    auto tryMakeProgram = [&](const char* vsrc, const char* fsrc) -> std::unique_ptr<OpenGLShaderProgram>
    {
        auto prog = std::make_unique<OpenGLShaderProgram>(context);
        bool ok = prog->addVertexShader(CharPointer_UTF8(vsrc))
               && prog->addFragmentShader(CharPointer_UTF8(fsrc))
               && prog->link();
        if (!ok)
        {
            DBG(String("[GL] Shader compile/link failed: ") + prog->getLastError());
            return nullptr;
        }
        return prog;
    };

    program = tryMakeProgram(VS_150, FS_150);
    if (!program) return;

    attrPos = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aPos");
    attrCol = std::make_unique<OpenGLShaderProgram::Attribute>(*program, "aCol");

    auto& ext = context.extensions;
    const float verts[] = {
        -0.95f, -0.95f, 1.f, 1.f, 1.f,
         0.95f, -0.95f, 1.f, 1.f, 1.f,
         0.95f,  0.95f, 1.f, 1.f, 1.f,
        -0.95f,  0.95f, 1.f, 1.f, 1.f
    };

    ext.glGenVertexArrays(1, &vao);
    ext.glBindVertexArray(vao);

    ext.glGenBuffers(1, &vbo);
    ext.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
    ext.glBufferData(gl::GL_ARRAY_BUFFER, sizeof(verts), verts, gl::GL_STATIC_DRAW);

    const GLsizei stride = (GLsizei) (sizeof(float) * 5);
    const GLvoid* posPtr = (const GLvoid*) 0;
    const GLvoid* colPtr = (const GLvoid*) (sizeof(float) * 2);

    if (attrPos && attrPos->attributeID >= 0)
    {
        ext.glEnableVertexAttribArray((GLuint) attrPos->attributeID);
        ext.glVertexAttribPointer((GLuint) attrPos->attributeID, 2, gl::GL_FLOAT, gl::GL_FALSE, stride, posPtr);
    }
    if (attrCol && attrCol->attributeID >= 0)
    {
        ext.glEnableVertexAttribArray((GLuint) attrCol->attributeID);
        ext.glVertexAttribPointer((GLuint) attrCol->attributeID, 3, gl::GL_FLOAT, gl::GL_FALSE, stride, colPtr);
    }

    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glClearColor(0.f, 0.f, 0.f, 1.f);

    #if defined(HAVE_PROJECTM)
        pmPresetDir = File::getSpecialLocation(File::currentApplicationFile)
                        .getParentDirectory().getParentDirectory().getParentDirectory() // .../MilkDAWp.vst3
                        .getChildFile("Contents").getChildFile("Resources").getChildFile("presets")
                        .getFullPathName();
        MDW_LOG("PM", "PresetDir guess: " + pmPresetDir);
    #endif
}

bool ProjectMRenderer::setViewportForCurrentScale()
{
    auto* comp = context.getTargetComponent();
    if (!comp) return false;

    const float scale = context.getRenderingScale();
    const int w = jmax(1, roundToInt((float) comp->getWidth()  * scale));
    const int h = jmax(1, roundToInt((float) comp->getHeight() * scale));

    if (w != fbWidth || h != fbHeight)
    {
        fbWidth = w; fbHeight = h;
        gl::glViewport(0, 0, fbWidth, fbHeight);
    }
    return true;
}

void ProjectMRenderer::renderOpenGL()
{
    if (!setViewportForCurrentScale()) return;

    // Apply brightness to background each frame (simple visual verification of UI -> renderer)
    const float b = juce::jlimit(0.0f, 2.0f, brightness.load());
    const float bg = juce::jlimit(0.0f, 1.0f, 0.5f * b);
    gl::glClearColor(bg, bg, bg, 1.0f);

    gl::glClear(gl::GL_COLOR_BUFFER_BIT);

    #if defined(HAVE_PROJECTM)
        initProjectMIfNeeded();
        feedProjectMAudioIfAvailable();
        if (pmReady) { renderProjectMFrame(); return; }
    #endif

    if (!program) return;
    program->use();
    auto& ext = context.extensions;
    ext.glBindVertexArray(vao);
    gl::glDrawArrays(gl::GL_TRIANGLE_FAN, 0, 4);
}

void ProjectMRenderer::openGLContextClosing()
{
    #if defined(HAVE_PROJECTM)
        shutdownProjectM();
    #endif
    auto& ext = context.extensions;
    if (vbo != 0) { ext.glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao != 0) { ext.glDeleteVertexArrays(1, &vao); vao = 0; }
    program.reset();
}

#if defined(HAVE_PROJECTM)
void ProjectMRenderer::initProjectMIfNeeded()
{
    if (pmReady) return;
   #if defined(PM_HAVE_V4)
    try {
        PM::Settings settings{};
        juce::File pd(pmPresetDir);
        if (pd.isDirectory())
            settings.presetPath = pd.getFullPathName().toStdString();
        settings.windowWidth  = juce::jmax(1, fbWidth);
        settings.windowHeight = juce::jmax(1, fbHeight);
        settings.meshX = 48; settings.meshY = 36; // typical defaults
        auto* engine = new PM::ProjectM(settings);
        pmHandle = static_cast<void*>(engine);
        pmReady = true;
        MDW_LOG("PM", "projectM v4 initialized");
    } catch (const std::exception& e) {
        MDW_LOG("PM", juce::String("Init failed: ") + e.what());
        pmReady = false;
    } catch (...) {
        MDW_LOG("PM", "Init failed: unknown exception");
        pmReady = false;
    }
   #elif defined(PM_HAVE_V4_C_API)
    static std::atomic<bool> logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true))
        MDW_LOG("PM", "projectM v4 C API detected, C++ API not available; skipping init (no C API binding yet)");
    pmReady = false;
   #else
    static std::atomic<bool> logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true))
        MDW_LOG("PM", "v4 headers not found at compile time");
    pmReady = false;
   #endif
}

void ProjectMRenderer::shutdownProjectM()
{
    if (!pmHandle) return;
   #if defined(PM_HAVE_V4)
    try { delete static_cast<PM::ProjectM*>(pmHandle); } catch (...) {}
   #endif
    pmHandle = nullptr;
    pmReady = false;
    MDW_LOG("PM", "Shutdown");
}

void ProjectMRenderer::renderProjectMFrame()
{
    if (!pmReady || !pmHandle) return;
   #if defined(PM_HAVE_V4)
    static_cast<PM::ProjectM*>(pmHandle)->renderFrame();
   #endif
}

void ProjectMRenderer::feedProjectMAudioIfAvailable()
{
   #if defined(PM_HAVE_V4)
    if (!pmReady || !pmHandle || audioFifo == nullptr) return;

    // Pull small chunks to keep audio current without stalling GL
    constexpr int kPull = 1024;
    float tmp[kPull];
    int popped = 0;
    do {
        const int got = audioFifo->pop(tmp, kPull);
        if (got <= 0) break;
        popped += got;

        try {
            auto* engine = static_cast<PM::ProjectM*>(pmHandle);
            // TODO: Replace with the correct audio ingestion API for your projectM build.
            // Common variants found in v4 builds (choose one your headers provide):
            // engine->pcm()->addPCM(tmp, got);
            // engine->addPCMfloat(tmp, got, audioSampleRate);
            // engine->audioSamples(tmp, got, audioSampleRate);
        } catch (...) {
            // Avoid per-frame logging during bring-up
        }
    } while (popped < 8192);
   #endif
}
#endif
