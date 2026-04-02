#include "PluginEditor.h"

// ── Authored canvas size (same as DADBADBASS) ─────────────────────────────────
namespace Layout
{
    static constexpr float W = 1040.f;
    static constexpr float H = 600.f;

    // Three circular displays — symmetrical, D=200, gap=60, cy_authored=220
    // Layout:  VU(cx=160)  CRT scope(cx=420)  Sample scope(cx=680)
    static constexpr float DISP_D  = 200.f;   // diameter of each circle
    static constexpr float DISP_CY = 220.f;   // shared vertical centre

    static constexpr float VU_CX   = 160.f;
    static constexpr float VU_X    = VU_CX - DISP_D*0.5f;    //  60
    static constexpr float VU_Y    = DISP_CY - DISP_D*0.5f;  // 120
    static constexpr float VU_W    = DISP_D;
    static constexpr float VU_H    = DISP_D;

    static constexpr float CRT_CX  = 420.f;
    static constexpr float CRT_CY  = DISP_CY;
    static constexpr float SCOPE_X = CRT_CX - DISP_D*0.5f;   // 320
    static constexpr float SCOPE_Y = DISP_CY - DISP_D*0.5f;  // 120
    static constexpr float SCOPE_W = DISP_D;
    static constexpr float SCOPE_H = DISP_D;

    static constexpr float SMP_CX  = 680.f;
    static constexpr float SMP_X   = SMP_CX - DISP_D*0.5f;   // 580
    static constexpr float SMP_Y   = DISP_CY - DISP_D*0.5f;  // 120
    static constexpr float SMP_W   = DISP_D;
    static constexpr float SMP_H   = DISP_D;

    // Legacy aliases used elsewhere in paint()
    static constexpr float SCOPE_CX = CRT_CX;

    // Record buttons (right side, authored coords) — unused; positions set directly in resized()
    // static constexpr float BTN_X = 740.f, BTN_W = 180.f, BTN_H = 38.f;
    // static constexpr float BTN_Y0 = 110.f, BTN_GAP = 52.f;

    // Knob row — authored
    static constexpr float KY = 445.f, KW = 80.f, KH = 80.f;
    static constexpr float KX0 = 80.f, KX_GAP = 110.f;
    // Trim knobs on right
    static constexpr float TRIM_X0 = 800.f;
}

BadDadVerbAudioProcessorEditor::BadDadVerbAudioProcessorEditor(BadDadVerbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      presetBar(p.apvts, p)
{
    setLookAndFeel(&lnf);
    setOpaque(true);

    for (auto* s : { &inputKnob, &mixKnob, &wetKnob, &outputKnob,
                     &timeKnob, &hpfKnob, &freqKnob, &resKnob,
                     &limitKnob, &trimStartKnob, &trimEndKnob })
        configureKnob(*s);
    // Filter knobs in button column
    // (freqKnob2/resKnob2 style removed)
    // Button texts used for wear-seed in LookAndFeel, labels drawn in paintOverChildren
    addAndMakeVisible(scope);
    addAndMakeVisible(sampleScope);
    addAndMakeVisible(presetBar);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(reverseButton);
    addAndMakeVisible(normalizeButton);

    recordButton  .setButtonText("PSYOP");
    stopButton    .setButtonText("OVERLOAD");
    reverseButton .setButtonText("REV");
    normalizeButton.setButtonText("MIND PUSH");

    recordButton.onStateChange = [this]
    {
        audioProcessor.setPsyOp(recordButton.getToggleState());
    };
    stopButton.onStateChange = [this]
    {
        // OVERLOAD: toggle heavy wet compression+saturation
        audioProcessor.setOverload(stopButton.getToggleState());
    };
    // Disable JUCE's automatic toggle-on-click — we manage state ourselves.
    // This makes the click behaviour completely unambiguous across JUCE versions.
    reverseButton.setClickingTogglesState(false);
    reverseButton.onClick = [this]
    {
        const bool newState = !audioProcessor.getIsReversed();
        audioProcessor.setReversed(newState);
        reverseButton.setToggleState(newState, juce::dontSendNotification);
        syncScope();
    };
    normalizeButton.onClick = [this]
    {
        audioProcessor.setMindPush(normalizeButton.getToggleState());
    };

    // hpfKnob: HPF is now real-time in processBlock — no IR rebuild needed
    // ALL onValueChange callbacks set AFTER attachments — attachments overwrite them if set before

    auto& apvts = audioProcessor.apvts;
    inputAtt    = std::make_unique<SA>(apvts, "input",    inputKnob);
    mixAtt      = std::make_unique<SA>(apvts, "mix",      mixKnob);
    wetAtt      = std::make_unique<SA>(apvts, "predel",   wetKnob);
    outputAtt   = std::make_unique<SA>(apvts, "output",   outputKnob);
    timeAtt     = std::make_unique<SA>(apvts, "irtime",   timeKnob);
    hpfAtt      = std::make_unique<SA>(apvts, "irhpf",    hpfKnob);
    freqAtt     = std::make_unique<SA>(apvts, "freq",     freqKnob);
    resAtt      = std::make_unique<SA>(apvts, "res",      resKnob);
    limitAtt    = std::make_unique<SA>(apvts, "limit",    limitKnob);
    trimStartAtt= std::make_unique<SA>(apvts, "trimstart",trimStartKnob);
    trimEndAtt  = std::make_unique<SA>(apvts, "trimend",  trimEndKnob);

    // Set AFTER attachments — attachments overwrite onValueChange if set before
    timeKnob.onValueChange = [this] { audioProcessor.applyTrimFromParameters(); syncScope(); };

    // Minimum gap: 10% of range — inlined to avoid MSVC lambda capture issues
    trimStartKnob.onValueChange = [this]
    {
        const float s = (float) trimStartKnob.getValue();
        const float e = (float) trimEndKnob.getValue();
        if (s >= e - 0.10f)
            trimEndKnob.setValue(juce::jlimit(
                (double) trimEndKnob.getMinimum(),
                (double) trimEndKnob.getMaximum(),
                (double)(s + 0.10f)), juce::sendNotificationAsync);
        trimRebuildTicks = 4;  // rebuild after ~67ms of inactivity (4 × 60Hz ticks)
        syncScope();
    };

    trimEndKnob.onValueChange = [this]
    {
        const float s = (float) trimStartKnob.getValue();
        const float e = (float) trimEndKnob.getValue();
        if (e <= s + 0.10f)
            trimStartKnob.setValue(juce::jlimit(
                (double) trimStartKnob.getMinimum(),
                (double) trimStartKnob.getMaximum(),
                (double)(e - 0.10f)), juce::sendNotificationAsync);
        trimRebuildTicks = 4;
        syncScope();
    };
    // Filter knobs in button column (share same params)
    // freqKnob2 / resKnob2 removed — use the main knobs in the strip instead

    timeKnob     .setDoubleClickReturnValue(true,  0.0);
    freqKnob     .setDoubleClickReturnValue(true,  1.0);
    resKnob      .setDoubleClickReturnValue(true,  0.0);
    inputKnob    .setDoubleClickReturnValue(true,  0.0);
    mixKnob      .setDoubleClickReturnValue(true,  0.30);
    wetKnob      .setDoubleClickReturnValue(true,  0.0);  // predel default = 0ms
    outputKnob   .setDoubleClickReturnValue(true, -2.0);
    hpfKnob      .setDoubleClickReturnValue(true, 140.0);
    limitKnob    .setDoubleClickReturnValue(true, -1.0);
    trimStartKnob.setDoubleClickReturnValue(true,  0.03);
    trimEndKnob  .setDoubleClickReturnValue(true,  0.97);

    setSize(728, 444);  // +24 px for preset bar at bottom
    startTimerHz(60);

    // Load "Elliot's Abduction" as default on first launch.
    // Only load if no IR is present — preserves DAW session state on reload.
    if (!audioProcessor.hasIRLoaded() && !audioProcessor.hasSourceAudio())
    {
        presetBar.loadDefault();
        // Enable Mind Push as specified for this preset
        audioProcessor.setMindPush(true);
        normalizeButton.setToggleState(true, juce::dontSendNotification);
    }
}

BadDadVerbAudioProcessorEditor::~BadDadVerbAudioProcessorEditor()
{
    stopTimer();              // stop callbacks BEFORE nulling LookAndFeel
    setLookAndFeel(nullptr);
}

void BadDadVerbAudioProcessorEditor::configureKnob(juce::Slider& s, const juce::String&)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // 300 pixels for full range = 2× slower than the JUCE default (~150 px)
    s.setMouseDragSensitivity(300);
    addAndMakeVisible(s);
}

void BadDadVerbAudioProcessorEditor::timerCallback()
{
    // Consume any pending IR rebuild (posted from audio thread to avoid stalling it)
    audioProcessor.rebuildIRIfNeeded();

    // Debounced trim rebuild — fires once the knobs stop moving for ~67ms
    if (trimRebuildTicks > 0)
    {
        --trimRebuildTicks;
        if (trimRebuildTicks == 0)
        {
            audioProcessor.applyTrimFromParameters();
            syncScope();
        }
    }

    // Sync psyOp button — processor can turn this off externally
    const bool psyOn = audioProcessor.isPsyOp();
    if (recordButton.getToggleState() != psyOn)
        recordButton.setToggleState(psyOn, juce::dontSendNotification);
    // Disable REV while psyOp is active
    reverseButton.setEnabled (!psyOn);
    trimStartKnob.setEnabled (!psyOn);
    trimEndKnob  .setEnabled (!psyOn);
    // Sync overload button
    const bool ovld = audioProcessor.isOverload();
    if (stopButton.getToggleState() != ovld)
        stopButton.setToggleState(ovld, juce::dontSendNotification);
    // reverseButton is only synced when the processor signals an external change
    // (e.g. state load from a preset). onClick is the normal trigger.
    if (audioProcessor.popReversalChangedExternal())
    {
        const bool rev = audioProcessor.getIsReversed();
        if (reverseButton.getToggleState() != rev)
            reverseButton.setToggleState(rev, juce::dontSendNotification);
    }
    // Sync mindPush and overload buttons when restored from DAW state
    if (audioProcessor.popButtonStateChangedExternal())
    {
        normalizeButton.setToggleState(audioProcessor.getMindPush(),  juce::dontSendNotification);
        stopButton     .setToggleState(audioProcessor.isOverload(),   juce::dontSendNotification);
    }

    const float newDb = audioProcessor.inputLevelDb.load(std::memory_order_relaxed);
    // Fast attack (instant snap up), slow decay (~1.5 s to fall 40 dB at 60 Hz)
    const float fallRate = 0.4f;  // dB per timer tick (60 Hz → ~24 dB/s fall)
    if (newDb > meterLevelDb)
        meterLevelDb = newDb;                            // instant attack
    else
        meterLevelDb = juce::jmax(newDb, meterLevelDb - fallRate); // slow decay
    repaint();
    if (audioProcessor.isRecordingIR())
    {
        recPulsePhase += 0.07f;
        if (recPulsePhase > juce::MathConstants<float>::twoPi)
            recPulsePhase -= juce::MathConstants<float>::twoPi;
        repaint();
    }
    syncScope();
}

void BadDadVerbAudioProcessorEditor::syncScope()
{
    scope.setRecording(audioProcessor.isRecordingIR());
    scope.setTrimming(audioProcessor.hasIRLoaded());
    if (audioProcessor.isRecordingIR() || !audioProcessor.hasIRLoaded())
    {
        const int wp = audioProcessor.scopeWritePos.load(std::memory_order_acquire);
        scope.snapshotFromRing(audioProcessor.scopeRing, wp);
    }
    else
    {
        scope.setStaticBuffer(audioProcessor.getIRForDisplay(), 0,
                              audioProcessor.getTrimStartNorm(),
                              audioProcessor.getTrimEndNorm());
    }
    scope.repaint();

    // Sample scope always shows input ring
    {
        const int wp = audioProcessor.scopeWritePos.load(std::memory_order_acquire);
        sampleScope.snapshotFromRing(audioProcessor.scopeRing, wp);
    }
    sampleScope.repaint();
}

// ── Main paint ────────────────────────────────────────────────────────────────
void BadDadVerbAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float W = Layout::W, H = Layout::H;
    // Use fixed 420px panel height so artwork never stretches into the preset bar strip
    const float scaleX = float(getWidth()) / W;
    const float scaleY = 420.f / H;
    g.addTransform(juce::AffineTransform::scale(scaleX, scaleY));

    // ── Panel texture ─────────────────────────────────────────────────────────
    static juce::Image panelImg = juce::ImageCache::getFromMemory(
        DadBassAssets::panel_rust_png, DadBassAssets::panel_rust_pngSize);
    if (panelImg.isValid())
        g.drawImage(panelImg, 0.f, 0.f, W, H, 0, 0, panelImg.getWidth(), panelImg.getHeight());

    const auto amber    = juce::Colour(0xffe3a34a);
    const auto amberDim = juce::Colour(0xffb8732d);

    // ── Title — fixed Cyrillic-mixed spelling ─────────────────────────────────
    g.setColour(juce::Colour(0xff242810).withAlpha(0.55f));
    g.fillRect(28.0f, 14.0f, 430.0f, 62.0f);
    g.setFont(cyrillicFont(46.0f, true));
    g.setColour(juce::Colour(0x50000000));
    g.drawText(juce::String::fromUTF8(u8"ВАдDАдVЗЯВ"), 33, 21, 420, 54, juce::Justification::centredLeft, false);
    g.setColour(amber.withAlpha(0.90f));
    g.drawText(juce::String::fromUTF8(u8"ВАдDАдVЗЯВ"), 30, 18, 420, 54, juce::Justification::centredLeft, false);

    // Subtitle
    g.setFont(cyrillicFont(12.0f, true));
    g.setColour(amberDim.withAlpha(0.75f));
    g.drawText(juce::String::fromUTF8(u8"МIНD СОНТЯОL / GООD СНILD РЯОGЯАММIНG"),
               32, 82, 340, 14, juce::Justification::left, false);
    g.drawText(juce::String::fromUTF8(u8"ЯЗСОЯD SАМРLЗ / SТЯАНGЗ VЗЯВ"),
               680, 82, 320, 14, juce::Justification::right, false);

    // ── Separator lines ───────────────────────────────────────────────────────
    g.setColour(juce::Colour(0xff6a4020));
    g.fillRect(28.0f,  100.0f, W - 56.0f, 1.5f);
    g.fillRect(28.0f,  420.0f, W - 56.0f, 1.5f);

    // ── Section panel behind knob row ─────────────────────────────────────────
    g.setColour(juce::Colour(0x99000000));
    g.fillRoundedRectangle(24.0f, 424.0f, W - 48.0f, H - 448.0f, 5.0f);
    g.setColour(juce::Colour(0xff5a3820));
    g.drawRoundedRectangle(24.0f, 424.0f, W - 48.0f, H - 448.0f, 5.0f, 1.0f);

    // ── Section labels above knob row ─────────────────────────────────────────
    // Bands match the new knob layout (K0=24, KGAP=88):
    //   Band A: IN MIX WET          (knobs 0-2, authored x 24..359)
    //   Band B: TIME HPF FREQ RES OUT LIMIT  (knobs 3-8, authored x 288..808)
    //   Band C: STRT END             (knobs 9-10, authored x 816..984)
    g.setFont(cyrillicFont(11.0f, true));
    g.setColour(amberDim.withAlpha(0.78f));
    g.drawText(juce::String::fromUTF8(u8"IНРUТ / МIХ / WЗТ"),
               24, 428, 280, 14, juce::Justification::centred, false);
    g.drawText(juce::String::fromUTF8(u8"IМРULSЗ СОНТЯОL / ОUТ / LIМIТ"),
               288, 428, 524, 14, juce::Justification::centred, false);
    g.drawText(juce::String::fromUTF8(u8"ТЯIМ / IЯ"),
               816, 428, 168, 14, juce::Justification::centred, false);

    // ── VU meter ──────────────────────────────────────────────────────────────
    drawVuMeter(g, { Layout::VU_X, Layout::VU_Y, Layout::VU_W, Layout::VU_H });

    // ── Labels above each circular display ────────────────────────────────────
    g.setFont(cyrillicFont(10.0f, true));
    g.setColour(amberDim.withAlpha(0.75f));
    // VU meter label — centred at VU_CX=160, just above circle top (y=120 → label at y=104)
    g.drawText(juce::String::fromUTF8(u8"IНРUТ LЗVЗL"),
               (int)Layout::VU_CX - 80,  104, 160, 12, juce::Justification::centred, false);
    // Main scope label — centred at CRT_CX=420
    g.drawText(audioProcessor.isPsyOp()       ? juce::String::fromUTF8(u8"● РSYОР LIИЗ")
               : (audioProcessor.hasIRLoaded() ? juce::String::fromUTF8(u8"IЯ WАVЗFОЯМ")
                                               : juce::String::fromUTF8(u8"ЯЗС IЯ НЗЯЗ")),
               (int)Layout::CRT_CX - 80,  104, 160, 12, juce::Justification::centred, false);
    // Sample scope label — centred at SMP_CX=680
    g.drawText(juce::String::fromUTF8(u8"SАМРLЗD SIGНАL"),
               (int)Layout::SMP_CX - 80,  104, 160, 12, juce::Justification::centred, false);

    // ── Limiter lamp — moved to below VU meter ────────────────────────────────
    if (audioProcessor.limiterLamp.load(std::memory_order_relaxed))
    {
        // Place lamp just below the VU circle bottom (VU_Y+VU_H=320, lamp at y=330)
        const float lampX = Layout::VU_CX, lampY = Layout::VU_Y + Layout::VU_H + 36.f;
        g.setColour(juce::Colour(0xffaa2210));
        g.fillEllipse(lampX - 7.f, lampY - 7.f, 14.f, 14.f);
        juce::ColourGradient limGlow(juce::Colour(0xffff2200).withAlpha(0.45f), lampX, lampY,
                                     juce::Colour(0x00ff2200), lampX + 20.f, lampY + 20.f, true);
        g.setGradientFill(limGlow);
        g.fillEllipse(lampX - 20.f, lampY - 14.f, 40.f, 28.f);
        g.setColour(amberDim);
        g.setFont(cyrillicFont(10.0f, true));
        g.drawText(juce::String::fromUTF8(u8"блядь"), (int)lampX - 30, (int)lampY + 10, 60, 12, juce::Justification::centred, false);
    }

    // ── Pulsing red recording glow around scope ───────────────────────────────
    if (audioProcessor.isRecordingIR())
    {
        const float pulse = 0.5f + 0.5f * std::sin(recPulsePhase);
        const float scopeCX = Layout::CRT_CX;
        const float scopeCY = Layout::CRT_CY;
        const float scopeR  = Layout::DISP_D * 0.5f;
        juce::ColourGradient recGlow(
            juce::Colour(0xffff0000).withAlpha(0.18f * pulse), scopeCX, scopeCY,
            juce::Colour(0x00ff0000), scopeCX + scopeR * 1.6f, scopeCY + scopeR * 1.6f, true);
        g.setGradientFill(recGlow);
        g.fillEllipse(scopeCX - scopeR*1.6f, scopeCY - scopeR*1.6f, scopeR*3.2f, scopeR*3.2f);
    }

    // ── Vignette — decades of grime on panel edges ────────────────────────────
    {
        juce::ColourGradient vig(juce::Colour(0x00000000), W*0.5f, H*0.5f,
                                 juce::Colour(0x99000000), 0.f, 0.f, true);
        g.setGradientFill(vig);
        g.fillRect(0.f, 0.f, W, H);
    }

    // Glow layer drawn in paintOverChildren() so no authored transform is active
}

void BadDadVerbAudioProcessorEditor::drawGlowLayer(juce::Graphics& g)
{
    // Draw all glows onto a separate transparent ARGB image at panel resolution,
    // then composite over the panel — eliminates JUCE radial gradient square artefacts.
    const int SW = getWidth();
    const int SH = 420;   // panel height only — preset bar is below
    juce::Image glowImg(juce::Image::ARGB, SW, SH, true);
    juce::Graphics gg(glowImg);

    auto& apvts = audioProcessor.apvts;
    auto norm = [&](const juce::String& id) -> float
    {
        auto* p = apvts.getParameter(id);
        return p ? p->getValue() : 0.f;
    };

    const float kr = float(Layout::KW) * 0.5f * float(SW) / Layout::W;

    // Helper: clip-then-fill an ellipse — eliminates JUCE's bounding-box square artefact
    // on radial gradients. The gradient is drawn over the full rect but only the
    // ellipse pixels survive via the clip region.
    auto glowEllipse = [&](float sx, float sy, float r, juce::ColourGradient grad)
    {
        juce::Graphics::ScopedSaveState ss2(gg);
        juce::Path clip;
        clip.addEllipse(sx - r, sy - r, r*2.f, r*2.f);
        gg.reduceClipRegion(clip);
        gg.setGradientFill(grad);
        gg.fillEllipse(sx - r, sy - r, r*2.f, r*2.f);
    };

    auto drawGlow = [&](float sx, float sy, float sr_px, float strength)
    {
        if (strength < 0.005f) return;
        const float s = strength * 0.22f;
        // Pass 1 — wide ambient
        glowEllipse(sx, sy, sr_px*5.f,
            [&]{ juce::ColourGradient g1(juce::Colour(0xff50a010).withAlpha(s*0.55f), sx, sy,
                                         juce::Colour(0x00204004), sx+sr_px*5.f, sy, true);
                 g1.addColour(0.30, juce::Colour(0xff3a7808).withAlpha(s*0.45f));
                 g1.addColour(0.60, juce::Colour(0xff204004).withAlpha(s*0.20f));
                 g1.addColour(0.85, juce::Colour(0xff0a1a02).withAlpha(s*0.05f));
                 return g1; }());
        // Pass 2 — mid bloom
        glowEllipse(sx, sy, sr_px*2.5f,
            [&]{ juce::ColourGradient g2(juce::Colour(0xff70c018).withAlpha(s*1.0f), sx, sy,
                                         juce::Colour(0x00204004), sx+sr_px*2.5f, sy, true);
                 g2.addColour(0.18, juce::Colour(0xff60b014).withAlpha(s*0.90f));
                 g2.addColour(0.45, juce::Colour(0xff408808).withAlpha(s*0.60f));
                 g2.addColour(0.72, juce::Colour(0xff1e4404).withAlpha(s*0.25f));
                 g2.addColour(0.92, juce::Colour(0x00204004));
                 return g2; }());
        // Pass 3 — hot core
        glowEllipse(sx, sy, sr_px*1.4f,
            [&]{ juce::ColourGradient g3(juce::Colour(0xff98e030).withAlpha(s*1.2f), sx, sy,
                                         juce::Colour(0x00204004), sx+sr_px*1.4f, sy, true);
                 g3.addColour(0.40, juce::Colour(0xff70c020).withAlpha(s*0.85f));
                 g3.addColour(0.75, juce::Colour(0xff306008).withAlpha(s*0.30f));
                 g3.addColour(0.95, juce::Colour(0x00204004));
                 return g3; }());
    };



    // Knobs — all 11, with correct parameter IDs
    struct KE { juce::Component* comp; const char* id; };
    const KE knobs[] = {
        { &inputKnob,    "input"     }, { &mixKnob,      "mix"      },
        { &wetKnob,      "predel"    }, { &outputKnob,   "output"   },
        { &timeKnob,     "irtime"    }, { &hpfKnob,      "irhpf"    },
        { &freqKnob,     "freq"      }, { &resKnob,      "res"       },
        { &limitKnob,    "limit"     }, { &trimStartKnob,"trimstart" },
        { &trimEndKnob,  "trimend"   },
    };
    for (const auto& k : knobs)
    {
        if (!k.comp->isEnabled()) continue;   // no glow on disabled knobs
        const auto cb = k.comp->getBounds();
        drawGlow(float(cb.getCentreX()), float(cb.getCentreY()), kr, norm(k.id));
    }

    // ── Button glows — tight, natural falloff, no rectangular bleed ─────────
    // Key: use small ambient radius (2.5× not 5×), strong inner core,
    // very low alpha on outer passes to avoid rectangular wash from overlap.
    auto drawBtnGlow = [&](float sx, float sy, float sr_px,
                            juce::Colour inner, juce::Colour mid, bool isOn)
    {
        if (!isOn && sr_px < 1.f) return;
        const float strength = isOn ? 1.0f : 0.28f;
        // Outermost: very faint, tight (2.5×) — keeps halo round, no bleed
        glowEllipse(sx, sy, sr_px*2.5f,
            [&]{ juce::ColourGradient g1(mid.withAlpha(0.08f * strength), sx, sy,
                                         juce::Colour(0x00000000), sx+sr_px*2.5f, sy, true);
                 g1.addColour(0.5f, mid.withAlpha(0.04f * strength));
                 return g1; }());
        // Mid bloom
        glowEllipse(sx, sy, sr_px*1.6f,
            [&]{ juce::ColourGradient g2(mid.withAlpha(0.22f * strength), sx, sy,
                                         juce::Colour(0x00000000), sx+sr_px*1.6f, sy, true);
                 g2.addColour(0.4f, mid.withAlpha(0.12f * strength));
                 return g2; }());
        // Hot core — bright, small
        glowEllipse(sx, sy, sr_px*0.9f,
            [&]{ juce::ColourGradient g3(inner.withAlpha(0.55f * strength), sx, sy,
                                         juce::Colour(0x00000000), sx+sr_px*0.9f, sy, true);
                 g3.addColour(0.3f, inner.withAlpha(0.35f * strength));
                 return g3; }());
    };

    for (juce::Component* btn : { (juce::Component*)&recordButton, (juce::Component*)&stopButton,
                                   (juce::Component*)&reverseButton, (juce::Component*)&normalizeButton })
    {
        if (!btn->isEnabled()) continue;  // no glow when disabled
        const auto  cb   = btn->getBounds();
        const float sr   = float(cb.getHeight()) * 0.5f;
        const auto* tb   = dynamic_cast<const juce::ToggleButton*>(btn);
        const bool  isOn = tb && tb->getToggleState();
        const float cx2  = float(cb.getCentreX());
        const float cy2  = float(cb.getCentreY());

        if (btn == &recordButton || btn == &stopButton)
            drawBtnGlow(cx2, cy2, sr,
                        juce::Colour(0xffff3020), juce::Colour(0xffcc1010), isOn);
        else if (btn == &normalizeButton)
            drawBtnGlow(cx2, cy2, sr,
                        juce::Colour(0xffff2040), juce::Colour(0xffaa0820), isOn);
        else
            drawBtnGlow(cx2, cy2, sr,
                        juce::Colour(0xff80e030), juce::Colour(0xff408810), isOn);
    }
    // FREQ2 and RES2 knobs glow
    // (freqKnob2/resKnob2 visibility removed)

    // ── Amber rim glows around all three circular displays ────────────────────
    // Each display gets a warm amber halo matching the panel aesthetic.
    auto drawAmberRim = [&](float authored_cx, float authored_cy, float authored_r)
    {
        const float scx = authored_cx * float(SW) / Layout::W;
        const float scy = authored_cy * float(SH) / Layout::H;
        const float sr  = authored_r  * float(SW) / Layout::W;
        juce::ColourGradient rim(
            juce::Colour(0x00000000), scx, scy,
            juce::Colour(0xffe8a030).withAlpha(0.22f), scx + sr * 1.15f, scy, true);
        gg.setGradientFill(rim);
        gg.fillEllipse(scx - sr*1.15f, scy - sr*1.15f, sr*2.3f, sr*2.3f);
    };
    const float dispR = Layout::DISP_D * 0.5f;
    drawAmberRim(Layout::VU_CX,  Layout::DISP_CY, dispR);
    drawAmberRim(Layout::CRT_CX, Layout::CRT_CY,  dispR);
    drawAmberRim(Layout::SMP_CX, Layout::DISP_CY, dispR);

    // Draw directly at screen coords — no transform manipulation needed because
    // drawGlowLayer is called from paintOverChildren before any transform is set.
    g.drawImageAt(glowImg, 0, 0);
}

// ── VU meter ──────────────────────────────────────────────────────────────────
void BadDadVerbAudioProcessorEditor::drawVuMeter(juce::Graphics& g, juce::Rectangle<float> area)
{
    const float MCX  = area.getCentreX();
    const float MCY  = area.getCentreY();
    const float MR   = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const float pivX = MCX;
    const float pivY = MCY + MR * 0.28f;
    const float scaleR = MR * 0.78f;
    const float dbVal  = juce::jlimit(-20.f, 3.f, meterLevelDb);
    const float t      = (dbVal + 20.f) / 23.f;
    const float needleA = (-57.5f + t * 115.f) * juce::MathConstants<float>::pi / 180.f;

    g.setColour(juce::Colour(0x80000000));
    g.fillEllipse(MCX-MR-3, MCY-MR-1, MR*2+6, MR*2+6);
    juce::ColourGradient bezel(juce::Colour(0xff2f2f2f), MCX-MR*0.4f, MCY-MR*0.5f,
                               juce::Colour(0xff080808), MCX+MR*0.4f, MCY+MR*0.5f, false);
    g.setGradientFill(bezel);
    g.fillEllipse(MCX-MR, MCY-MR, MR*2, MR*2);
    g.setColour(juce::Colour(0xfff0ecc8));
    g.fillEllipse(MCX-MR*0.82f, MCY-MR*0.82f, MR*1.64f, MR*1.64f);

    g.setColour(juce::Colour(0xff1a1410).withAlpha(0.65f));
    for (float db : { -20.f, -10.f, -7.f, -5.f, -3.f, 0.f, 1.f, 2.f, 3.f })
    {
        const float tt  = (db + 20.f) / 23.f;
        const float ang = (-57.5f + tt * 115.f) * juce::MathConstants<float>::pi / 180.f;
        const float r1  = scaleR - 6.f;
        const float r2  = scaleR + (((int)db % 5 == 0) ? 3.f : 0.f);
        g.drawLine(pivX + std::sin(ang)*r1, pivY - std::cos(ang)*r1,
                   pivX + std::sin(ang)*r2, pivY - std::cos(ang)*r2,
                   (db == 0.f || db == -20.f || db == 3.f) ? 1.2f : 0.8f);
    }
    g.setFont(cyrillicFont(7.5f, true));
    struct Label { float db; const char* txt; bool red; };
    const Label nums[] = { {-20,"-20",false},{-10,"-10",false},{-7,"-7",false},
                            {-5,"-5",false},{-3,"-3",false},{0,"0",false},
                            {1,"1",true},{2,"2",true},{3,"3",true} };
    for (auto& lb : nums)
    {
        const float tt  = (lb.db + 20.f) / 23.f;
        const float ang = (-57.5f + tt * 115.f) * juce::MathConstants<float>::pi / 180.f;
        const float nr  = scaleR - 19.f;
        const float nx  = pivX + std::sin(ang) * nr;
        const float ny  = pivY - std::cos(ang) * nr;
        g.setColour(lb.red ? juce::Colour(0xffcc3300) : juce::Colour(0xff1a1410));
        g.drawText(lb.txt, juce::Rectangle<float>(nx-11, ny-5, 22, 10), juce::Justification::centred, false);
    }

    const float nLen = scaleR * 0.93f;
    const float nx2  = pivX + std::sin(needleA) * nLen;
    const float ny2  = pivY - std::cos(needleA) * nLen;
    g.setColour(juce::Colour(0x40000000));
    g.drawLine(pivX+0.7f, pivY+0.7f, nx2+0.7f, ny2+0.7f, 1.8f);
    juce::Path needle;
    const float perpX = std::cos(needleA) * 1.1f;
    const float perpY = std::sin(needleA) * 1.1f;
    needle.startNewSubPath(pivX - perpX, pivY - perpY);
    needle.lineTo(pivX + perpX, pivY + perpY);
    needle.lineTo(nx2, ny2);
    needle.closeSubPath();
    g.setColour(juce::Colour(0xff151210));
    g.fillPath(needle);
    g.setColour(juce::Colour(0xff252220));
    g.fillEllipse(pivX-4.f, pivY-4.f, 8.f, 8.f);
    g.setColour(juce::Colour(0xff504840));
    g.fillEllipse(pivX-2.5f, pivY-2.5f, 5.f, 5.f);
}

// ── paintOverChildren — knob labels, button labels ────────────────────────────
void BadDadVerbAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Draw glow image first, at raw screen coords (no transform active yet)
    drawGlowLayer(g);

    const float W = Layout::W, H = Layout::H;
    const float scaleX = float(getWidth()) / W;
    const float scaleY = 420.f / H;
    g.addTransform(juce::AffineTransform::scale(scaleX, scaleY));

    const auto amber    = juce::Colour(0xffe3a34a);
    const auto amberDim = juce::Colour(0xffb8732d);

    // Button/knob labels drawn in authored space
    g.setFont(cyrillicFont(11.0f, true));
    struct BL { int ax, ay; const char* txt; };
    const BL btnLabels[] = {
        { 871, 189, u8"РSYОР" },
        { 934, 189, u8"ОVЗЯLОАD" },
        { 871, 246, u8"ЯЗV"  },
        { 934, 246, u8"МIНD РUSH" },
    };
    for (auto& lb : btnLabels)
    {
        g.setColour(juce::Colour(0xff000000).withAlpha(0.55f));
        g.drawText(juce::String::fromUTF8(lb.txt), lb.ax-36, lb.ay+14+1, 72, 14, juce::Justification::centred, false);
        g.setColour(amber.withAlpha(0.88f));
        g.drawText(juce::String::fromUTF8(lb.txt), lb.ax-36, lb.ay+14, 72, 14, juce::Justification::centred, false);
    }
    // Shadow overlays when psyOp is active — REV button + STЯТ/ЗНD knobs
    if (audioProcessor.isPsyOp())
    {
        const float scx = float(getWidth()) / Layout::W;
        const float scy = 420.f / Layout::H;

        // Helper: draw disabled shadow over a component (in authored coords)
        auto drawDisabledShadow = [&](juce::Component& comp, bool isKnob)
        {
            const auto  cb = comp.getBounds();
            const float ax = float(cb.getX())      / scx;
            const float ay = float(cb.getY())      / scy;
            const float aw = float(cb.getWidth())  / scx;
            const float ah = float(cb.getHeight()) / scy;
            const float cx = ax + aw * 0.5f;
            const float cy = ay + ah * 0.5f;

            if (isKnob)
            {
                // Circular shadow for knobs
                const float r = juce::jmin(aw, ah) * 0.5f + 3.f;
                g.setColour(juce::Colour(0xcc000000));
                g.fillEllipse(cx - r, cy - r, r*2.f, r*2.f);
                // Hatch lines clipped to circle
                {
                    juce::Graphics::ScopedSaveState ss(g);
                    juce::Path clip; clip.addEllipse(cx-r, cy-r, r*2.f, r*2.f);
                    g.reduceClipRegion(clip);
                    g.setColour(juce::Colour(0x44ff0000));
                    for (float off = -r*2.f; off < r*2.f; off += 7.f)
                        g.drawLine(cx + off, cy - r, cx + off + r*2.f, cy + r, 1.0f);
                }
                g.setFont(cyrillicFont(7.0f));
                g.setColour(juce::Colour(0xffff4020).withAlpha(0.85f));
                g.drawText(u8"РSYОР", (int)(cx - 20.f), (int)(cy - 5.f),
                           40, 10, juce::Justification::centred, false);
            }
            else
            {
                // Rounded rect shadow for buttons
                g.setColour(juce::Colour(0xcc000000));
                g.fillRoundedRectangle(ax-2.f, ay-2.f, aw+4.f, ah+4.f, 6.f);
                g.setColour(juce::Colour(0x44ff0000));
                for (float off = -ah; off < aw+ah; off += 8.f)
                    g.drawLine(ax+off, ay, ax+off+ah, ay+ah, 1.0f);
                g.setFont(cyrillicFont(8.0f));
                g.setColour(juce::Colour(0xffff4020).withAlpha(0.80f));
                g.drawText(u8"РSYОР", (int)ax, (int)(ay+ah*0.35f),
                           (int)aw, 12, juce::Justification::centred, false);
            }
        };

        drawDisabledShadow(reverseButton,  false);  // button — rect shadow
        drawDisabledShadow(trimStartKnob,  true);   // knob — circle shadow
        drawDisabledShadow(trimEndKnob,    true);   // knob — circle shadow
    }

    // Knob labels — must match resized() layout: K0=24, KGAP=88, KW=80
    g.setFont(cyrillicFont(11.0f, true));
    {
        struct KL { float cx; const char* txt; };
        constexpr float K0 = 24.f, KGAP = 88.f, KKW = 80.f;
        const KL kLabels[] = {
            { K0 + KGAP*0  + KKW*0.5f, u8"IН"     },
            { K0 + KGAP*1  + KKW*0.5f, u8"МIХ"    },
            { K0 + KGAP*2  + KKW*0.5f, u8"РЯЗDЗL" },
            { K0 + KGAP*3  + KKW*0.5f, u8"ТIМЗ"   },
            { K0 + KGAP*4  + KKW*0.5f, u8"НРF"    },
            { K0 + KGAP*5  + KKW*0.5f, u8"FЯЗQ"   },
            { K0 + KGAP*6  + KKW*0.5f, u8"ЯЗS"    },
            { K0 + KGAP*7  + KKW*0.5f, u8"ОUТ"    },
            { K0 + KGAP*8  + KKW*0.5f, u8"LIМIТ"  },
            { K0 + KGAP*9  + KKW*0.5f, u8"SТЯТ"   },
            { K0 + KGAP*10 + KKW*0.5f, u8"ЗНD"    },
        };
        const float labelY = Layout::KY + Layout::KH + 6.f;
        for (auto& kl : kLabels)
        {
            g.setColour(juce::Colour(0xff000000).withAlpha(0.55f));
            g.drawText(juce::String::fromUTF8(kl.txt), (int)kl.cx-50, (int)labelY+1, 100, 14, juce::Justification::centred, false);
            g.setColour(amber.withAlpha(0.85f));
            g.drawText(juce::String::fromUTF8(kl.txt), (int)kl.cx-50, (int)labelY,   100, 14, juce::Justification::centred, false);
        }

        // Separator between LIMIT (right=808) and STRT (left=816): draw at x=812
        g.setColour(juce::Colour(0xff6a4020).withAlpha(0.6f));
        g.fillRect(812.f, Layout::KY - 10.f, 1.5f, Layout::KH + 30.f);
    }

    // ── File drag-over highlight ──────────────────────────────────────────────
    if (fileDragOver)
    {
        const float r  = Layout::DISP_D * 0.5f;
        const float cx = Layout::CRT_CX;
        const float cy = Layout::CRT_CY;

        // Pulsing amber ring around the CRT scope
        g.setColour(amber.withAlpha(0.90f));
        g.drawEllipse(cx - r - 4.f, cy - r - 4.f, (r + 4.f)*2.f, (r + 4.f)*2.f, 3.f);
        g.setColour(amber.withAlpha(0.30f));
        g.drawEllipse(cx - r - 9.f, cy - r - 9.f, (r + 9.f)*2.f, (r + 9.f)*2.f, 2.f);

        // Semi-transparent amber fill over the scope face
        g.setColour(juce::Colour(0x44e8a030));
        juce::Path face;
        face.addEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
        g.fillPath(face);

        // Instruction text centred on scope
        g.setFont(dadbass::getPluginFont(14.0f, true));
        g.setColour(juce::Colour(0xff000000).withAlpha(0.65f));
        g.drawText("DROP AUDIO FILE", (int)(cx - 90), (int)(cy - 9), 180, 18,
                   juce::Justification::centred, false);
        g.setColour(amber);
        g.drawText("DROP AUDIO FILE", (int)(cx - 90), (int)(cy - 10), 180, 18,
                   juce::Justification::centred, false);
    }

    (void)amberDim;
    (void)H;
}

// ── File drag-and-drop ────────────────────────────────────────────────────────
// The user can drag any audio file (WAV, AIFF, MP3, FLAC…) onto the plugin
// window. The file is decoded to mono PCM and fed into AudioToIR::convert()
// via loadIRFromMemory(), which runs on the background conversion thread.

static bool isAudioFile(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    for (auto* e : { ".wav", ".aif", ".aiff", ".mp3", ".flac",
                     ".ogg", ".m4a", ".caf", ".wv", ".w64" })
        if (ext == e) return true;
    return false;
}

bool BadDadVerbAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile(f)) return true;
    return false;
}

void BadDadVerbAudioProcessorEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    fileDragOver = true;
    repaint();
}

void BadDadVerbAudioProcessorEditor::fileDragExit(const juce::StringArray&)
{
    fileDragOver = false;
    repaint();
}

void BadDadVerbAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    repaint();

    // Use the first valid audio file
    for (const auto& path : files)
    {
        if (!isAudioFile(path)) continue;

        const juce::File f(path);
        if (!f.existsAsFile()) continue;

        // Decode audio file → mono PCM → hand to processor.
        // AudioFormatManager handles WAV, AIFF, MP3, FLAC etc.
        juce::AudioFormatManager formatMgr;
        formatMgr.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatMgr.createReaderFor(f));

        if (!reader) continue;

        const int n  = (int) reader->lengthInSamples;
        const int ch = (int) reader->numChannels;
        if (n <= 0) continue;

        juce::AudioBuffer<float> buf(ch, n);
        reader->read(&buf, 0, n, 0, true, ch >= 2);

        // Mix to mono
        juce::AudioBuffer<float> mono(1, n);
        if (ch >= 2)
            for (int i = 0; i < n; ++i)
                mono.setSample(0, i, (buf.getSample(0,i) + buf.getSample(1,i)) * 0.5f);
        else
            mono.copyFrom(0, 0, buf, 0, 0, n);

        // Hand off to processor — non-blocking, background thread does conversion
        audioProcessor.loadSourceAudioBuffer(mono);
        break;  // only load the first valid file
    }
}

// ── resized ───────────────────────────────────────────────────────────────────
void BadDadVerbAudioProcessorEditor::resized()
{
    const float sx = float(getWidth()) / Layout::W;
    const float sy = 420.f / Layout::H;
    auto sc = [sx, sy](float x, float y, float w, float h) {
        return juce::Rectangle<int>((int)std::round(x*sx), (int)std::round(y*sy),
                                    (int)std::round(w*sx), (int)std::round(h*sy));
    };

    // ── Scopes ───────────────────────────────────────────────────────────────
    // Three symmetric circles: D=200, cx=160/420/680, cy=220 (all authored)
    scope      .setBounds(sc(Layout::SCOPE_X, Layout::SCOPE_Y, Layout::SCOPE_W, Layout::SCOPE_H));
    sampleScope.setBounds(sc(Layout::SMP_X,   Layout::SMP_Y,   Layout::SMP_W,   Layout::SMP_H));

    // ── Right panel: PSYOP/OVERLOAD (cy=189)  REV/MIND PUSH (cy=246) ──────────
    // Labels at authored cx=871 (left) and cx=934 (right).
    recordButton   .setBounds(sc(841, 164, 60, 50));   // cx=871, cy=189
    stopButton     .setBounds(sc(904, 164, 60, 50));   // cx=934, cy=189
    reverseButton  .setBounds(sc(841, 221, 60, 50));   // cx=871, cy=246
    normalizeButton.setBounds(sc(904, 221, 60, 50));   // cx=934, cy=246

    // ── Knob row ─────────────────────────────────────────────────────────────
    // 11 knobs: IN  MIX  WET  TIME  HPF  FREQ  RES  OUT  LIMIT | STRT  END
    // gap=88 starting at x=24: last right edge = 24+88*10+80 = 984 < 1040 ✓
    // Separator painted at x≈812 (between LIMIT right-edge and STRT left-edge).
    constexpr float K0   = 24.f;
    constexpr float KGAP = 88.f;
    constexpr float KKW  = 80.f;
    constexpr float KKH  = 80.f;
    constexpr float KKY  = Layout::KY;

    inputKnob    .setBounds(sc(K0 + KGAP*0,  KKY, KKW, KKH));
    mixKnob      .setBounds(sc(K0 + KGAP*1,  KKY, KKW, KKH));
    wetKnob      .setBounds(sc(K0 + KGAP*2,  KKY, KKW, KKH));
    timeKnob     .setBounds(sc(K0 + KGAP*3,  KKY, KKW, KKH));
    hpfKnob      .setBounds(sc(K0 + KGAP*4,  KKY, KKW, KKH));
    freqKnob     .setBounds(sc(K0 + KGAP*5,  KKY, KKW, KKH));
    resKnob      .setBounds(sc(K0 + KGAP*6,  KKY, KKW, KKH));
    outputKnob   .setBounds(sc(K0 + KGAP*7,  KKY, KKW, KKH));
    limitKnob    .setBounds(sc(K0 + KGAP*8,  KKY, KKW, KKH));
    trimStartKnob.setBounds(sc(K0 + KGAP*9,  KKY, KKW, KKH));
    trimEndKnob  .setBounds(sc(K0 + KGAP*10, KKY, KKW, KKH));

    // Preset bar at bottom
    presetBar.setBounds(2, getHeight() - 22, getWidth() - 4, 20);
}
