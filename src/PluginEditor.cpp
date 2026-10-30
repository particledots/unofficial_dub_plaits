#include "PluginEditor.h"
#include "core/Version.h"
#include "core/DubSynth.h"

namespace {
const juce::Colour kBg (0xff15171a), kPanel (0xff1f2328), kText (0xffe6e9ed), kDim (0xff8b929c), kAccent (0xffe8a33d), kOn (0xff4ea1d3);

struct KnobNames { const char *h, *t, *m; };
// Only models whose knob meanings were read straight from the code get their own names; others keep the plain names.
KnobNames knobNames (int model)
{
    switch (model)
    {
        case dub::kDubSaw:  return { "DETUNE", "PULSE WIDTH", "NOISE CHUFF" };
        case dub::kFM:      return { "RATIO", "FM INDEX", "FEEDBACK" };
        default:            return { "HARMONICS", "TIMBRE", "MORPH" };
    }
}
}

DubStabEditor::DubStabEditor (DubStabAudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    addChoice (model, "model", "MODEL", [] { juce::StringArray a; for (int i = 0; i < dub::kNumSynthModels; ++i) a.add (dub::synthModelName (i)); return a; }());
    addKnob (harmonics, "harmonics", "HARMONICS");
    addKnob (timbre, "timbre", "TIMBRE");
    addKnob (morph, "morph", "MORPH");
    addKnob (envelope, "envelope", "ENVELOPE");
    addKnob (resonance, "resonance", "RESONANCE");
    addKnob (drift, "drift", "DRIFT");
    addKnob (volume, "volume", "VOLUME");

    addChoice (root, "root", "ROOT", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    addChoice (scale, "scale", "SCALE", { "Natural minor", "Dorian", "Phrygian", "Harmonic minor" });
    addChoice (voicing, "voicing", "VOICING", { "Dub", "Triads", "Sevenths", "Mixed", "Deep" });
    addChoice (rate, "rate", "RATE", { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" });

    addNumber (density, "density", "DENSITY");
    addNumber (steps, "steps", "STEPS");
    addNumber (octave, "octave", "OCTAVE");
    addNumber (seed, "seed", "SEED");
    addNumber (melody, "melody", "MELODY");

    for (auto* b : { &chords, &minorOnly })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*b);
    }
    chordsAtt = std::make_unique<ButtonAtt> (proc.apvts, "chords", chords);
    minorAtt = std::make_unique<ButtonAtt> (proc.apvts, "minor_only", minorOnly);

    for (auto* b : { &newPattern, &newMelody, &ownMelody }) addAndMakeVisible (*b);
    newPattern.onClick = [this] { proc.setIntParameter ("seed", proc.randomSeedValue()); };
    newMelody.onClick = [this] { proc.setIntParameter ("melody", proc.randomSeedValue()); };
    ownMelody.onClick = [this] { proc.setIntParameter ("melody", 0); };

    setSize (700, 470);
    updateKnobLabels();
    timerCallback();
    startTimerHz (20);
}

DubStabEditor::~DubStabEditor() { stopTimer(); }

void DubStabEditor::addKnob (Knob& k, const char* id, const char* name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::thumbColourId, kText);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kDim);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kText);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
    k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 2); };   // after the attachment, which sets its own text
    k.slider.updateText();
}

void DubStabEditor::addChoice (Choice& c, const char* id, const char* name, const juce::StringArray& items)
{
    c.box.addItemList (items, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, kDim);
    c.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<ComboAtt> (proc.apvts, id, c.box);
}

void DubStabEditor::addNumber (Number& n, const char* id, const char* name)
{
    n.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    n.slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 20);
    n.slider.setNumDecimalPlacesToDisplay (0);
    n.slider.setColour (juce::Slider::trackColourId, kAccent);
    n.slider.setColour (juce::Slider::thumbColourId, kText);
    n.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    n.slider.setColour (juce::Slider::textBoxOutlineColourId, kDim);
    n.label.setText (name, juce::dontSendNotification);
    n.label.setColour (juce::Label::textColourId, kDim);
    n.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (n.slider);
    addAndMakeVisible (n.label);
    n.att = std::make_unique<SliderAtt> (proc.apvts, id, n.slider);
}

void DubStabEditor::updateKnobLabels()
{
    const int m = (int) std::lround (proc.apvts.getRawParameterValue ("model")->load());
    if (m == shownModel) return;
    shownModel = m;
    const auto n = knobNames (m);
    harmonics.label.setText (n.h, juce::dontSendNotification);
    timbre.label.setText (n.t, juce::dontSendNotification);
    morph.label.setText (n.m, juce::dontSendNotification);
}

void DubStabEditor::timerCallback()
{
    updateKnobLabels();
    const auto& d = proc.display;
    const unsigned on = d.onMask.load(), acc = d.accentMask.load();
    const int len = d.length.load(), step = d.currentStep.load(), tr = d.transpose.load();
    const bool playing = d.playing.load();
    if (on != shownOn || acc != shownAccent || len != shownLength || step != shownStep || playing != shownPlaying || tr != shownTranspose)
    {
        shownOn = on; shownAccent = acc; shownLength = len; shownStep = step; shownPlaying = playing; shownTranspose = tr;
        repaint (stepArea.expanded (4));
        repaint (headerArea);
    }
}

void DubStabEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Unofficial Dub Stab Plaits", 16, 8, 380, 28, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (shownPlaying ? kAccent : kDim);
    juce::String t = shownPlaying ? "PLAYING" : "STOPPED - press play in the host";
    if (shownTranspose != 0) t << "   transpose " << (shownTranspose > 0 ? "+" : "") << shownTranspose << " (played note)";
    g.drawText (t, headerArea, juce::Justification::centredRight);

    // Pattern
    g.setColour (kPanel);
    g.fillRoundedRectangle (stepArea.expanded (6).toFloat(), 6.0f);
    const int cell = stepArea.getWidth() / 16;
    for (int i = 0; i < 16; ++i)
    {
        auto r = juce::Rectangle<int> (stepArea.getX() + i * cell, stepArea.getY(), cell - 4, stepArea.getHeight()).toFloat();
        const bool inPattern = i < shownLength;
        const bool isOn = (shownOn >> i) & 1u, isAcc = (shownAccent >> i) & 1u;
        g.setColour (! inPattern ? juce::Colour (0xff262a30) : isOn ? (isAcc ? kAccent : kOn) : juce::Colour (0xff343a42));
        g.fillRoundedRectangle (r, 3.0f);
        if (inPattern && i == shownStep)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
        }
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("blue = plays, orange = plays accented, grey = rest.  Density sets how many steps play.",
                stepArea.getX(), stepArea.getBottom() + 8, stepArea.getWidth(), 14, juce::Justification::centredLeft);

    g.setColour (kDim);
    g.drawFittedText ("Version " + juce::String (dub::versionString()) +
                      "  -  Not affiliated with Narti Live or Mutable Instruments. See the licenses folder for credits.",
                      16, getHeight() - 22, getWidth() - 32, 16, juce::Justification::centredLeft, 1);
}

void DubStabEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromBottom (18);
    auto header = area.removeFromTop (32);
    headerArea = header.removeFromRight (getWidth() - 16 - 16 - 380);

    // Row 1: model menu + sound knobs
    auto row1 = area.removeFromTop (130);
    auto modelCol = row1.removeFromLeft (150);
    model.label.setBounds (modelCol.removeFromTop (14));
    model.box.setBounds (modelCol.removeFromTop (26));
    const int kw = row1.getWidth() / 7;
    for (auto* k : { &harmonics, &timbre, &morph, &envelope, &resonance, &drift, &volume })
    {
        auto c = row1.removeFromLeft (kw);
        k->label.setBounds (c.removeFromTop (16));
        k->slider.setBounds (c);
    }
    area.removeFromTop (10);

    // Row 2: harmony
    auto row2 = area.removeFromTop (44);
    const int cw = (row2.getWidth() - 3 * 12) / 4;
    for (auto* c : { &root, &scale, &voicing, &rate })
    {
        auto col = row2.removeFromLeft (cw);
        c->label.setBounds (col.removeFromTop (14));
        c->box.setBounds (col.removeFromTop (26));
        row2.removeFromLeft (12);
    }
    area.removeFromTop (8);

    // Row 3: numbers
    auto row3 = area.removeFromTop (44);
    const int nw = (row3.getWidth() - 2 * 12) / 3;
    for (auto* n : { &density, &steps, &octave })
    {
        auto col = row3.removeFromLeft (nw);
        n->label.setBounds (col.removeFromTop (14));
        n->slider.setBounds (col.removeFromTop (26));
        row3.removeFromLeft (12);
    }
    area.removeFromTop (6);

    // Row 4: seed / melody + buttons
    auto row4 = area.removeFromTop (44);
    const int sw = (row4.getWidth() - 12) / 2;
    for (auto* n : { &seed, &melody })
    {
        auto col = row4.removeFromLeft (sw);
        n->label.setBounds (col.removeFromTop (14));
        n->slider.setBounds (col.removeFromTop (26));
        row4.removeFromLeft (12);
    }
    area.removeFromTop (6);
    auto row5 = area.removeFromTop (30);
    const int bw = (row5.getWidth() - 3 * 10 - 150) / 3;
    newPattern.setBounds (row5.removeFromLeft (bw)); row5.removeFromLeft (10);
    newMelody.setBounds (row5.removeFromLeft (bw)); row5.removeFromLeft (10);
    ownMelody.setBounds (row5.removeFromLeft (bw)); row5.removeFromLeft (10);
    chords.setBounds (row5.removeFromTop (15));
    minorOnly.setBounds (row5.removeFromTop (15));
    area.removeFromTop (14);

    // Step display
    stepArea = area.removeFromTop (34).withTrimmedRight (4);
}
