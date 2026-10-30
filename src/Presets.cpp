#include "Presets.h"
#include <cmath>

namespace presets {

namespace {
// Setup parameters: not part of a preset.
bool isSetup (const juce::String& id)
{
    return id == "midi_mode" || id == "free_run" || id == "free_bpm" || id == "export_bars" || id == "export_len";
}

void setPlain (juce::RangedAudioParameter& p, float plain)
{
    if (! std::isfinite (plain)) return;
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
    p.endChangeGesture();
}

void setDefault (juce::RangedAudioParameter& p)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.getDefaultValue());
    p.endChangeGesture();
}
} // namespace

bool isPresetParameter (const juce::String& id) { return ! isSetup (id); }

// Index reminders.  Rate: 0 1/2, 1 1/4, 2 1/8, 3 1/8T, 4 1/16, 5 1/16T, 6 1/32, 7 1/32T.  Voicing: 0 Dub, 1 Triads, 2 Sevenths,
// 3 Mixed, 4 Deep.  Echo time: 0 1/16, 1 1/8T, 2 1/8, 3 1/8 dotted, 4 1/4, 5 1/4 dotted, 6 1/2.  Models: 0 DUB SAW, 1 VA,
// 2 WAVESHAPE, 3 FM, 4 PHASE DIST, 5 CHIPTUNE, 6 STRINGS, 7 ACID.
// These are starting points chosen from what each control does in the code; they have not been listened to.
const std::vector<Factory>& factory()
{
    static const std::vector<Factory> list = {
        { "Default", {} },
        { "Dry (no effects)", { { "echo_mix", 0.0f }, { "reverb_mix", 0.0f } } },
        { "Classic Dub Stab", {
            { "model", 0 }, { "harmonics", 0.45f }, { "timbre", 0.40f }, { "morph", 0.35f }, { "envelope", 0.30f }, { "resonance", 0.40f },
            { "voicing", 0 }, { "rate", 2 }, { "density", 4 }, { "steps", 16 }, { "seed", 1 },
            { "echo_mix", 0.45f }, { "echo_time", 3 }, { "echo_fb", 0.55f }, { "echo_tone", 0.40f },
            { "reverb_mix", 0.25f }, { "reverb_decay", 3.5f }, { "sub", 0.45f } } },
        { "Deep Chords", {
            { "model", 1 }, { "envelope", 0.45f }, { "resonance", 0.30f }, { "voicing", 4 }, { "rate", 1 }, { "density", 3 },
            { "steps", 8 }, { "octave", 2 }, { "echo_mix", 0.35f }, { "echo_time", 4 }, { "echo_fb", 0.50f },
            { "reverb_mix", 0.30f }, { "reverb_decay", 4.5f }, { "sub", 0.60f } } },
        { "Short Plucks", {
            { "model", 2 }, { "envelope", 0.08f }, { "resonance", 0.25f }, { "voicing", 1 }, { "rate", 4 }, { "density", 9 },
            { "steps", 16 }, { "echo_mix", 0.30f }, { "echo_time", 2 }, { "echo_fb", 0.40f },
            { "reverb_mix", 0.15f }, { "reverb_decay", 1.5f } } },
        { "Slow Swell Pad", {
            { "model", 6 }, { "envelope", 0.85f }, { "resonance", 0.20f }, { "drift", 0.30f }, { "voicing", 2 }, { "rate", 0 },
            { "density", 2 }, { "steps", 8 }, { "echo_mix", 0.25f }, { "echo_time", 5 }, { "echo_fb", 0.50f },
            { "reverb_mix", 0.45f }, { "reverb_decay", 7.0f }, { "width", 1.4f } } },
        { "FM Bells", {
            { "model", 3 }, { "harmonics", 0.55f }, { "timbre", 0.60f }, { "morph", 0.20f }, { "envelope", 0.15f }, { "drift", 0.25f },
            { "voicing", 2 }, { "rate", 4 }, { "density", 6 }, { "steps", 12 }, { "echo_mix", 0.40f }, { "echo_time", 5 },
            { "echo_fb", 0.50f }, { "reverb_mix", 0.30f }, { "reverb_decay", 3.5f }, { "width", 1.3f } } },
        { "Phase Dub Sub", {
            { "model", 4 }, { "envelope", 0.25f }, { "density", 5 }, { "octave", 2 }, { "sub", 0.70f },
            { "echo_mix", 0.35f }, { "echo_time", 3 }, { "reverb_mix", 0.20f } } },
        { "Chiptune Run", {
            { "model", 5 }, { "envelope", 0.10f }, { "voicing", 1 }, { "chords", 0 }, { "rate", 4 }, { "density", 10 },
            { "steps", 12 }, { "echo_mix", 0.30f }, { "echo_time", 2 }, { "reverb_mix", 0.10f }, { "reverb_decay", 1.2f } } },
        { "Acid Line", {
            { "model", 7 }, { "harmonics", 0.45f }, { "timbre", 0.55f }, { "morph", 0.60f }, { "envelope", 0.55f }, { "rate", 4 },
            { "density", 12 }, { "steps", 16 }, { "octave", 2 }, { "echo_mix", 0.25f }, { "echo_time", 3 }, { "echo_fb", 0.40f },
            { "reverb_mix", 0.10f }, { "reverb_decay", 1.5f } } },
        { "Wide Space", {
            { "model", 0 }, { "envelope", 0.60f }, { "resonance", 0.35f }, { "rate", 1 }, { "density", 4 },
            { "echo_mix", 0.50f }, { "echo_time", 3 }, { "echo_fb", 0.60f }, { "echo_tone", 0.35f },
            { "reverb_mix", 0.50f }, { "reverb_decay", 8.0f }, { "width", 1.8f } } },
    };
    return list;
}

void apply (juce::AudioProcessorValueTreeState& apvts, const std::vector<Value>& values)
{
    for (auto* prm : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (isPresetParameter (r->paramID)) setDefault (*r);
    for (const auto& v : values)
        if (auto* r = apvts.getParameter (v.id)) setPlain (*r, v.value);
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("particledots").getChildFile ("Unofficial Dub Stab Plaits").getChildFile ("Presets");
}

juce::Array<juce::File> userFiles (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.dubstabpreset");
    files.sort();
    return files;
}

juce::String cleanName (const juce::String& typed)
{
    auto n = juce::File::createLegalFileName (typed.trim()).trim();
    while (n.startsWithChar ('.')) n = n.substring (1);
    return n.substring (0, 60);
}

bool saveUser (juce::AudioProcessorValueTreeState& apvts, const juce::String& typedName, const juce::File& folder, juce::File* written)
{
    const auto name = cleanName (typedName);
    if (name.isEmpty()) return false;
    if (! folder.createDirectory().wasOk()) return false;
    juce::XmlElement xml ("DubStabPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (auto* prm : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (isPresetParameter (r->paramID))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", r->paramID);
                e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue()));
            }
    const auto file = folder.getChildFile (name + ".dubstabpreset");
    if (! xml.writeTo (file)) return false;
    if (written != nullptr) *written = file;
    return true;
}

bool loadUser (juce::AudioProcessorValueTreeState& apvts, const juce::File& file, juce::String* nameOut)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("DubStabPreset")) return false;
    std::vector<std::pair<juce::String, float>> found;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (id.isEmpty() || ! isPresetParameter (id) || apvts.getParameter (id) == nullptr) continue;
        found.push_back ({ id, (float) e->getDoubleAttribute ("value") });
    }
    if (found.empty()) return false;
    for (auto* prm : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (isPresetParameter (r->paramID)) setDefault (*r);
    for (const auto& kv : found)
        if (auto* r = apvts.getParameter (kv.first)) setPlain (*r, kv.second);
    if (nameOut != nullptr) *nameOut = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    return true;
}

} // namespace presets
