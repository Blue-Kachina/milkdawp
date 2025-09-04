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
   #if defined(HAVE_PROJECTM_PLAYLIST)
    #include <projectM-4/playlist.h>
   #endif
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

// Add this include so we can call methods on LockFreeAudioFifo
#include "../utils/LockFreeAudioFifo.h"

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
    ext.glBufferData(gl::GL_ARRAY_BUFFER, sizeof(verts), verts, gl::GL_DYNAMIC_DRAW);

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
        const bool exists = File(pmPresetDir).isDirectory();
        MDW_LOG("PM", "PresetDir guess: " + pmPresetDir + " exists=" + String(exists ? "true" : "false"));
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
       #if defined(HAVE_PROJECTM)
        #if defined(PM_HAVE_V4)
            // C++ API size update handled internally by engine
        #elif defined(PM_HAVE_V4_C_API)
            if (pmReady && pmHandle != nullptr)
                projectm_set_window_size((projectm_handle) pmHandle, (size_t) fbWidth, (size_t) fbHeight);
        #endif
       #endif
    }
    return true;
}

void ProjectMRenderer::renderOpenGL()
{
    if (!setViewportForCurrentScale()) return;

    // Pull UI params
    const float b = juce::jlimit(0.0f, 2.0f, brightness.load());
    const float sens = juce::jlimit(0.0f, 4.0f, sensitivity.load());

    #if defined(HAVE_PROJECTM)
        initProjectMIfNeeded();
        feedProjectMAudioIfAvailable();
        if (pmReady) {
            // If projectM is active, hand off the frame to it and return.
            renderProjectMFrame();
            return;
        }
    #endif

    // Fallback: derive a visual level from audio FIFO (mono), scaled by sensitivity.
    // This ensures the UI controls are obviously functional even without projectM.
    float level = fallbackLevel;
    if (audioFifo != nullptr)
    {
        constexpr int N = 512;
        float tmp[N];
        int got = audioFifo->pop(tmp, N); // ensure non-const local
        if (got > 0)
        {
            double acc = 0.0;
            for (int i = 0; i < got; ++i) acc += (double) tmp[i] * tmp[i];
            float rms = std::sqrt((float)(acc / jmax(1, got)));
            // Apply sensitivity as a pre-gain into our fallback visual
            rms = juce::jlimit(0.0f, 2.0f, rms * sens);
            // Simple smoothing
            const float a = 0.25f;
            level = level + a * (rms - level);
            fallbackLevel = level;
        }
        else
        {
            // Decay towards zero if no audio available
            level *= 0.95f;
            fallbackLevel = level;
        }
    }
    else
    {
        // Slow decay when no audio feed yet
        level *= 0.98f;
        fallbackLevel = level;
    }

    const float base = juce::jlimit(0.0f, 2.0f, b);
    const float rc = juce::jlimit(0.0f, 1.0f, 0.15f * base + 0.85f * juce::jlimit(0.0f, 1.0f, level));
    const float gc = juce::jlimit(0.0f, 1.0f, 0.10f * base + 0.45f * juce::jlimit(0.0f, 1.0f, level));
    const float bc = juce::jlimit(0.0f, 1.0f, 0.08f * base + 0.35f * juce::jlimit(0.0f, 1.0f, level));

    gl::glClearColor(0.f, 0.f, 0.f, 1.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);

    if (!program) return;
    program->use();
    auto& ext = context.extensions;

    const float verts[] = {
        -0.95f, -0.95f, rc, gc, bc,
         0.95f, -0.95f, rc, gc, bc,
         0.95f,  0.95f, rc, gc, bc,
        -0.95f,  0.95f, rc, gc, bc
    };
    ext.glBindBuffer(gl::GL_ARRAY_BUFFER, vbo);
    ext.glBufferSubData(gl::GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

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
        MDW_LOG("PM", "projectM v4 initialized (C++ API)");
    } catch (const std::exception& e) {
        MDW_LOG("PM", juce::String("Init failed: ") + e.what());
        pmReady = false;
    } catch (...) {
        MDW_LOG("PM", "Init failed: unknown exception");
        pmReady = false;
    }
   #elif defined(PM_HAVE_V4_C_API)
    // C API path
    projectm_handle inst = projectm_create();
    if (inst == nullptr)
    {
        MDW_LOG("PM", "projectM C API: projectm_create() returned null");
        pmReady = false;
        return;
    }

    projectm_set_window_size(inst, (size_t) jmax(1, fbWidth), (size_t) jmax(1, fbHeight));
    projectm_set_aspect_correction(inst, true);
    projectm_set_fps(inst, 60);

   #if defined(HAVE_PROJECTM_PLAYLIST)
    // Create and populate a playlist from our preset directory (if available)
    projectm_playlist_handle playlist = projectm_playlist_create(inst);
    if (playlist != nullptr)
    {
        pmPlaylist = (void*) playlist;

        const auto dir = pmPresetDir.toRawUTF8();
        // recurse_subdirs=true, allow_duplicates=false
        uint32_t added = projectm_playlist_add_path(playlist, dir, true, false);
        MDW_LOG("PM", "C API playlist: added " + String((int) added) + " presets from: " + pmPresetDir);

        // Start playback with a soft transition (hard_cut=false), optionally shuffle
        projectm_playlist_set_shuffle(playlist, true);
        projectm_playlist_play_next(playlist, false);
    }
    else
    {
        MDW_LOG("PM", "C API playlist: projectm_playlist_create() failed; will rely on manual preset loads if any");
    }
   #else
    MDW_LOG("PM", "C API: playlist lib not linked; skipping playlist setup");
   #endif

    pmHandle = (void*) inst;
    pmReady = true;
    MDW_LOG("PM", "projectM v4 initialized (C API)");
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
    pmHandle = nullptr;
    pmReady = false;
    MDW_LOG("PM", "Shutdown (C++ API)");
   #elif defined(PM_HAVE_V4_C_API)
   #if defined(HAVE_PROJECTM_PLAYLIST)
    if (pmPlaylist != nullptr)
    {
        projectm_playlist_destroy((projectm_playlist_handle) pmPlaylist);
        pmPlaylist = nullptr;
    }
   #endif
    projectm_destroy((projectm_handle) pmHandle);
    pmHandle = nullptr;
    pmReady = false;
    MDW_LOG("PM", "Shutdown (C API)");
   #endif
}

void ProjectMRenderer::renderProjectMFrame()
{
    if (!pmReady || !pmHandle) return;
   #if defined(PM_HAVE_V4)
    static_cast<PM::ProjectM*>(pmHandle)->renderFrame();
   #elif defined(PM_HAVE_V4_C_API)
    projectm_opengl_render_frame((projectm_handle) pmHandle);
   #endif
}

void ProjectMRenderer::feedProjectMAudioIfAvailable()
{
   #if defined(PM_HAVE_V4)
    if (!pmReady || !pmHandle || audioFifo == nullptr) return;

    constexpr int kPull = 1024;
    float tmp[kPull];
    int popped = 0;
    do {
        int got = audioFifo->pop(tmp, kPull); // ensure non-const local
        if (got <= 0) break;
        popped += got;

        try {
            auto* engine = static_cast<PM::ProjectM*>(pmHandle);
            // TODO: Replace with the correct audio ingestion API for your C++ projectM build, if used.
            // e.g., engine->addPCMfloat(tmp, got, audioSampleRate);
        } catch (...) {
        }
    } while (popped < 8192);
   #elif defined(PM_HAVE_V4_C_API)
    if (!pmReady || !pmHandle || audioFifo == nullptr) return;

    constexpr int kPull = 1024;
    float tmp[kPull];
    int popped = 0;
    const float sens = juce::jlimit(0.0f, 4.0f, sensitivity.load());

    do {
        int got = audioFifo->pop(tmp, kPull); // ensure non-const local
        if (got <= 0) break;
        popped += got;

        // Apply sensitivity as pre-gain (clamped to a sane range)
        if (sens != 1.0f)
        {
            for (int i = 0; i < got; ++i)
                tmp[i] = juce::jlimit(-1.5f, 1.5f, tmp[i] * sens);
        }

        // Feed as mono; projectM will handle stereo internally
        projectm_pcm_add_float((projectm_handle) pmHandle, tmp, (unsigned int) got, PROJECTM_MONO);
    } while (popped < 8192);
   #endif
}
#endif
