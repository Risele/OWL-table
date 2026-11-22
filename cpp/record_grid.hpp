#pragma once
#include <Vcl.Grids.hpp>

namespace t2f {
class RecordGrid : public TStringGrid {
    class CellEditor : public TInplaceEdit {
      public:
        __fastcall CellEditor(TComponent *owner) : TInplaceEdit(owner) { Alignment = taCenter; }
    };
  protected:
    TInplaceEdit *__fastcall CreateEditor() override { return new CellEditor(this); }
  public:
    __fastcall RecordGrid(TComponent *owner) : TStringGrid(owner) { DefaultColAlignment = taCenter; }
};
}
