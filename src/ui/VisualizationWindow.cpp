#include "VisualizationWindow.h"
#include "../renderers/ProjectMRenderer.h"
#include "../utils/Logging.h" // logging

VisualizationWindow::VisualizationWindow(LockFreeAudioFifo* fifo, int sampleRate)
    : juce::DocumentWindow(ProjectMRenderer::kWindowTitle,
                           juce::Colours::black,
                           juce::DocumentWindow::closeButton)
{
    MDW_LOG("UI", "VisualizationWindow: constructing");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    // Hand ownership of the GLComponent to the window.
    glView = new GLComponent(fifo, sampleRate);
    setContentOwned(glView, false);
    centreWithSize(900, 550);

    // Ensure we can receive key events (for ESC to exit fullscreen)
    setWantsKeyboardFocus(true);

    // Bring the window to the front and keep it on top so it doesn't hide behind the host
    setAlwaysOnTop(true);
    setVisible(true);
    toFront(true);

    startTimerHz(10);
}

VisualizationWindow::~VisualizationWindow()
{
    MDW_LOG("UI", "VisualizationWindow: destroying");
    stopTimer();

    // Ensure GL is torn down on the message thread BEFORE removing content/peer
    if (glView)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            glView->shutdownGL();
        }
        else
        {
            juce::MessageManager::getInstance()->callFunctionOnMessageThread(
                [](void* ctx) -> void*
                {
                    auto* self = static_cast<VisualizationWindow*>(ctx);
                    if (self->glView)
                        self->glView->shutdownGL();
                    return nullptr;
                }, this);
        }
    }

    // Now it is safe to remove the content from the window
    setContentOwned(nullptr, false);

    // Content has been deleted by the window; clear our non-owning pointer.
    glView = nullptr;
}

// Implementations that were missing (causing LNK2019/LNK2001)
void VisualizationWindow::closeButtonPressed()
{
    MDW_LOG("UI", "VisualizationWindow: close button pressed");
    if (onUserClosed)
        onUserClosed();
    setVisible(false);
}

void VisualizationWindow::setFullScreenParam(bool shouldBeFullScreen)
{
    if (shouldBeFullScreen != isFullScreen())
        setFullScreen(shouldBeFullScreen);
}

void VisualizationWindow::syncTitleForOBS()
{
    setName(ProjectMRenderer::kWindowTitle);
}

bool VisualizationWindow::keyPressed(const juce::KeyPress& key)
{
    // Safety: let ESC exit fullscreen
    if (key == juce::KeyPress::escapeKey && isFullScreen())
    {
        MDW_LOG("UI", "VisualizationWindow: ESC -> exit fullscreen");
        setFullScreen(false);
        return true;
    }
    return false;
}

void VisualizationWindow::timerCallback()
{
    syncTitleForOBS();
}

// New: forward visual params to the GL component
void VisualizationWindow::setVisualParams(float brightness, float sensitivity)
{
    if (glView != nullptr)
        glView->setVisualParams(brightness, sensitivity);
}

// ===== GLComponent =====

VisualizationWindow::GLComponent::GLComponent(LockFreeAudioFifo* fifo, int sampleRate)
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent: ctor begin (attach context)");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread

    // Do not force a specific GL version; let JUCE/driver choose a default
    glContext.setContinuousRepainting(true);
    glContext.setSwapInterval(1);
    glContext.attachTo(*this);

    MDW_LOG("UI", "VisualizationWindow::GLComponent: creating ProjectMRenderer");
    renderer = std::make_unique<ProjectMRenderer>(glContext, fifo, sampleRate);
    glContext.setRenderer(renderer.get());

    MDW_LOG("UI", "VisualizationWindow::GLComponent: ctor end");
}

VisualizationWindow::GLComponent::~GLComponent()
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent: dtor");
    // Destructor may be called from non-UI threads in some hosts; avoid touching the context here.
    // Ensure a clean shutdown sequence (prefer explicit shutdownGL by owner on UI thread).
    renderer.reset();
}

// New: forward to renderer
void VisualizationWindow::GLComponent::setVisualParams(float b, float s)
{
    if (renderer)
        renderer->setVisualParams(b, s);
}

// New: explicit GL teardown (must be called on the message thread)
void VisualizationWindow::GLComponent::shutdownGL()
{
    MDW_LOG("UI", "VisualizationWindow::GLComponent::shutdownGL");
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread()); // must be UI thread
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    if (glContext.isAttached())
        glContext.detach();
}