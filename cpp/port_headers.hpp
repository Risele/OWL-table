#pragma once
#include "core.hpp"
#include <Vcl.Grids.hpp>
#include <gdiplus.h>
#pragma comment(lib,"gdiplus.lib")

namespace t2f {
inline U headerLine(U text) {
    for(int i=1;i<=text.Length();++i)if(text[i]=='\r' || text[i]=='\n' || text[i]=='\t')text[i]=' ';
    return text;
}
inline U fitHeaderText(TCanvas *canvas,const U &text,int width) {
    if(canvas->TextWidth(text)<=width)return text;
    U ellipsis=wide(u8"…");
    int lo=0,hi=text.Length();
    while(lo<hi) {
        int mid=(lo+hi+1)/2;
        if(canvas->TextWidth(text.SubString(1,mid)+ellipsis)<=width)lo=mid;else hi=mid-1;
    }
    if(lo>0 && text[lo]>=0xd800 && text[lo]<=0xdbff)--lo;
    return text.SubString(1,lo)+ellipsis;
}
inline std::vector<U> headerLines(TCanvas *canvas,U text,int height) {
    text=StringReplace(text,"\r","",TReplaceFlags()<<rfReplaceAll).Trim();
    for(int i=1;i<=text.Length();++i)if(text[i]=='\t')text[i]=' ';
    int end=text.Pos("\n");
    U first=end?text.SubString(1,end-1):text;
    if(canvas->TextWidth(first)>height) {
        int lo=0,hi=first.Length();
        while(lo<hi) {
            int mid=(lo+hi+1)/2;
            if(canvas->TextWidth(first.SubString(1,mid))<=height)lo=mid;else hi=mid-1;
        }
        if(lo>0 && first[lo]>=0xd800 && first[lo]<=0xdbff)--lo;
        int word=lo;
        if(first[lo+1]!=' ' && first[lo+1]!='\t')
            while(word>0 && first[word]!=' ' && first[word]!='\t')--word;
        end=word>0?word:std::max(1,lo);
        first=text.SubString(1,end).TrimRight();
    } else if(!end) return {text};
    U rest=headerLine(text.SubString(end+1,text.Length())).Trim();
    return {first,fitHeaderText(canvas,rest,height)};
}
inline int inputNameHeight(TCanvas *canvas,int ppi) {
    return std::max(MulDiv(26,ppi,96),canvas->TextHeight("Ag")+MulDiv(8,ppi,96));
}
inline U portHeader(const U &name, const U &comment) {
    return comment.IsEmpty() ? name : name + L"\n" + comment;
}

// The rotated font occupies one em per line. TextHeight also includes font leading.
inline int portLineWidth(TCanvas *canvas) { return std::max(1, std::abs(canvas->Font->Height)); }
inline int portInset(int ppi) { return std::max(1,MulDiv(2,ppi,96)); }
inline void fitPortHeaders(TStringGrid *grid, int first, int count, int ppi) {
    auto canvas=grid->Canvas;
    canvas->Font->Assign(grid->Font);
    const int pad=2*portInset(ppi), nameHeight=inputNameHeight(canvas,ppi);
    const int minHeight=MulDiv(36,ppi,96);
    const int maxHeight=std::max(minHeight,std::min(MulDiv(90,ppi,96),
                                 grid->ClientHeight-nameHeight-4*grid->DefaultRowHeight));
    int height=0;
    std::vector<U> comments;
    for(int c=first;c<first+count;++c) {
        U label=grid->Cells[c][0];
        int split=label.Pos(L"\n");
        U comment=split ? label.SubString(split+1,label.Length()) : U();
        comments.push_back(comment);
        if(!comment.IsEmpty())
            height=std::max(height,std::min(maxHeight,std::max(minHeight,canvas->TextWidth(headerLine(comment))+pad)));
    }
    if(grid->RowHeights[0]!=nameHeight+height) grid->RowHeights[0]=nameHeight+height;
    int lineHeight=portLineWidth(canvas);
    for(int i=0;i<count;++i) {
        if(comments[i].IsEmpty()) continue;
        int lines=headerLines(canvas,comments[i],std::max(1,height-pad)).size();
        int width=lines*lineHeight+(lines-1)*std::max(1,MulDiv(1,ppi,96))+pad;
        if(grid->ColWidths[first+i]<width) grid->ColWidths[first+i]=width;
    }
}

// The name is drawn horizontally by the caller in the remaining text rectangle.
inline void drawPortComment(TCanvas *canvas,const TRect &rect,TRect &text,U &value,int ppi) {
    text.Top=std::max(rect.Top,rect.Bottom-inputNameHeight(canvas,ppi));
    int split=value.Pos(L"\n");
    if(!split) return;
    U comment=value.SubString(split+1,value.Length());
    value=value.SubString(1,split-1);
    int pad=portInset(ppi);
    auto lines=headerLines(canvas,comment,std::max(1L,text.Top-rect.Top-2*pad));
    // Grayscale antialiasing works in both orientations, including non-RGB panels.
    struct GdiText {
        ULONG_PTR token=0;
        GdiText(){ Gdiplus::GdiplusStartupInput init; Gdiplus::GdiplusStartup(&token,&init,nullptr); }
        ~GdiText(){ if(token) Gdiplus::GdiplusShutdown(token); }
    };
    static GdiText engine;
    Gdiplus::Graphics graphics(canvas->Handle);
    graphics.SetPageUnit(Gdiplus::UnitPixel);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.SetClip(Gdiplus::Rect(rect.Left,rect.Top,rect.Width(),text.Top-rect.Top));
    int h=portLineWidth(canvas), gap=std::max(1,MulDiv(1,ppi,96));
    int block=int(lines.size())*h+(int(lines.size())-1)*gap;
    graphics.TranslateTransform(float(rect.Left+(rect.Width()-block)/2),float(text.Top-pad));
    graphics.RotateTransform(-90);
    Gdiplus::Font font(L"Segoe UI",float(std::abs(canvas->Font->Height)),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush ink(Gdiplus::Color(255,32,32,32));
    float offset=0;
    for(const auto &line:lines) {
        graphics.DrawString(line.c_str(),line.Length(),&font,Gdiplus::PointF(0,offset),
                            Gdiplus::StringFormat::GenericTypographic(),&ink);
        offset+=h+gap;
    }
}
} // namespace t2f
