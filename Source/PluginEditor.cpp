#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780;
constexpr int kHeight = 510;
constexpr int kMargin = 36;
constexpr int kRowH = 32;
constexpr int kLabelW = 96;
constexpr int kValueW = 78;

constexpr int kLeftX = kMargin;
constexpr int kLeftW = 404;
constexpr int kRightX = 496;
constexpr int kRightW = kWidth - kMargin - kRightX;

constexpr int kPresetY = 82;
constexpr int kPresetH = 26;

constexpr int kSectionY = 134;
constexpr int kRowsTop = 160;
constexpr int kMasterY = kRowsTop + 3 * kRowH + 26;
constexpr int kMasterRowsTop = kMasterY + 26;

constexpr int kThemeW = 130;

// exponential approach, frame-rate independent
inline float approach (float current, float target, float rate, float dt, float snap)
{
    const float next = current + (target - current) * (1.0f - std::exp (-rate * dt));
    return std::abs (target - next) < snap ? target : next;
}
} // namespace

//==============================================================================
// One line: LABEL  [slider]  value
// The handle glides to its target and the number counts along with it.
class SkyverbAudioProcessorEditor::SliderRow : public juce::Component
{
public:
    SliderRow (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& labelText,
               const SkyLookAndFeel& l)
        : laf (l), label (labelText.toUpperCase())
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setScrollWheelEnabled (true);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        disp = (float) slider.valueToProportionOfLength (slider.getValue());
        publish();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (kLabelW);
        r.removeFromRight (kValueW);
        slider.setBounds (r);
    }

    void tick (float dt)
    {
        const float target = (float) slider.valueToProportionOfLength (slider.getValue());
        const float rate = slider.isMouseButtonDown() ? 40.0f : 12.0f;
        const float newDisp = approach (disp, target, rate, dt, 0.0004f);
        const float newHover = approach (hover, slider.isMouseOverOrDragging() ? 1.0f : 0.0f, 18.0f, dt, 0.01f);

        if (newDisp != disp || newHover != hover)
        {
            disp = newDisp;
            hover = newHover;
            publish();
            slider.repaint();
            repaint (getLocalBounds().removeFromRight (kValueW));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = laf.theme;
        auto r = getLocalBounds();
        g.setColour (t.grey);
        g.setFont (sky::ui::makeFont (10.5f, false, 0.16f));
        g.drawText (label, r.removeFromLeft (kLabelW), juce::Justification::centredLeft, false);

        const double shown = slider.proportionOfLengthToValue (juce::jlimit (0.0, 1.0, (double) disp));
        g.setColour (t.ink);
        g.setFont (sky::ui::makeFont (12.0f));
        g.drawText (slider.getTextFromValue (shown), getLocalBounds().removeFromRight (kValueW),
                    juce::Justification::centredRight, false);
    }

private:
    void publish()
    {
        slider.getProperties().set ("disp", (double) disp);
        slider.getProperties().set ("hover", (double) hover);
    }

    const SkyLookAndFeel& laf;
    juce::String label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float disp = 0.0f, hover = 0.0f;
};

//==============================================================================
// Flat selector. The filled block slides between options.
class SkyverbAudioProcessorEditor::Segmented : public juce::Component
{
public:
    Segmented (const SkyLookAndFeel& l, juce::StringArray opts) : laf (l), options (std::move (opts))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onChange; // only fired by user clicks

    void setIndex (int i, bool animate)
    {
        index = juce::jlimit (0, options.size() - 1, i);
        if (! animate)
            pos = (float) index;
        repaint();
    }

    void tick (float dt)
    {
        const float newPos = approach (pos, (float) index, 16.0f, dt, 0.003f);
        if (newPos != pos)
        {
            pos = newPos;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = laf.theme;
        const int n = options.size();
        const float w = (float) getWidth() / (float) n;

        g.setColour (t.bar);
        g.fillRect (juce::Rectangle<float> (pos * w, 0.0f, w, (float) getHeight()));
        g.drawRect (getLocalBounds().toFloat(), 1.5f);

        for (int i = 0; i < n; ++i)
        {
            const float cover = juce::jmax (0.0f, 1.0f - std::abs ((float) i - pos));
            g.setColour (t.ink.interpolatedWith (t.paper, cover));
            g.setFont (sky::ui::makeFont (10.5f, cover > 0.5f, 0.2f));
            g.drawText (options[i], juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, options.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) options.size()));
        if (i != index)
        {
            setIndex (i, true);
            if (onChange)
                onChange (i);
        }
    }

private:
    const SkyLookAndFeel& laf;
    juce::StringArray options;
    int index = 0;
    float pos = 0.0f;
};

//==============================================================================
// Flat button. Fills in on hover.
class SkyverbAudioProcessorEditor::Btn : public juce::Component
{
public:
    enum Kind { Text, Prev, Next, Name };

    Btn (const SkyLookAndFeel& l, Kind k, juce::String t = {}) : laf (l), kind (k), text (std::move (t))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClick;

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void tick (float dt)
    {
        const float newHover = approach (hover, isMouseOver() ? 1.0f : 0.0f, 18.0f, dt, 0.01f);
        if (newHover != hover)
        {
            hover = newHover;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = laf.theme;
        const auto r = getLocalBounds().toFloat().reduced (0.75f);

        g.setColour (t.bar.withAlpha (hover));
        g.fillRect (r);
        g.setColour (t.bar);
        g.drawRect (r, 1.5f);

        const auto fg = t.ink.interpolatedWith (t.paper, hover);
        g.setColour (fg);

        const float cx = r.getCentreX(), cy = r.getCentreY();
        if (kind == Prev || kind == Next)
        {
            const float d = kind == Prev ? 1.0f : -1.0f;
            juce::Path p;
            p.startNewSubPath (cx + 2.5f * d, cy - 5.0f);
            p.lineTo (cx - 2.5f * d, cy);
            p.lineTo (cx + 2.5f * d, cy + 5.0f);
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }
        else if (kind == Name)
        {
            g.setFont (sky::ui::makeFont (12.0f));
            g.drawText (text, getLocalBounds().withTrimmedLeft (12).withTrimmedRight (28),
                        juce::Justification::centredLeft, true);

            juce::Path tri;
            const float tx = r.getRight() - 14.0f;
            tri.addTriangle (tx - 4.0f, cy - 2.0f, tx + 4.0f, cy - 2.0f, tx, cy + 3.0f);
            g.fillPath (tri);
        }
        else
        {
            g.setFont (sky::ui::makeFont (10.5f, true, 0.2f));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    const SkyLookAndFeel& laf;
    Kind kind;
    juce::String text;
    float hover = 0.0f;
};

//==============================================================================
SkyverbAudioProcessorEditor::SkyverbAudioProcessorEditor (SkyverbAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // theme: project state first, otherwise the last one the user picked
    const auto saved = processor.apvts.state.getProperty ("theme", "").toString();
    const bool dark = saved.isNotEmpty() ? saved == "dark" : sky::PresetManager::loadDarkPref();
    themeMix = themeTarget = dark ? 1.0f : 0.0f;
    laf.setTheme (sky::ui::mixThemes (sky::ui::lightTheme(), sky::ui::darkTheme(), themeMix));

    setLookAndFeel (&laf);

    auto& s = processor.apvts;
    auto add = [&] (const char* id, const char* label)
    {
        rows.push_back (std::make_unique<SliderRow> (s, id, label, laf));
        addAndMakeVisible (*rows.back());
    };

    // reverb (0-8)
    add ("size", "Size");
    add ("decay", "Decay");
    add ("predelay", "Pre-delay");
    add ("damping", "Damping");
    add ("diffusion", "Diffusion");
    add ("modulation", "Modulation");
    add ("lowcut", "Low cut");
    add ("highcut", "High cut");
    add ("width", "Width");
    // lossy (9-10)
    add ("lossy_bitrate", "Bitrate");
    add ("lossy_amount", "Amount");
    // master (11-12)
    add ("mix", "Mix");
    add ("output", "Output");

    // lossy target switch, bound to the parameter
    targetSwitch = std::make_unique<Segmented> (laf, juce::StringArray { "WET ONLY", "ALL" });
    addAndMakeVisible (*targetSwitch);
    auto* targetParam = s.getParameter ("lossy_target");
    targetAttachment = std::make_unique<juce::ParameterAttachment> (*targetParam,
        [this] (float v) { targetSwitch->setIndex (juce::roundToInt (v), true); }, nullptr);
    targetSwitch->onChange = [this] (int i) { targetAttachment->setValueAsCompleteGesture ((float) i); };
    targetAttachment->sendInitialUpdate();
    targetSwitch->setIndex (juce::roundToInt (targetParam->convertFrom0to1 (targetParam->getValue())), false);

    // light / dark
    themeSwitch = std::make_unique<Segmented> (laf, juce::StringArray { "LIGHT", "DARK" });
    themeSwitch->setIndex (dark ? 1 : 0, false);
    themeSwitch->onChange = [this] (int i) { setDark (i == 1); };
    addAndMakeVisible (*themeSwitch);

    // presets
    prevBtn = std::make_unique<Btn> (laf, Btn::Prev);
    nextBtn = std::make_unique<Btn> (laf, Btn::Next);
    nameBtn = std::make_unique<Btn> (laf, Btn::Name);
    saveBtn = std::make_unique<Btn> (laf, Btn::Text, "SAVE");
    prevBtn->onClick = [this] { stepPreset (-1); };
    nextBtn->onClick = [this] { stepPreset (+1); };
    nameBtn->onClick = [this] { showPresetMenu(); };
    saveBtn->onClick = [this]
    {
        if (editingName) commitSave();
        else beginSave();
    };
    for (auto* b : { prevBtn.get(), nextBtn.get(), nameBtn.get(), saveBtn.get() })
        addAndMakeVisible (*b);

    nameEditor.setMultiLine (false);
    nameEditor.setReturnKeyStartsNewLine (false);
    nameEditor.setScrollbarsShown (false);
    nameEditor.setFont (sky::ui::makeFont (12.0f));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (12, 0);
    nameEditor.setBorder ({ 0, 0, 0, 0 });
    nameEditor.setInputRestrictions (32);
    nameEditor.onReturnKey = [this] { commitSave(); };
    nameEditor.onEscapeKey = [this] { cancelSave(); };
    addChildComponent (nameEditor);

    applyTheme();
    refreshPresetLabel();

    setSize (kWidth, kHeight);
    setResizable (false, false);

    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

SkyverbAudioProcessorEditor::~SkyverbAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void SkyverbAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = juce::jlimit (0.001f, 0.05f, (float) (now - lastTick));
    lastTick = now;

    if (themeMix != themeTarget)
    {
        themeMix = approach (themeMix, themeTarget, 10.0f, dt, 0.003f);
        applyTheme();
    }

    for (auto& r : rows)
        r->tick (dt);

    targetSwitch->tick (dt);
    themeSwitch->tick (dt);
    for (auto* b : { prevBtn.get(), nextBtn.get(), nameBtn.get(), saveBtn.get() })
        b->tick (dt);

    refreshPresetLabel();
}

void SkyverbAudioProcessorEditor::applyTheme()
{
    laf.setTheme (sky::ui::mixThemes (sky::ui::lightTheme(), sky::ui::darkTheme(), themeMix));
    const auto& t = laf.theme;

    nameEditor.setColour (juce::TextEditor::backgroundColourId, t.paper);
    nameEditor.setColour (juce::TextEditor::textColourId, t.ink);
    nameEditor.setColour (juce::TextEditor::highlightColourId, t.ink.withAlpha (0.22f));
    nameEditor.setColour (juce::TextEditor::highlightedTextColourId, t.ink);
    nameEditor.setColour (juce::TextEditor::outlineColourId, t.bar);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, t.bar);
    nameEditor.setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    nameEditor.setColour (juce::CaretComponent::caretColourId, t.ink);

    repaint();
}

void SkyverbAudioProcessorEditor::setDark (bool dark)
{
    themeTarget = dark ? 1.0f : 0.0f;
    processor.apvts.state.setProperty ("theme", dark ? "dark" : "light", nullptr);
    sky::PresetManager::saveDarkPref (dark);
}

//==============================================================================
void SkyverbAudioProcessorEditor::refreshPresetLabel()
{
    if (editingName)
        return;

    auto label = processor.presets.getDisplayName();
    if (processor.presets.isDirty())
        label << "  *";

    if (label != shownLabel)
    {
        shownLabel = label;
        nameBtn->setText (label);
    }
}

void SkyverbAudioProcessorEditor::stepPreset (int direction)
{
    if (editingName)
        cancelSave();

    auto& pm = processor.presets;
    const int nf = sky::numFactory();
    const auto users = pm.getUserNames();
    const int total = nf + users.size();
    if (total <= 0)
        return;

    int idx = direction > 0 ? -1 : total; // nothing selected -> first / last
    const auto& cur = pm.getCurrent();
    if (cur.valid)
    {
        if (cur.factory)
        {
            for (int i = 0; i < nf; ++i)
                if (cur.name == sky::kFactory[i].name)
                    idx = i;
        }
        else
        {
            const int u = users.indexOf (cur.name);
            if (u >= 0)
                idx = nf + u;
        }
    }

    idx = ((idx + direction) % total + total) % total;
    if (idx < nf)
        pm.loadFactory (idx);
    else
        pm.loadUser (users[idx - nf]);

    refreshPresetLabel();
}

void SkyverbAudioProcessorEditor::showPresetMenu()
{
    if (editingName)
        cancelSave();

    auto& pm = processor.presets;
    const auto& cur = pm.getCurrent();
    const auto users = pm.getUserNames();

    juce::PopupMenu m;
    m.addSectionHeader ("FACTORY");
    for (int i = 0; i < sky::numFactory(); ++i)
        m.addItem (1 + i, sky::kFactory[i].name, true, cur.valid && cur.factory && cur.name == sky::kFactory[i].name);

    m.addSeparator();
    m.addSectionHeader ("USER");
    if (users.isEmpty())
        m.addItem (999, "(none saved yet)", false, false);
    for (int i = 0; i < users.size(); ++i)
        m.addItem (100 + i, users[i], true, cur.valid && ! cur.factory && cur.name == users[i]);

    m.addSeparator();
    m.addItem (900, "Save as...");
    m.addItem (901, "Delete \"" + cur.name + "\"", cur.valid && ! cur.factory);
    m.addItem (902, "Open presets folder");

    juce::Component::SafePointer<SkyverbAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameBtn.get()).withMinimumWidth (nameBtn->getWidth()),
                     [safe, users] (int result)
                     {
                         if (safe == nullptr || result == 0)
                             return;

                         auto& presets = safe->processor.presets;
                         if (result >= 1 && result <= sky::numFactory())
                             presets.loadFactory (result - 1);
                         else if (result >= 100 && result < 100 + users.size())
                             presets.loadUser (users[result - 100]);
                         else if (result == 900)
                             safe->beginSave();
                         else if (result == 901 && presets.getCurrent().valid && ! presets.getCurrent().factory)
                             presets.deleteUser (presets.getCurrent().name);
                         else if (result == 902)
                         {
                             sky::PresetManager::presetsDir().createDirectory();
                             sky::PresetManager::presetsDir().revealToUser();
                         }

                         safe->refreshPresetLabel();
                     });
}

void SkyverbAudioProcessorEditor::beginSave()
{
    const auto& cur = processor.presets.getCurrent();
    editingName = true;
    saveBtn->setText ("OK");

    nameEditor.setText (cur.valid && ! cur.factory ? cur.name : juce::String ("My preset"), false);
    nameEditor.setVisible (true);
    nameEditor.toFront (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void SkyverbAudioProcessorEditor::commitSave()
{
    const auto text = nameEditor.getText();
    editingName = false;
    nameEditor.setVisible (false);
    saveBtn->setText ("SAVE");

    processor.presets.saveUser (text);
    shownLabel = {};
    refreshPresetLabel();
}

void SkyverbAudioProcessorEditor::cancelSave()
{
    editingName = false;
    nameEditor.setVisible (false);
    saveBtn->setText ("SAVE");
    shownLabel = {};
    refreshPresetLabel();
}

//==============================================================================
void SkyverbAudioProcessorEditor::resized()
{
    for (int i = 0; i < 9; ++i)
        rows[(size_t) i]->setBounds (kLeftX, kRowsTop + i * kRowH, kLeftW, kRowH);

    rows[9]->setBounds (kRightX, kRowsTop, kRightW, kRowH);
    rows[10]->setBounds (kRightX, kRowsTop + kRowH, kRightW, kRowH);
    targetSwitch->setBounds (kRightX + kLabelW, kRowsTop + 2 * kRowH + 5, 150, 22);

    rows[11]->setBounds (kRightX, kMasterRowsTop, kRightW, kRowH);
    rows[12]->setBounds (kRightX, kMasterRowsTop + kRowH, kRightW, kRowH);

    themeSwitch->setBounds (kWidth - kMargin - kThemeW, 30, kThemeW, 22);

    prevBtn->setBounds (kLeftX + kLabelW, kPresetY, 28, kPresetH);
    nameBtn->setBounds (kLeftX + kLabelW + 32, kPresetY, 280, kPresetH);
    nextBtn->setBounds (kLeftX + kLabelW + 32 + 280 + 4, kPresetY, 28, kPresetH);
    saveBtn->setBounds (kRightX, kPresetY, 76, kPresetH);
    nameEditor.setBounds (nameBtn->getBounds());
}

void SkyverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = laf.theme;
    g.fillAll (t.paper);

    // title
    g.setColour (t.ink);
    g.setFont (sky::ui::makeFont (26.0f, true, 0.30f));
    g.drawText ("SKYVERB", kMargin, 22, 220, 34, juce::Justification::centredLeft, false);

    g.setColour (t.grey);
    g.setFont (sky::ui::makeFont (10.5f, false, 0.22f));
    g.drawText ("REVERB + LOSSY CODEC", kWidth - kMargin - kThemeW - 16 - 260, 32, 260, 16,
                juce::Justification::centredRight, false);

    // heavy rule under the header
    g.setColour (t.ink);
    g.fillRect (kMargin, 68, kWidth - 2 * kMargin, 2);

    // preset bar
    g.setColour (t.grey);
    g.setFont (sky::ui::makeFont (10.5f, false, 0.16f));
    g.drawText ("PRESET", kLeftX, kPresetY, kLabelW, kPresetH, juce::Justification::centredLeft, false);

    auto section = [&] (const juce::String& name, int x, int y, int w)
    {
        const auto font = sky::ui::makeFont (11.0f, true, 0.30f);
        g.setColour (t.ink);
        g.setFont (font);
        g.drawText (name, x, y, 120, 16, juce::Justification::centredLeft, false);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, name, 0.0f, 0.0f);
        const int tw = (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth()) + 12;
        g.setColour (t.hair);
        g.fillRect (x + tw, y + 8, w - tw, 1);
    };

    section ("REVERB", kLeftX, kSectionY, kLeftW);
    section ("LOSSY", kRightX, kSectionY, kRightW);
    section ("MASTER", kRightX, kMasterY, kRightW);

    g.setColour (t.grey);
    g.setFont (sky::ui::makeFont (10.5f, false, 0.16f));
    g.drawText ("APPLY TO", kRightX, kRowsTop + 2 * kRowH, kLabelW, kRowH, juce::Justification::centredLeft, false);

    // column separator
    g.setColour (t.hair);
    g.fillRect (kLeftX + kLeftW + 28, kSectionY, 1, kHeight - kSectionY - 40);

    // footer
    g.setColour (t.grey);
    g.setFont (sky::ui::makeFont (9.5f, false, 0.18f));
    g.drawText ("LATENCY " + juce::String (sky::LossyCodec::kLatency) + " SAMPLES  /  REPORTED TO HOST",
                kMargin, kHeight - 30, 400, 14, juce::Justification::centredLeft, false);
    g.drawText ("tripleskyluv corp", kWidth - kMargin - 300, kHeight - 30, 300, 14,
                juce::Justification::centredRight, false);
}
