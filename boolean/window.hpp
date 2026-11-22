#pragma once
#include <vcl.h>
#include <Vcl.Grids.hpp>
#include <Vcl.ComCtrls.hpp>
#include <Vcl.Menus.hpp>
#include "logic.hpp"
#include "engine/control.hpp"
#include <memory>
class GenerationWorker;
class BooleanForm : public TForm {
    friend class Workbench;
    t2f::J state, clean;
    std::vector<t2f::J> undo, redo;
    bool rendering = false, owned = false;
    int invalidRow = -1;
    t2f::U source;
    t2f::S loadedSheet;
    unsigned page=0;
    uint64_t revision=0,jobRevision=0;
    GenerationWorker *worker=nullptr;
    TTimer *poll=nullptr;
    TButton *cancelButton=nullptr;
    TLabel *pageLabel=nullptr;
    bool closePending=false;
    t2f::J lastReport;
    t2f::S lastJobError;
    void startGeneration(const t2f::U &,std::shared_ptr<b2f::engine::Control> control=nullptr);
    void busy(bool);
    void __fastcall generationTick(TObject *);
    TPanel *top = nullptr, *bottom = nullptr, *messages = nullptr;
    TEdit *path = nullptr, *macro = nullptr, *output = nullptr, *inverted = nullptr;
    TComboBox *count = nullptr, *sheet = nullptr;
    TLabel *sheetLabel = nullptr, *summary = nullptr;
    TCheckBox *fbd = nullptr, *st = nullptr;
    TButton *generateButton = nullptr, *expandButton = nullptr;
    TMemo *status = nullptr;
    TPageControl *tabs = nullptr;
    TStringGrid *fields = nullptr, *rules = nullptr;
    TComboBox *inputColor = nullptr;
    TButton *inputUp = nullptr, *inputDown = nullptr;
    struct InputDetails { t2f::S name, comment; unsigned color = 0; };
    std::vector<InputDetails> removedInputs;
    void rememberRemoved(const t2f::J &next);
    t2f::J resizedInputs(int count);
    TComboBox *columnColor = nullptr;
    void syncInputTools(int row);
    void moveInput(int column, int delta);
    void __fastcall selectInput(TObject *, int, int, bool &);
    bool resetDocument(int tag, const t2f::U &label);
    TMenuItem *examples;
    TMenuItem *undoItem = nullptr, *redoItem = nullptr;
    TButton *button(TWinControl *, const t2f::U &, int, int, int, int);
    TMenuItem *item(TMenuItem *, const t2f::U &, int, const t2f::U &shortcut = "");
    void render(bool resetInputWidths=false);
    void validate();
    void caption();
    void commit(const t2f::J &);
    void error(const t2f::U &, bool notify = true);
    bool discard();
    bool save();
    void setDocument(t2f::Book, const t2f::U &);
    void load(const t2f::U &, const t2f::S &selected = "");
    void generate();
    void clipboard(bool paste);
    void __fastcall action(TObject *);
    void __fastcall edited(TObject *, int, int, const UnicodeString);
    void setCellValue(int, int, const t2f::U &);
    void cycleCell(int, int);
    void __fastcall cellMouseUp(TObject *, TMouseButton, TShiftState, int, int);
    void __fastcall settings(TObject *);
    void __fastcall resized(TObject *);
    void __fastcall closing(TObject *, bool &);
    void __fastcall keyDown(TObject *, WORD &, TShiftState);
    void __fastcall drawCell(TObject *, int, int, const TRect &, TGridDrawState);

  protected:
    void __fastcall CreateParams(TCreateParams &) override;

  public:
    __fastcall BooleanForm(TComponent *);
    __fastcall ~BooleanForm();
    void smoke(const t2f::U &);
};
