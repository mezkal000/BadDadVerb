#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace dadbass
{

class Oscilloscope : public juce::Component
{
public:
    static constexpr int DisplaySize = 512;

    Oscilloscope() { displayBuf.fill(0.0f); glowBuf.fill(0.0f); }

    void snapshotFromRing(const std::array<float, DisplaySize>& ring, int writePos)
    {
        for (int i = 0; i < DisplaySize; ++i)
        {
            displayBuf[(size_t) i] = ring[(size_t) ((writePos + i) % DisplaySize)];
            auto& glow = glowBuf[(size_t) i];
            glow = glow * 0.55f + displayBuf[(size_t) i] * 0.45f;
        }
    }

    template <typename FloatType>
    void setStaticBuffer(const juce::AudioBuffer<FloatType>& buffer, int channel,
                         float trimStartNorm, float trimEndNorm)
    {
        channel = juce::jlimit(0, juce::jmax(0, buffer.getNumChannels() - 1), channel);
        const auto* src = buffer.getReadPointer(channel);
        const int numSamples = buffer.getNumSamples();

        if (numSamples <= 0)
        {
            displayBuf.fill(0.0f);
            glowBuf.fill(0.0f);
            startNorm = 0.0f;
            endNorm = 1.0f;
            return;
        }

        for (int i = 0; i < DisplaySize; ++i)
        {
            const int s0 = juce::jlimit(0, numSamples - 1, (int) ((double) i / DisplaySize * numSamples));
            const int s1 = juce::jmin(s0 + juce::jmax(1, numSamples / DisplaySize), numSamples);
            float peak = 0.0f;
            for (int s = s0; s < s1; ++s)
                peak = juce::jmax(peak, std::abs((float) src[s]));
            const float sign = (float) src[s0] >= 0.0f ? 1.0f : -1.0f;
            displayBuf[(size_t) i] = peak * sign;
            glowBuf[(size_t) i] = glowBuf[(size_t) i] * 0.45f + displayBuf[(size_t) i] * 0.55f;
        }

        startNorm = juce::jlimit(0.0f, 1.0f, trimStartNorm);
        endNorm = juce::jlimit(startNorm, 1.0f, trimEndNorm);
    }

    void setAggressiveMode(bool b) noexcept { aggressiveMode = b; }
    void setRecording(bool b) noexcept { recording = b; }
    void setTrimming(bool b) noexcept { showTrim = b; }

    void paint(juce::Graphics& g) override
    {
        const auto  b  = getLocalBounds().toFloat();
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 1.0f;

        g.setColour(juce::Colours::transparentBlack);
        g.fillRect(b);

        {
            juce::ColourGradient outerGlow(
                juce::Colour(0xffc87010).withAlpha(0.22f), cx, cy,
                juce::Colour(0x00000000), cx + r*1.6f, cy + r*1.6f, true);
            g.setGradientFill(outerGlow);
            g.fillEllipse(cx - r*1.6f, cy - r*1.6f, r*3.2f, r*3.2f);
        }

        {
            const float bw = r * 0.22f;
            g.setColour(juce::Colour(0x80000000));
            g.fillEllipse(cx-r-bw+2, cy-r-bw+3, (r+bw)*2, (r+bw)*2);
            juce::ColourGradient bezel(
                juce::Colour(0xff303030), cx-(r+bw)*0.4f, cy-(r+bw)*0.4f,
                juce::Colour(0xff0a0a0a), cx+(r+bw)*0.5f, cy+(r+bw)*0.5f, false);
            g.setGradientFill(bezel);
            g.fillEllipse(cx-r-bw, cy-r-bw, (r+bw)*2, (r+bw)*2);
            g.setColour(juce::Colour(0xff050505));
            g.drawEllipse(cx-r-1, cy-r-1, r*2+2, r*2+2, 3.0f);
            g.setColour(juce::Colour(0xff484848).withAlpha(0.5f));
            g.drawEllipse(cx-r-bw+1, cy-r-bw+1, (r+bw)*2-2, (r+bw)*2-2, 1.0f);
        }

        {
            const float sr2 = r * 0.13f;
            const float sd  = r + r * 0.22f * 0.55f;
            const float angles[] = {
                juce::MathConstants<float>::pi * 0.25f,
                juce::MathConstants<float>::pi * 0.75f,
                juce::MathConstants<float>::pi * 1.25f,
                juce::MathConstants<float>::pi * 1.75f
            };
            for (int si = 0; si < 4; ++si)
            {
                const float a  = angles[si];
                const float sx = cx + std::cos(a) * sd;
                const float sy = cy + std::sin(a) * sd;
                g.setColour(juce::Colour(0xff6b2808).withAlpha(0.6f));
                g.fillEllipse(sx-sr2*1.8f, sy-sr2*1.8f, sr2*3.6f, sr2*3.6f);
                juce::ColourGradient sg(juce::Colour(0xff484030), sx-sr2*0.4f, sy-sr2*0.4f,
                                        juce::Colour(0xff181410), sx+sr2*0.4f, sy+sr2*0.4f, false);
                g.setGradientFill(sg);
                g.fillEllipse(sx-sr2, sy-sr2, sr2*2, sr2*2);
                g.setColour(juce::Colour(0xff060402));
                g.drawLine(sx-sr2*0.7f, sy, sx+sr2*0.7f, sy+0.3f, sr2*0.35f);
                g.setColour(juce::Colour(0xff000000).withAlpha(0.8f));
                g.drawEllipse(sx-sr2, sy-sr2, sr2*2, sr2*2, 0.8f);
                g.setColour(juce::Colour(0xff8b3a10).withAlpha(0.35f));
                g.fillRect(sx-1.0f, sy+sr2*0.8f, 2.0f, sr2*1.4f);
            }
        }

        g.saveState();
        {
            juce::Path screenClip;
            screenClip.addEllipse(cx-r, cy-r, r*2, r*2);
            g.reduceClipRegion(screenClip);
        }

        {
            juce::ColourGradient face(
                juce::Colour(0xfff0ecc8), cx-r*0.3f, cy-r*0.4f,
                juce::Colour(0xffd8d4a8), cx+r*0.5f, cy+r*0.5f, false);
            g.setGradientFill(face);
            g.fillEllipse(cx-r, cy-r, r*2, r*2);
            juce::ColourGradient centre(
                juce::Colour(0x00000000), cx, cy,
                juce::Colour(0x18000000), cx+r*0.7f, cy+r*0.7f, true);
            g.setGradientFill(centre);
            g.fillEllipse(cx-r, cy-r, r*2, r*2);
        }

        g.setColour(juce::Colour(0xff707060).withAlpha(0.55f));
        g.drawEllipse(cx-r*0.94f, cy-r*0.94f, r*1.88f, r*1.88f, 0.8f);
        g.setColour(juce::Colour(0xff606050).withAlpha(0.40f));
        g.drawEllipse(cx-r*0.88f, cy-r*0.88f, r*1.76f, r*1.76f, 0.6f);

        const int gridX = 8, gridY = 6;
        const float gw = r*2.0f / float(gridX);
        const float gh = r*2.0f / float(gridY);
        g.setColour(juce::Colour(0xff484030).withAlpha(0.55f));
        for (int i = 1; i < gridX; ++i)
            g.drawLine((cx-r)+float(i)*gw, cy-r, (cx-r)+float(i)*gw, cy+r, 0.6f);
        for (int i = 1; i < gridY; ++i)
            g.drawLine(cx-r, (cy-r)+float(i)*gh, cx+r, (cy-r)+float(i)*gh, 0.6f);

        g.setColour(juce::Colour(0xff585040).withAlpha(0.75f));
        g.drawLine(cx, cy-r, cx, cy+r, 0.9f);
        g.drawLine(cx-r, cy, cx+r, cy, 0.9f);

        if (showTrim)
        {
            const float x1 = (cx - r) + startNorm * r * 2.0f;
            const float x2 = (cx - r) + endNorm   * r * 2.0f;
            g.setColour(juce::Colour(0x20aa5500));
            g.fillRect(cx-r, cy-r, x1-(cx-r), r*2.0f);
            g.fillRect(x2, cy-r, (cx+r)-x2, r*2.0f);
            g.setColour(juce::Colour(0xffc87010).withAlpha(0.9f));
            g.drawLine(x1, cy-r, x1, cy+r, 1.5f);
            g.drawLine(x2, cy-r, x2, cy+r, 1.5f);
        }

        juce::Path trace;
        for (int i = 0; i < DisplaySize; ++i)
        {
            const float x = (cx-r) + (float(i)/float(DisplaySize-1)) * r*2.0f;
            const float y = cy - juce::jlimit(-r*0.97f, r*0.97f, displayBuf[(size_t)i] * r*1.44f);
            if (i == 0) trace.startNewSubPath(x, y); else trace.lineTo(x, y);
        }

        juce::Path glowPath;
        for (int i = 0; i < DisplaySize; ++i)
        {
            const float x = (cx-r) + (float(i)/float(DisplaySize-1)) * r*2.0f;
            const float y = cy - juce::jlimit(-r*0.97f, r*0.97f, glowBuf[(size_t)i] * r*1.44f);
            if (i == 0) glowPath.startNewSubPath(x, y); else glowPath.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xffc87010).withAlpha(0.24f));
        g.strokePath(glowPath, juce::PathStrokeType(18.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffe89030).withAlpha(0.42f));
        g.strokePath(glowPath, juce::PathStrokeType(8.0f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xfffff0a0).withAlpha(0.54f));
        g.strokePath(glowPath, juce::PathStrokeType(2.5f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colour(0xffaa5008).withAlpha(0.07f));
        g.strokePath(trace, juce::PathStrokeType(22.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffc87010).withAlpha(0.13f));
        g.strokePath(trace, juce::PathStrokeType(14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffe08020).withAlpha(0.22f));
        g.strokePath(trace, juce::PathStrokeType(7.0f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xfff0a030).withAlpha(0.42f));
        g.strokePath(trace, juce::PathStrokeType(3.0f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xfffff0a0).withAlpha(0.72f));
        g.strokePath(trace, juce::PathStrokeType(1.1f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.85f));
        g.strokePath(trace, juce::PathStrokeType(0.4f,  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (recording)
        {
            g.setColour(juce::Colour(0xffaa1010).withAlpha(0.8f));
            g.fillEllipse(cx + r*0.55f, cy-r*0.78f, r*0.13f, r*0.13f);
        }

        {
            juce::ColourGradient glint(
                juce::Colour(0x18ffffff), cx-r*0.5f, cy-r*0.65f,
                juce::Colours::transparentWhite, cx, cy-r*0.1f, true);
            g.setGradientFill(glint);
            g.fillEllipse(cx-r, cy-r, r*2, r*2);
        }
        {
            juce::ColourGradient vig(
                juce::Colour(0x00000000), cx, cy,
                juce::Colour(0x40000000), cx+r, cy+r, true);
            g.setGradientFill(vig);
            g.fillEllipse(cx-r, cy-r, r*2, r*2);
        }

        g.restoreState();
        g.setColour(juce::Colour(0xff000000).withAlpha(0.7f));
        g.drawEllipse(cx-r, cy-r, r*2, r*2, 2.0f);
    }

private:
    std::array<float, DisplaySize> displayBuf{};
    std::array<float, DisplaySize> glowBuf{};
    bool aggressiveMode = false;
    bool recording = false;
    bool showTrim = false;
    float startNorm = 0.0f;
    float endNorm = 1.0f;
};

} // namespace dadbass
