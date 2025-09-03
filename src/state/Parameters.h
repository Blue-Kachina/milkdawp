
#pragma once
#include <JuceHeader.h>

namespace om::milkdawp
{
    namespace ParamIDs
    {
        static constexpr const char* inputGain   = "inputGain";
        static constexpr const char* outputGain  = "outputGain";
        static constexpr const char* showWindow  = "showWindow";
        static constexpr const char* fullscreen  = "fullscreen";
        static constexpr const char* brightness  = "brightness";
        static constexpr const char* sensitivity = "sensitivity";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::inputGain, "Input Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::outputGain, "Output Gain",
            NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::showWindow, "Show Window", false));
        params.push_back(std::make_unique<AudioParameterBool>(ParamIDs::fullscreen, "Fullscreen", false));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::brightness, "Brightness",
            NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<AudioParameterFloat>(ParamIDs::sensitivity, "Sensitivity",
            NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
        return { params.begin(), params.end() };
    }
}
