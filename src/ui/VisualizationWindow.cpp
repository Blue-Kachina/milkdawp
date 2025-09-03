// src/ui/VisualizationWindow.cpp
#include "VisualizationWindow.h"
#include "../renderers/ProjectMRenderer.h"

VisualizationWindow::VisualizationWindow(LockFreeAudioFifo* fifo, int sampleRate)
    : juce::DocumentWindow(ProjectMRenderer::kWindowTitle,
                           juce::Colours::black,
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    glView = std::make_unique<GLComponent>(fifo, sampleRate);
    setContentOwned(glView.get(), false);
    centreWithSize(1024, 768);
    setVisible(true);
    startTimerHz(10);
}

VisualizationWindow::~VisualizationWindow()
{
    stopTimer();
    setContentOwned(nullptr, false);
    glView.reset();
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
    // Ensure renderer callbacks are not invoked during detach
    glContext.setRenderer(nullptr);
    glContext.setContinuousRepainting(false);
    glContext.detach();
    renderer.reset();
}

// New: forward to renderer
void VisualizationWindow::GLComponent::setVisualParams(float b, float s)
{
    if (renderer)
        renderer->setVisualParams(b, s);
}