#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/Oscilloscope.h"
#include "UI/RustLookAndFeel.h"
#include "PresetManager.h"
#include <BinaryData.h>

class BadDadVerbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       public juce::FileDragAndDropTarget
{
public:
    explicit BadDadVerbAudioProcessorEditor(BadDadVerbAudioProcessor&);
    ~BadDadVerbAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;

    // Returns a font guaranteed to have Cyrillic glyphs on Mac and Windows.
    // "Arial" is present on all Windows systems and maps to a Cyrillic-capable
    // font on macOS via JUCE's font substitution.
    static juce::Font cyrillicFont(float size, bool bold = false)
    {
        auto opts = juce::FontOptions("Arial", size, bold ? juce::Font::bold : juce::Font::plain);
        return juce::Font(opts);
    }
    void configureKnob(juce::Slider& s, const juce::String& id = {});
    void drawVuMeter(juce::Graphics& g, juce::Rectangle<float> area);
    void drawGlowLayer(juce::Graphics& g);
    void syncScope();

    BadDadVerbAudioProcessor& audioProcessor;
    dadbass::SovietLookAndFeel lnf;
    dadbass::Oscilloscope scope;        // main IR / input scope (centre)
    dadbass::Oscilloscope sampleScope;  // sampled signal display (right side)

    juce::Slider inputKnob, mixKnob, wetKnob, outputKnob,
                 timeKnob, hpfKnob, freqKnob, resKnob,
                 limitKnob, trimStartKnob, trimEndKnob;

    // Buttons as ToggleButton so SovietLookAndFeel draws them as Soviet rockers
    juce::ToggleButton recordButton, stopButton, reverseButton, normalizeButton;
    // freqKnob2/resKnob2 removed (use main strip knobs)
    // Note: freqKnob and resKnob are the main knob-strip versions

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> inputAtt, mixAtt, wetAtt, outputAtt,
                        timeAtt, hpfAtt, freqAtt, resAtt,
                        limitAtt, trimStartAtt, trimEndAtt;

    float meterLevelDb    = -60.0f;
    float recPulsePhase   = 0.0f;
    bool  fileDragOver    = false;
    int   trimRebuildTicks = 0;  // debounce: counts timer ticks since last trim change   // true while a valid audio file is being dragged over

    dadbass::PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BadDadVerbAudioProcessorEditor)
};
