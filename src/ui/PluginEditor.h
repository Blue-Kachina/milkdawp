
#pragma once
#include <JuceHeader.h>

class ProjectMRenderer;
class MilkDAWpAudioProcessor;
class VisualizationWindow;
// Forward-declare the FIFO used by the embedded GL view
class LockFreeAudioFifo;

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit MilkDAWpAudioProcessorEditor(MilkDAWpAudioProcessor&);
    ~MilkDAWpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Ensure graceful shutdown before destruction
    void editorBeingDeleted(); // not a JUCE override

protected:
    // Pause GL when editor is hidden, resume when shown
    void visibilityChanged() override;
    void parentHierarchyChanged() override; // detach when leaving desktop

private:
    MilkDAWpAudioProcessor& processor;

    // Embedded GL lives in a child component so we can tear it down deterministically
    class EditorGLComponent : public juce::Component
    {
    public:
        EditorGLComponent(LockFreeAudioFifo* fifo, int sampleRate);
        ~EditorGLComponent() override;

        void paint(juce::Graphics&) override {}

        void setVisualParams(float brightness, float sensitivity);
        void shutdownGL(); // message-thread only

    private:
        juce::OpenGLContext glContext;
        std::unique_ptr<ProjectMRenderer> renderer;
    };

    std::unique_ptr<EditorGLComponent> glView;

    juce::Label meterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAtt, outAtt;
    juce::Slider inGain, outGain;

    juce::ToggleButton btnShowWindow { "Show Window" };
    juce::ToggleButton btnFullscreen { "Fullscreen" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> showAtt, fullAtt;

    // New visual controls
    juce::Slider brightness, sensitivity;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brightAtt, sensAtt;

    // External visualization window
    std::unique_ptr<VisualizationWindow> visWindow;

    void timerCallback() override;

    // Ensure GL teardown runs on message thread exactly once
    void shutdownGLOnMessageThread();
    std::atomic<bool> glShutdownRequested { false };

    // New: destroy VisualizationWindow synchronously on the message thread
    void destroyVisWindowOnMessageThread();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilkDAWpAudioProcessorEditor)
};
