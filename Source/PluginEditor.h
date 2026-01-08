/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <functional>

class AuricClipperWebViewAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit AuricClipperWebViewAudioProcessorEditor (AuricClipperWebViewAudioProcessor&);
    ~AuricClipperWebViewAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class WebViewWithCallback : public juce::WebBrowserComponent
    {
    public:
        using juce::WebBrowserComponent::WebBrowserComponent;
        std::function<void()> onPageLoaded;

    private:
        void pageFinishedLoading (const juce::String&) override
        {
            if (onPageLoaded != nullptr)
                onPageLoaded();
        }
    };

    void timerCallback() override;
    void sendParamToUI (const juce::String& paramId, float value);

    AuricClipperWebViewAudioProcessor& processor;
    WebViewWithCallback web;

    juce::File uiTempDir;       // <<< FIX: add this
    juce::File uiTempHtmlFile;

    static constexpr size_t kNumParams = 7;
    std::array<float, kNumParams> lastSent {};
    bool pageReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuricClipperWebViewAudioProcessorEditor)
};
