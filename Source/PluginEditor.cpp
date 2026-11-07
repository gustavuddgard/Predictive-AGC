#include "PluginEditor.h"

namespace
{
const juce::Colour ink { 0xff141614 };
const juce::Colour panel { 0xff232622 };
const juce::Colour text { 0xfff3efe6 };
const juce::Colour muted { 0xff9c968b };
const juce::Colour amber { 0xffe0a23b };
const juce::Colour track { 0xff2e332e };
} // namespace

class Agc2AudioProcessorEditor::LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1f1b));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, amber);
        setColour(juce::Label::textColourId, text);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, amber);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1a140c));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height))
                          .reduced(8.0f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre(diameter, diameter);
        const auto centre = bounds.getCentre();
        const float radius = diameter * 0.5f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        constexpr float thickness = 2.6f;

        juce::Path background;
        background.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness,
                                 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff343834));
        g.strokePath(background, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.001f)
        {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness,
                                0.0f, startAngle, angle, true);
            g.setColour(amber);
            g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.15f, -radius + 10.0f, 2.3f, radius * 0.42f, 1.0f);
        g.setColour(text);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        auto fill = on ? amber : panel;
        if (highlighted)
            fill = fill.brighter(0.06f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(on ? amber.darker(0.2f) : juce::Colour(0xff3c413c));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
};

Agc2AudioProcessor& Agc2AudioProcessorEditor::agc()
{
    return static_cast<Agc2AudioProcessor&>(processor);
}

Agc2AudioProcessorEditor::Agc2AudioProcessorEditor(Agc2AudioProcessor& owner)
    : juce::AudioProcessorEditor(owner)
    , lookAndFeel(std::make_unique<LookAndFeel>())
{
    setLookAndFeel(lookAndFeel.get());

    title.setText("AGC 2.0", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, text);
    title.setJustificationType(juce::Justification::bottomLeft);
    addAndMakeVisible(title);

    subtitle.setText("Adaptive leveler", juce::dontSendNotification);
    subtitle.setFont(juce::Font(juce::FontOptions(13.0f)));
    subtitle.setColour(juce::Label::textColourId, muted);
    subtitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitle);

    latencyReadout.setText("2.0 ms", juce::dontSendNotification);

    for (int index = 0; index < owner.getNumPrograms(); ++index)
        presetBox.addItem(owner.getProgramName(index), index + 1);
    presetBox.setSelectedId(owner.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setColour(juce::ComboBox::backgroundColourId, panel);
    presetBox.setColour(juce::ComboBox::textColourId, text);
    presetBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3c413c));
    presetBox.setColour(juce::ComboBox::arrowColourId, amber);
    presetBox.onChange = [this]
    {
        agc().setCurrentProgram(presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible(presetBox);
    latencyReadout.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    latencyReadout.setColour(juce::Label::textColourId, amber);
    latencyReadout.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(latencyReadout);

    configureKnob(target, targetName, "Target");
    configureKnob(maxBoost, boostName, "Max gain");
    configureKnob(maxCut, cutName, "Release");
    configureKnob(noiseFloor, floorName, "VAD");
    configureKnob(duckAmount, duckName, "Duck");
    configureKnob(lookahead, lookaheadName, "Lookahead");
    configureKnob(depth, depthName, "Depth");
    configureKnob(scThreshold, thresholdName, "Threshold");
    configureKnob(scRelease, releaseName, "Release");
    configureKnob(crossover, crossoverName, "Crossover");
    configureKnob(sense, senseName, "Sense");

    levelerHeading.setText("Leveler", juce::dontSendNotification);
    levelerHeading.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    levelerHeading.setColour(juce::Label::textColourId, amber);
    addAndMakeVisible(levelerHeading);

    classifierHeading.setText("Classifier", juce::dontSendNotification);
    classifierHeading.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    classifierHeading.setColour(juce::Label::textColourId, amber);
    addAndMakeVisible(classifierHeading);

    sidechainHeading.setText("Sidechain", juce::dontSendNotification);
    sidechainHeading.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    sidechainHeading.setColour(juce::Label::textColourId, amber);
    addAndMakeVisible(sidechainHeading);

    link.setClickingTogglesState(true);
    bypass.setClickingTogglesState(true);
    addAndMakeVisible(link);
    addAndMakeVisible(bypass);

    auto& state = agc().apvts;
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "targetLoudness", target);
    boostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "maxGainBoost", maxBoost);
    cutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "baseRelease", maxCut);
    floorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "vad", noiseFloor);
    duckAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "duck", duckAmount);
    lookaheadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "peakLookahead", lookahead);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "duckDepth", depth);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "scThreshold", scThreshold);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "scRelease", scRelease);
    crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "crossover", crossover);
    senseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "scVad", sense);
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "link", link);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "bypass", bypass);

    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(900, 560, 1280, 900);
    setSize(980, 660);
    startTimerHz(30);
}

Agc2AudioProcessorEditor::~Agc2AudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void Agc2AudioProcessorEditor::configureKnob(juce::Slider& slider, juce::Label& name, const juce::String& titleText)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 88, 18);
    addAndMakeVisible(slider);

    name.setText(titleText, juce::dontSendNotification);
    name.setFont(juce::Font(juce::FontOptions(12.5f)));
    name.setColour(juce::Label::textColourId, muted);
    name.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(name);
}

void Agc2AudioProcessorEditor::timerCallback()
{
    gainDb = agc().getDisplayedGainDb();
    engageProbability = agc().getDisplayedEngage();
    engaged = agc().getDisplayedEngaged();
    shownClass = agc().getDisplayedClass();

    const double rate = agc().getSampleRate();
    if (rate > 0.0)
    {
        const double milliseconds = 1000.0 * static_cast<double>(agc().getLatencySamples()) / rate;
        latencyReadout.setText(juce::String(milliseconds, 1) + " ms", juce::dontSendNotification);
    }

    const int presetId = agc().getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != presetId)
        presetBox.setSelectedId(presetId, juce::dontSendNotification);

    repaint();
}

void Agc2AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ink);

    g.setColour(amber);
    g.fillRect(0, 0, getWidth(), 3);

    g.setColour(panel);
    g.fillRoundedRectangle(meterArea.toFloat(), 8.0f);

    auto meter = meterArea.reduced(14, 12);
    auto caption = meter.removeFromTop(16);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("Gain", caption.removeFromLeft(48), juce::Justification::centredLeft);
    g.setColour(text);
    const juce::String gainText = (gainDb >= 0.0f ? "+" : "") + juce::String(gainDb, 1) + " dB";
    g.drawText(gainText, caption, juce::Justification::centredRight);

    meter.removeFromTop(8);
    auto bar = meter.removeFromTop(14).toFloat();
    g.setColour(track);
    g.fillRoundedRectangle(bar, 4.0f);

    constexpr float meterMin = -24.0f;
    constexpr float meterMax = 12.0f;
    const float zero = juce::jmap(0.0f, meterMin, meterMax, bar.getX(), bar.getRight());
    const float clamped = juce::jlimit(meterMin, meterMax, gainDb);
    const float end = juce::jmap(clamped, meterMin, meterMax, bar.getX(), bar.getRight());
    const float left = std::min(zero, end);
    const float right = std::max(zero, end);

    g.setColour(clamped >= 0.0f ? amber : juce::Colour(0xffd07a4a));
    if (right - left > 0.5f)
        g.fillRoundedRectangle(left, bar.getY(), right - left, bar.getHeight(), 4.0f);

    g.setColour(text.withAlpha(0.45f));
    g.drawLine(zero, bar.getY() - 2.0f, zero, bar.getBottom() + 2.0f, 1.0f);

    const float brightness = engaged ? juce::jmax(0.65f, engageProbability) : engageProbability;
    auto lamp = lampArea.toFloat();
    const float diameter = juce::jmin(lamp.getWidth(), lamp.getHeight());
    auto bulb = lamp.withSizeKeepingCentre(diameter, diameter);

    g.setColour(juce::Colour(0xff2a2e2a));
    g.fillEllipse(bulb);
    g.setColour(amber.withAlpha(juce::jlimit(0.12f, 1.0f, 0.12f + 0.88f * brightness)));
    g.fillEllipse(bulb.reduced(3.0f));

    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("Peak", lampLabelArea, juce::Justification::centred);

    if (!classifierArea.isEmpty())
    {
        static const char* names[] = { "Silence", "Music", "Voice", "Telephone", "Stresser" };
        const int slot = classifierArea.getWidth() / 5;
        for (int index = 0; index < 5; ++index)
        {
            auto cell = classifierArea.withX(classifierArea.getX() + index * slot).withWidth(slot).reduced(4, 0);
            const bool active = index == shownClass;
            g.setColour(active ? amber : panel);
            g.fillRoundedRectangle(cell.toFloat(), 6.0f);
            g.setColour(active ? juce::Colour(0xff1a140c) : muted);
            g.setFont(juce::Font(juce::FontOptions(13.0f, active ? juce::Font::bold : juce::Font::plain)));
            g.drawText(names[index], cell, juce::Justification::centred);
        }
    }
}

void Agc2AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20, 16);
    auto header = area.removeFromTop(48);
    latencyReadout.setBounds(header.removeFromRight(88));
    presetBox.setBounds(header.removeFromRight(230).reduced(8, 10));
    title.setBounds(header.removeFromTop(28));
    subtitle.setBounds(header);

    area.removeFromTop(6);
    classifierHeading.setBounds(area.removeFromTop(16));
    classifierArea = area.removeFromTop(28);
    area.removeFromTop(8);
    levelerHeading.setBounds(area.removeFromTop(16));
    auto levelerKnobs = area.removeFromTop(158);
    const int levelerSlot = levelerKnobs.getWidth() / 6;

    auto place = [](juce::Rectangle<int>& row, int slotWidth, juce::Label& name, juce::Slider& slider)
    {
        auto slot = row.removeFromLeft(slotWidth).reduced(4, 0);
        name.setBounds(slot.removeFromTop(16));
        slider.setBounds(slot);
    };

    place(levelerKnobs, levelerSlot, targetName, target);
    place(levelerKnobs, levelerSlot, boostName, maxBoost);
    place(levelerKnobs, levelerSlot, cutName, maxCut);
    place(levelerKnobs, levelerSlot, floorName, noiseFloor);
    place(levelerKnobs, levelerSlot, duckName, duckAmount);
    place(levelerKnobs, levelerSlot, lookaheadName, lookahead);

    area.removeFromTop(4);
    sidechainHeading.setBounds(area.removeFromTop(16));
    auto sidechainKnobs = area.removeFromTop(158);
    const int sidechainSlot = sidechainKnobs.getWidth() / 5;
    place(sidechainKnobs, sidechainSlot, depthName, depth);
    place(sidechainKnobs, sidechainSlot, thresholdName, scThreshold);
    place(sidechainKnobs, sidechainSlot, releaseName, scRelease);
    place(sidechainKnobs, sidechainSlot, crossoverName, crossover);
    place(sidechainKnobs, sidechainSlot, senseName, sense);

    area.removeFromTop(8);
    auto footer = area;
    auto buttons = footer.removeFromLeft(juce::jmin(200, footer.getWidth() / 3));
    link.setBounds(buttons.removeFromTop(36).reduced(0, 4));
    bypass.setBounds(buttons.removeFromTop(36).reduced(0, 4));

    auto lampColumn = footer.removeFromRight(84).reduced(4, 0);
    lampLabelArea = lampColumn.removeFromBottom(18);
    lampArea = lampColumn.reduced(6, 2);
    meterArea = footer.reduced(8, 0);
}
