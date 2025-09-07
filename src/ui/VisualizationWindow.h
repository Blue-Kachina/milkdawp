#pragma once
#include <JuceHeader.h>
#include <functional>

class ProjectMRenderer;
class LockFreeAudioFifo;

class VisualizationWindow : public juce::DocumentWindow,
                            private juce::Timer
{
public:
    VisualizationWindow(LockFreeAudioFifo* fifo, int sampleRate);
    ~VisualizationWindow() override;

    void closeButtonPressed() override;

    void setFullScreenParam(bool shouldBeFullScreen);
    void syncTitleForOBS();
    bool keyPressed(const juce::KeyPress& key) override;

    // New: forward visual params to the internal renderer
    void setVisualParams(float brightness, float sensitivity);

    // New: notify editor when user closes the window (to sync the "Show Window" param)
    void setOnUserClose(std::function<void()> cb) { onUserClosed = std::move(cb); }

private:
    class GLComponent : public juce::Component
    {
    public:
        GLComponent(LockFreeAudioFifo* fifo, int sampleRate);
        ~GLComponent() override;
        void paint(juce::Graphics&) override {}

        // New: forward to renderer
        void setVisualParams(float brightness, float sensitivity);

        // explicit GL teardown (must be called on the message thread)
        void shutdownGL();

    private:
        juce::OpenGLContext glContext;
        std::unique_ptr<ProjectMRenderer> renderer;
    };

    // Owned by the DocumentWindow via setContentOwned; we keep a non-owning pointer.
    GLComponent* glView = nullptr;

    void timerCallback() override;

    // callback invoked when the user closes the window
    std::function<void()> onUserClosed;
};