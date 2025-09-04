#include "PluginEditor.h"
#include "../processor/PluginProcessor.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h"
#include "VisualizationWindow.h"

// ===== EditorGLComponent (embedded GL) =====

MilkDAWpAudioProcessorEditor::EditorGLComponent::EditorGLComponent(LockFreeAudioFifo* fifo, int sampleRate)
{
    renderer = std::make_unique<ProjectMRenderer>(glContext, fifo, sampleRate);
    glContext.setRenderer(renderer.get());
    glContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    glContext.setContinuousRepainting(true);
    glContext.setSwapInterval(1);
    // Only OpenGLRenderer is used; do not enable component painting into GL
    // glContext.setComponentPaintingEnabled(true);
    glContext.attachTo(*this);
}

MilkDAWpAudioProcessorEditor::EditorGLComponent::~EditorGLComponent()
{
    // Do not touch glContext here; owner calls shutdownGL on the message thread first.
    renderer.reset();
}

void MilkDAWpAudioProcessorEditor::EditorGLComponent::setVisualParams(float b, float s)
{
    if (renderer)
        renderer->setVisualParams(b, s);
}

void MilkDAWpAudioProcessorEditor::EditorGLComponent::shutdownGL()
{
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    if (glContext.isAttached())
        glContext.detach();
}

// ===== Editor =====

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

    // Embedded GL view (instead of attaching an OpenGLContext to the editor itself)
    glView = std::make_unique<EditorGLComponent>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
    addAndMakeVisible(*glView);

    // Set initial visual params on the embedded renderer
    if (glView)
    {
        const float b = processor.apvts.getRawParameterValue("brightness")->load();
        const float s = processor.apvts.getRawParameterValue("sensitivity")->load();
        glView->setVisualParams(b, s);
    }

    // Begin polling params to control both embedded and external visualization
    startTimerHz(15);
}

// Ensure GL teardown runs on the UI thread and only once
void MilkDAWpAudioProcessorEditor::shutdownGLOnMessageThread()
{
    if (glShutdownRequested.exchange(true))
        return; // already done or scheduled

    if (glView)
    {
        glView->shutdownGL();
        glView.reset();
    }
}

void MilkDAWpAudioProcessorEditor::destroyVisWindowOnMessageThread()
{
    if (!visWindow)
        return;

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        visWindow->setVisible(false);
        visWindow.reset();
    }
    else
    {
        // Run the destruction synchronously on the UI thread to ensure GL shutdown happens there
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                if (self->visWindow)
                {
                    self->visWindow->setVisible(false);
                    self->visWindow.reset();
                }
                return nullptr;
            }, this);
    }
}

void MilkDAWpAudioProcessorEditor::visibilityChanged()
{
    const bool visible = isShowing();

    if (!visible)
    {
        // Proactively shut down GL when hidden to avoid host tearing down the peer while attached
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            shutdownGLOnMessageThread();
        else
        {
            // Synchronously, to avoid peer destruction races
            juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                [](void* ctx) -> void*
                {
                    static_cast<MilkDAWpAudioProcessorEditor*>(ctx)->shutdownGLOnMessageThread();
                    return nullptr;
                }, this);
        }

        // Destroy any external window (also on UI thread)
        destroyVisWindowOnMessageThread();

        stopTimer();
        return;
    }

    if (!glShutdownRequested.load())
    {
        startTimerHz(15);
    }
}

void MilkDAWpAudioProcessorEditor::parentHierarchyChanged()
{
    // Called on message thread; detect removal from desktop (peer becomes null)
    if (getPeer() == nullptr)
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
}

// Ensure graceful shutdown before destruction, while Component peer still exists
void MilkDAWpAudioProcessorEditor::editorBeingDeleted()
{
    stopTimer();

    // Ensure both the embedded GL and any external window are torn down on UI thread
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
    else
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                self->shutdownGLOnMessageThread();
                self->destroyVisWindowOnMessageThread();
                return nullptr;
            }, this);
    }
}

MilkDAWpAudioProcessorEditor::~MilkDAWpAudioProcessorEditor()
{
    stopTimer();

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
    else
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread(
            [](void* ctx) -> void*
            {
                auto* self = static_cast<MilkDAWpAudioProcessorEditor*>(ctx);
                self->shutdownGLOnMessageThread();
                self->destroyVisWindowOnMessageThread();
                return nullptr;
            }, this);
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

    // Embedded GL takes the remaining area
    if (glView)
        glView->setBounds(r.reduced(5));
}

void MilkDAWpAudioProcessorEditor::timerCallback()
{
    const bool wantWindow = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    // Current visual params
    const float b = processor.apvts.getRawParameterValue("brightness")->load();
    const float s = processor.apvts.getRawParameterValue("sensitivity")->load();

    // Push current visual params to embedded renderer
    if (glView)
        glView->setVisualParams(b, s);

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