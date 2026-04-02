#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>   // snprintf
#include <BinaryData.h>

// Forward declaration — PresetBar holds a reference to the processor
// to call loadIRFromMemory() when a preset is selected.
class BadDadVerbAudioProcessor;

namespace dadbass
{

// ─────────────────────────────────────────────────────────────────────────────
//  PresetManager  — file-based preset save / load for BadDadVerb
//  Presets stored as XML in:
//    macOS:   ~/Library/Application Support/DadLabs/BadDadVerb/Presets/
//    Windows: %APPDATA%\DadLabs\BadDadVerb\Presets\
// ─────────────────────────────────────────────────────────────────────────────
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts)
        : apvts (apvts)
    {
        presetsDir = juce::File::getSpecialLocation (
                         juce::File::userApplicationDataDirectory)
                     .getChildFile ("DadLabs")
                     .getChildFile ("BadDadVerb")
                     .getChildFile ("Presets");
        presetsDir.createDirectory();

        // Always install/update factory presets so new presets added in updates
        // are written to disk even when a previous version already created files.
        // User presets use custom names (no numeric prefix) so they are unaffected.
        installFactoryPresets();
    }

    bool savePreset (const juce::String& name,
                     const juce::String& sourceAudioB64 = {})
    {
        if (name.trim().isEmpty()) return false;
        auto xml = apvts.copyState().createXml();
        if (!xml) return false;
        xml->setAttribute ("presetName", name.trim());
        if (sourceAudioB64.isNotEmpty())
            xml->setAttribute ("sourceAudioB64", sourceAudioB64);
        const auto file = presetsDir.getChildFile (
            juce::File::createLegalFileName (name.trim()) + ".xml");
        return xml->writeTo (file);
    }

    bool deletePreset (const juce::String& name)
    {
        const auto file = presetsDir.getChildFile (
            juce::File::createLegalFileName (name.trim()) + ".xml");
        return file.deleteFile();
    }

    bool loadPreset (const juce::String& name,
                     juce::String* sourceAudioB64Out = nullptr)
    {
        const auto file = presetsDir.getChildFile (
            juce::File::createLegalFileName (name.trim()) + ".xml");
        if (!file.existsAsFile()) return false;
        if (auto xml = juce::XmlDocument::parse (file))
        {
            if (xml->hasTagName (apvts.state.getType()))
            {
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
                if (sourceAudioB64Out)
                    *sourceAudioB64Out = xml->getStringAttribute ("sourceAudioB64");
                return true;
            }
        }
        return false;
    }

    juce::StringArray getPresetNames() const
    {
        juce::StringArray names;
        for (const auto& f : presetsDir.findChildFiles (
                 juce::File::findFiles, false, "*.xml"))
            names.add (f.getFileNameWithoutExtension());
        names.sort (true);
        return names;
    }

    juce::File getPresetsDir() const { return presetsDir; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::File presetsDir;

    // ── XML builder — uses snprintf instead of juce::String::formatted ────────
    // (String::formatted was removed in JUCE 8)
    static juce::String makeXml (
        const char* name,
        float input,  float mix,    float predel, float output,
        float irtime, float irhpf,  float freq,   float res,
        float limit,  float tstart, float tend)
    {
        char buf[2048];
        std::snprintf (buf, sizeof(buf),
            "<BADDADVERB presetName=\"%s\">\n"
            "  <PARAM id=\"input\"     value=\"%f\"/>\n"
            "  <PARAM id=\"mix\"       value=\"%f\"/>\n"
            "  <PARAM id=\"predel\"    value=\"%f\"/>\n"
            "  <PARAM id=\"output\"    value=\"%f\"/>\n"
            "  <PARAM id=\"irtime\"    value=\"%f\"/>\n"
            "  <PARAM id=\"irhpf\"     value=\"%f\"/>\n"
            "  <PARAM id=\"freq\"      value=\"%f\"/>\n"
            "  <PARAM id=\"res\"       value=\"%f\"/>\n"
            "  <PARAM id=\"limit\"     value=\"%f\"/>\n"
            "  <PARAM id=\"trimstart\" value=\"%f\"/>\n"
            "  <PARAM id=\"trimend\"   value=\"%f\"/>\n"
            "</BADDADVERB>",
            name,
            input, mix, predel, output, irtime, irhpf,
            freq, res, limit, tstart, tend);
        return juce::String (buf);
    }

    void installFactoryPresets()
    {
        struct FP { const char* name; juce::String body; };

        //                          input  mix    predel out    irtime  irhpf  freq   res    limit  tstart  tend
        FP factory[] = {
            // 01 — sparse Geiger clicks source → tight, glitchy ambience, no predel, very subtle
            { "01 MKULTRA ROOM",
              makeXml("01 MKULTRA ROOM",     0.f, 0.18f,  0.f, -4.f,  0.f, 180.f, 1.0f, 0.0f, -1.5f, 0.0f,  0.45f) },
            // 02 — beating sine cluster source → lush slow-chorus reverb, medium predel
            { "02 ARTICHOKE",
              makeXml("02 ARTICHOKE",        0.f, 0.55f, 22.f, -5.f,  0.f,  60.f, 0.72f,0.0f, -1.0f, 0.0f,  1.0f) },
            // 03 — FM bird chirps source → bright shimmer, pitched up, no res
            { "03 BLUEBIRD",
              makeXml("03 BLUEBIRD",         0.f, 0.40f,  5.f, -4.f,  4.f, 320.f, 0.85f,0.0f, -1.0f, 0.05f, 0.85f) },
            // 04 — metal plate partials source → long, dark, filtered room
            { "04 PAPERCLIP HALL",
              makeXml("04 PAPERCLIP HALL",   0.f, 0.50f, 35.f, -5.f, -3.f,  50.f, 0.55f,0.0f, -1.5f, 0.0f,  1.0f) },
            // 05 — pitch-descending glissando source → short, downward-toned halo
            { "05 MOCKINGBIRD",
              makeXml("05 MOCKINGBIRD",      0.f, 0.30f,  8.f, -4.f,  5.f, 260.f, 0.78f,0.0f, -1.0f, 0.0f,  0.70f) },
            // 06 — sub-bass drone source → dark, full-wet cavern, LP filter closed
            { "06 NORTHWOODS BUNKER",
              makeXml("06 NORTHWOODS BUNKER",0.f, 0.65f,  0.f, -5.f, -5.f,  28.f, 0.28f,0.0f, -1.5f, 0.0f,  0.88f) },
            // 07 — cathedral harmonic series → huge open reverb, very long, dry/wet balanced
            { "07 GLADIO CHURCH",
              makeXml("07 GLADIO CHURCH",    0.f, 0.45f, 55.f, -5.f,  0.f,  45.f, 0.90f,0.0f, -1.5f, 0.0f,  1.0f) },
            // 08 — telephone static source → narrow-band, gated, midrange character
            { "08 COINTELPRO",
              makeXml("08 COINTELPRO",       0.f, 0.35f,  0.f, -5.f,  6.f, 520.f, 0.60f,0.0f, -1.0f, 0.12f, 0.62f) },
            // 09 — frozen shimmer cloud → very wet, no HPF, open LP, long tail
            { "09 ECHELON SATELLITE",
              makeXml("09 ECHELON SATELLITE",0.f, 0.72f, 80.f, -6.f, -2.f,  22.f, 1.0f, 0.0f, -2.0f, 0.0f,  1.0f) },
            // 10 — LFO drone source → slow, undulating, dark, slight resonance
            { "10 DULCE BASE",
              makeXml("10 DULCE BASE",       0.f, 0.58f, 12.f, -5.f, -4.f,  35.f, 0.32f,0.30f,-1.5f, 0.0f,  0.92f) },
            // 11 — magnetic sweep source → short, metallic, pitched +7st, midpoint trim
            { "11 PHILADELPHIA EXP",
              makeXml("11 PHILADELPHIA EXP", 0.f, 0.38f,  0.f, -4.f,  7.f, 380.f, 0.68f,0.0f, -1.0f, 0.30f, 0.72f) },
            // 12 — resonant pulses source → sparse echoed pings, medium LP, long predel
            { "12 STARGATE REMOTE",
              makeXml("12 STARGATE REMOTE",  0.f, 0.42f, 60.f, -5.f, -6.f,  75.f, 0.58f,0.0f, -1.5f, 0.0f,  0.95f) },
            // 13 — choral swell source → lush, harmonically rich, slow attack character
            { "13 MONARCH THRONE",
              makeXml("13 MONARCH THRONE",   0.f, 0.62f, 30.f, -5.f,  3.f,  90.f, 0.80f,0.0f, -1.5f, 0.0f,  1.0f) },
            // 14 — scattered grains source → airy, sparse, diffuse, bright LP open
            { "14 CHEMTRAIL CANOPY",
              makeXml("14 CHEMTRAIL CANOPY", 0.f, 0.28f,  0.f, -4.f,  2.f, 140.f, 0.88f,0.0f, -1.0f, 0.0f,  0.80f) },
            // 15 — flutter echo + tail → large, slap-echo pre-verb, very open
            { "15 AREA 51 HANGAR",
              makeXml("15 AREA 51 HANGAR",   0.f, 0.48f, 45.f, -5.f,  1.f, 120.f, 0.95f,0.0f, -1.5f, 0.0f,  1.0f) },
            // 16 — tiny gated room source → very short, dry-ish, almost ambience
            { "16 ZOG CONTROL ROOM",
              makeXml("16 ZOG CONTROL ROOM", 0.f, 0.20f,  0.f, -4.f,  0.f, 280.f, 1.0f, 0.0f, -1.0f, 0.15f, 0.55f) },
            // 17 — Elliot's Abduction: uploaded source, 50/50 mix, open LP, mind push
            { "17 ELLIOTS ABDUCTION",
              makeXml("17 ELLIOTS ABDUCTION", 0.f, 0.7071f, 0.f, -4.f, 0.f, 90.6f, 1.0f, 0.0f, -6.10f, 0.03f, 0.97f) },
        };

        for (auto& fp : factory)
        {
            const auto file = presetsDir.getChildFile (
                juce::String (fp.name) + ".xml");
            // Always overwrite factory presets so parameter/source changes take effect.
            // User-created presets have different names so they are unaffected.
            file.replaceWithText (fp.body);
        }
    }
};


// ─────────────────────────────────────────────────────────────────────────────
//  PresetBar  — thin UI strip at bottom of plugin window
//  All modal dialogs rewritten for JUCE 8 (uses showAsync / MessageBoxOptions,
//  no enterModalState / ModalCallbackFunction / showOkCancelBox).
// ─────────────────────────────────────────────────────────────────────────────
class PresetBar : public juce::Component
{
public:
    // processor is needed to load the IR that matches each factory preset
    PresetBar (juce::AudioProcessorValueTreeState& apvts,
               class BadDadVerbAudioProcessor&     processor)
        : manager (apvts), processor (processor)
    {
        setOpaque (false);

        presetBox.setTextWhenNothingSelected ("-- sзlзct prзsзt --");
        presetBox.setTextWhenNoChoicesAvailable ("(no prзsзts)");
        presetBox.setJustificationType (juce::Justification::centredLeft);
        presetBox.setColour (juce::ComboBox::backgroundColourId,     juce::Colour (0xff0e0a04));
        presetBox.setColour (juce::ComboBox::textColourId,           juce::Colour (0xffe8a030));
        presetBox.setColour (juce::ComboBox::arrowColourId,          juce::Colour (0xffb87830));
        presetBox.setColour (juce::ComboBox::outlineColourId,        juce::Colour (0xff6a4020));
        presetBox.setColour (juce::ComboBox::focusedOutlineColourId, juce::Colour (0xffe8a030));
        presetBox.onChange = [this, &processor = this->processor]
        {
            const auto name = presetBox.getText();
            if (name.isNotEmpty() && name != "-- sзlзct prзsзt --")
            {
                juce::String b64;
                manager.loadPreset (name, &b64);
                if (b64.isNotEmpty())
                    processor.loadSourceAudioFromBase64 (b64);  // user preset: restore IR
                else
                    loadFactoryIR (name);   // factory preset: load embedded WAV
            }
        };
        addAndMakeVisible (presetBox);

        auto styleBtn = [](juce::TextButton& b, const juce::String& lbl)
        {
            b.setButtonText (lbl);
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a120a));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e200e));
            b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffe8a030));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xfffff0a0));
        };
        styleBtn (saveBtn,    "SAVE");
        styleBtn (deleteBtn,  "DEL");
        styleBtn (openDirBtn, "DIR");
        addAndMakeVisible (saveBtn);
        addAndMakeVisible (deleteBtn);
        addAndMakeVisible (openDirBtn);

        saveBtn.onClick    = [this] { promptSave(); };
        deleteBtn.onClick  = [this] { promptDelete(); };
        openDirBtn.onClick = [this] { manager.getPresetsDir().revealToUser(); };

        refreshList();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 2);
        openDirBtn.setBounds (r.removeFromRight (38));
        r.removeFromRight (2);
        deleteBtn .setBounds (r.removeFromRight (38));
        r.removeFromRight (2);
        saveBtn   .setBounds (r.removeFromRight (50));
        r.removeFromRight (4);
        presetBox .setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xee0e0a04));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.f);
        g.setColour (juce::Colour (0xff6a4020).withAlpha (0.7f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.f, 0.9f);
    }

    // Load preset 17 (Elliot's Abduction) as the default on first launch
    void loadDefault()
    {
        const juce::String name = "17 ELLIOTS ABDUCTION";

        // Load the IR audio for this factory preset
        loadFactoryIR(name);

        // Load the parameter XML
        juce::String b64;
        manager.loadPreset(name, &b64);

        // Refresh the list so preset 17 appears, then select it by ID
        refreshList();
        for (int i = 0; i < presetBox.getNumItems(); ++i)
        {
            if (presetBox.getItemText(i) == name)
            {
                presetBox.setSelectedId(presetBox.getItemId(i),
                                        juce::dontSendNotification);
                break;
            }
        }
    }

    void refreshList()
    {
        const auto current = presetBox.getText();
        presetBox.clear (juce::dontSendNotification);
        int idx = 0, selectedId = -1;
        for (const auto& name : manager.getPresetNames())
        {
            presetBox.addItem (name, ++idx);
            if (name == current) selectedId = idx;
        }
        if (selectedId > 0)
            presetBox.setSelectedId (selectedId, juce::dontSendNotification);
    }

private:
    PresetManager                manager;
    BadDadVerbAudioProcessor&    processor;
    juce::ComboBox               presetBox;
    juce::TextButton             saveBtn, deleteBtn, openDirBtn;

    // ── Load the factory IR that matches a preset name ──────────────────────────
    // The preset number prefix (e.g. "01 ") determines which embedded IR to load.
    // Called automatically when a preset is selected from the combo box.
    void loadFactoryIR (const juce::String& presetName)
    {
        // Match preset number prefix → embedded IR WAV data.
        // JUCE BinaryData names: filename with non-alphanumeric→_, prefixed _ if starts digit.
        struct IREntry { const char* prefix; const void* data; int size; };

        static const IREntry entries[] = {
            { "01", DadBassAssets::_01_MKULTRA_ROOM_wav,  DadBassAssets::_01_MKULTRA_ROOM_wavSize  },
            { "02", DadBassAssets::_02_ARTICHOKE_wav,     DadBassAssets::_02_ARTICHOKE_wavSize     },
            { "03", DadBassAssets::_03_BLUEBIRD_wav,      DadBassAssets::_03_BLUEBIRD_wavSize      },
            { "04", DadBassAssets::_04_PAPERCLIP_HALL_wav,DadBassAssets::_04_PAPERCLIP_HALL_wavSize},
            { "05", DadBassAssets::_05_MOCKINGBIRD_wav,   DadBassAssets::_05_MOCKINGBIRD_wavSize   },
            { "06", DadBassAssets::_06_NORTHWOODS_wav,    DadBassAssets::_06_NORTHWOODS_wavSize    },
            { "07", DadBassAssets::_07_GLADIO_CHURCH_wav, DadBassAssets::_07_GLADIO_CHURCH_wavSize },
            { "08", DadBassAssets::_08_COINTELPRO_wav,    DadBassAssets::_08_COINTELPRO_wavSize    },
            { "09", DadBassAssets::_09_ECHELON_wav,       DadBassAssets::_09_ECHELON_wavSize       },
            { "10", DadBassAssets::_10_DULCE_BASE_wav,    DadBassAssets::_10_DULCE_BASE_wavSize    },
            { "11", DadBassAssets::_11_PHILLY_EXP_wav,    DadBassAssets::_11_PHILLY_EXP_wavSize    },
            { "12", DadBassAssets::_12_STARGATE_wav,      DadBassAssets::_12_STARGATE_wavSize      },
            { "13", DadBassAssets::_13_MONARCH_wav,       DadBassAssets::_13_MONARCH_wavSize       },
            { "14", DadBassAssets::_14_CHEMTRAIL_wav,     DadBassAssets::_14_CHEMTRAIL_wavSize     },
            { "15", DadBassAssets::_15_AREA51_wav,        DadBassAssets::_15_AREA51_wavSize        },
            { "16", DadBassAssets::_16_ZOG_ROOM_wav,      DadBassAssets::_16_ZOG_ROOM_wavSize      },
            { "17", DadBassAssets::_17_ELLIOTS_ABDUCTION_wav, DadBassAssets::_17_ELLIOTS_ABDUCTION_wavSize },
        };

        // Match by numeric prefix at start of preset name
        for (const auto& e : entries)
            if (presetName.startsWith (e.prefix))
                { processor.loadIRFromMemory (e.data, e.size); return; }
    }

    // ── Save dialog — fully JUCE 8 compatible ────────────────────────────────
    // Custom component shown as a CallOutBox so we avoid all deprecated modal APIs.
    struct SaveDialog : public juce::Component
    {
        juce::Label         label;
        juce::TextEditor    editor;
        juce::TextButton    saveBtn  { "Save"   };
        juce::TextButton    cancelBtn{ "Cancel" };

        std::function<void(const juce::String&)> onSave;

        SaveDialog()
        {
            label.setText ("Preset name:", juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, juce::Colour (0xffe8a030));
            addAndMakeVisible (label);

            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a120a));
            editor.setColour (juce::TextEditor::textColourId,       juce::Colour (0xffe8a030));
            editor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff6a4020));
            addAndMakeVisible (editor);

            for (auto* b : { &saveBtn, &cancelBtn })
            {
                b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a120a));
                b->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffe8a030));
                addAndMakeVisible (*b);
            }

            saveBtn.onClick = [this]
            {
                const auto name = editor.getText().trim();
                if (name.isNotEmpty() && onSave)
                    onSave (name);
                closeParentCallOut();
            };
            cancelBtn.onClick = [this] { closeParentCallOut(); };

            setSize (240, 90);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            label .setBounds (r.removeFromTop (20));
            editor.setBounds (r.removeFromTop (24));
            r.removeFromTop (4);
            auto row = r.removeFromTop (24);
            cancelBtn.setBounds (row.removeFromRight (70));
            row.removeFromRight (4);
            saveBtn  .setBounds (row.removeFromRight (70));
        }

    private:
        void closeParentCallOut()
        {
            for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
                if (auto* box = dynamic_cast<juce::CallOutBox*> (c))
                    { box->dismiss(); return; }
        }
    };

    void promptSave()
    {
        auto dlg = std::make_unique<SaveDialog>();
        dlg->editor.setText (presetBox.getText());
        dlg->onSave = [this] (const juce::String& name)
        {
            // Embed source audio in preset so IR is restored on load
            const auto b64 = processor.getSourceAudioAsBase64();
            if (manager.savePreset (name, b64))
            {
                refreshList();
                for (int i = 1; i <= presetBox.getNumItems(); ++i)
                    if (presetBox.getItemText (i - 1) == name)
                        { presetBox.setSelectedId (i, juce::dontSendNotification); break; }
            }
        };

        auto& dlgRef = *dlg;
        juce::CallOutBox::launchAsynchronously (std::move (dlg),
                                                saveBtn.getScreenBounds(),
                                                nullptr);
        (void) dlgRef;  // launchAsynchronously takes ownership
    }

    // ── Delete dialog — JUCE 8 async API ─────────────────────────────────────
    void promptDelete()
    {
        const auto name = presetBox.getText();
        if (name.isEmpty() || name == "-- sзlзct prзsзt --") return;

        // JUCE 8: use juce::MessageBoxOptions + showAsync
        juce::MessageBoxOptions opts = juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("Delete Preset")
            .withMessage ("Delete \"" + name + "\"?")
            .withButton ("Delete")
            .withButton ("Cancel");

        juce::AlertWindow::showAsync (opts, [this, name] (int result)
        {
            if (result == 1)   // 1 = first button = "Delete"
            {
                manager.deletePreset (name);
                refreshList();
            }
        });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace dadbass
