#include "TakeReviewComponent.h"

#include "../../platform/take/TakeStatus.h"

#include <algorithm>

namespace
{
constexpr juce::uint32 correctColour = 0xff2e9e4f;
constexpr juce::uint32 wrongColour = 0xffd23c3c;
constexpr juce::uint32 timingColour = 0xffe08a1e;

[[nodiscard]] juce::String partNameOf(const score::Score& score, std::size_t part)
{
    if (part < score.parts.size() && !score.parts[part].name.empty())
    {
        return juce::String::fromUTF8(score.parts[part].name.c_str());
    }
    return "Part " + juce::String(static_cast<int>(part) + 1);
}
} // namespace

TakeReviewComponent::TakeReviewComponent(
    take::TakeRecorder& takeRecorder,
    transport::PracticeClock& practiceClock)
    : takes(takeRecorder), clock(practiceClock)
{
    setComponentID("take-review-tool");

    takeChooser.setComponentID("take-review-take");
    takeChooser.setTooltip("The take to review");
    takeChooser.onChange = [this]
    {
        const int id = takeChooser.getSelectedId();
        const take::TakeReview* review = takes.review();
        if (id > 0 && (review == nullptr || review->take->id != static_cast<std::uint64_t>(id)))
        {
            static_cast<void>(takes.reviewTake(static_cast<std::uint64_t>(id)));
        }
    };
    addAndMakeVisible(takeChooser);

    partChooser.setComponentID("take-review-part");
    partChooser.setTooltip("The part the take is judged against");
    partChooser.onChange = [this]
    {
        const int id = partChooser.getSelectedId();
        const take::TakeReview* review = takes.review();
        if (id > 0 && review != nullptr && review->part != static_cast<std::size_t>(id - 1))
        {
            static_cast<void>(takes.reviewTake(review->take->id, static_cast<std::size_t>(id - 1)));
        }
    };
    addAndMakeVisible(partChooser);

    list.setComponentID("take-review-list");
    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setTitle("Results");
    addAndMakeVisible(list);

    takes.addListener(this);
    setTheme(Theme::light);
    refresh();
}

TakeReviewComponent::~TakeReviewComponent()
{
    takes.removeListener(this);
    list.setModel(nullptr);
}

AppPalette TakeReviewComponent::palette() const
{
    return appPaletteFor(currentTheme);
}

void TakeReviewComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    const AppPalette colours = palette();
    list.setColour(juce::ListBox::backgroundColourId, colours.panel);
    list.setColour(juce::ListBox::outlineColourId, colours.outline);
    list.setOutlineThickness(1);
    list.repaint();
    repaint();
}

void TakeReviewComponent::resetToDefaults()
{
    // Nothing of its own to reset: what is reviewed is the recorder's.
    refresh();
}

void TakeReviewComponent::takesChanged()
{
    refresh();
}

void TakeReviewComponent::refresh()
{
    const take::TakeReview* review = takes.review();
    const auto& kept = takes.takes();

    // Newest first: the take just played is the one wanted.
    takeChooser.clear(juce::dontSendNotification);
    for (auto entry = kept.rbegin(); entry != kept.rend(); ++entry)
    {
        takeChooser.addItem(
            "Take " + juce::String(static_cast<juce::int64>((*entry)->id)),
            static_cast<int>((*entry)->id));
    }
    takeChooser.setEnabled(!kept.empty());

    partChooser.clear(juce::dontSendNotification);
    if (review != nullptr && review->take->score != nullptr)
    {
        takeChooser.setSelectedId(static_cast<int>(review->take->id), juce::dontSendNotification);
        const score::Score& score = *review->take->score;
        for (std::size_t part = 0; part < score.parts.size(); ++part)
        {
            partChooser.addItem(partNameOf(score, part), static_cast<int>(part) + 1);
        }
        partChooser.setSelectedId(static_cast<int>(review->part) + 1, juce::dontSendNotification);
    }
    partChooser.setEnabled(review != nullptr && partChooser.getNumItems() > 1);

    if (review != nullptr)
    {
        heading = juce::String::fromUTF8(take::describe(*review->take).c_str());
        if (review->take->score != nullptr)
        {
            heading += juce::String::fromUTF8(" \xC2\xB7 ") +
                       partNameOf(*review->take->score, review->part);
        }
        summary = juce::String::fromUTF8(evaluation::summaryLine(review->report).c_str());
        rows = evaluation::reviewRows(*review->take, review->report);
    }
    else
    {
        heading = {};
        summary = {};
        rows.clear();
    }

    // Nothing to choose between before the first take.
    takeChooser.setVisible(!kept.empty());
    partChooser.setVisible(!kept.empty());
    list.setVisible(review != nullptr);
    list.updateContent();
    list.repaint();
    repaint();
}

void TakeReviewComponent::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    auto header = bounds.removeFromTop(headerHeight);
    auto choosers = header.removeFromTop(26);
    takeChooser.setBounds(choosers.removeFromLeft(120));
    choosers.removeFromLeft(8);
    partChooser.setBounds(choosers.removeFromLeft(180));
    list.setBounds(bounds);
}

void TakeReviewComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();
    graphics.fillAll(colours.background);

    auto bounds = getLocalBounds().reduced(12);

    if (takes.review() == nullptr)
    {
        // Under the choosers while a take is being judged; at the top before
        // there is any.
        auto header = bounds;
        if (takeChooser.isVisible())
        {
            header.removeFromTop(34);
        }
        graphics.setColour(colours.foreground);
        graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        const bool judging = takes.isJudging();
        graphics.drawText(
            judging ? "Judging the take" : "No take to review", header.removeFromTop(26),
            juce::Justification::centredLeft);
        graphics.setColour(colours.muted);
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.drawFittedText(
            judging ? "Its results appear here in a moment."
                    : "Open a score, press Record in the transport and play. When the take "
                      "ends, every note is listed here and marked on the Score View.",
            header.removeFromTop(140), juce::Justification::topLeft, 8, 1.0f);
        return;
    }

    auto header = bounds.removeFromTop(headerHeight);
    header.removeFromTop(34);
    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    graphics.drawText(heading, header.removeFromTop(24), juce::Justification::centredLeft, true);
    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawText(summary, header.removeFromTop(22), juce::Justification::centredLeft, true);
}

int TakeReviewComponent::getNumRows()
{
    return static_cast<int>(rows.size());
}

juce::Colour TakeReviewComponent::colourOf(const evaluation::ReviewRow& row) const
{
    const take::TakeReview* review = takes.review();
    if (row.extra.has_value() || review == nullptr || !row.result.has_value() ||
        *row.result >= review->report.results.size())
    {
        return juce::Colour(wrongColour);
    }
    const evaluation::NoteResult& result = review->report.results[*row.result];
    switch (result.outcome)
    {
    case evaluation::NoteOutcome::wrong:
        return juce::Colour(wrongColour);
    case evaluation::NoteOutcome::missed:
        return palette().muted;
    case evaluation::NoteOutcome::pitchNotJudged:
        // A sung note too unsure to judge is neither right nor wrong.
        if (result.intonation.has_value() && !row.mistake)
        {
            return palette().muted;
        }
        break;
    case evaluation::NoteOutcome::correct:
        // A sung note in tune but not near perfect is blue, as on the score.
        if (result.intonation.has_value() &&
            result.intonation->verdict == evaluation::IntonationVerdict::inTune &&
            result.intonation->zone == pitch::IntonationZone::acceptable && !row.mistake)
        {
            return intonationZoneColour(pitch::IntonationZone::acceptable);
        }
        break;
    }
    return row.mistake ? juce::Colour(timingColour) : juce::Colour(correctColour);
}

void TakeReviewComponent::paintListBoxItem(
    int rowNumber,
    juce::Graphics& graphics,
    int width,
    int height,
    bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(rows.size()))
    {
        return;
    }
    const evaluation::ReviewRow& row = rows[static_cast<std::size_t>(rowNumber)];
    const AppPalette colours = palette();

    if (rowIsSelected)
    {
        graphics.fillAll(colours.accent.withAlpha(0.18f));
    }

    const auto dot = static_cast<float>(height) * 0.36f;
    graphics.setColour(colourOf(row));
    graphics.fillEllipse(10.0f, (static_cast<float>(height) - dot) * 0.5f, dot, dot);

    graphics.setColour(row.mistake ? colours.foreground : colours.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawText(
        juce::String::fromUTF8(row.text.c_str()), 28, 0, width - 32, height,
        juce::Justification::centredLeft, true);
}

void TakeReviewComponent::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    goTo(row);
}

juce::String TakeReviewComponent::getTooltipForRow(int row)
{
    juce::ignoreUnused(row);
    return clock.isLocked() ? "A take is recording; the clock cannot move"
                            : "Move the practice clock here";
}

void TakeReviewComponent::goTo(int row)
{
    if (row < 0 || row >= static_cast<int>(rows.size()) || clock.isLocked())
    {
        return;
    }
    clock.seek(static_cast<score::Tick>(rows[static_cast<std::size_t>(row)].tick));
}
