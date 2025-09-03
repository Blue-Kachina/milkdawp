
#include "PluginProcessor.h"
#include "../ui/PluginEditor.h"
#include "../utils/LockFreeAudioFifo.h"

MilkDAWpAudioProcessor::MilkDAWpAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    MDW_LOG("PROC", "Processor constructed");
}

void MilkDAWpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    if (sampleRate <= 0.0)
        return;
    MDW_LOG("PROC", "prepareToPlay: sr=" + juce::String(sampleRate));

    // Cache SR for renderer and allocate FIFO once (no RT changes here)
    currentSampleRate.store(sampleRate);
    if (!audioFifo)
        audioFifo.reset(new LockFreeAudioFifo(48000 * 2)); // capacity in samples (mono)
}

void MilkDAWpAudioProcessor::releaseResources()
{
    MDW_LOG("PROC", "releaseResources");
}

#if ! JucePlugin_PreferredChannelConfigurations
bool MilkDAWpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getChannelSet (true,  0);
    const auto out = layouts.getChannelSet (false, 0);
    if (in != out)                              return false;
    if (in.isDisabled())                        return false;
    if (in != juce::AudioChannelSet::mono()
     && in != juce::AudioChannelSet::stereo())  return false;
    return true;
}
#endif

void MilkDAWpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);

    const int numCh = buffer.getNumChannels();
    const int numSm = buffer.getNumSamples();
    if (numCh <= 0 || numSm <= 0)
        return;

    const auto gainIn  = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(om::milkdawp::ParamIDs::inputGain)->load());
    const auto gainOut = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(om::milkdawp::ParamIDs::outputGain)->load());

    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGain(ch, 0, numSm, gainIn);

    const int L = 0;
    const int R = juce::jmin(1, numCh - 1);
    const float* l = buffer.getReadPointer(L);
    const float* r = buffer.getReadPointer(R);
    double accL = 0.0, accR = 0.0;
    for (int i = 0; i < numSm; ++i) { accL += (double)l[i]*l[i]; accR += (double)r[i]*r[i]; }
    const float eps = 1.0e-12f;
    const float rmsLNow = std::sqrt((float)(accL / juce::jmax(1, numSm)) + eps);
    const float rmsRNow = std::sqrt((float)(accR / juce::jmax(1, numSm)) + eps);
    const float a = 0.2f;
    const float oldL = rmsL.load(std::memory_order_relaxed);
    const float oldR = rmsR.load(std::memory_order_relaxed);
    rmsL.store(oldL + a * (rmsLNow - oldL), std::memory_order_relaxed);
    rmsR.store(oldR + a * (rmsRNow - oldR), std::memory_order_relaxed);

    // Mix to mono and push into lock-free FIFO (real-time safe: no locks/allocations)
    if (audioFifo != nullptr)
    {
        constexpr int kChunk = 512;
        int remaining = numSm;
        int pos = 0;
        float mix[kChunk];
        while (remaining > 0)
        {
            const int n = juce::jmin(kChunk, remaining);
            for (int i = 0; i < n; ++i)
                mix[i] = 0.5f * (l[pos + i] + r[pos + i]);
            audioFifo->push(mix, n);
            pos += n;
            remaining -= n;
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGain(ch, 0, numSm, gainOut);
}

juce::AudioProcessorEditor* MilkDAWpAudioProcessor::createEditor()
{
    return new MilkDAWpAudioProcessorEditor(*this);
}

void MilkDAWpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MilkDAWpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MilkDAWpAudioProcessor();
}
