#pragma once
#include <JuceHeader.h>

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

    // New: forward visual params to the internal renderer
    void setVisualParams(float brightness, float sensitivity);

private:
    class GLComponent : public juce::Component
    {
    public:
        GLComponent(LockFreeAudioFifo* fifo, int sampleRate);
        ~GLComponent() override;
        void paint(juce::Graphics&) override {}

        // New: forward to renderer
        void setVisualParams(float brightness, float sensitivity);

    private:
        juce::OpenGLContext glContext;
        std::unique_ptr<ProjectMRenderer> renderer;
    };

    std::unique_ptr<GLComponent> glView;

    void timerCallback() override;
};