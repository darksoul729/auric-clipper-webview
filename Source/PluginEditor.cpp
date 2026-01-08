//==============================================================================
// PluginEditor.cpp — AuricClipperWebView
// - Writes index.html + style.css + app.js into the SAME temp folder (relative links work)
// - Uses UNIQUE temp folder per editor instance (safe for multi-instance)
//==============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr auto kBridgeEventId = "juceMessage";

    // Bridge: bikin window.juce.postMessage(...) bekerja (legacy JUCE web UI pattern)
    static const char* getLegacyBridgeScript()
    {
        return R"JS(
(() => {
  if (typeof window.__JUCE__ === "undefined" || !window.__JUCE__.backend) return;

  window.juce = window.juce || {};

  if (typeof window.juce.postMessage !== "function") {
    window.juce.postMessage = function (message) {
      let payload = message;
      try { payload = JSON.parse(message); } catch (e) {}
      window.__JUCE__.backend.emitEvent("juceMessage", payload);
    };
  }
})();
)JS";
    }

    // parameter ids yang dipush ke UI
    static constexpr size_t kNumParams = 7;
    static constexpr std::array<const char*, kNumParams> kParamIds =
    {{
        "pre",
        "trim",
        "satclip",
        "mix",
        "drive",
        "ceiling",
        "os2x"
    }};

    // Buat WebBrowserComponent::Options (JUCE 7/8).
    static juce::WebBrowserComponent::Options makeWebOptions (AuricClipperWebViewAudioProcessor& processor)
    {
        juce::WebBrowserComponent::Options opts;

        opts = opts.withNativeIntegrationEnabled (true)
                   .withUserScript (getLegacyBridgeScript())
                   .withEventListener (juce::Identifier (kBridgeEventId),
                                       [&processor] (juce::var payload)
                                       {
                                           processor.handleWebMessage (payload);
                                       });

       #if JUCE_WINDOWS
        // Optional: pakai WebView2 kalau tersedia + user data folder
        // Kalau API ini tidak ada di JUCE kamu dan error compile -> komen blok ini.
        if (juce::WebBrowserComponent::Options::Backend::webview2 != juce::WebBrowserComponent::Options::Backend{})
        {
            opts = opts.withBackend (juce::WebBrowserComponent::Options::Backend::webview2);

            const auto dataFolder =
                juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("AuricClipperWebView2");

            dataFolder.createDirectory();

            opts = opts.withWinWebView2Options (
                juce::WebBrowserComponent::Options::WinWebView2{}
                    .withUserDataFolder (dataFolder)
            );
        }
       #endif

        return opts;
    }

    // ===== FIX UTAMA =====
    // Tulis index.html + style.css + app.js ke folder yang sama,
    // supaya link ./style.css dan ./app.js kebaca.
    static juce::File writeWebUIToTempDir (juce::File& outDir)
    {
        // folder unik per instance (biar multi instance gak tabrakan)
        const auto uid = juce::String::toHexString ((int) juce::Random::getSystemRandom().nextInt());
        outDir =
            juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("AuricClipperWebUI")
                .getChildFile (uid);

        outDir.createDirectory();

        auto write = [&outDir](const juce::String& name, const void* data, size_t size)
        {
            auto f = outDir.getChildFile (name);
            f.deleteFile();
            f.replaceWithData (data, size);
        };

        // Nama file HARUS sama dengan yang direferensikan index.html (./style.css, ./app.js)
        write ("index.html", BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
        write ("style.css",  BinaryData::style_css,  (size_t) BinaryData::style_cssSize);
        write ("app.js",     BinaryData::app_js,     (size_t) BinaryData::app_jsSize);

        return outDir.getChildFile ("index.html");
    }

} // namespace

//==============================================================================

AuricClipperWebViewAudioProcessorEditor::AuricClipperWebViewAudioProcessorEditor (AuricClipperWebViewAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      web (makeWebOptions (p))
{
    addAndMakeVisible (web);

    web.onPageLoaded = [this]()
    {
        pageReady = true;
        lastSent.fill (std::numeric_limits<float>::quiet_NaN());
        timerCallback(); // sync pertama
    };

    // ===== load file:// ke index.html di folder temp yang berisi css+js =====
    uiTempHtmlFile = writeWebUIToTempDir (uiTempDir);

    juce::URL fileUrl (uiTempHtmlFile);
    web.goToURL (fileUrl.toString (true));

    startTimerHz (30);
    setSize (1024, 683);
}

AuricClipperWebViewAudioProcessorEditor::~AuricClipperWebViewAudioProcessorEditor()
{
    // Optional: bersihin folder temp instance ini (aman kalau kamu mau)
    // (Kalau kamu sering debug dan pengen inspect file temp, komen aja baris ini)
    if (uiTempDir.isDirectory())
        uiTempDir.deleteRecursively();
}

void AuricClipperWebViewAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void AuricClipperWebViewAudioProcessorEditor::resized()
{
    web.setBounds (getLocalBounds());
}

//==============================================================================

void AuricClipperWebViewAudioProcessorEditor::timerCallback()
{
    if (! pageReady)
        return;

    auto& apvts = processor.getAPVTS();

    for (size_t i = 0; i < kParamIds.size(); ++i)
    {
        if (auto* param = apvts.getParameter (kParamIds[i]))
        {
            const float v = param->getValue(); // normalized 0..1

            if (! std::isfinite (lastSent[i]) || std::abs (v - lastSent[i]) > 1.0e-5f)
            {
                sendParamToUI (kParamIds[i], v);
                lastSent[i] = v;
            }
        }
    }
}

void AuricClipperWebViewAudioProcessorEditor::sendParamToUI (const juce::String& paramId, float value)
{
    // JS: window.__setParam("pre", 0.123456);
    const auto script = "window.__setParam(" + paramId.quoted() + ", " + juce::String (value, 6) + ");";
    web.evaluateJavascript (script);
}
