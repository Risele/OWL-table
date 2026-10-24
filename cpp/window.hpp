#pragma once
#include <vcl.h>
#include <Vcl.Grids.hpp>
#include <Vcl.ComCtrls.hpp>
#include <Vcl.Menus.hpp>
#include "core.hpp"
class EditorForm : public TForm {
    t2f::J state, clean;
    std::vector<t2f::J> undo, redo;
    bool rendering = false, owned = false, busy = false;
    UnicodeString source, lastOutput;
    std::string loadedSheet;
    TPanel *top = nullptr, *bottom = nullptr, *messages = nullptr;
    TEdit *path = nullptr, *macroName = nullptr, *fbdStep = nullptr, *stStep = nullptr;
    TComboBox *sheet = nullptr, *fbdMode = nullptr, *stMode = nullptr, *typeEditor = nullptr;
    TLabel *sheetLabel = nullptr, *summary = nullptr;
    TMemo *status = nullptr;
    TCheckBox *fbd = nullptr, *st = nullptr;
    TButton *generateButton = nullptr;
    TPageControl *tabs = nullptr;
    TStringGrid *fields = nullptr, *records = nullptr;
    TMenuItem *undoItem, *redoItem, *reloadItem, *folderItem;
    TButton *button(TWinControl *, const UnicodeString &, int, int, int, int);
    TMenuItem *menu(TMenuItem *, const UnicodeString &, int, Word = 0);
    void render(bool resizeColumns = false);
    void commit(const t2f::J &);
    void validate();
    void newTable();
    bool discard();
    bool save();
    bool load(const UnicodeString &, const std::string &selected = "");
    void copyCells();
    void pasteCells();
    TStringGrid *grid();
    void generate();
    void setDocument(const t2f::Book &, const UnicodeString &, bool);
    void error(const UnicodeString &);
    void updateCaption();
    void __fastcall action(TObject *);
    void __fastcall edited(TObject *, int, int, const UnicodeString);
    void __fastcall selectCell(TObject *, int, int, bool &);
    void __fastcall selectType(TObject *);
    void __fastcall modeChanged(TObject *);
    void __fastcall sheetChanged(TObject *);
    void __fastcall resized(TObject *);
    void __fastcall closing(TObject *, bool &);
    void __fastcall keyDown(TObject *, WORD &, TShiftState);
    void __fastcall drawCell(TObject *, int, int, const TRect &, TGridDrawState);

  protected:
    void __fastcall CreateParams(TCreateParams &params) override;

  public:
    __fastcall EditorForm(TComponent *owner);
    void smoke(const UnicodeString &folder);
};
