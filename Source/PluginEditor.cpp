#include "PluginEditor.h"

namespace
{
const juce::Colour floorTop    { 0xff1d2128 };
const juce::Colour floorBottom { 0xff090a0d };
const juce::Colour coatLight   { 0xff1a1d23 };
const juce::Colour coatDark    { 0xff07080a };
const juce::Colour ink         { 0xfff3f1ec };
const juce::Colour inkDim      { 0xff8d929b };
const juce::Colour electric    { 0xff3fa9ff };
const juce::Colour optoRed     { 0xffff4a1c };
const juce::Colour pcbTop      { 0xff15553a };
const juce::Colour pcbBottom   { 0xff0b3524 };
const juce::Colour silk        { 0xffe9f2ea };
const juce::Colour gold        { 0xffd6b25e };

juce::Font headline (float size)
{
    return juce::Font (juce::FontOptions().withName ("Futura").withStyle ("Condensed ExtraBold").withHeight (size));
}

juce::Font caption (float size)
{
    return juce::Font (juce::FontOptions().withName ("Avenir Next Condensed").withStyle ("Demi Bold").withHeight (size));
}

juce::Font silkscreen (float size)
{
    return juce::Font (juce::FontOptions().withName ("Menlo").withStyle ("Bold").withHeight (size));
}

void drawSpaced (juce::Graphics& g, const juce::String& text, juce::Font font, float kerning,
                 juce::Rectangle<float> area, juce::Justification just)
{
    g.setFont (font.withExtraKerningFactor (kerning));
    g.drawText (text, area, just, false);
}

// A jagged bolt, in a 0..1 box.
juce::Path boltPath()
{
    juce::Path p;
    p.startNewSubPath (0.62f, 0.00f);
    p.lineTo (0.20f, 0.52f);
    p.lineTo (0.47f, 0.52f);
    p.lineTo (0.30f, 1.00f);
    p.lineTo (0.82f, 0.40f);
    p.lineTo (0.54f, 0.40f);
    p.lineTo (0.74f, 0.00f);
    p.closeSubPath();
    return p;
}

void drawBigKnob (juce::Graphics& g, juce::Point<float> centre, float radius, float angle,
                  float startAngle, float endAngle, bool numbered)
{
    // Scale: ticks, with numbers on the big ones.
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.setColour (ink.withAlpha (i % 5 == 0 ? 0.95f : 0.6f));
        g.drawLine ({ centre + dir * (radius + 4.0f), centre + dir * (radius + (i % 5 == 0 ? 10.0f : 7.5f)) },
                    i % 5 == 0 ? 1.8f : 1.1f);

        if (numbered && i % 5 == 0)
        {
            g.setColour (inkDim);
            g.setFont (caption (11.0f));
            const auto at = centre + dir * (radius + 19.0f);
            g.drawText (juce::String (i), juce::Rectangle<float> (22.0f, 14.0f).withCentre (at),
                        juce::Justification::centred);
        }
    }

    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    // Drop shadow.
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (skirt.translated (0.0f, (float) i * 1.6f).expanded ((float) i * 0.8f));
    }

    // Knurled black skirt.
    g.setGradientFill ({ juce::Colour (0xff3b3e44), centre.x, skirt.getY(),
                         juce::Colour (0xff050506), centre.x, skirt.getBottom(), false });
    g.fillEllipse (skirt);

    const int ridges = 56;
    for (int i = 0; i < ridges; ++i)
    {
        const float a = angle + (float) i / (float) ridges * juce::MathConstants<float>::twoPi;
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        const float light = 0.5f + 0.5f * -std::cos (a - 0.6f);   // lit from the top left
        g.setColour (juce::Colours::white.withAlpha (0.04f + 0.13f * light));
        g.drawLine ({ centre + dir * (radius * 0.80f), centre + dir * (radius - 1.0f) }, 1.2f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (skirt, 1.0f);

    // Polished cap, inset.
    const auto cap = skirt.reduced (radius * 0.27f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (cap.expanded (2.0f).translated (0.0f, 1.0f));
    g.setGradientFill ({ juce::Colour (0xfff7f8fa), cap.getX(), cap.getY(),
                         juce::Colour (0xff6c7078), cap.getRight(), cap.getBottom(), false });
    g.fillEllipse (cap);

    // Spun-metal rings.
    for (float r = 0.15f; r < 0.98f; r += 0.11f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (cap.reduced (cap.getWidth() * 0.5f * (1.0f - r)), 0.7f);
    }

    // Specular sweep.
    {
        juce::Path sheen;
        sheen.addCentredArc (cap.getCentreX(), cap.getCentreY(), cap.getWidth() * 0.42f, cap.getHeight() * 0.42f,
                             0.0f, -2.2f, -0.4f, true);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.strokePath (sheen, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer: an engraved line across the cap, and a white dot on the skirt.
    const auto turn = juce::AffineTransform::rotation (angle).translated (centre);
    juce::Path line;
    line.addRoundedRectangle (-1.4f, -cap.getHeight() * 0.48f, 2.8f, cap.getHeight() * 0.36f, 1.4f);
    g.setColour (juce::Colour (0xff15171b));
    g.fillPath (line, turn);

    const float dot = juce::jmax (3.0f, radius * 0.075f);
    g.setColour (ink);
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f)
                       .withCentre ({ 0.0f, -radius * 0.89f }).transformedBy (turn));
}

void drawTrimmer (juce::Graphics& g, juce::Rectangle<float> bounds, float angle)
{
    const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 6.0f;
    const auto body = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto centre = body.getCentre();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (body.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill ({ juce::Colour (0xff3a78d8), body.getX(), body.getY(),
                         juce::Colour (0xff17408a), body.getRight(), body.getBottom(), false });
    g.fillRoundedRectangle (body, 3.0f);

    const auto rotor = body.reduced (side * 0.16f);
    g.setGradientFill ({ juce::Colour (0xfff4f4f2), rotor.getX(), rotor.getY(),
                         juce::Colour (0xffa9a9a4), rotor.getRight(), rotor.getBottom(), false });
    g.fillEllipse (rotor);

    const auto turn = juce::AffineTransform::rotation (angle).translated (centre);
    const float r = rotor.getWidth() * 0.42f;
    juce::Path slot;
    slot.addRoundedRectangle (-1.6f, -r, 3.2f, r * 2.0f, 1.2f);
    slot.addRoundedRectangle (-r * 0.55f, -1.6f, r * 1.1f, 3.2f, 1.2f);
    g.setColour (juce::Colour (0xff5b5b58));
    g.fillPath (slot, turn);

    // Index nick.
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ 0.0f, -r - 1.0f }).transformedBy (turn));
}
} // namespace

void TempestLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float startAngle, float endAngle,
                                           juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    if (slider.getName() == "trim")
    {
        drawTrimmer (g, bounds, angle);
        return;
    }

    const bool numbered = slider.getName() == "big";
    const float margin = numbered ? 26.0f : 13.0f;
    const auto area = bounds.reduced (margin);
    drawBigKnob (g, area.getCentre(), juce::jmin (area.getWidth(), area.getHeight()) * 0.5f,
                 angle, startAngle, endAngle, numbered);
}

void StompButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Washer.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (bounds.translated (0.0f, 3.0f));
    g.setGradientFill ({ juce::Colour (0xffcfd2d6), centre.x, bounds.getY(),
                         juce::Colour (0xff4a4d52), centre.x, bounds.getBottom(), false });
    g.fillEllipse (bounds);

    // Hex nut.
    juce::Path nut;
    nut.addPolygon (centre, 6, radius * 0.80f, 0.0f);
    g.setGradientFill ({ juce::Colour (0xffeef0f2), centre.x - radius, bounds.getY(),
                         juce::Colour (0xff5d6066), centre.x + radius, bounds.getBottom(), false });
    g.fillPath (nut);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (nut, juce::PathStrokeType (1.0f));

    // The plunger sinks a little while held.
    const float inset = down ? 0.47f : 0.42f;
    const auto plunger = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                             .withCentre (centre).reduced (radius * inset);
    g.setColour (juce::Colour (0xff2a2c30));
    g.fillEllipse (plunger.expanded (2.5f));
    g.setGradientFill ({ juce::Colour (down ? 0xffb8bcc2 : 0xfffbfcfd), centre.x, plunger.getY(),
                         juce::Colour (0xff767a82), centre.x, plunger.getBottom(), false });
    g.fillEllipse (plunger);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (plunger.reduced (plunger.getWidth() * 0.32f).translated (-2.0f, -3.0f));

    if (highlighted && ! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (plunger);
    }
}

void SlideSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill ({ juce::Colour (0xff3a3c40), 0.0f, bounds.getY(),
                         juce::Colour (0xff121315), 0.0f, bounds.getBottom(), false });
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto slot = bounds.reduced (5.0f, bounds.getHeight() * 0.3f);
    g.setColour (juce::Colour (0xff050506));
    g.fillRoundedRectangle (slot, 2.0f);

    const float w = slot.getWidth() * 0.5f;
    const auto actuator = juce::Rectangle<float> (w, slot.getHeight() + 6.0f)
                              .withCentre ({ getToggleState() ? slot.getRight() - w * 0.5f
                                                              : slot.getX() + w * 0.5f,
                                             slot.getCentreY() });
    g.setGradientFill ({ juce::Colour (highlighted ? 0xffffffff : 0xfff0f0ee), 0.0f, actuator.getY(),
                         juce::Colour (0xffa4a6aa), 0.0f, actuator.getBottom(), false });
    g.fillRoundedRectangle (actuator, 2.0f);
    for (int i = 1; i < 4; ++i)
    {
        const float lx = actuator.getX() + actuator.getWidth() * (float) i / 4.0f;
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine (lx, actuator.getY() + 3.0f, lx, actuator.getBottom() - 3.0f, 1.0f);
    }
}

void Jumper::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const float pitch = 15.0f;

    // Pins.
    for (int i = 0; i < 2; ++i)
    {
        const auto at = centre + juce::Point<float> ((i == 0 ? -0.5f : 0.5f) * pitch, 0.0f);
        g.setColour (gold.darker (0.4f));
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (at));
        g.setColour (gold);
        g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (at));
    }

    // The jumper cap: across both pins when fitted, parked on one when open.
    const bool fitted = getToggleState();
    const auto capArea = fitted ? juce::Rectangle<float> (pitch + 13.0f, 15.0f).withCentre (centre)
                                : juce::Rectangle<float> (13.0f, 15.0f).withCentre (centre + juce::Point<float> (-0.5f * pitch, 0.0f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (capArea.translated (0.0f, 2.5f), 2.0f);
    g.setGradientFill ({ juce::Colour (highlighted ? 0xff4b4e55 : 0xff35373c), 0.0f, capArea.getY(),
                         juce::Colour (0xff0d0e10), 0.0f, capArea.getBottom(), false });
    g.fillRoundedRectangle (capArea, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (capArea.reduced (0.5f), 2.0f, 1.0f);
}

TempestEditor::TempestEditor (TempestProcessor& p)
    : AudioProcessorEditor (p),
      tempestProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    gain.slider.setName ("big");
    boost.slider.setName ("big");
    limiter.slider.setName ("small");
    pickup.slider.setName ("trim");
    inputLevel.slider.setName ("trim");

    addKnob (gain, "gain");
    addKnob (boost, "boost");
    addKnob (limiter, "limiter");
    addKnob (pickup, "pickup");
    addKnob (inputLevel, "inputLevel");

    // Trimmers want a screwdriver's worth of drag.
    for (auto* s : { &pickup.slider, &inputLevel.slider })
        s->setMouseDragSensitivity (220);
    pickup.slider.setPopupDisplayEnabled (false, false, nullptr);

    addAndMakeVisible (footswitch);
    addAndMakeVisible (supply);
    addAndMakeVisible (isolator);
    footswitchAttachment = std::make_unique<ButtonAttachment> (p.apvts, "on", footswitch);
    supplyAttachment     = std::make_unique<ButtonAttachment> (p.apvts, "supply", supply);
    isolatorAttachment   = std::make_unique<ButtonAttachment> (p.apvts, "isolator", isolator);

    // Board legends follow the controls.
    for (auto* s : { &pickup.slider, &inputLevel.slider })
        s->onValueChange = [this] { repaint (board.toNearestInt()); };
    supply.onStateChange = [this] { repaint (board.toNearestInt()); };
    isolator.onStateChange = [this] { repaint (board.toNearestInt()); };

    setSize (440, 800);
    timerCallback();
    startTimerHz (30);
}

TempestEditor::~TempestEditor()
{
    setLookAndFeel (nullptr);
}

void TempestEditor::addKnob (Knob& knob, const juce::String& paramId)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.slider.setPopupDisplayEnabled (true, true, this);
    knob.slider.setRotaryParameters (juce::degreesToRadians (-150.0f), juce::degreesToRadians (150.0f), true);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (tempestProcessor.apvts, paramId, knob.slider);
}

void TempestEditor::timerCallback()
{
    const float nextLed = tempestProcessor.isOn() ? 1.0f : 0.0f;

    // The opto window glows with the LED inside the cell; the clip window
    // flashes as the LM386 runs out of room.
    const float gr = tempestProcessor.isOn() ? tempestProcessor.getGainReductionDb() : 0.0f;
    const float optoTarget = juce::jlimit (0.0f, 1.0f, gr / 14.0f);
    const float drive = tempestProcessor.isOn() ? tempestProcessor.getDrive() : 0.0f;
    const float clipTarget = juce::jlimit (0.0f, 1.0f, (drive - 0.75f) / 0.6f);

    const float nextOpto = opto + (optoTarget - opto) * (optoTarget > opto ? 0.6f : 0.25f);
    const float nextClip = clipTarget > clip ? clipTarget : clip * 0.82f;

    if (std::abs (nextLed - led) > 0.001f)
    {
        led = nextLed;
        repaint (ledArea.expanded (40));
    }
    if (std::abs (nextOpto - opto) > 0.002f || std::abs (gr - reductionDb) > 0.05f)
    {
        opto = nextOpto;
        reductionDb = gr;
        repaint (optoWindow.expanded (20, 36));
    }
    if (std::abs (nextClip - clip) > 0.002f)
    {
        clip = nextClip < 0.003f ? 0.0f : nextClip;
        repaint (clipWindow.expanded (20, 36));
    }
}

void TempestEditor::paintWindow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float level)
{
    const auto centre = area.getCentre();

    // Glow spilling onto the coat.
    if (level > 0.0f)
    {
        const float spill = area.getWidth() * (0.7f + 0.5f * level);
        g.setGradientFill ({ colour.withAlpha (0.35f * level), centre.x, centre.y,
                             colour.withAlpha (0.0f), centre.x + spill, centre.y, true });
        g.fillEllipse (juce::Rectangle<float> (spill * 2.0f, spill * 2.0f).withCentre (centre));
    }

    // Chrome bezel.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (area.expanded (5.0f).translated (0.0f, 2.0f));
    g.setGradientFill ({ juce::Colour (0xffe8eaee), area.getX(), area.getY(),
                         juce::Colour (0xff3d4046), area.getRight(), area.getBottom(), false });
    g.fillEllipse (area.expanded (5.0f));

    // Smoked glass with the element behind it.
    g.setColour (juce::Colour (0xff050506));
    g.fillEllipse (area);
    const auto element = area.reduced (area.getWidth() * 0.3f);
    g.setGradientFill ({ colour.withAlpha (0.15f + 0.85f * level), centre.x, centre.y,
                         colour.withAlpha (0.0f), centre.x + area.getWidth() * (0.25f + 0.3f * level), centre.y, true });
    g.fillEllipse (area);
    g.setColour (colour.withMultipliedBrightness (0.35f + 0.65f * level).withAlpha (0.9f));
    g.fillEllipse (element);
    g.setColour (juce::Colours::white.withAlpha (0.75f * level * level));
    g.fillEllipse (element.reduced (element.getWidth() * 0.28f));

    // Reflection on the glass.
    juce::Path shine;
    shine.addCentredArc (centre.x, centre.y, area.getWidth() * 0.36f, area.getHeight() * 0.36f, 0.0f, -2.0f, -0.6f, true);
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.strokePath (shine, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TempestEditor::paintEnclosure (juce::Graphics& g)
{
    const auto e = enclosure;

    // Jacks and DC socket, poking out of the box.
    auto jack = [&] (juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xffd9dbe0), r.getX(), r.getY(),
                             juce::Colour (0xff5a5d63), r.getX(), r.getBottom(), false });
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        for (int i = 1; i < 5; ++i)
        {
            if (r.getWidth() > r.getHeight())
                g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 5.0f), r.getY() + 2.0f, r.getBottom() - 2.0f);
            else
                g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 5.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
        }
    };
    jack ({ e.getX() - 13.0f, e.getY() + 94.0f, 16.0f, 36.0f });
    jack ({ e.getRight() - 3.0f, e.getY() + 94.0f, 16.0f, 36.0f });
    g.setColour (juce::Colour (0xff111214));
    g.fillRoundedRectangle ({ e.getCentreX() - 15.0f, e.getY() - 11.0f, 30.0f, 16.0f }, 3.0f);
    g.setColour (juce::Colour (0xff2d2f33));
    g.drawRoundedRectangle ({ e.getCentreX() - 15.0f, e.getY() - 11.0f, 30.0f, 16.0f }, 3.0f, 1.0f);

    // Drop shadow under the box.
    for (int i = 10; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        g.fillRoundedRectangle (e.translated (0.0f, (float) i * 1.8f).expanded ((float) i * 1.2f), 22.0f);
    }

    // The coat.
    g.drawImageAt (finish, (int) e.getX(), (int) e.getY());

    // Edge: a highlight along the top and left, a dark lip along the bottom.
    g.setGradientFill ({ juce::Colours::white.withAlpha (0.20f), e.getX(), e.getY(),
                         juce::Colours::white.withAlpha (0.0f), e.getX() + 140.0f, e.getY() + 220.0f, false });
    g.drawRoundedRectangle (e.reduced (1.0f), 20.0f, 2.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (e, 21.0f, 1.2f);

    // The storm: a bolt behind the name plate.
    {
        const auto box = juce::Rectangle<float> (128.0f, 190.0f).withCentre ({ e.getCentreX() + 6.0f, e.getY() + 466.0f });
        auto bolt = boltPath();
        bolt.applyTransform (juce::AffineTransform::scale (box.getWidth(), box.getHeight()).translated (box.getPosition()));
        g.setGradientFill ({ electric.withAlpha (0.16f), box.getCentreX(), box.getY(),
                             electric.withAlpha (0.02f), box.getCentreX(), box.getBottom(), false });
        g.fillPath (bolt);
        g.setColour (electric.withAlpha (0.30f));
        g.strokePath (bolt, juce::PathStrokeType (1.2f));
    }

    // LED.
    {
        const float lit = juce::jmax (0.0f, led);
        const auto c = ledArea.toFloat().getCentre();
        if (lit > 0.0f)
        {
            const float glow = 40.0f;
            g.setGradientFill ({ electric.withAlpha (0.55f * lit), c.x, c.y, electric.withAlpha (0.0f), c.x + glow, c.y, true });
            g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (c));
        }
        g.setGradientFill ({ juce::Colour (0xffe6e8ec), c.x, c.y - 10.0f, juce::Colour (0xff45484e), c.x, c.y + 10.0f, false });
        g.fillEllipse (ledArea.toFloat().expanded (4.0f));
        g.setColour (juce::Colour (0xff0b1a2a).interpolatedWith (juce::Colour (0xff9ad6ff), lit));
        g.fillEllipse (ledArea.toFloat());
        g.setColour (juce::Colours::white.withAlpha (0.35f + 0.5f * lit));
        g.fillEllipse (ledArea.toFloat().reduced (3.5f).translated (-1.5f, -1.5f));
    }

    // Knob legends.
    g.setColour (ink);
    for (auto* k : { &gain, &boost })
    {
        const auto b = k->slider.getBounds().toFloat();
        drawSpaced (g, k == &gain ? "GAIN" : "BOOST", caption (17.0f), 0.18f,
                    { b.getX(), b.getBottom() - 6.0f, b.getWidth(), 20.0f }, juce::Justification::centred);
    }
    {
        const auto b = limiter.slider.getBounds().toFloat();
        drawSpaced (g, "LIMITER", caption (15.0f), 0.18f,
                    { b.getX() - 20.0f, b.getBottom() - 2.0f, b.getWidth() + 40.0f, 18.0f }, juce::Justification::centred);
    }

    // The two windows.
    paintWindow (g, optoWindow.toFloat(), optoRed, opto);
    paintWindow (g, clipWindow.toFloat(), electric, clip);
    g.setColour (inkDim);
    drawSpaced (g, "OPTO", caption (12.0f), 0.25f,
                optoWindow.toFloat().withY ((float) optoWindow.getBottom() + 8.0f).withHeight (14.0f).expanded (20.0f, 0.0f),
                juce::Justification::centred);
    drawSpaced (g, "CLIP", caption (12.0f), 0.25f,
                clipWindow.toFloat().withY ((float) clipWindow.getBottom() + 8.0f).withHeight (14.0f).expanded (20.0f, 0.0f),
                juce::Justification::centred);
    g.setColour (optoRed.withAlpha (0.35f + 0.65f * opto).interpolatedWith (inkDim, 0.4f * (1.0f - opto)));
    g.setFont (silkscreen (10.0f));
    g.drawText (reductionDb < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (reductionDb, 1) + " dB",
                optoWindow.toFloat().withY ((float) optoWindow.getBottom() + 22.0f).withHeight (13.0f).expanded (24.0f, 0.0f),
                juce::Justification::centred);

    // Name plate.
    {
        const auto plate = juce::Rectangle<float> (e.getX(), e.getY() + 396.0f, e.getWidth(), 66.0f);
        g.setColour (electric.withAlpha (0.35f));
        drawSpaced (g, "TEMPEST", headline (60.0f), 0.12f, plate.translated (0.0f, 1.0f).expanded (2.0f, 0.0f), juce::Justification::centred);
        g.setColour (ink);
        drawSpaced (g, "TEMPEST", headline (60.0f), 0.12f, plate, juce::Justification::centred);

        const float y = plate.getBottom() + 6.0f;
        g.setColour (inkDim);
        drawSpaced (g, "OPTICAL LIMITER  /  BOOST", caption (11.5f), 0.32f,
                    { e.getX(), y, e.getWidth(), 14.0f }, juce::Justification::centred);
        g.setColour (electric.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (34.0f, 1.5f).withCentre ({ e.getX() + 52.0f, y + 7.0f }));
        g.fillRect (juce::Rectangle<float> (34.0f, 1.5f).withCentre ({ e.getRight() - 52.0f, y + 7.0f }));
    }

    // Footswitch legend.
    {
        const auto s = footswitch.getBounds().toFloat();
        g.setColour (inkDim);
        drawSpaced (g, "ENGAGE", caption (11.0f), 0.3f, { s.getX() - 20.0f, s.getBottom() + 2.0f, s.getWidth() + 40.0f, 14.0f },
                    juce::Justification::centred);
    }
}

void TempestEditor::paintBoard (juce::Graphics& g)
{
    const auto b = board;

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 3.0f), 8.0f);
    g.setGradientFill ({ pcbTop, b.getX(), b.getY(), pcbBottom, b.getX(), b.getBottom(), false });
    g.fillRoundedRectangle (b, 8.0f);

    // Traces, seeded so they don't move.
    {
        juce::Random random (386);
        g.setColour (juce::Colour (0xff1f6b4a).withAlpha (0.8f));
        for (int i = 0; i < 18; ++i)
        {
            juce::Path trace;
            float x = b.getX() + random.nextFloat() * b.getWidth();
            float y = b.getY() + 22.0f + random.nextFloat() * (b.getHeight() - 30.0f);
            trace.startNewSubPath (x, y);
            for (int s = 0; s < 3; ++s)
            {
                if (s % 2 == 0) x += (random.nextFloat() - 0.5f) * 140.0f;
                else            y += (random.nextFloat() - 0.5f) * 40.0f;
                trace.lineTo (juce::jlimit (b.getX() + 8.0f, b.getRight() - 8.0f, x),
                              juce::jlimit (b.getY() + 8.0f, b.getBottom() - 8.0f, y));
            }
            g.strokePath (trace, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }

    // Mounting holes.
    for (auto c : { b.getTopLeft() + juce::Point<float> (12.0f, 12.0f), b.getTopRight() + juce::Point<float> (-12.0f, 12.0f),
                    b.getBottomLeft() + juce::Point<float> (12.0f, -12.0f), b.getBottomRight() + juce::Point<float> (-12.0f, -12.0f) })
    {
        g.setColour (gold);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
        g.setColour (juce::Colour (0xff050806));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
    }

    g.setColour (silk.withAlpha (0.55f));
    g.setFont (silkscreen (9.5f));
    g.drawText ("INSIDE  //  REV 2  4558 . LM386 . NSL-32", b.reduced (26.0f, 8.0f).withHeight (14.0f),
                juce::Justification::centredLeft);

    // Each control's legend above it and its setting below.
    auto legend = [&] (juce::Component& c, const juce::String& name, const juce::String& value)
    {
        const auto r = c.getBounds().toFloat();
        g.setColour (silk);
        g.setFont (silkscreen (10.0f));
        g.drawText (name, juce::Rectangle<float> (r.getCentreX() - 50.0f, r.getY() - 16.0f, 100.0f, 13.0f),
                    juce::Justification::centred);
        g.setColour (silk.withAlpha (0.7f));
        g.setFont (silkscreen (9.5f));
        g.drawText (value, juce::Rectangle<float> (r.getCentreX() - 50.0f, r.getBottom() + 3.0f, 100.0f, 13.0f),
                    juce::Justification::centred);
        g.setColour (silk.withAlpha (0.35f));
        g.drawRect (r.expanded (4.0f, 2.0f), 0.8f);
    };

    legend (supply, "SUPPLY", supply.getToggleState() ? "9V   [12V]" : "[9V]   12V");
    legend (isolator, "ISO CAP 10u", isolator.getToggleState() ? "FITTED" : "OPEN (STOCK)");
    const char* const pickups[] = { "NONE", "SINGLE COIL", "HUMBUCKER" };
    legend (pickup.slider, "PICKUP LOAD", pickups[juce::jlimit (0, 2, juce::roundToInt (pickup.slider.getValue()))]);
    const double level = inputLevel.slider.getValue();
    legend (inputLevel.slider, "INPUT REF", (level >= 0.0 ? "+" : "") + juce::String (level, 1) + " dBV");
}

void TempestEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // The floor it sits on, with a pool of light behind the pedal.
    g.setGradientFill ({ floorTop, 0.0f, 0.0f, floorBottom, 0.0f, bounds.getBottom(), false });
    g.fillAll();
    g.setGradientFill ({ electric.withAlpha (0.07f), enclosure.getCentreX(), enclosure.getCentreY() - 60.0f,
                         electric.withAlpha (0.0f), enclosure.getCentreX() + 360.0f, enclosure.getCentreY(), true });
    g.fillRect (bounds);

    paintEnclosure (g);
    paintBoard (g);
}

void TempestEditor::resized()
{
    enclosure = { 50.0f, 26.0f, 340.0f, 600.0f };
    board = { 20.0f, 650.0f, 400.0f, 136.0f };

    const int ex = (int) enclosure.getX(), ey = (int) enclosure.getY(), cx = (int) enclosure.getCentreX();

    ledArea = juce::Rectangle<int> (13, 13).withCentre ({ cx, ey + 46 });

    gain.slider.setBounds (juce::Rectangle<int> (168, 168).withCentre ({ ex + 86, ey + 146 }));
    boost.slider.setBounds (juce::Rectangle<int> (168, 168).withCentre ({ ex + 254, ey + 146 }));
    limiter.slider.setBounds (juce::Rectangle<int> (112, 112).withCentre ({ cx, ey + 300 }));

    optoWindow = juce::Rectangle<int> (38, 38).withCentre ({ ex + 66, ey + 296 });
    clipWindow = juce::Rectangle<int> (38, 38).withCentre ({ ex + 274, ey + 296 });

    footswitch.setBounds (juce::Rectangle<int> (92, 92).withCentre ({ cx, ey + 530 }));

    // Board: four controls across.
    const int by = (int) board.getY() + 70;
    const float step = board.getWidth() / 4.0f;
    auto column = [&] (int i) { return (int) (board.getX() + step * ((float) i + 0.5f)); };
    supply.setBounds (juce::Rectangle<int> (64, 36).withCentre ({ column (0), by }));
    isolator.setBounds (juce::Rectangle<int> (48, 30).withCentre ({ column (1), by }));
    pickup.slider.setBounds (juce::Rectangle<int> (40, 40).withCentre ({ column (2), by }));
    inputLevel.slider.setBounds (juce::Rectangle<int> (40, 40).withCentre ({ column (3), by }));

    // The crinkle finish: dense light and dark specks over a soft gradient,
    // clipped to the box. Rendered once.
    finish = juce::Image (juce::Image::ARGB, (int) enclosure.getWidth(), (int) enclosure.getHeight(), true);
    {
        juce::Graphics fg (finish);
        const auto area = finish.getBounds().toFloat();
        juce::Path shape;
        shape.addRoundedRectangle (area, 21.0f);
        fg.reduceClipRegion (shape);

        fg.setGradientFill ({ coatLight, 0.0f, 0.0f, coatDark, area.getWidth() * 0.4f, area.getBottom(), false });
        fg.fillAll();

        juce::Random random (9);
        for (int i = 0; i < 26000; ++i)
        {
            const float x = random.nextFloat() * area.getWidth(), y = random.nextFloat() * area.getHeight();
            const bool light = random.nextBool();
            fg.setColour (light ? juce::Colours::white.withAlpha (0.015f + random.nextFloat() * 0.04f)
                                : juce::Colours::black.withAlpha (0.10f + random.nextFloat() * 0.25f));
            fg.fillEllipse (x, y, 1.0f + random.nextFloat() * 1.6f, 1.0f + random.nextFloat() * 1.2f);
        }

        // A soft sheen across the top third.
        fg.setGradientFill ({ juce::Colours::white.withAlpha (0.06f), area.getWidth() * 0.3f, 0.0f,
                              juce::Colours::white.withAlpha (0.0f), area.getWidth() * 0.5f, area.getHeight() * 0.45f, false });
        fg.fillRect (area);
    }
}
