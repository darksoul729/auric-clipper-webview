#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
AuricClipperWebViewAudioProcessor::AuricClipperWebViewAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    preParam     = apvts.getRawParameterValue ("pre");
    trimParam    = apvts.getRawParameterValue ("trim");
    satclipParam = apvts.getRawParameterValue ("satclip");
    mixParam     = apvts.getRawParameterValue ("mix");
    driveParam   = apvts.getRawParameterValue ("drive");
    ceilingParam = apvts.getRawParameterValue ("ceiling");

    // SINGLE toggle: dipakai sebagai POWER/BYPASS (UI kamu pakai data-param="os2x")
    os2xParam    = apvts.getRawParameterValue ("os2x");
}

AuricClipperWebViewAudioProcessor::~AuricClipperWebViewAudioProcessor() = default;

//==============================================================================
const juce::String AuricClipperWebViewAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AuricClipperWebViewAudioProcessor::acceptsMidi() const  { return false; }
bool AuricClipperWebViewAudioProcessor::producesMidi() const { return false; }
bool AuricClipperWebViewAudioProcessor::isMidiEffect() const { return false; }
double AuricClipperWebViewAudioProcessor::getTailLengthSeconds() const { return 0.0; }

//==============================================================================
int AuricClipperWebViewAudioProcessor::getNumPrograms() { return 1; }
int AuricClipperWebViewAudioProcessor::getCurrentProgram() { return 0; }
void AuricClipperWebViewAudioProcessor::setCurrentProgram (int) {}
const juce::String AuricClipperWebViewAudioProcessor::getProgramName (int) { return {}; }
void AuricClipperWebViewAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void AuricClipperWebViewAudioProcessor::prepareToPlay (double /*sampleRate*/, int samplesPerBlock)
{
    dryBuffer.setSize (getTotalNumInputChannels(), samplesPerBlock, false, false, true);

   #if JUCE_MODULE_AVAILABLE_juce_dsp
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) juce::jmax (1, getTotalNumInputChannels()),
        1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

    oversampler->initProcessing ((size_t) samplesPerBlock);
   #endif
}

void AuricClipperWebViewAudioProcessor::releaseResources()
{
   #if JUCE_MODULE_AVAILABLE_juce_dsp
    oversampler.reset();
   #endif
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool AuricClipperWebViewAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn.isDisabled() || mainOut.isDisabled())
        return false;

    if (mainIn != mainOut)
        return false;

    return (mainIn == juce::AudioChannelSet::mono()
         || mainIn == juce::AudioChannelSet::stereo());
}
#endif

//==============================================================================
static inline float softClipTanh (float x) { return std::tanh (x); }
static inline float hardClip (float x, float c) { return juce::jlimit (-c, c, x); }

void AuricClipperWebViewAudioProcessor::clipBufferInPlace (juce::AudioBuffer<float>& buffer,
                                                          float pre01, float drive01, float sat01, float ceiling01,
                                                          float mix01, float trim01,
                                                          bool enableOversampling)
{
    const int numCh = buffer.getNumChannels();
    const int nS    = buffer.getNumSamples();

    // dry copy for mix
    dryBuffer.setSize (numCh, nS, false, false, true);
    dryBuffer.makeCopyOf (buffer, true);

    // map UI 0..1 -> dB (fungsi mapPreDb/mapDriveDb/mapTrimDb milik kamu)
    const float preGain   = juce::Decibels::decibelsToGain (mapPreDb   (pre01));
    const float driveGain = juce::Decibels::decibelsToGain (mapDriveDb (drive01));
    const float trimGain  = juce::Decibels::decibelsToGain (mapTrimDb  (trim01));

    float ceilingLin = juce::Decibels::decibelsToGain (mapCeilingDb (ceiling01));
    ceilingLin = juce::jlimit (0.05f, 1.0f, ceilingLin);

    const float sat = juce::jlimit (0.0f, 1.0f, sat01);
    const float mix = juce::jlimit (0.0f, 1.0f, mix01);

    auto processNoOS = [&]()
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);

            for (int i = 0; i < nS; ++i)
            {
                float x = w[i] * preGain * driveGain;

                const float s = softClipTanh (x);
                const float h = hardClip (x, ceilingLin);

                float y = s + (h - s) * sat;   // lerp soft -> hard
                y = hardClip (y, ceilingLin);  // safety

                w[i] = y;
            }
        }
    };

   #if JUCE_MODULE_AVAILABLE_juce_dsp
    const bool canOS = (oversampler != nullptr);
    if (enableOversampling && canOS)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);

        const int upCh = (int) up.getNumChannels();
        const int upN  = (int) up.getNumSamples();

        for (int ch = 0; ch < upCh; ++ch)
        {
            auto* x = up.getChannelPointer ((size_t) ch);
            for (int i = 0; i < upN; ++i)
            {
                float v = x[i] * preGain * driveGain;

                const float s = softClipTanh (v);
                const float h = hardClip (v, ceilingLin);

                float y = s + (h - s) * sat;
                y = hardClip (y, ceilingLin);

                x[i] = y;
            }
        }

        oversampler->processSamplesDown (block);
    }
    else
   #endif
    {
        processNoOS();
    }

    // trim
    buffer.applyGain (trimGain);

    // dry/wet mix
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        auto* d = dryBuffer.getReadPointer (ch);

        for (int i = 0; i < nS; ++i)
            w[i] = d[i] + (w[i] - d[i]) * mix;
    }
}

void AuricClipperWebViewAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const float pre01     = preParam     ? preParam->load()     : 0.35f;
    const float trim01    = trimParam    ? trimParam->load()    : 0.35f;
    const float sat01     = satclipParam ? satclipParam->load() : 0.50f;
    const float mix01     = mixParam     ? mixParam->load()     : 1.00f;
    const float drive01   = driveParam   ? driveParam->load()   : 0.50f;
    const float ceiling01 = ceilingParam ? ceilingParam->load() : 0.70f;

    // os2x dipakai sebagai POWER/BYPASS (sesuai UI kamu sekarang)
    const bool powerOn = os2xParam ? (os2xParam->load() >= 0.5f) : false;

    if (! powerOn)
        return; // bypass

    // kalau powerOn = true, proses dengan oversampling (enable = true)
    clipBufferInPlace (buffer, pre01, drive01, sat01, ceiling01, mix01, trim01, true);
}

//==============================================================================
juce::AudioProcessorEditor* AuricClipperWebViewAudioProcessor::createEditor()
{
    return new AuricClipperWebViewAudioProcessorEditor (*this);
}

bool AuricClipperWebViewAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
void AuricClipperWebViewAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void AuricClipperWebViewAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AuricClipperWebViewAudioProcessor::createParameterLayout()
{
    using APF  = juce::AudioParameterFloat;
    using APB  = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APF> ("pre",     "PRE",      juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f));
    layout.add (std::make_unique<APF> ("trim",    "TRIM",     juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f));
    layout.add (std::make_unique<APF> ("satclip", "SAT/CLIP", juce::NormalisableRange<float> (0.0f, 1.0f), 0.50f));
    layout.add (std::make_unique<APF> ("mix",     "MIX",      juce::NormalisableRange<float> (0.0f, 1.0f), 1.00f));
    layout.add (std::make_unique<APF> ("drive",   "DRIVE",    juce::NormalisableRange<float> (0.0f, 1.0f), 0.50f));
    layout.add (std::make_unique<APF> ("ceiling", "CEILING",  juce::NormalisableRange<float> (0.0f, 1.0f), 0.70f));

    // SINGLE toggle (host akan lihat sebagai POWER; id tetap "os2x" biar match UI)
    layout.add (std::make_unique<APB> ("os2x", "POWER", false));

    return layout;
}

//==============================================================================
void AuricClipperWebViewAudioProcessor::setParameterFromNormalized (const juce::String& paramId, float normalized01)
{
    if (auto* p = apvts.getParameter (paramId))
    {
        const bool treatAsToggle =
            (paramId == "os2x" || dynamic_cast<juce::AudioParameterBool*> (p) != nullptr);

        const float v = treatAsToggle
                          ? ((normalized01 >= 0.5f) ? 1.0f : 0.0f)
                          : juce::jlimit (0.0f, 1.0f, normalized01);

        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
}

void AuricClipperWebViewAudioProcessor::handleWebMessage (const juce::var& payload)
{
    juce::var obj = payload;

    if (obj.isString())
    {
        juce::var parsed;
        if (juce::JSON::parse (obj.toString(), parsed).wasOk())
            obj = parsed;
    }

    if (! obj.isObject())
        return;

    auto* dyn = obj.getDynamicObject();
    if (dyn == nullptr)
        return;

    const auto type = dyn->getProperty ("type").toString();
    if (type != "param")
        return;

    const auto id = dyn->getProperty ("id").toString();
    const float value = (float) dyn->getProperty ("value");

    setParameterFromNormalized (id, value);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuricClipperWebViewAudioProcessor();
}
