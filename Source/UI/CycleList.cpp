#include "CycleList.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
enum Column { colIndex = 1, colType, colNumber, colStart, colLength, colSuggest, colName };
}

/** Leaves Space / digits / undo keys to the main window so shortcuts work while the list has focus. */
class CycleList::Table : public juce::TableListBox
{
public:
    using juce::TableListBox::TableListBox;

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        if (code == juce::KeyPress::spaceKey || (key.getModifiers().isCommandDown() && code != 'A'))
            return false;
        return juce::TableListBox::keyPressed (key);
    }
};

class CycleList::TypeCell : public juce::ComboBox
{
public:
    int row = -1;
};

CycleList::CycleList (CycleModel& m)
    : model (m),
      tableOwner (new Table ("cycles", this)),
      table (*tableOwner)
{
    addAndMakeVisible (table);
    table.setMultipleSelectionEnabled (true);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setColour (juce::ListBox::outlineColourId, theme::outline);
    table.setOutlineThickness (1);

    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    header.setColour (juce::TableHeaderComponent::outlineColourId, theme::outline);
    header.addColumn ("#", colIndex, 46, 30, 80, juce::TableHeaderComponent::notResizable);
    header.addColumn ("Type", colType, 130, 80, 220, juce::TableHeaderComponent::notSortable);
    header.addColumn ("No.", colNumber, 50, 40, 80, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Start", colStart, 110, 80, 200, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Length", colLength, 90, 70, 160, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("候補 (確信度)"), colSuggest, 170, 100, 300, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Output name", colName, 300, 120, 800, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);
    header.setPopupMenuActive (false);

    model.addChangeListener (this);
}

CycleList::~CycleList()
{
    model.removeChangeListener (this);
    table.setModel (nullptr);
}

juce::Array<int> CycleList::getSelectedRows() const
{
    juce::Array<int> rows;
    const auto sel = table.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
        rows.add (sel[i]);
    return rows;
}

void CycleList::selectRows (const juce::Array<int>& rows)
{
    const juce::ScopedValueSetter<bool> guard (suppressCallbacks, true);
    juce::SparseSet<int> set;
    for (auto r : rows)
        set.addRange ({ r, r + 1 });
    table.setSelectedRows (set, juce::dontSendNotification);
    if (! rows.isEmpty())
        table.scrollToEnsureRowIsOnscreen (rows[0]);
    table.repaint();
}

void CycleList::selectRow (int row)
{
    selectRows (row >= 0 ? juce::Array<int> { row } : juce::Array<int>());
}

void CycleList::changeListenerCallback (juce::ChangeBroadcaster*)
{
    table.updateContent();
    table.repaint();
}

void CycleList::firePreview (int row)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPreviewTick < 120)
        return;
    lastPreviewTick = now;
    if (onPreview && row >= 0 && row < model.size())
        onPreview (row);
}

void CycleList::selectedRowsChanged (int lastRow)
{
    if (suppressCallbacks)
        return;

    if (onSelectionChanged)
        onSelectionChanged();
    if (table.getNumSelectedRows() == 1)
        firePreview (lastRow);
}

void CycleList::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (column != colType && ! e.mods.isPopupMenu() && table.getNumSelectedRows() == 1)
        firePreview (row);
}

//==============================================================================
void CycleList::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
}

void CycleList::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row >= model.size())
        return;

    const auto& c = model[row];
    const auto colour = c.type >= 0 ? model.getType (c.type).colour : juce::Colours::grey;
    juce::String text;

    switch (column)
    {
        case colIndex:
            g.setColour (colour);
            g.fillRect (0, 3, 4, height - 6);
            text = juce::String (row + 1);
            break;
        case colNumber:   text = juce::String (model.numberOf (row)).paddedLeft ('0', 2); break;
        case colStart:    text = formatTime ((double) c.start / sampleRate); break;
        case colLength:   text = juce::String ((double) (c.end - c.start) / sampleRate, 3) + " s"; break;
        case colSuggest:
        {
            if (c.autoType.isEmpty() || c.autoConf < 0.0f)
                return;

            const auto percent = juce::String ((int) std::round (c.autoConf * 100.0f)) + "%";
            auto sugColour = c.autoConf >= 0.7f ? juce::Colour (0xff6fd06f) : c.autoConf >= 0.4f ? juce::Colour (0xffe0cf4f) : theme::textDim;
            if (c.type >= 0 && c.confirmed)
            {
                const auto agreed = model.getType (c.type).name.equalsIgnoreCase (c.autoType);
                text = agreed ? WT_U8 ("✓ 採用 ") + c.autoType + " " + percent : WT_U8 ("✎ 修正 (候補 ") + c.autoType + " " + percent + ")";
                sugColour = theme::textDim;
            }
            else
            {
                text = (c.type >= 0 ? WT_U8 ("(自動) ") : juce::String()) + c.autoType + " " + percent;
            }

            g.setColour (sugColour);
            g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
            g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
            return;
        }
        case colName:     text = model.nameOf (row) + ".wav"; break;
        default:          return;
    }

    g.setColour (column == colName ? theme::text : theme::textDim.brighter (0.3f));
    g.setFont (juce::FontOptions (column == colStart ? "Consolas" : "Meiryo UI", 13.0f, juce::Font::plain));
    g.drawText (text, column == colIndex ? 10 : 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

juce::Component* CycleList::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (column != colType || row >= model.size())
    {
        delete existing;
        return nullptr;
    }

    auto* cell = dynamic_cast<TypeCell*> (existing);
    if (cell == nullptr)
    {
        cell = new TypeCell();
        cell->setWantsKeyboardFocus (false);
        cell->onChange = [this, cell] {
            if (cell->row >= 0 && onTypeChosen)
            {
                auto rows = getSelectedRows();
                if (! rows.contains (cell->row))
                    rows = { cell->row };
                onTypeChosen (rows, cell->getSelectedId() - 2);
            }
        };
    }

    cell->row = -1;
    cell->clear (juce::dontSendNotification);
    cell->addItem (WT_U8 ("— (未分類)"), 1);
    for (int i = 0; i < CycleModel::numSlots; ++i)
        if (model.isTypeUsable (i))
            cell->addItem (juce::String (i + 1) + "  " + model.getType (i).name, i + 2);

    const auto type = model[row].type;
    cell->setSelectedId (type + 2, juce::dontSendNotification);
    cell->setColour (juce::ComboBox::textColourId, type >= 0 ? model.getType (type).colour.brighter (0.3f) : theme::textDim);
    cell->setColour (juce::ComboBox::backgroundColourId, theme::panelLight);
    cell->setColour (juce::ComboBox::outlineColourId, theme::outline);
    cell->row = row;
    return cell;
}
} // namespace wt
