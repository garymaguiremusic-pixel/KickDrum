#include "KickDisplays.h"
#include "Shared/CustomLookAndFeel.h"
#include "../DSP/FxChain.h"

using namespace juce;

namespace kd::ui
{
    Colour bandColour (int band)
    {
        static const Colour colours[] { Colour (0xff3fa9ff), Colour (0xff3fe0c8), Colour (0xffffc04a), Colour (0xffd38cff) };
        return colours[jlimit (0, 3, band)];
    }

    namespace
    {
        void drawScreen (Graphics& g, Rectangle<float> b, bool dimmed)
        {
            CustomLookAndFeel::drawLcd (g, b, dimmed);
        }

        void drawScreenText (Graphics& g, const String& text, Rectangle<float> area, Justification j,
                             float size = 11.0f, Colour colour = Colours::white)
        {
            g.setFont (CustomLookAndFeel::uiFont (size, true));
            g.setColour (Colours::black.withAlpha (0.35f));
            g.drawText (text, area.translated (0.0f, 1.0f), j, true);
            g.setColour (colour);
            g.drawText (text, area, j, true);
        }

        void drawDropHighlight (Graphics& g, Rectangle<float> b)
        {
            g.setColour (Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (Colours::white);
            g.drawRoundedRectangle (b.reduced (1.5f), 3.0f, 2.0f);
        }

        String pitchText (const dsp::PitchResult& p)
        {
            if (! p.isValid())
                return "PITCH --";
            const double note = hzToNote (p.hz);
            const int n = roundToInt (note);
            const int cents = roundToInt ((note - n) * 100.0);
            return "PITCH " + noteName (n) + (cents == 0 ? String() : String (cents > 0 ? " +" : " ") + String (cents) + "ct")
                   + "  (" + String (p.hz, 1) + " Hz)";
        }
    }

    //==============================================================================
    bool DropTarget::isInterestedInFileDrag (const StringArray& files)
    {
        for (auto& f : files)
            if (isSupported == nullptr || isSupported (File (f)))
                return true;
        return false;
    }

    void DropTarget::filesDropped (const StringArray& files, int, int)
    {
        setDragHover (false);
        for (auto& f : files)
            if (isSupported == nullptr || isSupported (File (f)))
            {
                if (onFileDropped)
                    onFileDropped (File (f));
                return;
            }
    }

    //==============================================================================
    MainWaveform::MainWaveform()
    {
        setTooltip ("The kick (main sample). Click to play it, double-click to load a file, or drop a file here");
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void MainWaveform::setInfo (const KickBank::DisplayInfo& newInfo)
    {
        if (newInfo.generation != info.generation || newInfo.loading != info.loading)
        {
            info = newInfo;
            repaint();
        }
    }

    void MainWaveform::setPlayhead (double seconds, double r)
    {
        if (std::abs (seconds - playhead) > 1.0e-4 || r != ratio)
        {
            playhead = seconds;
            ratio = r;
            repaint();
        }
    }

    void MainWaveform::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        drawScreen (g, b, false);

        auto inner = b.reduced (6.0f, 4.0f);
        auto header = inner.removeFromTop (15.0f);
        const auto& ov = info.globalOverview;

        // waveform
        if (ov != nullptr && ov->peak > 0.0f)
        {
            const float mid = inner.getCentreY();
            const float halfH = inner.getHeight() * 0.5f;
            const float scale = 0.95f / ov->peak;
            Path wave;
            const int w = (int) inner.getWidth();
            for (int x = 0; x < w; ++x)
            {
                const int bin = jlimit (0, Overview::kNumBins - 1, x * Overview::kNumBins / jmax (1, w));
                const float top = mid - ov->maxs[(size_t) bin] * scale * halfH;
                const float bottom = mid - ov->mins[(size_t) bin] * scale * halfH;
                wave.addRectangle (inner.getX() + (float) x, top, 1.0f, jmax (1.0f, bottom - top));
            }
            g.setColour (Colours::white.withAlpha (0.92f));
            g.fillPath (wave);

            g.setColour (Colours::white.withAlpha (0.2f));
            g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());

            // playhead (in the sample's own time)
            if (playhead >= 0.0 && ov->seconds > 0.0)
            {
                const double pos = playhead * ratio / ov->seconds;
                if (pos <= 1.0)
                {
                    const float x = inner.getX() + (float) pos * inner.getWidth();
                    g.setColour (VirusColours::red);
                    g.fillRect (x - 0.75f, inner.getY(), 1.5f, inner.getHeight());
                }
            }
        }

        // header text
        String name = info.global.name.isEmpty() ? String ("Loading...") : info.global.name;
        if (info.global.missing)  name << "  (FILE NOT FOUND)";
        if (info.global.error)    name << "  (CAN'T READ FILE)";
        drawScreenText (g, name, header.withTrimmedRight (header.getWidth() * 0.45f), Justification::centredLeft, 12.0f,
                        info.global.missing || info.global.error ? Colour (0xffffb0a8) : Colours::white);

        String right = pitchText (info.pitch);
        if (info.global.seconds > 0.0)
            right << "    " << String (info.global.seconds, 2) << " s";
        if (info.loading)
            right = "SPLITTING...    " + right;
        drawScreenText (g, right, header, Justification::centredRight, 11.0f);

        if (dragHover)
            drawDropHighlight (g, b);
    }

    void MainWaveform::mouseDown (const MouseEvent& e)
    {
        if (e.getNumberOfClicks() == 1 && ! e.mods.isPopupMenu() && onAudition)
            onAudition();
    }

    void MainWaveform::mouseDoubleClick (const MouseEvent&)
    {
        if (onBrowse)
            onBrowse();
    }

    //==============================================================================
    CrossoverDisplay::CrossoverDisplay (RangedAudioParameter& x1, RangedAudioParameter& x2,
                                        RangedAudioParameter& x3, RangedAudioParameter& slope)
        : params { &x1, &x2, &x3 }
    {
        for (int i = 0; i < 3; ++i)
        {
            attachments[(size_t) i] = std::make_unique<ParameterAttachment> (*params[(size_t) i],
                [this, i] (float v) { freqs[(size_t) i] = v; repaint(); });
            attachments[(size_t) i]->sendInitialUpdate();
        }
        slopeAttachment = std::make_unique<ParameterAttachment> (slope, [this] (float v) { slopeIndex = roundToInt (v); repaint(); });
        slopeAttachment->sendInitialUpdate();

        setTooltip ("Linear-phase crossover. Drag the split lines to move the bands (shift = fine, double-click = default)");
    }

    Rectangle<float> CrossoverDisplay::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (7.0f, 5.0f).withTrimmedTop (14.0f).withTrimmedBottom (12.0f);
    }

    float CrossoverDisplay::xForFreq (double f) const
    {
        const auto a = plotArea();
        const double norm = std::log (jlimit (20.0, 20000.0, f) / 20.0) / std::log (1000.0);
        return a.getX() + (float) norm * a.getWidth();
    }

    double CrossoverDisplay::freqForX (float x) const
    {
        const auto a = plotArea();
        const double norm = jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
        return 20.0 * std::pow (1000.0, norm);
    }

    dsp::CrossoverSettings CrossoverDisplay::current() const
    {
        dsp::CrossoverSettings s;
        s.freqs = freqs;
        s.slopeIndex = slopeIndex;
        return s.sanitised (48000.0);
    }

    int CrossoverDisplay::handleAt (float x) const
    {
        int best = -1;
        float bestDist = 7.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float d = std::abs (xForFreq (freqs[(size_t) i]) - x);
            if (d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    void CrossoverDisplay::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        drawScreen (g, b, false);
        const auto a = plotArea();
        const auto s = current();

        // grid: decades and the usual landmarks
        g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xForFreq (f);
            const bool decade = f == 100.0 || f == 1000.0 || f == 10000.0;
            g.setColour (Colours::white.withAlpha (decade ? 0.22f : 0.1f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (Colours::white.withAlpha (0.55f));
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f),
                        Rectangle<float> (x - 15.0f, a.getBottom() + 1.0f, 30.0f, 11.0f), Justification::centred);
        }

        // band curves, in dB (0 dB at the top, -36 dB at the bottom)
        auto yForGain = [&a] (float gain)
        {
            const float db = jmax (-36.0f, Decibels::gainToDecibels (gain, -100.0f));
            return a.getY() + (-db / 36.0f) * a.getHeight();
        };

        for (int band = 0; band < 4; ++band)
        {
            Path curve, fill;
            const int steps = jmax (2, (int) a.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const float x = a.getX() + a.getWidth() * (float) i / (float) steps;
                const float y = yForGain (dsp::bandGain (band, freqForX (x), s));
                if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); }
                else        curve.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (a.getRight(), a.getBottom());
            fill.closeSubPath();

            const auto c = bandColour (band);
            g.setColour (c.withAlpha (0.16f));
            g.fillPath (fill);
            g.setColour (c.brighter (0.3f));
            g.strokePath (curve, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // band names in the middle of each band
        const StringArray names { "SUB", "BODY", "CLICK", "AIR" };
        const double edges[] { 20.0, s.freqs[0], s.freqs[1], s.freqs[2], 20000.0 };
        for (int band = 0; band < 4; ++band)
        {
            const float x0 = xForFreq (edges[band]), x1 = xForFreq (edges[band + 1]);
            drawScreenText (g, names[band], { x0, a.getY() + a.getHeight() * 0.42f, x1 - x0, 12.0f },
                            Justification::centred, 9.5f, bandColour (band).brighter (0.6f));
        }

        // split handles
        for (int i = 0; i < 3; ++i)
        {
            const float x = xForFreq (s.freqs[(size_t) i]);
            const bool hot = i == hover || i == dragging;
            g.setColour (Colours::white.withAlpha (hot ? 1.0f : 0.75f));
            g.fillRect (x - (hot ? 1.0f : 0.6f), a.getY() - 2.0f, hot ? 2.0f : 1.2f, a.getHeight() + 2.0f);

            Path grip;
            grip.addTriangle (x - 5.0f, a.getY() - 7.0f, x + 5.0f, a.getY() - 7.0f, x, a.getY() - 1.0f);
            g.fillPath (grip);
        }

        // header: the three frequencies and the slope
        auto header = b.reduced (7.0f, 3.0f).withHeight (13.0f);
        drawScreenText (g, "CROSSOVER  " + formatHz (s.freqs[0]) + "  |  " + formatHz (s.freqs[1]) + "  |  " + formatHz (s.freqs[2]),
                        header, Justification::centredLeft, 10.5f);
        drawScreenText (g, getSlopeNames()[slopeIndex].toUpperCase() + "  LINEAR PHASE", header, Justification::centredRight, 10.0f,
                        Colours::white.withAlpha (0.8f));
    }

    void CrossoverDisplay::mouseMove (const MouseEvent& e)
    {
        const int h = handleAt (e.position.x);
        if (h != hover)
        {
            hover = h;
            setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
            repaint();
        }
    }

    void CrossoverDisplay::mouseExit (const MouseEvent&)
    {
        hover = -1;
        repaint();
    }

    void CrossoverDisplay::mouseDown (const MouseEvent& e)
    {
        dragging = handleAt (e.position.x);
        if (dragging >= 0)
            attachments[(size_t) dragging]->beginGesture();
    }

    void CrossoverDisplay::mouseDrag (const MouseEvent& e)
    {
        if (dragging < 0)
            return;

        double f = freqForX (e.position.x);
        if (e.mods.isShiftDown())
        {
            // fine: a tenth of the mouse movement
            const double start = freqForX ((float) e.getMouseDownX());
            const double startValue = freqs[(size_t) dragging];
            f = startValue * std::pow (f / start, 0.1);
        }
        const auto range = params[(size_t) dragging]->getNormalisableRange();
        attachments[(size_t) dragging]->setValueAsPartOfGesture (jlimit (range.start, range.end, (float) f));
    }

    void CrossoverDisplay::mouseUp (const MouseEvent&)
    {
        if (dragging >= 0)
            attachments[(size_t) dragging]->endGesture();
        dragging = -1;
    }

    void CrossoverDisplay::mouseDoubleClick (const MouseEvent& e)
    {
        const int h = handleAt (e.position.x);
        if (h >= 0)
        {
            auto* p = params[(size_t) h];
            attachments[(size_t) h]->setValueAsCompleteGesture (p->convertFrom0to1 (p->getDefaultValue()));
        }
    }

    //==============================================================================
    bool BandView::State::operator== (const State& o) const
    {
        return overview == o.overview && env.attack == o.env.attack && env.hold == o.env.hold && env.decay == o.env.decay
            && env.curve == o.env.curve && env.gain == o.env.gain && spanSeconds == o.spanSeconds && ratio == o.ratio
            && displayScale == o.displayScale && tailOn == o.tailOn && tailStart == o.tailStart && tailLevel == o.tailLevel
            && tailText == o.tailText && lengthText == o.lengthText && rangeText == o.rangeText && enabled == o.enabled;
    }

    BandView::BandView (int bandIndex) : band (bandIndex)
    {
        setTooltip ("Click to hear this band on its own. Double-click to browse for a sample for this band, or drop one here");
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void BandView::setState (const State& s)
    {
        if (! (s == state))
        {
            state = s;
            repaint();
        }
    }

    void BandView::setPlayhead (double seconds)
    {
        if (std::abs (seconds - playhead) > 1.0e-4)
        {
            playhead = seconds;
            repaint();
        }
    }

    void BandView::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        drawScreen (g, b, ! state.enabled);

        auto inner = b.reduced (5.0f, 4.0f);
        auto header = inner.removeFromTop (13.0f);
        auto footer = inner.removeFromBottom (12.0f);
        const float span = (float) jmax (0.05, state.spanSeconds);
        auto xForT = [&] (double t) { return inner.getX() + (float) (t / span) * inner.getWidth(); };

        // time grid every 100 ms (every 500 ms when zoomed out)
        const double gridStep = span > 2.0 ? 0.5 : 0.1;
        g.setColour (Colours::white.withAlpha (0.08f));
        for (double t = gridStep; t < span; t += gridStep)
            g.drawVerticalLine ((int) xForT (t), inner.getY(), inner.getBottom());

        const float mid = inner.getCentreY();
        const float halfH = inner.getHeight() * 0.5f;
        const auto& ov = state.overview;
        const int w = (int) inner.getWidth();

        if (ov != nullptr && ov->seconds > 0.0)
        {
            auto binAt = [&] (double t) -> int
            {
                const double srcT = t * state.ratio;          // Tune Body plays faster/slower
                if (srcT >= ov->seconds) return -1;
                return jlimit (0, Overview::kNumBins - 1, (int) (srcT / ov->seconds * Overview::kNumBins));
            };

            Path raw, shaped;
            for (int x = 0; x < w; ++x)
            {
                const double t = (double) x / (double) w * span;
                const int bin = binAt (t);
                if (bin < 0)
                    continue;

                float hi = ov->maxs[(size_t) bin] * state.displayScale;
                float lo = ov->mins[(size_t) bin] * state.displayScale;
                raw.addRectangle (inner.getX() + (float) x, mid - hi * halfH, 1.0f, jmax (1.0f, (hi - lo) * halfH));

                float gain = envelopeAt (state.env, t) * state.env.gain;
                if (state.tailOn && t > state.tailStart - KickEngine::kTailCrossfade * 0.5)
                    gain *= 1.0f - (float) jlimit (0.0, 1.0, (t - (state.tailStart - KickEngine::kTailCrossfade * 0.5)) / KickEngine::kTailCrossfade);
                hi = jlimit (-1.0f, 1.0f, hi * gain);
                lo = jlimit (-1.0f, 1.0f, lo * gain);
                if (gain > 0.0f)
                    shaped.addRectangle (inner.getX() + (float) x, mid - hi * halfH, 1.0f, jmax (1.0f, (hi - lo) * halfH));
            }

            g.setColour (Colours::white.withAlpha (0.18f));
            g.fillPath (raw);
            g.setColour (Colours::white.withAlpha (state.enabled ? 0.95f : 0.5f));
            g.fillPath (shaped);

            // the tuned tail, as a filled sine envelope from the hand-over point
            if (state.tailOn)
            {
                const int startBin = binAt (state.tailStart);
                float amp = 0.0f;
                if (startBin >= 0)
                    for (int k = jmax (0, startBin - 4); k <= jmin (Overview::kNumBins - 1, startBin + 4); ++k)
                        amp = jmax (amp, ov->maxs[(size_t) k], -ov->mins[(size_t) k]);
                amp *= state.displayScale * state.tailLevel;

                Path tail;
                const double t0 = state.tailStart - KickEngine::kTailCrossfade * 0.5;
                bool started = false;
                for (int x = jmax (0, (int) (t0 / span * w)); x < w; ++x)
                {
                    const double t = (double) x / (double) w * span;
                    const float fadeIn = (float) jlimit (0.0, 1.0, (t - t0) / KickEngine::kTailCrossfade);
                    const float a = jmin (1.0f, amp * fadeIn * envelopeAt (state.env, t) * state.env.gain);
                    if (a <= 0.0f && started)
                        break;
                    tail.addRectangle (inner.getX() + (float) x, mid - a * halfH, 1.0f, jmax (1.0f, 2.0f * a * halfH));
                    started = true;
                }
                g.setColour (Colour (0xff9fe0ff).withAlpha (0.55f));
                g.fillPath (tail);

                const float tx = xForT (state.tailStart);
                g.setColour (Colour (0xff9fe0ff));
                g.drawVerticalLine ((int) tx, inner.getY(), inner.getBottom());
            }
        }

        // envelope line
        {
            Path env;
            for (int x = 0; x <= w; ++x)
            {
                const double t = (double) x / (double) jmax (1, w) * span;
                const float y = inner.getBottom() - envelopeAt (state.env, t) * inner.getHeight();
                if (x == 0) env.startNewSubPath (inner.getX(), y);
                else        env.lineTo (inner.getX() + (float) x, y);
            }
            g.setColour (bandColour (band).brighter (0.5f).withAlpha (state.enabled ? 0.95f : 0.4f));
            g.strokePath (env, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

            // end of the band
            const double end = state.env.length();
            if (end < span)
            {
                const float x = xForT (end);
                g.setColour (Colours::white.withAlpha (0.35f));
                for (float y = inner.getY(); y < inner.getBottom(); y += 4.0f)
                    g.fillRect (x - 0.5f, y, 1.0f, 2.0f);
            }
        }

        // playhead
        if (playhead >= 0.0 && playhead <= span && state.enabled)
        {
            g.setColour (VirusColours::red);
            g.fillRect (xForT (playhead) - 0.75f, inner.getY(), 1.5f, inner.getHeight());
        }

        // labels
        const StringArray names { "SUB", "BODY", "CLICK", "AIR" };
        drawScreenText (g, String (band + 1) + "  " + names[band], header, Justification::centredLeft, 10.5f);
        drawScreenText (g, state.rangeText, header, Justification::centredRight, 10.0f, Colours::white.withAlpha (0.85f));
        drawScreenText (g, state.lengthText, footer, Justification::centredRight, 9.5f, Colours::white.withAlpha (0.85f));
        if (state.tailOn && state.tailText.isNotEmpty())
            drawScreenText (g, state.tailText, footer, Justification::centredLeft, 9.5f, Colour (0xff9fe0ff));
        if (! state.enabled)
            drawScreenText (g, "MUTED", inner, Justification::centred, 12.0f, Colours::white.withAlpha (0.6f));

        if (dragHover)
            drawDropHighlight (g, b);
    }

    void BandView::mouseDown (const MouseEvent& e)
    {
        if (e.getNumberOfClicks() == 1 && ! e.mods.isPopupMenu() && onAudition)
            onAudition();
    }

    void BandView::mouseDoubleClick (const MouseEvent&)
    {
        if (onBrowse)
            onBrowse();
    }

    //==============================================================================
    void SaturationCurve::setState (bool o, int t, float d, float m)
    {
        if (o != on || t != type || d != driveDb || m != mix)
        {
            on = o; type = t; driveDb = d; mix = m;
            repaint();
        }
    }

    void SaturationCurve::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        drawScreen (g, b, ! on);
        auto a = b.reduced (8.0f, 6.0f);

        g.setColour (Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());

        // input -1..1 across, output -1.25..1.25 up
        const float drive = Decibels::decibelsToGain (driveDb);
        Path clean, curve;
        const int steps = (int) a.getWidth();
        for (int i = 0; i <= steps; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) steps;
            const float y = saturate (type, x * drive) * mix + x * (1.0f - mix);
            const float px = a.getX() + a.getWidth() * (float) i / (float) steps;
            const float py = a.getCentreY() - jlimit (-1.25f, 1.25f, y) / 1.25f * a.getHeight() * 0.5f;
            const float cy = a.getCentreY() - x / 1.25f * a.getHeight() * 0.5f;
            if (i == 0) { curve.startNewSubPath (px, py); clean.startNewSubPath (px, cy); }
            else        { curve.lineTo (px, py); clean.lineTo (px, cy); }
        }
        g.setColour (Colours::white.withAlpha (0.25f));
        g.strokePath (clean, PathStrokeType (1.0f));
        g.setColour (Colours::white.withAlpha (on ? 1.0f : 0.5f));
        g.strokePath (curve, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

        drawScreenText (g, getSatTypeNames()[type].toUpperCase(), b.reduced (7.0f, 3.0f).withHeight (13.0f),
                        Justification::centredLeft, 10.0f);
    }

    //==============================================================================
    void GainReductionMeter::setValue (float db, bool a)
    {
        if (std::abs (db - value) > 0.05f || a != active)
        {
            value = db;
            active = a;
            repaint();
        }
    }

    void GainReductionMeter::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        CustomLookAndFeel::drawInsetBox (g, b);

        auto bar = b.reduced (32.0f, 6.0f).withTrimmedRight (40.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRect (bar);

        const float norm = jlimit (0.0f, 1.0f, value / 24.0f);
        if (active && norm > 0.0f)
        {
            auto fill = bar.withWidth (bar.getWidth() * norm);
            g.setGradientFill (ColourGradient (VirusColours::blue, fill.getX(), 0.0f, VirusColours::red, bar.getRight(), 0.0f, false));
            g.fillRect (fill);
        }

        g.setColour (Colours::white.withAlpha (0.2f));
        for (int db = 3; db < 24; db += 3)
        {
            const float x = bar.getX() + bar.getWidth() * (float) db / 24.0f;
            g.drawVerticalLine ((int) x, bar.getY(), bar.getBottom());
        }

        g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
        g.setColour (VirusColours::labelText);
        g.drawText ("GR", b.withWidth (30.0f), Justification::centred);
        g.setColour (Colours::white.withAlpha (active ? 1.0f : 0.4f));
        g.drawText (active ? "-" + String (value, 1) + " dB" : String ("OFF"), b.withTrimmedLeft (b.getWidth() - 44.0f).withTrimmedRight (4.0f),
                    Justification::centredRight);
    }
}
