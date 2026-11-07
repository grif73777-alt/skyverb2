#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace sky
{
constexpr int kNumPresetParams = 14;

// Parameter order used by FactoryPreset::v
inline const char* const kPresetIds[kNumPresetParams] = {
    "size", "decay", "predelay", "damping", "diffusion", "modulation", "lowcut", "highcut", "width",
    "lossy_bitrate", "lossy_amount", "lossy_target", "mix", "output"
};

struct FactoryPreset
{
    const char* name;
    float v[kNumPresetParams];
};

//                              size  decay  pre  damp diff mod  low   high   width kbps amt tgt mix out
inline const FactoryPreset kFactory[] = {
    { "Init",              {  50,  2.5f,  15,  40,  70,  30,  100, 14000, 100,  96,   0, 0, 30, 0 } },
    { "Small Room",        {  15,  0.6f,   3,  55,  60,  10,  150, 12000,  80,  96,   0, 0, 25, 0 } },
    { "Tight Plate",       {  35,  1.4f,   0,  25,  90,  20,  200, 16000, 110,  96,   0, 0, 28, 0 } },
    { "Warm Hall",         {  70,  3.2f,  25,  55,  75,  35,  120,  9000, 120,  96,   0, 0, 30, 0 } },
    { "Dark Vocal Space",  {  55,  2.2f,  60,  75,  70,  30,  250,  6000, 100,  96,   0, 0, 22, 0 } },
    { "Cathedral",         { 100,  9.0f,  40,  45,  80,  45,  150, 11000, 130,  96,   0, 0, 35, 0 } },
    { "Endless Sky",       {  90, 22.0f,  60,  20,  85,  60,  200, 15000, 150,  96,   0, 0, 40, 0 } },
    { "MP3 Memory",        {  60,  3.0f,  20,  40,  70,  35,  100, 14000, 100,  64, 100, 0, 35, 0 } },
    { "Dial-Up Cloud",     {  75,  5.0f,  30,  30,  80,  50,  150, 12000, 120,  24, 100, 0, 40, 0 } },
    { "Glitch Hall",       {  85,  6.5f,  35,  25,  85,  70,  120, 13000, 140,  16,  85, 0, 45, 0 } },
    { "Crushed Mix",       {  40,  1.8f,  10,  35,  70,  25,  100, 14000, 100,  32,  70, 1, 25, 0 } },
    { "Broken Radio",      {  30,  1.2f,   5,  70,  60,  20,  300,  5000,  60,  12, 100, 1, 30, 0 } },
};

inline int numFactory() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }

// Factory presets live in the code above. User presets are small XML files in
//   Windows: %APPDATA%\skyverb\Presets\*.skyvpreset
// "Dirty" (edited since load) is computed by comparing the live parameters to what the
// preset contained, so it needs no listeners and is safe on any thread arrangement.
class PresetManager
{
public:
    struct Current
    {
        juce::String name;
        bool factory = false;
        bool valid = false; // false = "Custom" (no preset, or preset no longer exists)
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) { snapshotFromCurrent(); }

    static juce::File rootDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("skyverb");
    }
    static juce::File presetsDir() { return rootDir().getChildFile ("Presets"); }

    static bool loadDarkPref()
    {
        const auto f = rootDir().getChildFile ("settings.txt");
        return f.existsAsFile() && f.loadFileAsString().trim() == "dark";
    }

    static void saveDarkPref (bool dark)
    {
        const auto d = rootDir();
        if (d.createDirectory().wasOk())
            d.getChildFile ("settings.txt").replaceWithText (dark ? "dark" : "light");
    }

    const Current& getCurrent() const { return current; }
    juce::String getDisplayName() const { return current.valid ? current.name : juce::String ("Custom"); }

    juce::StringArray getUserNames() const
    {
        juce::StringArray names;
        for (const auto& f : presetsDir().findChildFiles (juce::File::findFiles, false, "*.skyvpreset"))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

    bool isDirty() const
    {
        if (! current.valid)
            return false;
        for (int k = 0; k < kNumPresetParams; ++k)
            if (auto* p = apvts.getParameter (kPresetIds[k]))
                if (std::abs (p->getValue() - snapshot[(size_t) k]) > 0.001f)
                    return true;
        return false;
    }

    bool loadFactory (int index)
    {
        if (index < 0 || index >= numFactory())
            return false;

        Values v {};
        Flags has {};
        for (int k = 0; k < kNumPresetParams; ++k) { v[(size_t) k] = kFactory[index].v[k]; has[(size_t) k] = true; }

        apply (v, has);
        current = { kFactory[index].name, true, true };
        return true;
    }

    bool loadUser (const juce::String& name)
    {
        Values v {};
        Flags has {};
        if (! readUser (name, v, has))
            return false;

        apply (v, has);
        current = { name, false, true };
        return true;
    }

    // Returns the final (sanitised) name, or an empty string on failure.
    juce::String saveUser (const juce::String& rawName)
    {
        const auto dir = presetsDir();
        if (! dir.createDirectory().wasOk())
            return {};

        auto name = juce::File::createLegalFileName (rawName.trim()).substring (0, 32).trimCharactersAtEnd (". ").trim();
        if (name.isEmpty())
        {
            int n = 1;
            while (dir.getChildFile ("User " + juce::String (n) + ".skyvpreset").existsAsFile())
                ++n;
            name = "User " + juce::String (n);
        }

        juce::XmlElement xml ("SKYVERB_PRESET");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (int k = 0; k < kNumPresetParams; ++k)
        {
            if (auto* p = apvts.getParameter (kPresetIds[k]))
            {
                auto* e = xml.createNewChildElement ("P");
                e->setAttribute ("id", kPresetIds[k]);
                e->setAttribute ("v", (double) p->convertFrom0to1 (p->getValue()));
            }
        }

        if (! xml.writeTo (dir.getChildFile (name + ".skyvpreset")))
            return {};

        current = { name, false, true };
        snapshotFromCurrent();
        return name;
    }

    bool deleteUser (const juce::String& name)
    {
        const bool ok = presetsDir().getChildFile (name + ".skyvpreset").deleteFile();
        if (ok && current.valid && ! current.factory && current.name == name)
            current.valid = false;
        return ok;
    }

    // Re-attach to a preset by name without changing any parameter (used when the host restores a project).
    void restore (const juce::String& name, bool factory)
    {
        current = {};
        Values v {};
        Flags has {};

        if (factory)
        {
            for (int i = 0; i < numFactory(); ++i)
            {
                if (name == kFactory[i].name)
                {
                    for (int k = 0; k < kNumPresetParams; ++k) { v[(size_t) k] = kFactory[i].v[k]; has[(size_t) k] = true; }
                    current = { name, true, true };
                    break;
                }
            }
        }
        else if (readUser (name, v, has))
        {
            current = { name, false, true };
        }

        if (current.valid)
        {
            for (int k = 0; k < kNumPresetParams; ++k)
            {
                auto* p = apvts.getParameter (kPresetIds[k]);
                if (p != nullptr && has[(size_t) k])
                    snapshot[(size_t) k] = p->convertTo0to1 (v[(size_t) k]);
                else if (p != nullptr)
                    snapshot[(size_t) k] = p->getValue();
            }
        }
        else
        {
            snapshotFromCurrent();
        }
    }

    void clear()
    {
        current = {};
        snapshotFromCurrent();
    }

private:
    using Values = std::array<float, (size_t) kNumPresetParams>;
    using Flags = std::array<bool, (size_t) kNumPresetParams>;

    void apply (const Values& v, const Flags& has)
    {
        for (int k = 0; k < kNumPresetParams; ++k)
        {
            if (! has[(size_t) k])
                continue;
            if (auto* p = apvts.getParameter (kPresetIds[k]))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v[(size_t) k]));
                p->endChangeGesture();
            }
        }
        snapshotFromCurrent();
    }

    bool readUser (const juce::String& name, Values& v, Flags& has) const
    {
        const auto file = presetsDir().getChildFile (name + ".skyvpreset");
        if (! file.existsAsFile())
            return false;

        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("SKYVERB_PRESET"))
            return false;

        for (auto* e : xml->getChildWithTagNameIterator ("P"))
        {
            const auto id = e->getStringAttribute ("id");
            for (int k = 0; k < kNumPresetParams; ++k)
            {
                if (id == kPresetIds[k])
                {
                    v[(size_t) k] = (float) e->getDoubleAttribute ("v");
                    has[(size_t) k] = true;
                }
            }
        }
        return true;
    }

    void snapshotFromCurrent()
    {
        for (int k = 0; k < kNumPresetParams; ++k)
            if (auto* p = apvts.getParameter (kPresetIds[k]))
                snapshot[(size_t) k] = p->getValue();
    }

    juce::AudioProcessorValueTreeState& apvts;
    Current current;
    std::array<float, (size_t) kNumPresetParams> snapshot {};
};
} // namespace sky
