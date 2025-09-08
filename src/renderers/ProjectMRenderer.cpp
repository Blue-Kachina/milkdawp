#include "ProjectMRenderer.h"

#include <excpt.h>

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

#if defined(HAVE_PROJECTM) && defined(PM_HAVE_V4_C_API)
// SEH-safe minimal initializer for projectM C API.
// Important: keep this function free of C++ objects with destructors in scope.
static projectm_handle mdw_seh_projectm_minimal_init(size_t w, size_t h, int* outOk) noexcept
{
    if (outOk) *outOk = 0;
    projectm_handle inst = nullptr;
   #if defined(_MSC_VER)
    __try
    {
   #endif
        inst = projectm_create();
        if (inst != nullptr)
        {
            projectm_set_window_size(inst, w, h);
            projectm_set_aspect_correction(inst, 1 /*true*/);
            projectm_set_fps(inst, 60);
            if (outOk) *outOk = 1;
        }
   #if defined(_MSC_VER)
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        inst = nullptr;
        if (outOk) *outOk = 0;
    }
   #endif
    return inst;
}

// SEH-safe wrappers for render and PCM ingestion (minimal)
static int mdw_seh_projectm_render(projectm_handle h) noexcept
{
   #if defined(_MSC_VER)
    __try { projectm_opengl_render_frame(h); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
   #else
    projectm_opengl_render_frame(h); return 1;
   #endif
}

static int mdw_seh_projectm_pcm_add_mono(projectm_handle h, float* data, unsigned int count) noexcept
{
   #if defined(_MSC_VER)
    __try { projectm_pcm_add_float(h, data, count, PROJECTM_MONO); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
   #else
    projectm_pcm_add_float(h, data, count, PROJECTM_MONO); return 1;
   #endif
}
#endif

ProjectMRenderer::ProjectMRenderer(juce::OpenGLContext& ctx, LockFreeAudioFifo* f, int sr)
    : context(ctx), audioFifo(f), audioSampleRate(sr) {}
ProjectMRenderer::~ProjectMRenderer() = default;

void ProjectMRenderer::setAudioSource(LockFreeAudioFifo* f, int sr)
{
    audioFifo = f;
    audioSampleRate = sr;
}

// Provide the missing definition (unconditional)
bool ProjectMRenderer::setViewportForCurrentScale()
{
    auto* comp = context.getTargetComponent();
    if (comp == nullptr)
        return false;

    const float scale = context.getRenderingScale();

    const int w = std::max(1, juce::roundToInt((float) comp->getWidth()  * scale));
    const int h = std::max(1, juce::roundToInt((float) comp->getHeight() * scale));

    if (w != fbWidth || h != fbHeight)
    {
        fbWidth = w;
        fbHeight = h;
        gl::glViewport(0, 0, fbWidth, fbHeight);

       #if defined(HAVE_PROJECTM) && defined(PM_HAVE_V4_C_API)
        if (pmReady && pmHandle != nullptr)
            projectm_set_window_size((projectm_handle) pmHandle, (size_t) fbWidth, (size_t) fbHeight);
       #endif
    }
    return true;
}

void ProjectMRenderer::newOpenGLContextCreated()
{
    MDW_LOG("GL", "newOpenGLContextCreated: begin");

    DBG("[GL] Version: "  + String((const char*) gl::glGetString(gl::GL_VERSION)));
    DBG("[GL] Renderer: " + String((const char*) gl::glGetString(gl::GL_RENDERER)));
    DBG("[GL] Vendor: "   + String((const char*) gl::glGetString(gl::GL_VENDOR)));

    // Dev: latch test visualization mode from env once per context life
    {
        const char* env = std::getenv("MILKDAWP_TEST_VIS");
        testVisMode = (env && (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y'));
        MDW_LOG("GL", juce::String("TestVisMode=") + (testVisMode ? "ON" : "OFF") + " (MILKDAWP_TEST_VIS)");
    }
    lastFifoLogTimeSec = Time::getMillisecondCounterHiRes() * 0.001;

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
    if (!program) { MDW_LOG("GL", "newOpenGLContextCreated: shader program null"); return; }

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
        // Resolve preset dir (unchanged)...
        auto exe = File::getSpecialLocation(File::currentApplicationFile);
        auto bundleRoot = exe.getParentDirectory().getParentDirectory();
        File presetsA = bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("presets");
        File presetsB = bundleRoot.getChildFile("Resources").getChildFile("presets");
        File chosen = presetsA.isDirectory() ? presetsA : (presetsB.isDirectory() ? presetsB : File());
        pmPresetDir = chosen.getFullPathName();
        const bool exists = chosen.isDirectory();
        MDW_LOG("PM", "PresetDir resolved: " + (exists ? pmPresetDir : String("(not found)"))
                        + " exists=" + String(exists ? "true" : "false"));

        // NEW: derive runtime enable flag from DISABLE env only (no separate ENABLE var).
        {
            const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM");
            const bool disable = env && (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y');
            projectMEnabled.store(!disable, std::memory_order_relaxed);
            juce::String envStr = env ? juce::String(env) : "(null)";
            MDW_LOG("PM", juce::String("Env MILKDAWP_DISABLE_PROJECTM=") + envStr
                            + " -> runtime projectMEnabled=" + (disable ? "false" : "true"));
        }
    #endif

    MDW_LOG("GL", "newOpenGLContextCreated: end");
}

// Provide the missing definition (unconditional)


void ProjectMRenderer::renderOpenGL()
{
    static unsigned frameCount = 0;
    ++frameCount;
    const bool shouldLog = (frameCount % 60u) == 0;

    if (shouldLog) MDW_LOG("GL", "renderOpenGL: begin");

    if (!setViewportForCurrentScale())
    {
        if (shouldLog) MDW_LOG("GL", "renderOpenGL: no target component");
        return;
    }

    const float b = juce::jlimit(0.0f, 2.0f, brightness.load());
    const float sens = juce::jlimit(0.0f, 4.0f, sensitivity.load());

    static bool pmDisabledEnv = []() {
        if (const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM"))
            return (env[0] == '1' || env[0] == 'T' || env[0] == 't' || env[0] == 'Y' || env[0] == 'y');
        return false;
    }();

    // Log once whether the env var is seen and its effect
    static std::atomic<bool> loggedOnce{false};
    bool expected = false;
    if (loggedOnce.compare_exchange_strong(expected, true))
    {
        const char* env = std::getenv("MILKDAWP_DISABLE_PROJECTM");
        juce::String envStr = env ? juce::String(env) : "(null)";
        MDW_LOG("PM", juce::String("Env MILKDAWP_DISABLE_PROJECTM=") + envStr
                        + " -> pmDisabledEnv=" + (pmDisabledEnv ? "true" : "false"));
        MDW_LOG("PM", juce::String("Initial projectMEnabled flag = ")
                        + (projectMEnabled.load(std::memory_order_relaxed) ? "true" : "false"));
    }

    const bool pmDisabled = pmDisabledEnv || !projectMEnabled.load(std::memory_order_relaxed);

   #if defined(HAVE_PROJECTM)
    if (!pmDisabled)
    {
        // Backoff: try init once, then at most every 5 seconds if it failed.
        const double nowSec = Time::getMillisecondCounterHiRes() * 0.001;
        const double retryIntervalSec = 5.0;

        if (!pmReady)
        {
            if (!pmInitAttempted || (nowSec - pmInitLastAttemptSec) >= retryIntervalSec)
            {
                pmInitAttempted = true;
                pmInitLastAttemptSec = nowSec;

                MDW_LOG("PM", "renderOpenGL: initProjectMIfNeeded");
                initProjectMIfNeeded();
                MDW_LOG("PM", juce::String("renderOpenGL: pmReady=") + (pmReady ? "true" : "false"));
            }
        }

        if (pmReady)
        {
            feedProjectMAudioIfAvailable();
            MDW_LOG("PM", "renderOpenGL: after feedProjectMAudioIfAvailable");

            MDW_LOG("PM", "renderOpenGL: calling renderProjectMFrame");
            renderProjectMFrame();
            MDW_LOG("PM", "renderOpenGL: after renderProjectMFrame");
            return;
        }
    }
    else
    {
        static std::atomic<bool> logged{false};
        bool expected2=false;
        if (logged.compare_exchange_strong(expected2, true))
            MDW_LOG("PM", "ProjectM disabled (env or runtime flag); using fallback renderer");
    }
   #endif

    // Fallback visualization
    float level = fallbackLevel;

    // Dev: optionally drive level from a deterministic oscillator to decouple from audio path
    if (testVisMode)
    {
        const double nowSec = Time::getMillisecondCounterHiRes() * 0.001;
        const double dt = 1.0 / 60.0; // approximate per-frame integration
        // 1.2 Hz slow pulsing, mix with brightness and a touch of noise
        const double freq = 1.2;
        testPhase += 2.0 * double_Pi * freq * dt;
        if (testPhase > 2.0 * double_Pi)
            testPhase -= 2.0 * double_Pi;
        const float osc = 0.5f * (1.0f + std::sin((float) testPhase));
        level = 0.7f * level + 0.3f * juce::jlimit(0.0f, 1.0f, osc);
        fallbackLevel = level;
    }
    else
    {
        if (audioFifo != nullptr)
        {
            constexpr int N = 512;
            float tmp[N];
            int got = audioFifo->pop(tmp, N);
            if (got > 0)
            {
                // FIFO health metrics
                fifoSamplesPoppedThisSecond += got;
                const double tNow = Time::getMillisecondCounterHiRes() * 0.001;
                if (tNow - lastFifoLogTimeSec >= 1.0)
                {
                    MDW_LOG("Audio", "FIFO popped ~" + String(fifoSamplesPoppedThisSecond) + " samples in last second");
                    fifoSamplesPoppedThisSecond = 0;
                    lastFifoLogTimeSec = tNow;
                }

                double acc = 0.0;
                for (int i = 0; i < got; ++i) acc += (double) tmp[i] * tmp[i];
                float rms = std::sqrt((float)(acc / jmax(1, got)));
                rms = juce::jlimit(0.0f, 2.0f, rms * sens);
                const float a = 0.25f;
                level = level + a * (rms - level);
                fallbackLevel = level;
            }
            else
            {
                level *= 0.95f;
                fallbackLevel = level;
            }
        }
        else
        {
            level *= 0.98f;
            fallbackLevel = level;
        }
    }

    const float base = juce::jlimit(0.0f, 2.0f, b);
    const float rc = juce::jlimit(0.0f, 1.0f, 0.15f * base + 0.85f * juce::jlimit(0.0f, 1.0f, level));
    const float gc = juce::jlimit(0.0f, 1.0f, 0.10f * base + 0.45f * juce::jlimit(0.0f, 1.0f, level));
    const float bc = juce::jlimit(0.0f, 1.0f, 0.08f * base + 0.35f * juce::jlimit(0.0f, 1.0f, level));

    gl::glClearColor(0.f, 0.f, 0.f, 1.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);

    if (!program) { MDW_LOG("GL", "renderOpenGL: program missing"); return; }
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

    if (shouldLog) MDW_LOG("GL", "renderOpenGL: end");
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
    MDW_LOG("PM", "initProjectMIfNeeded: using v4 C++ API");
    // Guard: require a valid preset directory, or skip initializing projectM
    {
        juce::File pd(pmPresetDir);
        if (!pd.isDirectory())
        {
            MDW_LOG("PM", "Preset directory missing; skipping projectM init (fallback renderer will be used)");
            pmReady = false;
            return;
        }
    }
    try {
        PM::Settings settings{};
        juce::File pd(pmPresetDir);
        if (pd.isDirectory())
            settings.presetPath = pd.getFullPathName().toStdString();
        settings.windowWidth  = juce::jmax(1, fbWidth);
        settings.windowHeight = juce::jmax(1, fbHeight);
        settings.meshX = 48; settings.meshY = 36; // typical defaults
        MDW_LOG("PM", "C++ API: constructing PM::ProjectM");
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
    MDW_LOG("PM", "initProjectMIfNeeded: using v4 C API (minimal)");
    const size_t w = (size_t) jmax(1, fbWidth);
    const size_t h = (size_t) jmax(1, fbHeight);
    int ok = 0;
    projectm_handle inst = mdw_seh_projectm_minimal_init(w, h, &ok);
    if (inst == nullptr || ok == 0)
    {
        MDW_LOG("PM", "C API init failed or was trapped by SEH; staying on fallback");
        pmHandle = nullptr;
        pmReady = false;
        return;
    }

    pmHandle = (void*) inst;
    pmReady = true;
    MDW_LOG("PM", "projectM v4 initialized (C API, minimal)");
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
    if (!mdw_seh_projectm_render((projectm_handle) pmHandle))
    {
        MDW_LOG("PM", "SEH: exception during projectM render; shutting down and falling back");
        shutdownProjectM();
    }
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

        if (sens != 1.0f)
        {
            for (int i = 0; i < got; ++i)
                tmp[i] = juce::jlimit(-1.5f, 1.5f, tmp[i] * sens);
        }

        if (!mdw_seh_projectm_pcm_add_mono((projectm_handle) pmHandle, tmp, (unsigned int) got))
        {
            MDW_LOG("PM", "SEH: exception during projectM PCM add; shutting down and falling back");
            shutdownProjectM();
            break;
        }
    } while (popped < 8192);
   #endif
}
#endif
