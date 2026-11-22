#pragma once
#include <vcl.h>
#include <Vcl.Grids.hpp>
#include <algorithm>
#include <vector>

namespace t2f {
// Measure with the actual VCL font. Native edits and combos choose their own height.
class EditorBar : public TPanel {
    std::vector<TControl *> items;
    TControl *stretch = nullptr;
    bool arranging = false;
protected:
    void __fastcall WndProc(TMessage &message) override {
        TPanel::WndProc(message);
        if(message.Msg==CM_FONTCHANGED || message.Msg==WM_SIZE || message.Msg==CM_SHOWINGCHANGED) arrange();
    }
    void __fastcall AlignControls(TControl *control, TRect &rect) override {
        TPanel::AlignControls(control,rect);
        arrange();
    }
public:
    __fastcall EditorBar(TComponent *owner) : TPanel(owner) { BevelOuter = bvNone; }
    void configure(std::vector<TControl *> controls, TControl *flex = nullptr) {
        items = std::move(controls);
        stretch = flex;
        arrange();
    }
    void arrange() {
        if (arranging || ComponentState.Contains(csDestroying) || !Owner ||
            Owner->ComponentState.Contains(csDestroying) || items.empty() || ClientWidth < 1) return;
        arranging = true;
        try {
            Canvas->Font->Assign(Font);
            auto px = [this](int n){ return MulDiv(n, PixelsPerInch, 96); };
            const int gap=px(5), th=Canvas->TextHeight(L"Ау"), h=std::max(px(26), th+px(10));
            struct Box { TControl *c; int w,h; };
            std::vector<Box> line;
            int used=gap, y=gap;
            auto place = [&] {
                int height=0;
                for(auto &b:line) height=std::max(height,b.h);
                int x=gap;
                for(auto &b:line) {
                    int w=b.w+(b.c==stretch ? std::max(0,ClientWidth-used) : 0);
                    b.c->SetBounds(x,y+(height-b.h)/2,w,b.h);
                    x+=w+gap;
                }
                y+=height+gap;
                line.clear();used=gap;
            };
            for(auto c:items) {
                if(!c->Visible) continue;
                int w=px(100), ch=h;
                if(auto b=dynamic_cast<TButton*>(c)) w=Canvas->TextWidth(b->Caption)+px(20);
                else if(auto l=dynamic_cast<TLabel*>(c)) {
                    w=Canvas->TextWidth(l->Caption)+px(3); ch=th+px(2);
                } else if(auto b=dynamic_cast<TCheckBox*>(c)) w=Canvas->TextWidth(b->Caption)+px(28);
                else if(auto combo=dynamic_cast<TComboBox*>(c)) {
                    int label=0;
                    for(int i=0;i<combo->Items->Count;++i) label=std::max(label,Canvas->TextWidth(combo->Items->Strings[i]));
                    bool swatch=combo->Style==csOwnerDrawFixed;
                    if(swatch && combo->ItemHeight!=std::max(th+px(6),px(18)))
                        combo->ItemHeight=std::max(th+px(6),px(18));
                    w=label+px(swatch ? 59 : 32); ch=combo->Height;
                } else if(auto e=dynamic_cast<TEdit*>(c)) ch=e->Height;
                w=std::min(w,std::max(1,ClientWidth-2*gap));
                if(!line.empty() && used+w+gap>ClientWidth) {
                    // Wrap a field together with its label.
                    auto l=dynamic_cast<TLabel*>(line.back().c);
                    if(line.size()>1 && l && l->FocusControl==c) {
                        Box saved=line.back();line.pop_back();used-=saved.w+gap;
                        place();line.push_back(saved);used+=saved.w+gap;
                    } else place();
                }
                line.push_back({c,w,ch});used+=w+gap;
            }
            if(!line.empty()) place();
            if(Height!=y) Height=y;
        } __finally { arranging=false; }
    }
    void __fastcall ScaleForPPI(int ppi) override { TPanel::ScaleForPPI(ppi); arrange(); }
};
inline void inputDivider(TCanvas *canvas,const TRect &cell,int ppi) {
    canvas->Brush->Style=bsSolid;
    canvas->Brush->Color=TColor(RGB(82,92,105));
    canvas->FillRect(TRect(cell.Left,cell.Top,cell.Left+std::max(2,MulDiv(3,ppi,96)),cell.Bottom));
}
inline void fitGridRows(TStringGrid *grid) {
    grid->Canvas->Font->Assign(grid->Font);
    int h=std::max(MulDiv(26,grid->PixelsPerInch,96),grid->Canvas->TextHeight(L"Ау")+MulDiv(8,grid->PixelsPerInch,96));
    if(grid->DefaultRowHeight!=h) grid->DefaultRowHeight=h;
}
}
