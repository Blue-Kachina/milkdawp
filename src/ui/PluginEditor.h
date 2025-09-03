
#pragma once
#include <JuceHeader.h>

class ProjectMRenderer;
class MilkDAWpAudioProcessor;
class VisualizationWindow;

class MilkDAWpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit MilkDAWpAudioProcessorEditor (MilkDAWpAudioProcessor&);
    ~MilkDAWpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Ensure graceful shutdown before destruction
    void editorBeingDeleted();

protected:
    // Pause GL when editor is hidden, resume when shown
    void visibilityChanged() override;

private:
    MilkDAWpAudioProcessor& processor;
    juce::OpenGLContext glContext;
    std::unique_ptr<ProjectMRenderer> renderer;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilkDAWpAudioProcessorEditor)
};
