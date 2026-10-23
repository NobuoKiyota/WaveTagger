#include "CategoryPicker.h"
#include "Theme.h"
#include "Util/Text.h"
#include <set>

namespace wt
{
namespace
{
constexpr int rowHeight = 26;
constexpr int mergeThreshold = 10;
const juce::String freqKey = "*frequent";

// Remembered between openings so the user's accordion state is not lost.
std::set<juce::String> expandedGroups { freqKey };
bool mergeGroups = false;

juce::String groupLetter (const juce::String& name)
{
    const auto c = juce::CharacterFunctions::toUpperCase (name.isEmpty() ? (juce::juce_wchar) '#' : name[0]);
    return juce::CharacterFunctions::isLetter (c) ? juce::String::charToString (c) : juce::String ("#");
}
} // namespace

class CategoryPicker::RowList : public juce::Component
{
public:
    explicit RowList (CategoryPicker& o) : owner (o) {}

    void updateSize (int width) { setSize (width, juce::jmax (1, (int) owner.rows.size() * rowHeight)); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
        const auto clip = g.getClipBounds();
        const auto first = juce::jmax (0, clip.getY() / rowHeight);
        const auto last = juce::jmin ((int) owner.rows.size() - 1, clip.getBottom() / rowHeight);

        for (int i = first; i <= last; ++i)
        {
            const auto& r = owner.rows[(size_t) i];
            const juce::Rectangle<int> area (0, i * rowHeight, getWidth(), rowHeight);

            if (r.header)
            {
                g.setColour (theme::panelLight);
                g.fillRect (area.reduced (0, 1));
                g.setColour (theme::text);
                g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
                g.drawText (juce::String (r.expanded ? WT_U8 ("▾  ") : WT_U8 ("▸  ")) + r.label, area.withTrimmedLeft (8),
                            juce::Justification::centredLeft);
                g.setColour (theme::textDim);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (juce::String (r.count), area.withTrimmedRight (10), juce::Justification::centredRight);
            }
            else
            {
                if (i == hover)
                {
                    g.setColour (theme::accent.withAlpha (0.25f));
                    g.fillRect (area);
                }
                if (r.current)
                {
                    g.setColour (theme::accent.withAlpha (0.5f));
                    g.fillRect (area.withWidth (3));
                }

                g.setColour (theme::text);
                g.setFont (juce::FontOptions (14.0f));
                g.drawText (r.label, area.withTrimmedLeft (26).withWidth (170), juce::Justification::centredLeft, true);
                g.setColour (theme::textDim);
                g.drawText (r.jp, area.withTrimmedLeft (200), juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto row = e.y / rowHeight;
        if (row != hover)
        {
            hover = row;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto i = e.y / rowHeight;
        if (i < 0 || i >= (int) owner.rows.size())
            return;

        const auto& r = owner.rows[(size_t) i];
        if (r.header)
        {
            if (expandedGroups.count (r.key) != 0)
                expandedGroups.erase (r.key);
            else
                expandedGroups.insert (r.key);

            const auto scrollY = owner.viewport.getViewPositionY();
            owner.rebuild();
            owner.viewport.setViewPosition (0, scrollY);
        }
        else
        {
            owner.pick (r.key);
            owner.dismiss();
        }
    }

private:
    CategoryPicker& owner;
    int hover = -1;
};

CategoryPicker::CategoryPicker (Catalog& c, const juce::String& current,
                                std::function<void (const juce::String&)> onPick, std::function<void()> onAdd)
    : catalog (c), currentCategory (current), pick (std::move (onPick)), addRequested (std::move (onAdd))
{
    if (current.isNotEmpty())
        for (int i = 0; i < (int) catalog.getCategories().size(); ++i)
            if (catalog.getCategories()[(size_t) i].name == current)
                expandedGroups.insert (groupLetter (current)); // merged labels are resolved in rebuild()

    search.setTextToShowWhenEmpty (WT_U8 ("検索 (英語 / 日本語)"), theme::textDim);
    search.setFont (juce::FontOptions (15.0f));
    search.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    search.setColour (juce::TextEditor::outlineColourId, theme::outline);
    search.setColour (juce::TextEditor::textColourId, theme::text);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    list = std::make_unique<RowList> (*this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    mergeToggle.setButtonText (WT_U8 ("A–E のようにまとめる"));
    mergeToggle.setToggleState (mergeGroups, juce::dontSendNotification);
    mergeToggle.setWantsKeyboardFocus (false);
    mergeToggle.onClick = [this] {
        mergeGroups = mergeToggle.getToggleState();
        rebuild();
    };
    addAndMakeVisible (mergeToggle);

    addButton.setButtonText (WT_U8 ("＋ 追加..."));
    openButton.setButtonText (WT_U8 ("CSV の場所"));
    reloadButton.setButtonText (WT_U8 ("再読込"));
    for (auto* b : { &addButton, &openButton, &reloadButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    addButton.onClick = [this] {
        auto cb = addRequested;
        dismiss();
        if (cb)
            cb();
    };
    openButton.onClick = [this] { catalog.getDirectory().revealToUser(); };
    reloadButton.onClick = [this] {
        catalog.reload();
        rebuild();
    };

    footer.setColour (juce::Label::textColourId, juce::Colour (0xffffc533));
    footer.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (footer);

    rebuild();
}

CategoryPicker::~CategoryPicker() = default;

void CategoryPicker::parentHierarchyChanged()
{
    if (isShowing())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<CategoryPicker> (this)] {
            if (safe != nullptr)
                safe->search.grabKeyboardFocus();
        });
}

void CategoryPicker::dismiss()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void CategoryPicker::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void CategoryPicker::resized()
{
    auto r = getLocalBounds().reduced (8);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (26);
    r.removeFromBottom (4);
    footer.setBounds (r.removeFromBottom (18));
    r.removeFromBottom (4);

    reloadButton.setBounds (bottom.removeFromRight (64));
    bottom.removeFromRight (4);
    openButton.setBounds (bottom.removeFromRight (84));
    bottom.removeFromRight (4);
    addButton.setBounds (bottom.removeFromRight (84));
    mergeToggle.setBounds (bottom);

    viewport.setBounds (r);
    list->updateSize (viewport.getMaximumVisibleWidth());
}

void CategoryPicker::rebuild()
{
    rows.clear();
    const auto& cats = catalog.getCategories();
    const auto query = search.getText().trim();

    const auto itemRow = [&] (const CategoryInfo& c) {
        Row r;
        r.label = c.name;
        r.jp = c.jp;
        r.key = c.name;
        r.current = c.name == currentCategory;
        return r;
    };

    if (query.isNotEmpty())
    {
        for (const auto& c : cats)
            if (c.name.containsIgnoreCase (query) || c.jp.containsIgnoreCase (query))
                rows.push_back (itemRow (c));

        if (rows.empty())
        {
            Row r;
            r.header = true;
            r.label = WT_U8 ("該当なし (＋ 追加... で新しい Category を作れます)");
            r.key = "*none";
            rows.push_back (r);
        }
    }
    else
    {
        Row none;
        none.label = WT_U8 ("(Category なし)");
        none.key = "";
        none.current = currentCategory.isEmpty();
        rows.push_back (none);

        const auto freq = catalog.frequentIndices (6);
        if (! freq.empty())
        {
            Row h;
            h.header = true;
            h.label = WT_U8 ("★ よく使う");
            h.key = freqKey;
            h.count = (int) freq.size();
            h.expanded = expandedGroups.count (freqKey) != 0;
            rows.push_back (h);
            if (h.expanded)
                for (auto i : freq)
                    rows.push_back (itemRow (cats[(size_t) i]));
        }

        // letters (A..Z, #) in order, optionally merged into blocks of at least mergeThreshold entries
        std::map<juce::String, std::vector<int>> byLetter;
        for (int i = 0; i < (int) cats.size(); ++i)
            byLetter[groupLetter (cats[(size_t) i].name)].push_back (i);

        struct Bucket
        {
            juce::String first, last;
            std::vector<int> items;
        };
        std::vector<Bucket> buckets;
        Bucket cur;
        for (const auto& [letter, idx] : byLetter)
        {
            if (cur.items.empty())
                cur.first = letter;
            cur.last = letter;
            cur.items.insert (cur.items.end(), idx.begin(), idx.end());

            if (! mergeGroups || (int) cur.items.size() >= mergeThreshold)
            {
                buckets.push_back (cur);
                cur = {};
            }
        }
        if (! cur.items.empty())
            buckets.push_back (cur);

        for (const auto& b : buckets)
        {
            Row h;
            h.header = true;
            h.label = b.first == b.last ? b.first : b.first + WT_U8 ("–") + b.last;
            h.key = h.label;
            h.count = (int) b.items.size();

            // the bucket holding the current category opens by itself
            for (auto i : b.items)
                if (cats[(size_t) i].name == currentCategory && h.key != freqKey)
                    expandedGroups.insert (h.key);

            h.expanded = expandedGroups.count (h.key) != 0;
            rows.push_back (h);
            if (h.expanded)
                for (auto i : b.items)
                    rows.push_back (itemRow (cats[(size_t) i]));
        }
    }

    const auto warnings = catalog.getWarnings();
    footer.setText (warnings.isEmpty() ? juce::String() : WT_U8 ("⚠ CSV に ") + juce::String (warnings.size()) + WT_U8 (" 件の注意: ") + warnings[0],
                    juce::dontSendNotification);

    list->updateSize (viewport.getMaximumVisibleWidth());
    list->repaint();
}
} // namespace wt
