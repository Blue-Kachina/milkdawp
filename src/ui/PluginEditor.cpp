#include "PluginEditor.h"
#include "../processor/PluginProcessor.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h"
#include "VisualizationWindow.h"

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor (MilkDAWpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setResizable(true, true);
    setSize (900, 600);

    meterLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (meterLabel);

    addAndMakeVisible(inGain);
    inGain.setTextValueSuffix(" dB");
    inGain.setRange(-24.0, 24.0, 0.01);

    addAndMakeVisible(outGain);
    outGain.setTextValueSuffix(" dB");
    outGain.setRange(-24.0, 24.0, 0.01);

    inAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "inputGain",  inGain);
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "outputGain", outGain);

    // Visual controls: Brightness & Sensitivity
    addAndMakeVisible(brightness);
    brightness.setTextValueSuffix("");
    brightness.setRange(0.0, 2.0, 0.001);
    brightness.setTooltip("Brightness");
    brightAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "brightness", brightness);

    addAndMakeVisible(sensitivity);
    sensitivity.setTextValueSuffix("");
    sensitivity.setRange(0.0, 2.0, 0.001);
    sensitivity.setTooltip("Sensitivity");
    sensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "sensitivity", sensitivity);

    addAndMakeVisible(btnShowWindow);
    addAndMakeVisible(btnFullscreen);
    showAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "showWindow", btnShowWindow);
    fullAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "fullscreen", btnFullscreen);

    // OpenGL setup
    renderer = std::make_unique<ProjectMRenderer>(glContext, processor.getAudioFifo(), processor.getCurrentSampleRateHz());
    glContext.setRenderer (renderer.get());
    glContext.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    glContext.setContinuousRepainting (true);
    glContext.setSwapInterval (1);
    glContext.setComponentPaintingEnabled (true);
    glContext.attachTo (*this);

    // Set initial visual params on the embedded renderer
    if (renderer)
    {
        const float b = processor.apvts.getRawParameterValue("brightness")->load();
        const float s = processor.apvts.getRawParameterValue("sensitivity")->load();
        renderer->setVisualParams(b, s);
    }

    // Begin polling params to control the external visualization window
    startTimerHz(15);
}

// Ensure graceful shutdown before destruction, while Component peer still exists
void MilkDAWpAudioProcessorEditor::editorBeingDeleted()
{
    // Stop all periodic callbacks first
    stopTimer();

    // Ensure external window is not alive (and thus not running its own GL)
    if (visWindow)
    {
        visWindow->setVisible(false);
        visWindow.reset();
    }

    // Quiesce GL before detaching
    glContext.setContinuousRepainting(false);
    glContext.setComponentPaintingEnabled(false);
    glContext.setRenderer(nullptr);
    glContext.detach();

    // Now it's safe to drop the renderer
    renderer.reset();
}

// Add missing methods
MilkDAWpAudioProcessorEditor::~MilkDAWpAudioProcessorEditor()
{
    // In case some hosts skip editorBeingDeleted, repeat defensive shutdown
    stopTimer();

    if (visWindow)
    {
        visWindow->setVisible(false);
        visWindow.reset();
    }

    glContext.setContinuousRepainting(false);
    glContext.setComponentPaintingEnabled(false);
    glContext.setRenderer(nullptr);
    glContext.detach();
    renderer.reset();
}

void MilkDAWpAudioProcessorEditor::visibilityChanged()
{
    // When editor is hidden, stop GL and timers to avoid racing the host teardown
    const bool visible = isShowing();
    if (!visible)
    {
        stopTimer();
        glContext.setContinuousRepainting(false);
        glContext.setComponentPaintingEnabled(false);
    }
    else
    {
        glContext.setComponentPaintingEnabled(true);
        glContext.setContinuousRepainting(true);
        startTimerHz(15);
    }
}

void MilkDAWpAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Simple header text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(16.0f);
    g.drawText("MilkDAWp", 10, 10, 200, 24, juce::Justification::left);

    // Meter text (example: show current RMS if you later wire it up)
    g.setColour(juce::Colours::lightgrey);
    g.setFont(14.0f);
    g.drawText(meterLabel.getText(), meterLabel.getBounds().toFloat(), juce::Justification::centredLeft, true);
}

void MilkDAWpAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(10);

    // Top row: title + buttons
    auto top = r.removeFromTop(30);
    meterLabel.setBounds(top.removeFromLeft(200));
    top.removeFromLeft(10);
    btnShowWindow.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(10);
    btnFullscreen.setBounds(top.removeFromLeft(120));

    r.removeFromTop(10);

    // Sliders area
    auto sliders = r.removeFromTop(80);
    const int sliderWidth = 180;
    const int sliderHeight = 60;
    inGain.setBounds(sliders.removeFromLeft(sliderWidth).reduced(5).removeFromTop(sliderHeight));
    sliders.removeFromLeft(10);
    outGain.setBounds(sliders.removeFromLeft(sliderWidth).reduced(5).removeFromTop(sliderHeight));
    sliders.removeFromLeft(10);
    brightness.setBounds(sliders.removeFromLeft(sliderWidth).reduced(5).removeFromTop(sliderHeight));
    sliders.removeFromLeft(10);
    sensitivity.setBounds(sliders.removeFromLeft(sliderWidth).reduced(5).removeFromTop(sliderHeight));
}

void MilkDAWpAudioProcessorEditor::timerCallback()
{
    const bool wantWindow = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    // Current visual params
    const float b = processor.apvts.getRawParameterValue("brightness")->load();
    const float s = processor.apvts.getRawParameterValue("sensitivity")->load();

    // Push current visual params to embedded renderer
    if (renderer)
        renderer->setVisualParams(b, s);

    if (wantWindow)
    {
        if (!visWindow)
        {
            visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
            // Initialize window visuals on creation
            visWindow->setVisualParams(b, s);
        }

        if (!visWindow->isVisible())
            visWindow->setVisible(true);

        visWindow->setFullScreenParam(wantFullscreen);
        // Keep visuals in sync
        visWindow->setVisualParams(b, s);
    }
    else
    {
        if (visWindow && visWindow->isVisible())
            visWindow->setVisible(false);
    }
}