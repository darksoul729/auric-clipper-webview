/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#if JUCE_MODULE_AVAILABLE_juce_dsp
 #include <juce_dsp/juce_dsp.h>
#endif

//==============================================================================
class AuricClipperWebViewAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AuricClipperWebViewAudioProcessor();
    ~AuricClipperWebViewAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    void handleWebMessage (const juce::var& payload);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // UI kirim normalized 0..1 -> set parameter normalized 0..1 (APVTS range 0..1 juga)
    void setParameterFromNormalized (const juce::String& paramId, float normalized01);

    // DSP core (tanpa alokasi)
    void clipBufferInPlace (juce::AudioBuffer<float>& buffer,
                            float pre01, float drive01, float sat01, float ceiling01,
                            float mix01, float trim01,
                            bool enableOversampling);

    // mapping musical (0..1 -> unit)
    static float mapPreDb     (float t01) { return juce::jmap (t01, -12.0f,  24.0f); }
    static float mapDriveDb   (float t01) { return juce::jmap (t01,   0.0f,  18.0f); }
    static float mapTrimDb    (float t01) { return juce::jmap (t01, -24.0f,  12.0f); }
    static float mapCeilingDb (float t01) { return juce::jmap (t01, -12.0f,   0.0f); }

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* preParam     = nullptr;
    std::atomic<float>* trimParam    = nullptr;
    std::atomic<float>* satclipParam = nullptr;
    std::atomic<float>* mixParam     = nullptr;
    std::atomic<float>* driveParam   = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* os2xParam    = nullptr;

    juce::AudioBuffer<float> dryBuffer; // untuk mix (preallocated)

   #if JUCE_MODULE_AVAILABLE_juce_dsp
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
   #endif

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuricClipperWebViewAudioProcessor)
};
