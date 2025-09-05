#include "PluginEditor.h"
#include "../processor/PluginProcessor.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h"
#include "VisualizationWindow.h"

// ===== EditorGLComponent (embedded GL) =====
// Removed: embedded OpenGL preview inside the editor

// ===== Editor =====

MilkDAWpAudioProcessorEditor::MilkDAWpAudioProcessorEditor (MilkDAWpAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    MDW_LOG("UI", "Editor: constructed");
    setResizable(true, true);
    setSize (640, 140); // compact controls-only editor

    // Register APVTS listeners (react to param changes ASAP)
    processor.apvts.addParameterListener("showWindow", this);
    processor.apvts.addParameterListener("fullscreen", this);

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

    // New: log and ensure APVTS reflects button click (guarded to avoid redundant sets)
    btnShowWindow.onClick = [this]()
    {
        const bool want = btnShowWindow.getToggleState();
        MDW_LOG("UI", juce::String("Editor: btnShowWindow.onClick -> ") + (want ? "true" : "false"));
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(processor.apvts.getParameter("showWindow")))
        {
            const bool cur = p->get();
            if (cur != want)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(want ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        }
    };

    // Removed: embedded GL view creation and initial param push

    // Begin polling params to control external visualization
    startTimerHz(15);
    MDW_LOG("UI", "Editor: timer started");
}

// Ensure GL teardown runs on the UI thread and only once
void MilkDAWpAudioProcessorEditor::shutdownGLOnMessageThread()
{
    if (glShutdownRequested.exchange(true))
        return; // already done or scheduled

    MDW_LOG("UI", "Editor: shutdownGLOnMessageThread");
    // Removed: no embedded GL to shut down anymore
}

void MilkDAWpAudioProcessorEditor::destroyVisWindowOnMessageThread()
{
    if (!visWindow)
        return;

    MDW_LOG("UI", "Editor: destroyVisWindowOnMessageThread");
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        visWindow->setVisible(false);
        visWindow.reset();
    }
    else
    {
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
    MDW_LOG("UI", juce::String("Editor: visibilityChanged -> ") + (visible ? "showing" : "hidden") + ", hasPeer=" + (getPeer() ? "yes" : "no"));

    if (!visible)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            shutdownGLOnMessageThread();
        else
        {
            juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                [](void* ctx) -> void*
                {
                    static_cast<MilkDAWpAudioProcessorEditor*>(ctx)->shutdownGLOnMessageThread();
                    return nullptr;
                }, this);
        }

        destroyVisWindowOnMessageThread();
        stopTimer();
        MDW_LOG("UI", "Editor: timer stopped");
        return;
    }

    if (!glShutdownRequested.load())
    {
        startTimerHz(15);
        MDW_LOG("UI", "Editor: timer (re)started");
    }
}

void MilkDAWpAudioProcessorEditor::parentHierarchyChanged()
{
    // Called on message thread; detect removal from desktop (peer becomes null)
    MDW_LOG("UI", juce::String("Editor: parentHierarchyChanged, hasPeer=") + (getPeer() ? "yes" : "no"));
    if (getPeer() == nullptr)
    {
        shutdownGLOnMessageThread();
        destroyVisWindowOnMessageThread();
    }
}

// Ensure graceful shutdown before destruction, while Component peer still exists
void MilkDAWpAudioProcessorEditor::editorBeingDeleted()
{
    MDW_LOG("UI", "Editor: editorBeingDeleted");
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
    MDW_LOG("UI", "Editor: destructor");

    // Unregister APVTS listeners
    processor.apvts.removeParameterListener("showWindow", this);
    processor.apvts.removeParameterListener("fullscreen", this);

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

// ===== Add back the missing virtuals =====
void MilkDAWpAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Simple header text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(16.0f);
    g.drawText("MilkDAWp", 10, 10, 200, 24, juce::Justification::left);

    // Meter text (if you wire it up later)
    g.setColour(juce::Colours::lightgrey);
    g.setFont(14.0f);
    g.drawText(meterLabel.getText(), meterLabel.getBounds().toFloat(), juce::Justification::centredLeft, true);
}

void MilkDAWpAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(8);

    // Top row: title + buttons
    auto top = r.removeFromTop(24);
    meterLabel.setBounds(top.removeFromLeft(160));
    top.removeFromLeft(8);
    btnShowWindow.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(8);
    btnFullscreen.setBounds(top.removeFromLeft(120));

    r.removeFromTop(6);

    // Sliders area
    auto sliders = r.removeFromTop(56);
    const int sliderWidth = 150;
    const int sliderHeight = 44;
    inGain.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    outGain.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    brightness.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
    sliders.removeFromLeft(8);
    sensitivity.setBounds(sliders.removeFromLeft(sliderWidth).reduced(4).removeFromTop(sliderHeight));
}

void MilkDAWpAudioProcessorEditor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "showWindow")
    {
        const bool want = newValue > 0.5f;
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, want]()
        {
            if (editorSP == nullptr) return;
            editorSP->handleShowWindowChangeOnUI(want);
        });
    }
    else if (paramID == "fullscreen")
    {
        const bool wantFS = newValue > 0.5f;
        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        juce::MessageManager::callAsync([editorSP, wantFS]()
        {
            if (editorSP == nullptr) return;
            editorSP->handleFullscreenChangeOnUI(wantFS);
        });
    }
}

// NEW: UI-thread logic for showWindow changes
void MilkDAWpAudioProcessorEditor::handleShowWindowChangeOnUI(bool wantWindow)
{
    MDW_LOG("UI", juce::String("Editor: handleShowWindowChangeOnUI -> ") + (wantWindow ? "true" : "false"));

    if (!isOnDesktop())
    {
        MDW_LOG("UI", "Editor: not on desktop; deferring to timer");
        return;
    }

    const float b = processor.apvts.getRawParameterValue("brightness")->load();
    const float s = processor.apvts.getRawParameterValue("sensitivity")->load();
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    if (wantWindow)
    {
        if (!visWindow && !creationPending.exchange(true))
        {
            MDW_LOG("UI", "Editor: creating VisualizationWindow (event)");
            visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
            visWindow->setVisualParams(b, s);
            visWindow->setFullScreenParam(wantFullscreen);
            visWindow->toFront(true);

            juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
            visWindow->setOnUserClose([editorSP]()
            {
                if (editorSP == nullptr) return;
                MDW_LOG("UI", "Editor: onUserClose -> set showWindow = false");
                if (auto* p = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("showWindow")))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(0.0f);
                    p->endChangeGesture();
                }
            });

            creationPending.store(false);
        }

        if (visWindow && !visWindow->isVisible())
        {
            MDW_LOG("UI", "Editor: showing VisualizationWindow (event)");
            visWindow->setVisible(true);
            visWindow->toFront(true);
        }

        if (visWindow)
        {
            visWindow->setFullScreenParam(wantFullscreen);
            visWindow->setVisualParams(b, s);
        }
    }
    else
    {
        if (visWindow && visWindow->isVisible())
        {
            MDW_LOG("UI", "Editor: hiding VisualizationWindow (event)");
            visWindow->setVisible(false);
        }
    }
}

// NEW: UI-thread logic for fullscreen changes
void MilkDAWpAudioProcessorEditor::handleFullscreenChangeOnUI(bool wantFullscreen)
{
    if (visWindow)
        visWindow->setFullScreenParam(wantFullscreen);
}

// ===== timerCallback remains (visual param pushes and fallback pickup) =====
void MilkDAWpAudioProcessorEditor::timerCallback()
{
    const bool wantWindow = processor.apvts.getRawParameterValue("showWindow")->load() > 0.5f;
    const bool wantFullscreen = processor.apvts.getRawParameterValue("fullscreen")->load() > 0.5f;

    if (wantWindow != lastWantWindow)
    {
        MDW_LOG("UI", juce::String("Editor: showWindow param changed -> ") + (wantWindow ? "true" : "false"));
        lastWantWindow = wantWindow;
    }

    const float b = processor.apvts.getRawParameterValue("brightness")->load();
    const float s = processor.apvts.getRawParameterValue("sensitivity")->load();

    // Removed: embedded GL param push

    if (!isOnDesktop())
    {
        if (!lastVisibleStateLogged)
        {
            MDW_LOG("UI", "Editor: timer blocked (not on desktop yet)");
            lastVisibleStateLogged = true;
        }
        return;
    }
    else if (lastVisibleStateLogged)
    {
        MDW_LOG("UI", "Editor: now on desktop; timer active");
        lastVisibleStateLogged = false;
    }

    // Keep external window visuals synced while open
    if (visWindow)
    {
        visWindow->setFullScreenParam(wantFullscreen);
        visWindow->setVisualParams(b, s);
    }

    // If host delivered param change while we weren’t on desktop, act on it here
    if (wantWindow && !visWindow && !creationPending.exchange(true))
    {
        MDW_LOG("UI", "Editor: creating VisualizationWindow (timer catch-up)");
        visWindow = std::make_unique<VisualizationWindow>(processor.getAudioFifo(), processor.getCurrentSampleRateHz());
        visWindow->setVisualParams(b, s);
        visWindow->setFullScreenParam(wantFullscreen);
        visWindow->toFront(true);

        juce::Component::SafePointer<MilkDAWpAudioProcessorEditor> editorSP(this);
        visWindow->setOnUserClose([editorSP]()
        {
            if (editorSP == nullptr) return;
            MDW_LOG("UI", "Editor: onUserClose -> set showWindow = false");
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(editorSP->processor.apvts.getParameter("showWindow")))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(0.0f);
                p->endChangeGesture();
            }
        });

        creationPending.store(false);
    }
}