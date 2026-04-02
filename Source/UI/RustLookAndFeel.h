#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../DSP/DSPHelpers.h"
#include <BinaryData.h>
#include <cmath>

namespace dadbass
{

// ── Embedded font with Cyrillic support ──────────────────────────────────────
// Returns a typeface loaded from the embedded DejaVu Sans Bold binary data.
// Call from any paint() method instead of relying on a system font.
inline juce::Font getPluginFont (float size, bool bold = true)
{
    static juce::Typeface::Ptr boldFace = []() -> juce::Typeface::Ptr {
        return juce::Typeface::createSystemTypefaceFor (
            DadBassAssets::DejaVuSansBold_ttf,
            DadBassAssets::DejaVuSansBold_ttfSize);
    }();
    static juce::Typeface::Ptr regularFace = []() -> juce::Typeface::Ptr {
        return juce::Typeface::createSystemTypefaceFor (
            DadBassAssets::DejaVuSans_ttf,
            DadBassAssets::DejaVuSans_ttfSize);
    }();
    auto* face = bold ? boldFace.get() : regularFace.get();
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withHeight (size));
    return juce::Font (juce::FontOptions (size));  // fallback
}

namespace Col
{
    static const juce::Colour chassis   { 0xff1a100a };
    static const juce::Colour panel     { 0xff221510 };
    static const juce::Colour amber     { 0xffe8a030 };  // warm gold — pops on brown
    static const juce::Colour amberDim  { 0xff8a5c18 };  // muted gold
    static const juce::Colour bakelite  { 0xff1e1410 };  // dark brown knob body
    static const juce::Colour bakeliteH { 0xff342418 };  // lighter brown
    static const juce::Colour crtGreen  { 0xffb8d44a };
    static const juce::Colour crtGlow   { 0xff6aaa10 };
    static const juce::Colour rivet     { 0xff6a5030 };  // brass-brown rivet
    static const juce::Colour border    { 0xff4a3020 };  // warm brown border
    static const juce::Colour tick      { 0xffc89030 };  // warm gold tick
    static const juce::Colour ledRed    { 0xffff2200 };
    static const juce::Colour ledBlue   { 0xff2255ff };
    // Battle damage colours
    static const juce::Colour rustDeep  { 0xff6b2a0a }; // cracked brown-red
    static const juce::Colour rustMid   { 0xff9a4818 }; // mid warm brown-rust
    static const juce::Colour rustLight { 0xffc07030 }; // light rust bloom
    static const juce::Colour bareSteel { 0xff706050 }; // warm scratch-through
    static const juce::Colour oliveDrab { 0xff3a2418 }; // warm brown surface
    static const juce::Colour paintChip { 0xff2a1810 }; // lifted paint edge
}

class SovietLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Font getComboBoxFont   (juce::ComboBox&)                           override { return dadbass::getPluginFont(12.f, false); }
    juce::Font getLabelFont      (juce::Label&)                              override { return dadbass::getPluginFont(12.f, false); }
    juce::Font getTextButtonFont (juce::TextButton&, int h)                  override { return dadbass::getPluginFont((float)h * 0.55f, true); }
    juce::Font getPopupMenuFont  ()                                          override { return dadbass::getPluginFont(13.f, false); }
    SovietLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId,        Col::amber);
        setColour(juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId,  juce::Colours::transparentBlack);
        setColour(juce::ToggleButton::textColourId,         Col::amber);
        setColour(juce::Label::textColourId,                Col::amber);
    }

    // ── Rotary knob — Soviet instrument style, flat matte black body,
    //    sharp triangular fin pointer, flat-head centre screw,
    //    printed numbers 0-10 on panel arc. Reference: УСИЛЕНИe-type knob.
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int w, int h,
                          float sliderPos,
                          float startAngle, float endAngle,
                          juce::Slider& s) override
    {
        const float cx     = x + w * 0.5f;
        const float cy     = y + h * 0.5f;
        const float outerR = juce::jmin(w, h) * 0.46f;
        const float knobR  = outerR * 0.84f;
        const float angle  = startAngle + sliderPos * (endAngle - startAngle);
        const int   seed   = int(reinterpret_cast<uintptr_t>(&s) & 0xffff);

        // ── 1. Drop shadow ────────────────────────────────────────────────
        {
            juce::ColourGradient sh(
                juce::Colour(0x60000000), cx + 2.0f, cy + 3.0f,
                juce::Colour(0x00000000), cx + knobR*1.5f, cy + knobR*1.6f, true);
            g.setGradientFill(sh);
            g.fillEllipse(cx - knobR*1.1f, cy - knobR*0.9f, knobR*2.4f, knobR*2.5f);
        }

        // ── 2. Tick marks and numbers on panel arc ────────────────────────
        // Minor ticks between each number position
        for (int i = 0; i <= 50; ++i)
        {
            const float t   = float(i) / 50.0f;
            const float ang = startAngle + t * (endAngle - startAngle);
            const bool  maj = (i % 5 == 0);   // major = number position
            const bool  mid = (i % 5 == 0);   // same — only maj and minor
            const float r1  = outerR + 1.5f;
            const float r2  = outerR + (maj ? 9.0f : 4.5f);
            const float thick = maj ? 1.4f : 0.8f;
            // Slight wear: skip ~12% of minor ticks randomly
            if (!maj && hashf(seed + i * 17) < 0.12f) continue;
            const float worn = maj ? (0.6f + hashf(seed + i*3)*0.35f)
                                   : (0.3f + hashf(seed + i*7)*0.45f);
            g.setColour(Col::amber.withAlpha(worn));
            g.drawLine(cx + std::sin(ang)*r1, cy - std::cos(ang)*r1,
                       cx + std::sin(ang)*r2, cy - std::cos(ang)*r2,
                       thick);
            (void)mid;
        }
        // Numbers 0–10 printed at major positions
        {
            g.setFont(dadbass::getPluginFont(outerR * 0.30f, true));
            const float numR = outerR + 16.0f;
            for (int i = 0; i <= 10; ++i)
            {
                const float t   = float(i) / 10.0f;
                const float ang = startAngle + t * (endAngle - startAngle);
                const float nx  = cx + std::sin(ang) * numR;
                const float ny  = cy - std::cos(ang) * numR;
                const float nr  = outerR * 0.18f;  // half-size of number box
                const float worn = 0.45f + hashf(seed + i*11) * 0.50f;
                // Shadow
                g.setColour(juce::Colour(0xff000000).withAlpha(0.6f));
                g.drawText(juce::String(i),
                           juce::Rectangle<float>(nx-nr+0.7f, ny-nr+0.8f, nr*2, nr*2),
                           juce::Justification::centred, false);
                // Number
                g.setColour(Col::amber.withAlpha(worn));
                g.drawText(juce::String(i),
                           juce::Rectangle<float>(nx-nr, ny-nr, nr*2, nr*2),
                           juce::Justification::centred, false);
            }
        }

        // ── 3. Knob body — flat matte cylinder, very dark ─────────────────
        // Base fill: nearly black with subtle top-lit gradient (not spherical)
        {
            juce::ColourGradient base(
                juce::Colour(0xff201610), cx - knobR*0.2f, cy - knobR*0.5f,
                juce::Colour(0xff0e0908), cx + knobR*0.3f, cy + knobR*0.5f, false);
            g.setGradientFill(base);
            g.fillEllipse(cx - knobR, cy - knobR, knobR*2, knobR*2);
        }
        // Subtle rim shadow — cylindrical edge darkening
        {
            juce::ColourGradient rim(
                juce::Colour(0x00000000), cx, cy,
                juce::Colour(0x90000000), cx + knobR, cy + knobR, true);
            g.setGradientFill(rim);
            g.fillEllipse(cx - knobR, cy - knobR, knobR*2, knobR*2);
        }
        // Very faint top-edge highlight — matte surface, not shiny
        {
            const float hx = cx - knobR*0.15f, hy = cy - knobR*0.55f;
            juce::ColourGradient spec(
                juce::Colour(0xffffffff).withAlpha(0.06f), hx, hy,
                juce::Colour(0x00000000), hx + knobR*0.7f, hy + knobR*0.6f, true);
            g.setGradientFill(spec);
            g.fillEllipse(cx - knobR, cy - knobR, knobR*2, knobR*2);
        }
        // Knob border — thin dark rim with faint upper highlight
        g.setColour(juce::Colour(0xff000000));
        g.drawEllipse(cx - knobR + 0.5f, cy - knobR + 0.5f,
                      knobR*2 - 1.0f, knobR*2 - 1.0f, 1.2f);
        {
            juce::Path arc;
            arc.addArc(cx-knobR+1.f, cy-knobR+1.f, knobR*2-2.f, knobR*2-2.f,
                       -2.0f, -0.8f, true);
            g.setColour(juce::Colour(0xff383830).withAlpha(0.6f));
            g.strokePath(arc, juce::PathStrokeType(0.8f));
        }

        // ── Is this the output knob? (flagged via componentID) ──────────────
        const bool isOutput = (s.getComponentID() == "output");

        // ── Output knob: darker red-brown base tint ────────────────────────
        if (isOutput)
        {
            juce::ColourGradient redTint(
                juce::Colour(0x55660808), cx, cy,
                juce::Colour(0x00000000), cx + knobR, cy + knobR, true);
            g.setGradientFill(redTint);
            g.fillEllipse(cx - knobR, cy - knobR, knobR*2, knobR*2);
        }

        // ── 4. Surface wear — heavy damage: deep wear rings, gouges, chips ─
        // Concentric wear rings — two, heavy
        g.setColour(juce::Colour(0xff000000).withAlpha(0.55f));
        g.drawEllipse(cx - knobR*0.72f, cy - knobR*0.72f,
                      knobR*1.44f, knobR*1.44f, 1.1f);
        g.setColour(juce::Colour(0xff000000).withAlpha(0.30f));
        g.drawEllipse(cx - knobR*0.52f, cy - knobR*0.52f,
                      knobR*1.04f, knobR*1.04f, 0.6f);

        // Deep radial gouges — 6-8 per knob, some reaching near the edge
        {
            const int nScratches = isOutput ? 9 : 6;
            for (int i = 0; i < nScratches; ++i)
            {
                const float ang = hashf(seed + i*13) * juce::MathConstants<float>::twoPi;
                const float r0  = knobR * (0.05f + hashf(seed+i*7+1)*0.10f);
                const float r1  = knobR * (0.45f + hashf(seed+i*7+2)*0.50f);
                const float dep = 0.25f + hashf(seed+i*3)*0.40f;
                // Shadow side of gouge
                g.setColour(juce::Colour(0xff000000).withAlpha(dep * 0.8f));
                g.drawLine(cx + std::cos(ang)*r0 + 0.5f, cy + std::sin(ang)*r0 + 0.5f,
                           cx + std::cos(ang)*r1 + 0.5f, cy + std::sin(ang)*r1 + 0.5f,
                           0.9f);
                // Bright edge of gouge (exposed lighter material)
                g.setColour(juce::Colour(0xff503828).withAlpha(dep * 0.55f));
                g.drawLine(cx + std::cos(ang)*r0, cy + std::sin(ang)*r0,
                           cx + std::cos(ang)*r1, cy + std::sin(ang)*r1,
                           0.5f);
            }
        }

        // Chipped-out bits — dark craters in the surface
        {
            const int nChips = isOutput ? 5 : 3;
            for (int i = 0; i < nChips; ++i)
            {
                const float ca  = hashf(seed + i*31 + 5) * juce::MathConstants<float>::twoPi;
                const float cr  = knobR * (0.30f + hashf(seed+i*17+3) * 0.55f);
                const float csz = knobR * (0.06f + hashf(seed+i*11+7) * (isOutput ? 0.12f : 0.08f));
                const float chipX = cx + std::cos(ca)*cr;
                const float chipY = cy + std::sin(ca)*cr;
                // Chip crater — dark pit
                g.setColour(juce::Colour(0xff000000).withAlpha(0.85f));
                g.fillEllipse(chipX - csz, chipY - csz, csz*2, csz*2);
                // Bright rim — exposed material at chip edge (upper-left catch)
                g.setColour(juce::Colour(isOutput ? 0xff603028u : 0xff503828u)
                              .withAlpha(0.70f));
                g.drawEllipse(chipX - csz, chipY - csz, csz*2, csz*2, 0.7f);
                // Tiny ejecta dot beside chip
                g.setColour(juce::Colour(0xff000000).withAlpha(0.50f));
                const float ex = chipX + std::cos(ca + 0.4f) * csz * 1.8f;
                const float ey = chipY + std::sin(ca + 0.4f) * csz * 1.8f;
                g.fillEllipse(ex - csz*0.4f, ey - csz*0.4f, csz*0.8f, csz*0.8f);
            }
        }

        // Cracks — stress fractures radiating from centre and rim
        {
            const int nCracks = isOutput ? 4 : 2;
            for (int i = 0; i < nCracks; ++i)
            {
                const float ca   = hashf(seed + i*43 + 9) * juce::MathConstants<float>::twoPi;
                const float cLen = knobR * (0.40f + hashf(seed+i*23+2) * 0.50f);
                const float cOff = knobR * (0.05f + hashf(seed+i*17+1) * 0.20f);
                const float sx   = cx + std::cos(ca)*cOff;
                const float sy   = cy + std::sin(ca)*cOff;
                const float ex2  = cx + std::cos(ca)*cLen;
                const float ey2  = cy + std::sin(ca)*cLen;
                // Shadow side of crack
                g.setColour(juce::Colour(0xff000000).withAlpha(0.85f));
                g.drawLine(sx+0.6f, sy+0.6f, ex2+0.6f, ey2+0.6f, 1.5f);
                // Crack itself
                g.setColour(juce::Colour(0xff000000));
                g.drawLine(sx, sy, ex2, ey2, 0.8f);
                // Lit edge
                g.setColour(juce::Colour(isOutput ? 0xff5c2020u : 0xff483020u)
                              .withAlpha(0.45f));
                g.drawLine(sx-0.5f, sy-0.5f, ex2-0.5f, ey2-0.5f, 0.5f);
                // Branch crack
                const float ba   = ca + 0.28f + hashf(seed+i*7+4)*0.35f;
                const float bLen = knobR * (0.10f + hashf(seed+i*13+6)*0.20f);
                g.setColour(juce::Colour(0xff000000).withAlpha(0.65f));
                g.drawLine(ex2, ey2,
                           ex2 + std::cos(ba)*bLen, ey2 + std::sin(ba)*bLen, 0.6f);
            }
        }

        // Worn thumb spot — output knob has much heavier wear
        {
            const float wx = cx + (hashf(seed+20)-0.5f)*knobR*(isOutput ? 0.15f : 0.30f);
            const float wy = cy + (hashf(seed+21)-0.5f)*knobR*(isOutput ? 0.15f : 0.30f);
            const float wSize = isOutput ? 0.50f : 0.40f;
            juce::ColourGradient wp(
                juce::Colour(isOutput ? 0xff3a1a10u : 0xff282820u).withAlpha(0.55f), wx, wy,
                juce::Colour(0x00000000), wx + knobR*wSize, wy + knobR*wSize*0.75f, true);
            g.setGradientFill(wp);
            g.fillEllipse(wx - knobR*wSize*0.5f, wy - knobR*wSize*0.38f,
                          knobR*wSize, knobR*wSize*0.75f);
        }

        // Output knob: additional red rust bleed and heat-damaged discolouration
        if (isOutput)
        {
            // Heat stain — dark reddish-brown bloom off-centre
            juce::ColourGradient heat(
                juce::Colour(0x40781818), cx + knobR*0.15f, cy + knobR*0.20f,
                juce::Colour(0x00000000), cx + knobR, cy + knobR, true);
            g.setGradientFill(heat);
            g.fillEllipse(cx - knobR, cy - knobR, knobR*2, knobR*2);
            // Bright red rim fleck
            g.setColour(juce::Colour(0xff6a1808).withAlpha(0.50f));
            g.fillEllipse(cx + knobR*0.45f, cy + knobR*0.38f, knobR*0.22f, knobR*0.16f);
        }

        // ── 5. Triangular fin pointer — the key feature ───────────────────
        // A raised blade: wide at base (near centre), tapers to sharp point at rim.
        // Drawn as a filled polygon with lighting and shadow sides.
        {
            const float sinA = std::sin(angle);
            const float cosA = std::cos(angle);
            // Perpendicular direction (for fin width)
            const float perpX =  cosA;
            const float perpY =  sinA;

            const float tipR  = knobR * 0.91f;   // tip reaches near knob edge
            const float baseR2= knobR * 0.18f;   // base of fin (near centre)
            const float halfW = knobR * 0.09f;   // half-width at base

            // Tip point
            const float tipX = cx + sinA * tipR;
            const float tipY = cy - cosA * tipR;
            // Base left and right points
            const float blX  = cx + sinA * baseR2 - perpX * halfW;
            const float blY  = cy - cosA * baseR2 - perpY * halfW;
            const float brX  = cx + sinA * baseR2 + perpX * halfW;
            const float brY  = cy - cosA * baseR2 + perpY * halfW;

            // ── Shadow under fin (offset slightly) ─────────────────────
            juce::Path shadow;
            shadow.startNewSubPath(tipX + 1.2f, tipY + 1.2f);
            shadow.lineTo(blX  + 1.2f, blY  + 1.2f);
            shadow.lineTo(brX  + 1.2f, brY  + 1.2f);
            shadow.closeSubPath();
            g.setColour(juce::Colour(0xff000000).withAlpha(0.70f));
            g.fillPath(shadow);

            // ── Fin body — dark, slightly lit ──────────────────────────
            juce::Path fin;
            fin.startNewSubPath(tipX, tipY);
            fin.lineTo(blX, blY);
            fin.lineTo(brX, brY);
            fin.closeSubPath();

            // Dark fill
            g.setColour(juce::Colour(0xff101010));
            g.fillPath(fin);

            // Lit edge — the left (upstream) face catches top-left light
            // Light factor: how much the left edge faces the light source
            const float litFactor = juce::jlimit(0.f, 1.f,
                (-sinA - cosA) * 0.5f + 0.5f);
            // Left edge of fin (tip→blX): bright metallic
            g.setColour(juce::Colour(0xffc8b870).withAlpha(0.20f + litFactor * 0.55f));
            g.drawLine(tipX, tipY, blX, blY, 1.2f);
            // Right edge: darker
            g.setColour(juce::Colour(0xff303020).withAlpha(0.5f));
            g.drawLine(tipX, tipY, brX, brY, 0.8f);
            // Base: thin slot-like dark line
            g.setColour(juce::Colour(0xff000000).withAlpha(0.8f));
            g.drawLine(blX, blY, brX, brY, 1.0f);

            // Tip glint — tiny bright pixel at the very point
            g.setColour(juce::Colour(0xffd8c888).withAlpha(0.50f + litFactor*0.35f));
            g.fillEllipse(tipX - 1.2f, tipY - 1.2f, 2.4f, 2.4f);
        }

        // ── 6. Centre flat-head screw — large, clearly visible ────────────
        {
            const float sr2  = knobR * 0.155f;  // radius scales with knob size
            // Dome body
            {
                juce::ColourGradient dome(
                    juce::Colour(0xff484038), cx - sr2*0.4f, cy - sr2*0.45f,
                    juce::Colour(0xff141210), cx + sr2*0.5f, cy + sr2*0.55f, false);
                g.setGradientFill(dome);
                g.fillEllipse(cx - sr2, cy - sr2, sr2*2, sr2*2);
            }
            // Rust accumulation around lower half
            g.setColour(Col::rustMid.withAlpha(0.45f));
            g.fillEllipse(cx - sr2*0.55f, cy + sr2*0.0f, sr2*1.1f, sr2*0.9f);
            g.setColour(Col::rustDeep.withAlpha(0.3f));
            g.fillEllipse(cx - sr2*0.3f, cy + sr2*0.2f, sr2*0.6f, sr2*0.6f);

            // Flat-head slot — prominent, slightly off-horizontal (worn/turned)
            // Slot angle varies per-knob (deterministic from seed)
            const float slotA = (hashf(seed+30) - 0.5f) * 0.55f;
            const float scx   = cx + (hashf(seed+31) - 0.5f) * sr2 * 0.25f; // slight off-centre
            const float scy2  = cy + (hashf(seed+32) - 0.5f) * sr2 * 0.25f;
            g.setColour(juce::Colour(0xff060402));
            g.drawLine(scx - sr2*0.85f*std::cos(slotA), scy2 - sr2*0.85f*std::sin(slotA),
                       scx + sr2*0.85f*std::cos(slotA), scy2 + sr2*0.85f*std::sin(slotA),
                       2.2f);
            // Slot edge highlight
            g.setColour(juce::Colour(0xff383020).withAlpha(0.5f));
            g.drawLine(scx - sr2*0.80f*std::cos(slotA), scy2 - sr2*0.80f*std::sin(slotA) - 0.6f,
                       scx + sr2*0.80f*std::cos(slotA), scy2 + sr2*0.80f*std::sin(slotA) - 0.6f,
                       0.7f);
            // Screw rim
            g.setColour(juce::Colour(0xff000000).withAlpha(0.75f));
            g.drawEllipse(cx-sr2+0.5f, cy-sr2+0.5f, sr2*2-1.0f, sr2*2-1.0f, 1.0f);
            g.setColour(juce::Colour(0xff403020).withAlpha(0.45f));
            g.drawEllipse(cx-sr2+1.2f, cy-sr2+1.2f, sr2*2-2.4f, sr2*2-2.4f, 0.5f);
        }
    }

    // ── Toggle button — vintage Soviet Bakelite rocker switch ──────────────────
    // Round base, oval raised rocker paddle, two flat-head screws.
    // Paddle tilts: top-raised = OFF, top-down = ON (physical rocker motion).
    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& btn,
                          bool highlighted, bool /*down*/) override
    {
        const auto  b    = btn.getLocalBounds().toFloat();
        const bool  on   = btn.getToggleState();
        const float cx   = b.getCentreX();
        const float cy   = b.getCentreY();

        // Derive a stable per-button seed from its text for deterministic wear
        int seed = 0;
        for (auto c : btn.getButtonText()) seed = seed * 31 + (int)c;
        seed = std::abs(seed) & 0xffff;

        // ── Layout constants ──────────────────────────────────────────────────
        const float baseR   = b.getHeight() * 0.46f;   // circular base radius
        const float padW    = baseR * 0.55f;            // paddle half-width
        const float padH    = baseR * 0.80f;            // paddle half-height

        // ── 0. Detect button type ─────────────────────────────────────────────
        const bool isMindPush = btn.getButtonText().contains("MIND");
        const bool isOverload  = btn.getButtonText().contains("OVERLOAD") ||
                                  btn.getButtonText().contains("ОVЗЯ");
        const bool isRedType   = isMindPush || isOverload ||
                                  btn.getButtonText().contains("PSYOP") ||
                                  btn.getButtonText().contains("РSYОР");

        // ── Clipped glow helper — prevents radial gradient squaring ──────────
        auto clipGlow = [&](float r, juce::Colour centre, juce::Colour edge)
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path clip;
            clip.addEllipse(cx - r, cy - r, r*2.f, r*2.f);
            g.reduceClipRegion(clip);
            juce::ColourGradient grad(centre, cx, cy, edge, cx+r, cy, true);
            g.setGradientFill(grad);
            g.fillEllipse(cx-r, cy-r, r*2.f, r*2.f);
        };

        // ── 0. Glow when ON — multi-pass clipped so always perfectly circular
        if (on)
        {
            const juce::Colour gc1 = isRedType
                ? juce::Colour(0xffcc1030) : juce::Colour(0xff28e808);
            const juce::Colour gc2 = isRedType
                ? juce::Colour(0xffff3050) : juce::Colour(0xff60ff20);

            // Outermost — very faint ambient, tight radius
            clipGlow(baseR*2.2f, gc1.withAlpha(0.10f), juce::Colour(0x00000000));
            // Mid halo
            clipGlow(baseR*1.5f, gc1.withAlpha(0.25f), juce::Colour(0x00000000));
            // Inner bright ring just outside the base
            clipGlow(baseR*1.15f, gc2.withAlpha(0.50f), juce::Colour(0x00000000));
        }

        // ── 1. Deep drop shadow ────────────────────────────────────────────────
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path shadowClip;
            shadowClip.addEllipse(cx-baseR-2.f, cy-baseR+2.f, (baseR+2.f)*2.f, (baseR+2.f)*2.f);
            g.reduceClipRegion(shadowClip);
            juce::ColourGradient shadow(
                juce::Colour(0xaa000000), cx, cy+baseR*0.5f,
                juce::Colour(0x00000000), cx, cy-baseR, false);
            g.setGradientFill(shadow);
            g.fillEllipse(cx-baseR-2.f, cy-baseR+2.f, (baseR+2.f)*2.f, (baseR+2.f)*2.f);
        }

        // ── 2. Circular base — deep metal ring with bevelled edge ─────────────
        // Outer bevel ring — deep metal with strong light/shadow split
        {
            juce::ColourGradient bevel(
                juce::Colour(0xff4a3220), cx - baseR*0.55f, cy - baseR*0.65f,
                juce::Colour(0xff050302), cx + baseR*0.65f, cy + baseR*0.75f, false);
            bevel.addColour(0.5f, juce::Colour(0xff1a1008));
            g.setGradientFill(bevel);
            g.fillEllipse(cx - baseR, cy - baseR, baseR*2, baseR*2);
        }
        // Inner recess
        {
            const float iR = baseR * 0.88f;
            juce::ColourGradient recess(
                juce::Colour(0xff0c0806), cx - iR*0.3f, cy - iR*0.3f,
                juce::Colour(0xff181008), cx + iR*0.4f, cy + iR*0.4f, false);
            g.setGradientFill(recess);
            g.fillEllipse(cx - iR, cy - iR, iR*2, iR*2);
        }
        // Top-left specular catch
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path clip; clip.addEllipse(cx-baseR, cy-baseR, baseR*2, baseR*2);
            g.reduceClipRegion(clip);
            juce::ColourGradient spec(
                juce::Colour(0xffe8d0a0).withAlpha(0.18f), cx-baseR*0.5f, cy-baseR*0.6f,
                juce::Colour(0x00000000), cx, cy, false);
            g.setGradientFill(spec);
            g.fillEllipse(cx-baseR, cy-baseR, baseR*2, baseR*2);
        }
        // Outer rim line
        g.setColour(juce::Colour(0xff000000).withAlpha(0.9f));
        g.drawEllipse(cx-baseR+0.5f, cy-baseR+0.5f, baseR*2-1.f, baseR*2-1.f, 1.2f);
        // Inner rim — brighter when ON to suggest light reflection
        const float rimAlpha = on ? 0.75f : 0.45f;
        const juce::Colour rimCol = on && isRedType
            ? juce::Colour(0xff8a2030).withAlpha(rimAlpha)
            : on ? juce::Colour(0xff3a6818).withAlpha(rimAlpha)
                 : juce::Colour(0xff5a3c20).withAlpha(rimAlpha);
        g.setColour(rimCol);
        g.drawEllipse(cx-baseR+2.f, cy-baseR+2.f, baseR*2-4.f, baseR*2-4.f, on ? 1.2f : 0.8f);

        // Heavy surface scratches on base — radiating outward from centre
        for (int i = 0; i < 6; ++i)
        {
            const float ang = hashf(seed + i*13) * juce::MathConstants<float>::twoPi;
            const float r0  = baseR * (0.15f + hashf(seed + i*7 + 1) * 0.25f);
            const float r1  = baseR * (0.55f + hashf(seed + i*7 + 2) * 0.40f);
            g.setColour(juce::Colour(0xff000000).withAlpha(0.35f + hashf(seed+i*3)*0.3f));
            g.drawLine(cx + std::cos(ang)*r0, cy + std::sin(ang)*r0,
                       cx + std::cos(ang)*r1, cy + std::sin(ang)*r1,
                       0.6f + hashf(seed+i*11)*0.5f);
        }
        // Rust bloom near bottom-right of base
        g.setColour(Col::rustDeep.withAlpha(0.22f));
        g.fillEllipse(cx + baseR*0.25f, cy + baseR*0.30f, baseR*0.55f, baseR*0.45f);
        g.setColour(Col::rustMid.withAlpha(0.12f));
        g.fillEllipse(cx + baseR*0.35f, cy + baseR*0.38f, baseR*0.35f, baseR*0.28f);

        // ── 3. Two flat-head mounting screws (left and right of rocker) ───────
        const float screwY = cy;
        const float screwOff = baseR * 0.68f;
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = cx + side * screwOff;
            const float sr2 = baseR * 0.155f;
            const int   ss  = seed + side * 37;

            // Screw body — dome gradient
            {
                juce::ColourGradient dome(
                    juce::Colour(0xff504030), sx - sr2*0.4f, screwY - sr2*0.45f,
                    juce::Colour(0xff141008), sx + sr2*0.5f, screwY + sr2*0.55f, false);
                g.setGradientFill(dome);
                g.fillEllipse(sx - sr2, screwY - sr2, sr2*2, sr2*2);
            }
            // Rust on screw
            g.setColour(Col::rustMid.withAlpha(0.5f + hashf(ss+1)*0.3f));
            g.fillEllipse(sx - sr2*0.5f, screwY + sr2*0.1f, sr2*0.9f, sr2*0.7f);

            // Slot — flat-head, slightly off-horizontal, worn
            const float slotAngle = (hashf(ss+2) - 0.5f) * 0.5f; // ±0.25 rad tilt
            g.setColour(juce::Colour(0xff060402));
            g.drawLine(sx - sr2*0.82f + std::sin(slotAngle)*sr2*0.3f,
                       screwY - std::cos(slotAngle)*sr2*0.25f,
                       sx + sr2*0.82f - std::sin(slotAngle)*sr2*0.3f,
                       screwY + std::cos(slotAngle)*sr2*0.25f, 1.6f);

            // Slot highlight (worn edge)
            g.setColour(juce::Colour(0xff302010).withAlpha(0.4f));
            g.drawLine(sx - sr2*0.75f + std::sin(slotAngle)*sr2*0.3f,
                       screwY - std::cos(slotAngle)*sr2*0.25f - 0.5f,
                       sx + sr2*0.75f - std::sin(slotAngle)*sr2*0.3f,
                       screwY + std::cos(slotAngle)*sr2*0.25f - 0.5f, 0.6f);

            // Screw rim
            g.setColour(juce::Colour(0xff302010).withAlpha(0.55f));
            g.drawEllipse(sx-sr2+0.5f, screwY-sr2+0.5f, sr2*2-1.0f, sr2*2-1.0f, 0.7f);
        }

        // ── 3. LED indicator — small pill above paddle centre ────────────────
        {
            const float ledR = baseR * 0.12f;
            const float ledX = cx;
            const float ledY = cy - baseR * 0.52f;
            // LED housing
            g.setColour(juce::Colour(0xff0a0806));
            g.fillEllipse(ledX-ledR*1.3f, ledY-ledR*1.0f, ledR*2.6f, ledR*2.0f);
            if (on)
            {
                // Lit LED
                const juce::Colour ledCol = isRedType
                    ? juce::Colour(0xffff2030) : juce::Colour(0xff40ff10);
                juce::Graphics::ScopedSaveState ss3(g);
                juce::Path ledClip; ledClip.addEllipse(ledX-ledR, ledY-ledR*0.7f, ledR*2.f, ledR*1.4f);
                g.reduceClipRegion(ledClip);
                juce::ColourGradient ledGrad(ledCol, ledX, ledY-ledR*0.3f,
                                              ledCol.darker(0.5f), ledX, ledY+ledR*0.5f, false);
                g.setGradientFill(ledGrad);
                g.fillEllipse(ledX-ledR, ledY-ledR*0.7f, ledR*2.f, ledR*1.4f);
                // LED specular
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.fillEllipse(ledX-ledR*0.45f, ledY-ledR*0.55f, ledR*0.7f, ledR*0.4f);
            }
            else
            {
                // Unlit LED — dark glass
                g.setColour(juce::Colour(0xff1a1210));
                g.fillEllipse(ledX-ledR, ledY-ledR*0.7f, ledR*2.f, ledR*1.4f);
            }
        }

        // ── 4. Rocker cap — physical dome, tilts on state ────────────────────
        const float tiltAngle = on ? 0.30f : -0.30f;  // stronger tilt = more physical

        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::rotation(tiltAngle, cx, cy));

        // Cap recess groove
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path gr; gr.addRoundedRectangle(cx-padW*1.08f, cy-padH*0.85f,
                                                    padW*2.16f, padH*1.75f, padW*0.5f);
            g.reduceClipRegion(gr);
            g.setColour(juce::Colour(0xff000000).withAlpha(0.7f));
            g.fillRoundedRectangle(cx-padW*1.08f, cy-padH*0.85f, padW*2.16f, padH*1.75f, padW*0.5f);
        }

        // Cap body — rounded dome, light from top-left
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path capPath; capPath.addRoundedRectangle(cx-padW, cy-padH*0.80f,
                                                              padW*2, padH*1.65f, padW*0.42f);
            g.reduceClipRegion(capPath);

            // Base gradient — dark rubber/plastic with strong curvature
            juce::ColourGradient cap(
                juce::Colour(0xff3a2e22), cx-padW*0.3f, cy-padH*0.55f,
                juce::Colour(0xff0a0806), cx+padW*0.5f, cy+padH*0.4f, false);
            cap.addColour(0.65f, juce::Colour(0xff070504));
            g.setGradientFill(cap);
            g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);

            // Dome highlight — bright thin line at the raised edge
            juce::ColourGradient dome(
                juce::Colour(0xfffaf0d8).withAlpha(on ? 0.08f : 0.26f),
                cx-padW*0.25f, cy-padH*0.75f,
                juce::Colour(0x00000000), cx+padW*0.2f, cy-padH*0.1f, false);
            g.setGradientFill(dome);
            g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);

            // Side-light specular — thin bright line on left edge of cap
            {
                juce::Graphics::ScopedSaveState ss4(g);
                juce::Path sideClip;
                sideClip.addRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
                g.reduceClipRegion(sideClip);
                juce::ColourGradient sideLt(
                    juce::Colour(0xffe8d8b0).withAlpha(on ? 0.05f : 0.15f), cx-padW, cy,
                    juce::Colour(0x00000000), cx-padW*0.3f, cy, false);
                g.setGradientFill(sideLt);
                g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
            }

            // Pressed shadow (bottom heavy when ON)
            if (on)
            {
                juce::ColourGradient pressed(
                    juce::Colour(0x00000000), cx, cy-padH*0.3f,
                    juce::Colour(0x80000000), cx, cy+padH*0.5f, false);
                g.setGradientFill(pressed);
                g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
            }

            // Colour wash when ON
            if (on)
            {
                const juce::Colour wash = isRedType
                    ? juce::Colour(0xffcc1030).withAlpha(0.18f)
                    : juce::Colour(0xff30e808).withAlpha(0.12f);
                g.setColour(wash);
                g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
            }
        }

        // Cap border
        g.setColour(juce::Colour(0xff000000).withAlpha(0.85f));
        g.drawRoundedRectangle(cx-padW+0.5f, cy-padH*0.80f+0.5f,
                               padW*2-1.f, padH*1.65f-1.f, padW*0.42f, 1.0f);
        // Fine surface wear lines
        for (int i = 0; i < 3; ++i)
        {
            const float sy2 = cy-padH*0.7f + hashf(seed+40+i)*padH*1.2f;
            const float sx2 = cx-padW*0.55f + hashf(seed+50+i)*padW*1.1f;
            g.setColour(juce::Colour(0xff000000).withAlpha(0.15f+hashf(seed+60+i)*0.12f));
            g.drawLine(sx2, sy2, sx2+padW*(0.2f+hashf(seed+70+i)*0.4f), sy2+1.f, 0.5f);
        }

        // Hover
        if (highlighted)
        {
            juce::Graphics::ScopedSaveState ss3(g);
            juce::Path hp; hp.addRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
            g.reduceClipRegion(hp);
            g.setColour(juce::Colour(0xffe8c870).withAlpha(0.10f));
            g.fillRoundedRectangle(cx-padW, cy-padH*0.80f, padW*2, padH*1.65f, padW*0.42f);
        }
        // Labels drawn in PluginEditor::paint() in authored space (semi-Cyrillic style).
    } // end drawToggleButton

    void drawLabel(juce::Graphics& g, juce::Label& lbl) override
    {
        g.setFont(dadbass::getPluginFont(10.0f, true));
        g.setColour(Col::amber);
        g.drawText(lbl.getText(), lbl.getLocalBounds(), juce::Justification::centred, false);
    }
};

} // namespace dadbass
