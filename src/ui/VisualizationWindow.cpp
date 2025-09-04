#include "VisualizationWindow.h"
#include "../renderers/ProjectMRenderer.h"

VisualizationWindow::VisualizationWindow(LockFreeAudioFifo* fifo, int sampleRate)
    : juce::DocumentWindow(ProjectMRenderer::kWindowTitle,
                           juce::Colours::black,
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    // Hand ownership of the GLComponent to the window.
    glView = new GLComponent(fifo, sampleRate);
    setContentOwned(glView, false);
    centreWithSize(1024, 768);
    setVisible(true);
    startTimerHz(10);
}

VisualizationWindow::~VisualizationWindow()
{
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

void VisualizationWindow::closeButtonPressed()
{
    // Hide instead of destroying so the host param "showWindow" can be synced externally
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
    renderer = std::make_unique<ProjectMRenderer>(glContext, fifo, sampleRate);
    glContext.setRenderer(renderer.get());
    glContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    glContext.setContinuousRepainting(true);
    glContext.setSwapInterval(1);
    glContext.attachTo(*this);
}

VisualizationWindow::GLComponent::~GLComponent()
{
    // Destructor may be called from non-UI threads in some hosts; avoid touching the context here.
    // The owning window ensures shutdownGL() runs on the message thread prior to destruction.
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
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    if (glContext.isAttached())
        glContext.detach();
}