#pragma once
#include <vcl.h>
#include <algorithm>
#include "input_colors.hpp"
namespace t2f {
inline TColor groupFill(unsigned id, TColor background) {
    if (!id)
        return background;
    // The stored Excel fill is the tint over white. Composite the same tint over
    // the grid's header or selection background with one-third opacity.
    static const std::vector<unsigned> rgb = [] {
        std::vector<unsigned> values{0};
        for (size_t i = 1; i < inputPalette().size(); ++i)
            values.push_back(std::stoul(inputPalette()[i].rgb, nullptr, 16));
        return values;
    }();
    unsigned tint = rgb.at(id);
    COLORREF base = ColorToRGB(background);
    auto blend = [](int light, int base) {
        return std::max(0, light - (2 * (255 - base) + 1) / 3);
    };
    return TColor(RGB(blend((tint >> 16) & 255, GetRValue(base)),
                      blend((tint >> 8) & 255, GetGValue(base)),
                      blend(tint & 255, GetBValue(base))));
}
class InputColorBox : public TComboBox {
    void __fastcall draw(TWinControl *, int index, const TRect &rect, TOwnerDrawState flags) {
        auto canvas = Canvas;
        bool selected = flags.Contains(odSelected);
        canvas->Brush->Color = selected ? clHighlight : clWindow;
        canvas->FillRect(rect);
        if (index < 0)
            return;
        int size = std::min<int>(MulDiv(16, CurrentPPI, 96), std::max<int>(1, rect.Height() - 2));
        int pad = MulDiv(4, CurrentPPI, 96), top = rect.Top + (rect.Height() - size) / 2;
        canvas->Brush->Color = groupFill(index, clWhite);
        canvas->Pen->Color = clGray;
        canvas->Rectangle(rect.Left + pad, top, rect.Left + pad + size, top + size);
        canvas->Brush->Style = bsClear;
        canvas->Font->Color = selected ? clHighlightText : clWindowText;
        TRect label = rect;
        label.Left += size + 2 * pad;
        DrawTextW(canvas->Handle, Items->Strings[index].c_str(), -1, &label,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        canvas->Brush->Style = bsSolid;
    }

  public:
    TLabel *label;
    __fastcall InputColorBox(TForm *owner, TPanel *bar, int x, int height, TNotifyEvent changed)
        : TComboBox(owner) {
        label = new TLabel(owner);
        label->Parent = bar;
        label->Caption = wide(u8"Цвет столбца");
        label->Left = x;
        label->Top = 8;
        Parent = bar;
        Style = csOwnerDrawFixed;
        DropDownCount = inputPalette().size();
        SetBounds(x + owner->Canvas->TextWidth(label->Caption) + 12, 5, MulDiv(165,CurrentPPI,96), height);
        label->FocusControl = this;
        for (auto &color : inputPalette())
            Items->Add(wide(color.name));
        ItemHeight = owner->Canvas->TextHeight("Ag") + MulDiv(6, CurrentPPI, 96);
        bar->Height = std::max(height, Height) + 12;
        OnDrawItem = draw;
        OnChange = changed;
    }
};
inline void addInputColorMenu(TMenuItem *parent, int firstTag, TNotifyEvent changed) {
    auto colors = new TMenuItem(parent);
    colors->Caption = wide(u8"Цвет столбца");
    parent->Add(colors);
    for (unsigned i = 0; i < inputPalette().size(); ++i) {
        auto choice = new TMenuItem(colors);
        colors->Add(choice);
        choice->Caption = wide(inputPalette()[i].name);
        choice->Tag = firstTag + i;
        choice->OnClick = changed;
        choice->RadioItem = true;
        auto swatch = choice->Bitmap;
        swatch->SetSize(14, 14);
        swatch->Canvas->Brush->Color = groupFill(i, clWhite);
        swatch->Canvas->Pen->Color = clGray;
        swatch->Canvas->Rectangle(0, 0, 14, 14);
    }
}
} // namespace t2f
