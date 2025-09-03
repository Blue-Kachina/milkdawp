
#pragma once
#include <JuceHeader.h>
#include "../state/Parameters.h"
#include "../utils/Logging.h"

class LockFreeAudioFifo;

class MilkDAWpAudioProcessor : public juce::AudioProcessor
{
public:
    MilkDAWpAudioProcessor();
    ~MilkDAWpAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #if ! JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool supportsDoublePrecisionProcessing() const override { return false; }
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", om::milkdawp::createParameterLayout() };

    std::atomic<float> rmsL { 0.0f }, rmsR { 0.0f };

    // Real-time safe accessors for the renderer thread
    LockFreeAudioFifo* getAudioFifo() noexcept { return audioFifo.get(); }
    int getCurrentSampleRateHz() const noexcept { return (int) juce::roundToInt(currentSampleRate.load()); }

private:
    om::milkdawp::FileLoggerGuard logGuard;
    std::atomic<double> currentSampleRate { 44100.0 };
    std::unique_ptr<LockFreeAudioFifo> audioFifo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilkDAWpAudioProcessor)
};
