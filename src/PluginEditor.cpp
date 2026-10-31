#include "PluginEditor.h"
#include "Presets.h"
#include "core/Version.h"
#include "AboutText.h"
#include "core/DubSynth.h"
#include "core/Effects.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
const juce::Colour kBg (0xff15171a), kPanel (0xff1f2328), kText (0xffe6e9ed), kDim (0xff8b929c), kAccent (0xffe8a33d), kOn (0xff4ea1d3);

// What HARMONICS / TIMBRE / MORPH (and the FREQUENCY knob) mean in each model. Read from the code in dub.cc (the comment at each
// model's oscillator), string_machine_engine.cc and AcidVoice.h.
struct KnobNames { const char *h, *t, *m, *env; };
KnobNames knobNames (int model)
{
    switch (model)
    {
        case dub::kDubSaw:    return { "DETUNE", "SAW > PULSE", "NOISE CHUFF", "ENVELOPE" };
        case dub::kVA:        return { "INTERVAL", "PULSE WIDTH", "OSC MIX", "ENVELOPE" };
        case dub::kWaveshape: return { "ASYMMETRY", "FOLD", "TRIANGLE > SAW", "ENVELOPE" };
        case dub::kFM:        return { "RATIO", "FM INDEX", "FEEDBACK", "ENVELOPE" };
        case dub::kPhaseDist: return { "RES. RATIO", "DISTORTION", "BLEND", "ENVELOPE" };
        case dub::kChiptune:  return { "DUTY", "CRUSH", "TRIANGLE", "ENVELOPE" };
        case dub::kStrings:   return { "FILTER OPEN", "CUTOFF + ENSEMBLE", "REGISTRATION", "ENVELOPE" };
        case dub::kAcid:      return { "CUTOFF", "ACCENT", "RESONANCE", "ENV AMOUNT" };
        default:              return { "HARMONICS", "TIMBRE", "MORPH", "ENVELOPE" };
    }
}

// A box you drag from: it makes the MIDI file when the drag starts and hands it to the host (Logic takes it on a track).
struct DragHandle : public juce::Component, public juce::SettableTooltipClient
{
    std::function<juce::File()> makeFile;

    DragHandle() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (kAccent);
        const float dashes[] = { 5.0f, 3.0f };
        juce::Path p; p.addRoundedRectangle (r, 5.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
        g.setColour (kText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("DRAG MIDI TO A TRACK", getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { started = false; }
    void mouseUp (const juce::MouseEvent&) override { started = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 6 || ! makeFile) return;
        started = true;
        const auto f = makeFile();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
    bool started = false;
};
} // namespace

// ============================================================================================ the About panel

// Title, what model 6 is, who made what, then the credits and both licence texts verbatim (generated from licenses/ and LICENSE).
juce::String buildAboutText()
{
    auto plain = [] (juce::String s)
    {
        juce::StringArray lines;
        lines.addLines (s.replace ("**", "").replace ("`", ""));
        for (auto& l : lines) if (l.startsWithChar ('#')) l = l.trimCharactersAtStart ("# ");   // markdown headings: no # marks
        return lines.joinIntoString ("\n");
    };
    juce::String t;
    t << "Unofficial Dub Stab Plaits  " << dub::versionString() << "\n\n"
      << "A community AU / VST3 instrument based on the DUB STAB PLAITS firmware and Max for Live devices by Narti Live, "
         "built on the Plaits engines by Emilie Gillet (Mutable Instruments). Not affiliated with, made or supported by "
         "Narti Live, Mutable Instruments or Emilie Gillet.\n\n"
      << dub::model6Note() << "\n\n"
      << "Source code: github.com/particledots/unofficial_dub_plaits\n\n"
      << "------------------------------ CREDITS ------------------------------\n\n" << plain (dub::creditsText()) << "\n\n"
      << "------------- LICENCE: Narti Live and Emilie Gillet (MIT, verbatim) -------------\n\n" << plain (dub::nartiLicenseText()) << "\n\n"
      << "------------- LICENCE: this plug-in (MIT) -------------\n\n" << plain (dub::particledotsLicense()) << "\n";
    return t;
}

class AboutOverlay : public juce::Component
{
public:
    AboutOverlay()
    {
        text.setMultiLine (true, true);
        text.setReadOnly (true);
        text.setCaretVisible (false);
        text.setScrollbarsShown (true);
        text.setFont (juce::FontOptions (13.0f));
        text.setColour (juce::TextEditor::backgroundColourId, kPanel);
        text.setColour (juce::TextEditor::textColourId, kText);
        text.setColour (juce::TextEditor::outlineColourId, kDim);
        text.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (text);
        addAndMakeVisible (close);
    }
    void setText (const juce::String& s) { text.setText (s, false); text.moveCaretToTop (false); }
    juce::String getText() const { return text.getText(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xe6101214));
        g.setColour (kAccent);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("ABOUT / CREDITS / LICENCES", 40, 18, getWidth() - 80, 24, juce::Justification::centredLeft);
    }
    void resized() override
    {
        text.setBounds (40, 52, getWidth() - 80, getHeight() - 52 - 64);
        close.setBounds (getWidth() / 2 - 60, getHeight() - 52, 120, 30);
    }
    void mouseDown (const juce::MouseEvent&) override {}      // the panel under it does not get the click
private:
    juce::TextEditor text;
    juce::TextButton close { "CLOSE" };
};

// ============================================================================================ the panel

class DubStabPanel : public juce::Component, private juce::Timer
{
public:
    explicit DubStabPanel (DubStabAudioProcessor& p);
    ~DubStabPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::File makeMidiFileForDrag() { return exportFile(); }
    void showAbout (bool show) { if (show) { about.setText (buildAboutText()); about.setVisible (true); about.toFront (false); } else about.setVisible (false); }
    bool aboutShown() const { return about.isVisible(); }
    juce::String aboutTextShown() const { return about.getText(); }

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct Knob    { juce::String id; juce::Slider slider; juce::Label label; std::unique_ptr<APVTS::SliderAttachment> att; };
    struct Choice  { juce::ComboBox box; juce::Label label; std::unique_ptr<APVTS::ComboBoxAttachment> att; };
    struct Number  { juce::String id; juce::Slider slider; juce::Label label; std::unique_ptr<APVTS::SliderAttachment> att; };

    void addKnob (Knob&, const char* id, const char* name, const char* tip, bool paramText = false);
    void addChoice (Choice&, const char* id, const char* name, const juce::StringArray& items, const char* tip = nullptr);
    void addNumber (Number&, const char* id, const char* name, const char* tip = nullptr);
    void addToggle (juce::ToggleButton&, std::unique_ptr<APVTS::ButtonAttachment>&, const char* id, const char* tip);
    void updateModelDependent();
    void timerCallback() override;
    void showPresetMenu();
    void askPresetName();
    void saveMidiDialog();
    juce::File exportFile();

    DubStabAudioProcessor& proc;
    std::vector<Knob*> knobs;
    std::vector<Number*> numbers;

    Choice model, root, scale, voicing, rate, echoTime, midiMode, exportBars, exportLen, direction;
    Knob harmonics, timbre, morph, envelope, resonance, drift, sub, volume;
    Knob echoMix, echoFb, echoTone, reverbMix, reverbDecay, width;
    Number density, steps, octave, seed, melody, freeBpm;
    Number runMin, runMax, wFwd, wRev, wPend, wPing, wRand, wDrunk;
    juce::ToggleButton chords { "CHORDS" }, minorOnly { "MINOR CHORDS ONLY" }, freeRun { "FREE-RUN WHEN STOPPED" };
    std::unique_ptr<APVTS::ButtonAttachment> chordsAtt, minorAtt, freeRunAtt;
    juce::TextButton newPattern { "NEW PATTERN" }, newMelody { "NEW MELODY" }, ownMelody { "ORIGINAL MELODY" };
    juce::TextButton presetButton { "PRESETS" }, saveMidi { "SAVE MIDI..." }, aboutButton { "ABOUT" };
    AboutOverlay about;
    DragHandle dragMidi;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> stepArea, statusArea, presetNameArea, orderNoteArea;
    int shownModel = -1, shownStep = -2, shownLength = -1, shownTranspose = 0, shownDirection = -1;
    uint64_t shownOn = ~(uint64_t) 0, shownAccent = ~(uint64_t) 0;
    bool shownPlaying = false, shownFree = false, shownDirect = false;
    float shownBpm = 0;
    juce::String shownPreset;
};

DubStabPanel::DubStabPanel (DubStabAudioProcessor& p) : proc (p)
{
    addChoice (model, "model", "MODEL", [] { juce::StringArray a; for (int i = 0; i < dub::kNumSynthModels; ++i) a.add (dub::synthModelName (i)); return a; }(),
               "The sound. Each model has its own meaning for the three knobs next to it.");
    addKnob (harmonics, "harmonics", "HARMONICS", nullptr);
    addKnob (timbre, "timbre", "TIMBRE", nullptr);
    addKnob (morph, "morph", "MORPH", nullptr);
    addKnob (envelope, "envelope", "ENVELOPE", "Short pluck -> dub stab -> long swell.");
    addKnob (resonance, "resonance", "RESONANCE", "Filter resonance (never self-oscillates). Not used by ACID: its resonance is the third knob.");
    addKnob (drift, "drift", "DRIFT", "The slow random movement of the three sound knobs and the filter. 15 % is the module's amount.");
    addKnob (sub, "sub", "SUB", "The module's AUX output: a sine one octave below the chord root, mixed in. Models 1-6 only (STRINGS and ACID have none).", true);
    addKnob (volume, "volume", "VOLUME", nullptr);

    addKnob (echoMix, "echo_mix", "MIX", "How much echo is added to the dry sound.", true);
    {
        juce::StringArray t; for (int i = 0; i < dub::kNumEchoTimes; ++i) t.add (dub::echoTimeName (i));
        addChoice (echoTime, "echo_time", "TIME", t, "Echo time, in note lengths at the current tempo (host tempo, or the free-run tempo).");
    }
    addKnob (echoFb, "echo_fb", "FEEDBACK", "How many repeats. The echoes bounce left and right.", true);
    addKnob (echoTone, "echo_tone", "TONE", "How dark the repeats get: left = dark, right = bright.", true);
    addKnob (reverbMix, "reverb_mix", "MIX", "How much reverb is added. The echoes go into the reverb too.", true);
    addKnob (reverbDecay, "reverb_decay", "DECAY", "Reverb time (time to fall by 60 dB).", true);
    addKnob (width, "width", "WIDTH", "0 % = mono, 100 % = as made, up to 200 % = extra wide (the dry sound is spread too).", true);

    addChoice (root, "root", "ROOT", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    addChoice (scale, "scale", "SCALE", { "Natural minor", "Dorian", "Phrygian", "Harmonic minor" });
    addChoice (voicing, "voicing", "VOICING", { "Dub", "Triads", "Sevenths", "Mixed", "Deep" });
    addChoice (rate, "rate", "RATE", { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" });

    addNumber (density, "density", "DENSITY", "How many of the steps play.");
    addNumber (steps, "steps", "STEPS", "Pattern length, 1 to 64 steps. Steps 1-16 are the original's pattern; every further 16 steps is another bar of the same recipe from the same seed. Density counts the steps that play in each bar of 16.");
    addNumber (octave, "octave", "OCTAVE");
    addNumber (seed, "seed", "SEED", "The pattern. The same seed always gives the same pattern.");
    addNumber (melody, "melody", "MELODY", "0 = the pattern's own melody; any other number = a different melody on the same rhythm.");

    addChoice (direction, "direction", "ORDER OF THE STEPS", { "Forward", "Reverse", "Pendulum", "Ping-pong", "Random", "Random (no repeats)", "Drunk walk", "Chance mix" },
               "Forward = as the original. Reverse = last step first. Pendulum = up and back, the end steps once. Ping-pong = up and back, the end steps twice. Random = any step each time (repeatable from the seed). Random (no repeats) = every step once, then a new order. Drunk walk = one step left, one step right or stay. Chance mix = picks one of these by the chances below, keeps it for a run of steps, then picks again.");
    addNumber (runMin, "run_min", "CHANCE: SHORTEST RUN", "Chance mix: an order is kept for at least this many steps.");
    addNumber (runMax, "run_max", "CHANCE: LONGEST RUN", "Chance mix: an order is kept for at most this many steps.");
    addNumber (wFwd, "dir_w_fwd", "FORWARD", "Chance mix: how likely forward is to be picked, compared with the others (0 = never). The order just finished is not picked again unless it is the only one with a chance.");
    addNumber (wRev, "dir_w_rev", "REVERSE", "Chance mix: how likely reverse is to be picked, compared with the others (0 = never).");
    addNumber (wPend, "dir_w_pend", "PENDULUM", "Chance mix: how likely pendulum is to be picked, compared with the others (0 = never).");
    addNumber (wPing, "dir_w_ping", "PING-PONG", "Chance mix: how likely ping-pong is to be picked, compared with the others (0 = never).");
    addNumber (wRand, "dir_w_rand", "RANDOM", "Chance mix: how likely random is to be picked, compared with the others (0 = never).");
    addNumber (wDrunk, "dir_w_drunk", "DRUNK WALK", "Chance mix: how likely the drunk walk is to be picked, compared with the others (0 = never).");
    for (auto* n : { &wFwd, &wRev, &wPend, &wPing, &wRand, &wDrunk }) n->slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);

    addToggle (chords, chordsAtt, "chords", "Off = single notes instead of chords.");
    addToggle (minorOnly, minorAtt, "minor_only", "Only the scale's minor and diminished chords.");

    addChoice (midiMode, "midi_mode", "MIDI NOTES DO THIS", { "Transpose the pattern", "Play the notes directly" },
               "Transpose: a played note moves the pattern (C3 = no change). Directly: the pattern is off and each note, or several notes held together, plays the sound - for driving it from a MIDI effect.");
    addToggle (freeRun, freeRunAtt, "free_run", "The pattern plays at the tempo on the right while the host is stopped. The host's play button takes over.");
    addNumber (freeBpm, "free_bpm", "FREE-RUN TEMPO", "Tempo when free-running.");

    addChoice (exportBars, "export_bars", "EXPORT BARS", { "1", "2", "4", "8", "16" }, "How many bars the MIDI file has.");
    addChoice (exportLen, "export_len", "EXPORT NOTE LENGTH", { "Short (25%)", "Half (50%)", "Full (100%)" }, "How long each exported note is, as part of a step.");

    for (auto* b : { &newPattern, &newMelody, &ownMelody, &presetButton, &saveMidi }) addAndMakeVisible (*b);
    newPattern.onClick = [this] { proc.setIntParameter ("seed", proc.randomSeedValue()); };
    newMelody.onClick = [this] { proc.setIntParameter ("melody", proc.randomSeedValue()); };
    ownMelody.onClick = [this] { proc.setIntParameter ("melody", 0); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveMidi.onClick = [this] { saveMidiDialog(); };
    presetButton.setTooltip ("Factory presets, your own presets, and saving the current sound as a preset.");
    saveMidi.setTooltip ("Save the pattern as a MIDI file (or drag from the box on the left onto a track).");
    dragMidi.setTooltip ("Drag this onto a track in the host: the current pattern arrives as a MIDI region.");
    dragMidi.makeFile = [this] { return exportFile(); };
    addAndMakeVisible (dragMidi);
    addAndMakeVisible (aboutButton);
    aboutButton.setTooltip ("Credits, who made what, and the licence texts.");
    aboutButton.onClick = [this] { showAbout (true); };
    addChildComponent (about);       // last, so it covers everything while it is open

    setSize (DubStabEditor::kBaseWidth, DubStabEditor::kBaseHeight);
    updateModelDependent();
    timerCallback();
    startTimerHz (20);
}

void DubStabPanel::addKnob (Knob& k, const char* id, const char* name, const char* tip, bool paramText)
{
    k.id = id;
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::thumbColourId, kText);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kDim);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* prm = proc.apvts.getParameter (id);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kText);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    if (tip != nullptr) { k.slider.setTooltip (tip); k.label.setTooltip (tip); }
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, k.slider);
    if (! paramText)   // after the attachment, which sets its own text
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
    k.slider.updateText();
    knobs.push_back (&k);
}

void DubStabPanel::addChoice (Choice& c, const char* id, const char* name, const juce::StringArray& items, const char* tip)
{
    c.box.addItemList (items, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, kDim);
    c.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    if (tip != nullptr) { c.box.setTooltip (tip); c.label.setTooltip (tip); }
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, id, c.box);
}

void DubStabPanel::addNumber (Number& n, const char* id, const char* name, const char* tip)
{
    n.id = id;
    n.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    n.slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 78, 20);
    n.slider.setColour (juce::Slider::trackColourId, kAccent);
    n.slider.setColour (juce::Slider::thumbColourId, kText);
    n.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    n.slider.setColour (juce::Slider::textBoxOutlineColourId, kDim);
    n.label.setText (name, juce::dontSendNotification);
    n.label.setColour (juce::Label::textColourId, kDim);
    n.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    if (tip != nullptr) { n.slider.setTooltip (tip); n.label.setTooltip (tip); }
    addAndMakeVisible (n.slider);
    addAndMakeVisible (n.label);
    n.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, n.slider);
    numbers.push_back (&n);
}

void DubStabPanel::addToggle (juce::ToggleButton& b, std::unique_ptr<APVTS::ButtonAttachment>& att, const char* id, const char* tip)
{
    b.setColour (juce::ToggleButton::textColourId, kText);
    b.setColour (juce::ToggleButton::tickColourId, kAccent);
    b.setTooltip (tip);
    addAndMakeVisible (b);
    att = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, id, b);
}

void DubStabPanel::updateModelDependent()
{
    const int m = (int) std::lround (proc.apvts.getRawParameterValue ("model")->load());
    if (m == shownModel) return;
    shownModel = m;
    const auto n = knobNames (m);
    harmonics.label.setText (n.h, juce::dontSendNotification);
    timbre.label.setText (n.t, juce::dontSendNotification);
    morph.label.setText (n.m, juce::dontSendNotification);
    envelope.label.setText (n.env, juce::dontSendNotification);
    // controls a model does not use are dimmed (STRINGS and ACID have no sub-bass; ACID's resonance is the MORPH knob)
    const bool subUsed = m <= dub::kChiptune;
    sub.slider.setAlpha (subUsed ? 1.0f : 0.35f);  sub.label.setAlpha (subUsed ? 1.0f : 0.35f);
    const bool resUsed = m != dub::kAcid;
    resonance.slider.setAlpha (resUsed ? 1.0f : 0.35f);  resonance.label.setAlpha (resUsed ? 1.0f : 0.35f);
}

void DubStabPanel::timerCallback()
{
    updateModelDependent();
    const auto& d = proc.display;
    const uint64_t on = d.onMask.load(), acc = d.accentMask.load();
    const int len = d.length.load(), step = d.currentStep.load(), tr = d.transpose.load(), dirn = d.direction.load();
    const bool playing = d.playing.load(), freeRunning = d.freeRunning.load(), direct = d.direct.load();
    const float bpm = d.bpm.load();
    const auto preset = proc.presetName();
    if (dirn != shownDirection)
    {
        shownDirection = dirn;
        const float a = dirn == dub::DirChance ? 1.0f : 0.35f;       // the chance controls only matter for Chance mix
        for (auto* n : { &runMin, &runMax, &wFwd, &wRev, &wPend, &wPing, &wRand, &wDrunk }) { n->slider.setAlpha (a); n->label.setAlpha (a); }
        repaint (orderNoteArea);
    }
    if (on != shownOn || acc != shownAccent || len != shownLength || step != shownStep || playing != shownPlaying || tr != shownTranspose
        || freeRunning != shownFree || direct != shownDirect || std::fabs (bpm - shownBpm) > 0.05f || preset != shownPreset)
    {
        shownOn = on; shownAccent = acc; shownLength = len; shownStep = step; shownPlaying = playing; shownTranspose = tr;
        shownFree = freeRunning; shownDirect = direct; shownBpm = bpm; shownPreset = preset;
        repaint (stepArea.expanded (8));
        repaint (statusArea.expanded (2, 4));
        repaint (presetNameArea);
    }
}

void DubStabPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Unofficial Dub Stab Plaits", 16, 10, 340, 28, juce::Justification::centredLeft);

    // preset name
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (kDim);
    g.drawText (shownPreset.isEmpty() ? juce::String ("(no preset chosen)") : shownPreset, presetNameArea, juce::Justification::centredLeft);

    // status
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (shownPlaying || shownDirect ? kAccent : kDim);
    juce::String t = shownDirect ? "DIRECT PLAY"
                   : shownFree ? "FREE-RUNNING  " + juce::String (shownBpm, 1) + " BPM"
                   : shownPlaying ? "PLAYING" : "STOPPED (press play)";
    g.drawText (t, statusArea, juce::Justification::centredRight);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kDim);
    if (! shownDirect && shownTranspose != 0)
        g.drawText ("transpose " + juce::String (shownTranspose > 0 ? "+" : "") + juce::String (shownTranspose) + " (played note)",
                    statusArea.translated (0, 16), juce::Justification::centredRight);

    // group captions
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (kAccent);
    g.drawText ("ECHO", 16, 172, 120, 14, juce::Justification::centredLeft);
    g.drawText ("REVERB", 430, 172, 120, 14, juce::Justification::centredLeft);
    g.drawText ("STEREO", 634, 172, 120, 14, juce::Justification::centredLeft);
    g.setColour (kPanel);
    g.fillRect (16, 188, 400, 1); g.fillRect (430, 188, 190, 1); g.fillRect (634, 188, 100, 1);

    // pattern
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Run lengths and chances apply to Chance mix only: it picks an order by its chance, keeps it for a run of steps, then picks again.",
                      orderNoteArea, juce::Justification::topLeft, 4);
    g.setColour (kPanel);
    g.fillRoundedRectangle (stepArea.expanded (6).toFloat(), 6.0f);
    const int cell = stepArea.getWidth() / 16, gap = 3, rowH = (stepArea.getHeight() - 3 * gap) / 4;
    const float alpha = shownDirect ? 0.3f : 1.0f;
    for (int i = 0; i < dub::DubSequencer::kMaxSteps; ++i)
    {
        const int row = i / 16, col = i % 16;
        auto r = juce::Rectangle<int> (stepArea.getX() + col * cell, stepArea.getY() + row * (rowH + gap), cell - 4, rowH).toFloat();
        const bool inPattern = i < shownLength;
        const bool isOn = (shownOn >> i) & 1u, isAcc = (shownAccent >> i) & 1u;
        g.setColour ((! inPattern ? juce::Colour (0xff262a30) : isOn ? (isAcc ? kAccent : kOn) : juce::Colour (0xff343a42)).withAlpha (alpha));
        g.fillRoundedRectangle (r, 3.0f);
        if (inPattern && i == shownStep && ! shownDirect)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
        }
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (shownDirect ? "Pattern off: the MIDI notes play the sound directly."
                            : "blue = plays, orange = plays accented, grey = rest.  Each row is a bar of 16 steps; Steps 1-16 use the first row.",
                stepArea.getX(), stepArea.getBottom() + 10, stepArea.getWidth(), 14, juce::Justification::centredLeft);

    g.drawFittedText ("Version " + juce::String (dub::versionString()) +
                      "  -  Not affiliated with Narti Live or Mutable Instruments. See the licenses folder for credits.",
                      16, getHeight() - 24, getWidth() - 32 - 96, 16, juce::Justification::centredLeft, 1);
}

void DubStabPanel::resized()
{
    const int left = 16, right = getWidth() - 16, fullW = right - left;

    presetButton.setBounds (360, 12, 100, 26);
    aboutButton.setBounds (right - 84, getHeight() - 30, 84, 24);
    about.setBounds (getLocalBounds());
    presetNameArea = { 468, 12, 200, 26 };
    statusArea = { right - 220, 10, 220, 16 };

    // row 1: model + sound knobs
    const int y1 = 56;
    model.label.setBounds (left, y1, 150, 14);
    model.box.setBounds (left, y1 + 16, 150, 26);
    {
        const int x0 = left + 150 + 12;
        const int kw = (right - x0) / 8;
        int x = x0;
        for (auto* k : { &harmonics, &timbre, &morph, &envelope, &resonance, &drift, &sub, &volume })
        {
            k->label.setBounds (x, y1, kw, 16);
            k->slider.setBounds (x, y1 + 16, kw, 96);
            x += kw;
        }
    }

    // row 2: echo, reverb, stereo
    const int y2 = 192;
    auto place = [&] (Knob& k, int x, int w) { k.label.setBounds (x, y2, w, 16); k.slider.setBounds (x, y2 + 16, w, 96); };
    place (echoMix, left, 88);
    echoTime.label.setBounds (left + 96, y2 + 24, 110, 14);
    echoTime.box.setBounds (left + 96, y2 + 40, 110, 26);
    place (echoFb, left + 214, 88);
    place (echoTone, left + 310, 88);
    place (reverbMix, 430, 88);
    place (reverbDecay, 526, 88);
    place (width, 634, 100);

    // row 3: harmony menus
    const int cw = (fullW - 3 * 12) / 4;
    const int y3 = 312;
    {
        int x = left;
        for (auto* c : { &root, &scale, &voicing, &rate })
        {
            c->label.setBounds (x, y3, cw, 14);
            c->box.setBounds (x, y3 + 16, cw, 26);
            x += cw + 12;
        }
    }
    // row 4: numbers
    const int y4 = 360;
    {
        int x = left;
        for (auto* n : { &density, &steps, &octave, &seed })
        {
            n->label.setBounds (x, y4, cw, 14);
            n->slider.setBounds (x, y4 + 16, cw, 26);
            x += cw + 12;
        }
    }
    // row 5: melody, buttons, switches
    const int y5 = 408;
    melody.label.setBounds (left, y5, cw, 14);
    melody.slider.setBounds (left, y5 + 16, cw, 26);
    {
        int x = left + cw + 12;
        newPattern.setBounds (x, y5 + 14, 118, 28); x += 124;
        newMelody.setBounds (x, y5 + 14, 118, 28); x += 124;
        ownMelody.setBounds (x, y5 + 14, 138, 28); x += 150;
        chords.setBounds (x, y5 + 14, 84, 28); x += 90;
        minorOnly.setBounds (x, y5 + 14, right - x, 28);
    }

    // row 6: how MIDI and the clock behave
    const int y6 = 464;
    midiMode.label.setBounds (left, y6, cw, 14);
    midiMode.box.setBounds (left, y6 + 16, cw, 26);
    freeRun.setBounds (left + cw + 12, y6 + 16, 210, 26);
    freeBpm.label.setBounds (left + cw + 12 + 222, y6, cw, 14);
    freeBpm.slider.setBounds (left + cw + 12 + 222, y6 + 16, cw, 26);

    // row 7: the order of the steps
    const int y7a = 520;
    direction.label.setBounds (left, y7a, cw, 14);
    direction.box.setBounds (left, y7a + 16, cw, 26);
    runMin.label.setBounds (left + (cw + 12), y7a, cw, 14);
    runMin.slider.setBounds (left + (cw + 12), y7a + 16, cw, 26);
    runMax.label.setBounds (left + 2 * (cw + 12), y7a, cw, 14);
    runMax.slider.setBounds (left + 2 * (cw + 12), y7a + 16, cw, 26);
    orderNoteArea = { left + 3 * (cw + 12), y7a, cw, 50 };
    // row 8: the chances
    const int y7b = 570;
    {
        const int ww = (fullW - 5 * 10) / 6;
        int x = left;
        for (auto* n : { &wFwd, &wRev, &wPend, &wPing, &wRand, &wDrunk })
        {
            n->label.setBounds (x, y7b, ww, 14);
            n->slider.setBounds (x, y7b + 16, ww, 26);
            x += ww + 10;
        }
    }

    // row 9: the pattern, and the MIDI file
    const int y7 = 634;
    stepArea = { left, y7, 16 * 33, 84 };
    dragMidi.setBounds (right - 316, y7, 150, 34);
    saveMidi.setBounds (right - 158, y7, 158, 34);
    exportBars.label.setBounds (right - 316, y7 + 50, 150, 14);
    exportBars.box.setBounds (right - 316, y7 + 66, 150, 26);
    exportLen.label.setBounds (right - 158, y7 + 50, 158, 14);
    exportLen.box.setBounds (right - 158, y7 + 66, 158, 26);
}

// ---------------------------------------------------------------------------------------------- presets and MIDI file

void DubStabPanel::showPresetMenu()
{
    juce::PopupMenu menu, factoryMenu, userMenu;
    const auto& list = presets::factory();
    const auto current = proc.presetName();
    for (int i = 0; i < (int) list.size(); ++i)
        factoryMenu.addItem (100 + i, list[(size_t) i].name, true, current == list[(size_t) i].name);
    menu.addSubMenu ("Factory presets", factoryMenu);

    auto files = presets::userFiles (proc.userPresetFolder());
    if (files.isEmpty()) userMenu.addItem (9999, "(none saved yet)", false, false);
    for (int i = 0; i < files.size(); ++i)
        userMenu.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, current == files[i].getFileNameWithoutExtension());
    menu.addSubMenu ("My presets", userMenu);
    menu.addSeparator();
    menu.addItem (2000, "Save the current sound as...");
    menu.addItem (2001, "Show the presets folder");

    juce::Component::SafePointer<DubStabPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, files] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 100 && r < 100 + (int) presets::factory().size()) safe->proc.applyFactoryPreset (r - 100);
        else if (r >= 1000 && r < 1000 + files.size()) safe->proc.loadUserPreset (files[r - 1000]);
        else if (r == 2000) safe->askPresetName();
        else if (r == 2001)
        {
            const auto dir = safe->proc.userPresetFolder();
            dir.createDirectory();
            dir.revealToUser();
        }
    });
}

void DubStabPanel::askPresetName()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name for the preset (it is kept in your Application Support folder):",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presetName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DubStabPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = presets::cleanName (w->getTextEditorContents ("name"));
        if (name.isEmpty()) return;
        const auto target = safe->proc.userPresetFolder().getChildFile (name + ".dubstabpreset");
        auto doSave = [safe, name] { if (safe != nullptr) safe->proc.saveUserPreset (name); };
        if (! target.existsAsFile()) { doSave(); return; }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                          "Replace preset?", "A preset called \"" + name + "\" already exists.", "Replace", "Cancel", safe.getComponent()),
                                      [doSave] (int ok) { if (ok == 1) doSave(); });
    }), true);
}

juce::File DubStabPanel::exportFile()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Unofficial Dub Stab Plaits");
    const auto f = dir.getChildFile (presets::cleanName (proc.patternMidiName()) + ".mid");
    return proc.writePatternMidi (f) ? f : juce::File();
}

void DubStabPanel::saveMidiDialog()
{
    const auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                           .getChildFile (presets::cleanName (proc.patternMidiName()) + ".mid");
    chooser = std::make_unique<juce::FileChooser> ("Save the pattern as a MIDI file", start, "*.mid");
    juce::Component::SafePointer<DubStabPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe != nullptr && f != juce::File()) safe->proc.writePatternMidi (f.withFileExtension ("mid"));
    });
}

// ============================================================================================ the editor window

DubStabEditor::DubStabEditor (DubStabAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), panel (std::make_unique<DubStabPanel> (p))
{
    addAndMakeVisible (*panel);
    panel->setBounds (0, 0, kBaseWidth, kBaseHeight);
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 6 / 10, kBaseHeight * 6 / 10, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
    const int saved = (int) proc.apvts.state.getProperty ("uiWidth", kBaseWidth);
    const int w = std::clamp (saved, kBaseWidth * 6 / 10, kBaseWidth * 2);
    setSize (w, (int) std::lround ((double) w * kBaseHeight / kBaseWidth));
}

DubStabEditor::~DubStabEditor() = default;

juce::File DubStabEditor::makeDragFile() { return panel->makeMidiFileForDrag(); }
void DubStabEditor::showAbout (bool show) { panel->showAbout (show); }
bool DubStabEditor::aboutShown() const { return panel->aboutShown(); }
juce::String DubStabEditor::aboutTextShown() const { return panel->aboutTextShown(); }

int DubStabEditor::countVisibleControls() const
{
    int n = 0;
    for (auto* c : panel->getChildren()) if (c->isVisible()) ++n;
    return n;
}

void DubStabEditor::paint (juce::Graphics& g) { g.fillAll (kBg); }

void DubStabEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseWidth;
    panel->setBounds (0, 0, kBaseWidth, kBaseHeight);
    panel->setTransform (juce::AffineTransform::scale (s));
    proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}
