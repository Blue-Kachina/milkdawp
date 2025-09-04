
#pragma once
#include <JuceHeader.h>

class LockFreeAudioFifo;

class ProjectMRenderer : public juce::OpenGLRenderer
{
public:
    explicit ProjectMRenderer(juce::OpenGLContext& ctx,
                              LockFreeAudioFifo* audioFifo = nullptr,
                              int sampleRate = 44100);
    ~ProjectMRenderer() override;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void setAudioSource(LockFreeAudioFifo* fifo, int sampleRate);

    // Visual controls (thread-safe setters)
    void setVisualParams(float newBrightness, float newSensitivity)
    {
        brightness.store(newBrightness, std::memory_order_relaxed);
        sensitivity.store(newSensitivity, std::memory_order_relaxed);
    }

    static constexpr const char* kWindowTitle = "MilkDAWp Visualizer (OBS Capture)";

private:
    juce::OpenGLContext& context;

    std::unique_ptr<juce::OpenGLShaderProgram> program;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrPos;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attrCol;

    unsigned int vao = 0;
    unsigned int vbo = 0;
    int fbWidth = 0, fbHeight = 0;

    // Audio feed for visualization (SPSC FIFO)
    LockFreeAudioFifo* audioFifo = nullptr;
    int audioSampleRate = 44100;

    // Visual params (polled in render thread)
    std::atomic<float> brightness { 1.0f };
    std::atomic<float> sensitivity { 1.0f };

    // Simple fallback visual energy (updated by reading FIFO when projectM is not active)
    float fallbackLevel = 0.0f;

    bool setViewportForCurrentScale();

    #if defined(HAVE_PROJECTM)
        void* pmHandle = nullptr;
        bool  pmReady  = false;
        juce::String pmPresetDir;
        void initProjectMIfNeeded();
        void shutdownProjectM();
        void renderProjectMFrame();
        void feedProjectMAudioIfAvailable();
        // Keep playlist state when using the C API
        void* pmPlaylist = nullptr;
    #endif
};
