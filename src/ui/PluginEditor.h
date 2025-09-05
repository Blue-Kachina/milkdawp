
#pragma once
#include <JuceHeader.h>

class ProjectMRenderer;
class MilkDAWpAudioProcessor;
class VisualizationWindow;
// Forward-declare the FIFO used by the embedded GL view
class LockFreeAudioFifo;

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::AudioProcessorValueTreeState::Listener // NEW: react to param changes
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

    // destroy VisualizationWindow synchronously on the message thread
    void destroyVisWindowOnMessageThread();

    // Race-safe external window creation state
    std::atomic<bool> creationPending { false };
    bool lastWantWindow { false };
    bool lastVisibleStateLogged { false };

    // Helper: are we safely on the desktop (peer created)?
    bool isOnDesktop() const noexcept { return getPeer() != nullptr && isShowing(); }

    // NEW: APVTS listener hook
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // NEW: UI-thread handlers
    void handleShowWindowChangeOnUI(bool wantWindow);
    void handleFullscreenChangeOnUI(bool wantFullscreen);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilkDAWpAudioProcessorEditor)
};
