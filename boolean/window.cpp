#include <vcl.h>
#pragma hdrstop
#include "window.hpp"
#include "../cpp/editor_bar.hpp"
#include "../cpp/editor_dialog.hpp"
#include "../cpp/port_headers.hpp"
#include "../cpp/record_grid.hpp"
#include "../cpp/input_color_box.hpp"
#include "editor_model.hpp"
#include "engine/export.hpp"
#include <atomic>
#include <Vcl.Dialogs.hpp>
#include <Vcl.Clipbrd.hpp>
#include <algorithm>
#include <functional>
#include <memory>
using namespace t2f;

static void inputHeaders(TStringGrid *grid,const J &state,int ppi,int firstNew=16) {
    int count=state.at("inputs").size();
    grid->Canvas->Font->Assign(grid->Font);
    for(int i=0;i<count;++i) {
        U name=wide(state.at("inputs")[i]);
        U comment=state.contains("input_comments") ? wide(state.at("input_comments")[i]) : U();
        U label=portHeader(name,comment);
        if(i>=firstNew || grid->Cells[i+1][0]!=label) {
            U old=grid->Cells[i+1][0];
            int split=old.Pos(L"\n");
            if(i>=firstNew || (split ? old.SubString(1,split-1) : old)!=name) {
                int width=std::min(MulDiv(150,ppi,96),grid->Canvas->TextWidth(name)+2*portInset(ppi)+MulDiv(2,ppi,96));
                if(grid->ColWidths[i+1]<width) grid->ColWidths[i+1]=width;
            }
            grid->Cells[i+1][0]=label;
        }
    }
    fitPortHeaders(grid,1,count,ppi);
}

static J newEditorRule(const J &state) {
    unsigned n=state.at("inputs").size(),count=state.at("rules").size();
    require(count<65536,u8"Не более 65536 полных строк.");
    if(count<4096)return {{"inputs",std::vector<S>(n,"X")},{"q","1"},{"comment",""}};
    std::set<unsigned> used;
    for(auto &r:state.at("rules")) {
        unsigned k=0;
        for(unsigned i=0;i<n;++i) {
            require(r["inputs"][i]=="0" || r["inputs"][i]=="1",u8"Более 4096 строк допускаются только с полностью заданными входами 0/1.");
            if(r["inputs"][i]=="1")k|=1U<<i;
        }
        used.insert(k);
    }
    unsigned k=0;while(k<(1U<<n) && used.count(k))++k;
    require(k<(1U<<n),u8"Все комбинации уже присутствуют в таблице.");
    J bits=J::array();for(unsigned i=0;i<n;++i)bits.push_back((k>>i)&1?"1":"0");
    return {{"inputs",bits},{"q","0"},{"comment",""}};
}

static bool hasAppendRow(const J &document) {
    return !document.contains("truth") && document.at("rules").size()<65536;
}
static unsigned editorPages(const J &document) {
    unsigned rows=b2f::editor_row_count(document)+(hasAppendRow(document)?1:0);
    return std::max(1U,(rows+255)/256);
}

static void saveEditorJson(const U &path,const J &state) {
    b2f::parse(state);
    U absolute=ExpandFileName(path);wchar_t temporary[MAX_PATH];
    require(GetTempFileNameW(ExtractFileDir(absolute).c_str(),L"b2f",0,temporary)!=0,"Cannot create JSON staging file");
    try {
        write_file(U(temporary),state.dump(2));
        require(J::parse(read_file(U(temporary)))==state,"JSON write verification failed");
        require(MoveFileExW(temporary,absolute.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Cannot save JSON document");
    } catch(...) { DeleteFileW(temporary);throw; }
}

// The worker owns only values. UI controls and completion handling stay on the main thread.
class GenerationWorker : public TThread {
    J snapshot;
    U destination;
    S name;
    bool fbd,st;
  protected:
    void __fastcall Execute() override {
        b2f::engine::ControlScope scope(control.get());
        try { result=b2f::generate_strategy(snapshot,destination,name,fbd,st,b2f::engine::default_policy()); }
        catch(const b2f::engine::Cancelled &) { failure=u8"Генерация отменена. Прежние результаты сохранены."; }
        catch(const Exception &e) { failure=utf8(e.Message); }
        catch(const std::exception &e) { failure=e.what(); }
        catch(...) { failure="Unknown generation error"; }
        done.store(true);
    }
  public:
    std::shared_ptr<b2f::engine::Control> control;
    std::atomic<bool> done{false};
    J result;
    S failure;
    GenerationWorker(J state,U path,S macro,bool wantFbd,bool wantSt,
                     std::shared_ptr<b2f::engine::Control> c)
      :TThread(true),snapshot(std::move(state)),destination(path),name(macro),fbd(wantFbd),st(wantSt),control(c) { FreeOnTerminate=false; }
};

void BooleanForm::busy(bool active) {
    top->Enabled=!active;tabs->Enabled=!active;
    for(int i=0;i<bottom->ControlCount;++i)bottom->Controls[i]->Enabled=!active;
    for(int i=0;Menu && i<Menu->Items->Count;++i)Menu->Items->Items[i]->Enabled=!active;
    generateButton->Visible=!active;cancelButton->Visible=active;cancelButton->Enabled=active;
    if(!active)validate();
}
void BooleanForm::startGeneration(const U &target,std::shared_ptr<b2f::engine::Control> control) {
    require(!worker,"Generation already running");b2f::parse(state,utf8(macro->Text));
    // Commit an active comment editor to the model before taking the immutable copy.
    rules->EditorMode=false;fields->EditorMode=false;
    J snapshot=state;
    if(!control)control=std::make_shared<b2f::engine::Control>();
    U base=ChangeFileExt(ExpandFileName(target),"");
    if(!source.IsEmpty())for(auto ext:{".tple",".fbst"})
        require((base+wide(ext)).LowerCase()!=ExpandFileName(source).LowerCase(),"Output would overwrite input");
    lastReport=nullptr;lastJobError.clear();jobRevision=revision;
    worker=new GenerationWorker(snapshot,target,utf8(macro->Text),fbd->Checked,st->Checked,control);
    busy(true);poll->Enabled=true;
    try { worker->Start(); } catch(...) { poll->Enabled=false;delete worker;worker=nullptr;busy(false);throw; }
}
void __fastcall BooleanForm::generationTick(TObject *) {
    if(!worker)return;
    int phase=worker->control->phase.load();
    static const char *labels[]={u8"Подготовка",u8"Подготовка",u8"Поиск вариантов",u8"Сравнение вариантов",u8"Создание файлов",u8"Проверка всех комбинаций",u8"Сохранение комплекта",u8"Завершение"};
    cancelButton->Enabled=phase<6;
    if(!worker->done.load()) { status->Font->Color=clWindowText;status->Text=wide(labels[std::min(7,phase)]);return; }
    worker->WaitFor();lastJobError=worker->failure;lastReport=worker->result;
    delete worker;worker=nullptr;poll->Enabled=false;busy(false);
    if(jobRevision!=revision) { lastReport=nullptr;lastJobError=u8"Документ изменился: результат относится к предыдущей версии."; }
    if(!lastJobError.empty())error(wide(lastJobError));
    else {
        S text=u8"Готово. Проверены все "+std::to_string(1U<<state["inputs"].size())+u8" комбинаций обоих выходов.\r\n";
        for(auto &t:lastReport.at("targets")) {
            auto &a=t.at("alternatives").at(t.at("selected").get<size_t>());
            text+=t.at("language").get<S>()+": "+a.at("method").get<S>()+
                  (a.at("inverted_first").get<bool>()?u8", сначала инверсный выход":u8", сначала основной выход")+
                  u8"; уровней: "+a.at("metrics").at("height").dump()+".\r\n";
        }
        status->Text=wide(text);
    }
    if(closePending) { closePending=false;Close(); }
}

class CycleGrid : public RecordGrid {
    int pressedCol=-1,pressedRow=-1;
  protected:
    void __fastcall WndProc(TMessage &message) override {
        if(message.Msg==WM_LBUTTONDOWN || message.Msg==WM_LBUTTONDBLCLK)
            MouseToCell(short(LOWORD(message.LParam)),short(HIWORD(message.LParam)),pressedCol,pressedRow);
        TStringGrid::WndProc(message);
    }
    bool __fastcall SelectCell(int col,int row) override {
        if(!TStringGrid::SelectCell(col,row))return false;
        // Toggle cells must never enter VCL's inplace editing mode on a repeat click.
        Options=col==ColCount-1 ? Options<<goEditing : Options>>goEditing;
        if(cellSelected)cellSelected(col,row);
        return true;
    }
    bool __fastcall CanEditShow() override {
        return Col == ColCount - 1 && !Cells[0][Row].IsEmpty() && TStringGrid::CanEditShow();
    }
  public:
    std::function<void(int,int)> cellSelected;
    bool pressedCell(int col,int row) const { return col==pressedCol && row==pressedRow; }
    void resizeHandler(System::Classes::TNotifyEvent handler) { OnResize=handler; }
    __fastcall CycleGrid(TComponent *owner) : RecordGrid(owner) {}
};
__fastcall BooleanForm::~BooleanForm() {
    // VCL destroys child controls in the base destructor, after our JSON members.
    // Resizing/hiding their editors must no longer call back into this model.
    rendering=true;OnResize=nullptr;
    if(rules) {
        static_cast<CycleGrid *>(rules)->cellSelected=nullptr;
        static_cast<CycleGrid *>(rules)->resizeHandler(nullptr);
        rules->OnTopLeftChanged=nullptr;rules->OnMouseUp=nullptr;
        rules->OnSetEditText=nullptr;rules->OnDrawCell=nullptr;
    }
    if(fields) { fields->OnSelectCell=nullptr; fields->OnSetEditText=nullptr;fields->OnDrawCell=nullptr;fields->OnMouseUp=nullptr; }
    if(poll)poll->Enabled=false;
    if(worker) { worker->control->cancel();worker->WaitFor();delete worker;worker=nullptr; }
}
enum {
    New = 1,
    Open,
    Save,
    Reload,
    Example,
    Undo,
    Redo,
    Help,
    Add,
    Duplicate,
    RemoveRule,
    Expand,
    Compact,
    Generate, Cancel, PreviousPage, NextPage, MoveUp, MoveDown, InputUp, InputDown,
    CodeExample, CompareExample, RowMaskExample, ColumnLeft, ColumnRight, FirstInputColor
};
TButton *BooleanForm::button(TWinControl *parent, const U &text, int tag, int x, int y, int width) {
    auto b = new TButton(this);
    b->Parent = parent;
    b->Caption = text;
    b->Tag = tag;
    b->SetBounds(x, y, width, 25);
    b->OnClick = action;
    return b;
}
TMenuItem *BooleanForm::item(TMenuItem *parent, const U &text, int tag, const U &shortcut) {
    auto m = new TMenuItem(this);
    m->Caption = text;
    m->Tag = tag;
    if (!shortcut.IsEmpty())
        m->ShortCut = TextToShortCut(shortcut);
    m->OnClick = action;
    parent->Add(m);
    return m;
}
void __fastcall BooleanForm::CreateParams(TCreateParams &p) {
    TForm::CreateParams(p);
    if (Parent) return;
    p.WndParent = 0;
    p.ExStyle = (p.ExStyle | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW;
}
__fastcall BooleanForm::BooleanForm(TComponent *owner) : TForm(owner, 0) {
    Width = 1120;
    Height = 720;
    Constraints->MinWidth = 920;
    Constraints->MinHeight = 560;
    Position = poScreenCenter;
    Font->Name = "Segoe UI";
    Font->Size = 9;
    Color = (TColor)RGB(246, 246, 246);
    KeyPreview = true;
    OnKeyDown = keyDown;
    OnCloseQuery = closing;
    OnResize = resized;
    auto main = new TMainMenu(this);
    Menu = main;
    auto file = new TMenuItem(this);
    file->Caption = wide(u8"Файл");
    main->Items->Add(file);
    item(file, wide(u8"Новая таблица"), New, "Ctrl+N");
    item(file, wide(u8"Открыть таблицу…"), Open, "Ctrl+O");
    item(file, wide(u8"Сохранить таблицу…"), Save, "Ctrl+S");
    item(file, wide(u8"Перечитать файл"), Reload);
    auto edit = new TMenuItem(this);
    edit->Caption = wide(u8"Правка");
    main->Items->Add(edit);
    undoItem = item(edit, wide(u8"Отменить"), Undo, "Ctrl+Z");
    redoItem = item(edit, wide(u8"Повторить"), Redo, "Ctrl+Y");
    auto help = new TMenuItem(this);
    help->Caption = wide(u8"Справка");
    main->Items->Add(help);
    item(help, wide(u8"Правила и форматы"), Help, "F1");
    examples = new TMenuItem(this);
    examples->Caption = wide(u8"Примеры");
    help->Add(examples);
    item(examples, wide(u8"Совпадение двух входов"), Example);
    item(examples, wide(u8"Разрешение пятибитного кода"), CodeExample);
    item(examples, wide(u8"Сравнение двух шестибитных кодов"), CompareExample);
    item(examples, wide(u8"Выбор по маскам строк"), RowMaskExample);
    top = new TPanel(this);
    top->Parent = this;
    top->Align = alTop;
    top->Height = 36;
    top->BevelOuter = bvNone;
    top->ParentBackground = false;
    top->Color = Color;
    button(top, wide(u8"Новая"), New, 6, 5, 60);
    button(top, wide(u8"Открыть…"), Open, 70, 5, 82);
    button(top, wide(u8"Сохранить…"), Save, 156, 5, 96);
    path = new TEdit(this);
    path->Parent = top;
    path->ReadOnly = true;
    path->SetBounds(260, 5, 490, 25);
    sheetLabel = new TLabel(this);
    sheetLabel->Parent = top;
    sheetLabel->Caption = wide(u8"Лист:");
    sheetLabel->SetBounds(758, 9, 36, 20);
    sheet = new TComboBox(this);
    sheet->Parent = top;
    sheet->Style = csDropDownList;
    sheet->SetBounds(798, 5, 200, 25);
    sheet->OnChange = settings;
    sheet->Visible = false;
    sheetLabel->Visible = false;
    messages = new TPanel(this);
    messages->Parent = this;
    messages->Align = alBottom;
    messages->Height = 70;
    messages->BevelOuter = bvNone;
    messages->ParentBackground = false;
    messages->Color = Color;
    messages->Padding->SetBounds(8, 4, 8, 8);
    status = new TMemo(this);
    status->Parent = messages;
    status->Align = alClient;
    status->ReadOnly = true;
    status->BorderStyle = bsNone;
    status->Color = Color;
    status->ScrollBars = ssVertical;
    status->TabStop = false;
    bottom = new TPanel(this);
    bottom->Parent = this;
    bottom->Align = alBottom;
    bottom->Height = 105;
    bottom->BevelOuter = bvNone;
    bottom->ParentBackground = false;
    bottom->Color = Color;
    bottom->Top = messages->Top - bottom->Height;
    auto label = [&](const U &text, int x, int y, int w) {
        auto l = new TLabel(this);
        l->Parent = bottom;
        l->Caption = text;
        l->SetBounds(x, y, w, 20);
    };
    label(wide(u8"Макрос:"), 8, 11, 74);
    macro = new TEdit(this);
    macro->Parent = bottom;
    macro->Text = "BoolLogic";
    macro->MaxLength = 64;
    macro->SetBounds(85, 7, 285, 25);
    macro->OnChange = settings;
    fbd = new TCheckBox(this);
    fbd->Parent = bottom;
    fbd->Caption = "FBD";
    fbd->Checked = true;
    fbd->SetBounds(390, 8, 65, 24);
    fbd->OnClick = settings;
    st = new TCheckBox(this);
    st->Parent = bottom;
    st->Caption = "ST";
    st->Checked = true;
    st->SetBounds(460, 8, 60, 24);
    st->OnClick = settings;
    generateButton = button(bottom, wide(u8"Создать макросы…"), Generate, 900, 7, 170);
    auto automatic = new TLabel(this);
    automatic->Parent = bottom;
    automatic->Caption = wide(u8"Полная проверка, до 3 уровней");
    automatic->SetBounds(510, 47, 380, 20);
    summary = new TLabel(this);
    summary->Parent = bottom;
    summary->SetBounds(8, 79, 1000, 20);
    tabs = new TPageControl(this);
    tabs->Parent = this;
    tabs->Align = alClient;
    auto inputsPage = new TTabSheet(this);
    inputsPage->PageControl = tabs;
    inputsPage->Caption = wide(u8"Входы");
    auto inputsBar = new EditorBar(this);
    inputsBar->Parent = inputsPage;
    inputsBar->Align = alTop;
    inputsBar->Height = 38;
    inputsBar->BevelOuter = bvNone;
    auto countLabel = new TLabel(this);
    countLabel->Parent = inputsBar;
    countLabel->Caption = wide(u8"Число входов:");
    countLabel->SetBounds(6, 11, 100, 20);
    count = new TComboBox(this);
    count->Parent = inputsBar;
    count->Style = csDropDownList;
    count->SetBounds(110, 6, 80, 25);
    Canvas->Font->Assign(Font);
    countLabel->Width=Canvas->TextWidth(countLabel->Caption)+8;
    count->Left=countLabel->Left+countLabel->Width+6;
    inputsBar->Height=count->Height+12;
    for (int i = 1; i <= b2f::engine::max_inputs; ++i)
        count->Items->Add(IntToStr(i));
    count->OnChange = settings;
    countLabel->FocusControl=count;
    auto outputEdit = [&](const U &caption, TEdit *&edit) {
        auto l=new TLabel(this); l->Parent=inputsBar; l->Caption=caption;
        edit=new TEdit(this);edit->Parent=inputsBar;edit->MaxLength=64;edit->OnChange=settings;
        l->FocusControl=edit;return l;
    };
    auto qLabel=outputEdit(wide(u8"Выход"),output);
    auto nqLabel=outputEdit(wide(u8"НЕ"),inverted);
    inputUp = button(inputsBar, wide(u8"↑ Вверх"), InputUp, count->Left + count->Width + 16, 5, 82);
    inputDown = button(inputsBar, wide(u8"↓ Вниз"), InputDown, inputUp->Left + inputUp->Width + 4, 5, 82);
    inputUp->Hint = wide(u8"Переместить выбранный вход вверх (Alt+↑)");
    inputDown->Hint = wide(u8"Переместить выбранный вход вниз (Alt+↓)");
    inputUp->ShowHint = true;
    inputDown->ShowHint = true;
    inputColor = new InputColorBox(this, inputsBar, inputDown->Left + inputDown->Width + 16,
                                   count->Height, settings);
    inputsBar->configure({countLabel,count,qLabel,output,nqLabel,inverted,inputUp,inputDown,
                          static_cast<InputColorBox*>(inputColor)->label,inputColor});
    auto combinations = new TTabSheet(this);
    combinations->PageControl = tabs;
    combinations->Caption = wide(u8"Комбинации");
    auto bar = new EditorBar(this);
    bar->Parent = combinations;
    bar->Align = alTop;
    bar->Height = 37;
    bar->BevelOuter = bvNone;
    auto add = button(bar, wide(u8"Добавить"), Add, 4, 5, 90);
    auto duplicate=button(bar, wide(u8"Копировать"), Duplicate, 98, 5, 100);
    auto remove = button(bar, wide(u8"Удалить"), RemoveRule, 202, 5, 90);
    expandButton = button(bar, wide(u8"Все комбинации"), Expand, 310, 5, 145);
    auto compress = button(bar, wide(u8"Сжать с X"), Compact, 459, 5, 110);
    compress->Hint = wide(
        u8"Убрать дубликаты, объединить соседние включающие комбинации с X. Комментарии сохраняются; нулевые строки без комментариев удаляются. Ctrl+Z — отмена.");
    compress->ShowHint = true;
    auto makeGrid = [&](TWinControl *parent, bool cycle) {
        TStringGrid *g = cycle ? new CycleGrid(this) : new TStringGrid(this);
        g->Parent = parent;
        g->Align = alClient;
        g->ColCount = 2;
        g->RowCount = 2;
        g->FixedCols = 1;
        g->FixedRows = 1;
        g->DefaultRowHeight = 26;
        g->DefaultColWidth = 70;
        g->BorderStyle = bsNone;
        g->Color = clWhite;
        g->FixedColor = (TColor)RGB(237, 237, 237);
        g->DefaultDrawing = false;
        g->Options = (g->Options << goEditing << goColSizing << goRangeSelect) >> goVertLine >>
                     goHorzLine >> goFixedVertLine >> goFixedHorzLine;
        if(cycle)g->Options=g->Options>>goEditing;
        g->OnSetEditText = edited;
        g->OnDrawCell = drawCell;
        return g;
    };
    fields = makeGrid(inputsPage, false);
    fields->OnSelectCell = selectInput;
    fields->OnMouseUp = cellMouseUp;
    rules = makeGrid(combinations, true);
    rules->OnMouseUp = cellMouseUp;
    auto columnMenu = new TPopupMenu(this);
    columnMenu->AutoPopup = false;
    rules->PopupMenu = columnMenu;
    item(columnMenu->Items,wide(u8"Переместить влево"),ColumnLeft);
    item(columnMenu->Items,wide(u8"Переместить вправо"),ColumnRight);
    addInputColorMenu(columnMenu->Items, FirstInputColor, action);
    static_cast<CycleGrid *>(rules)->resizeHandler(resized);
    auto pager=new EditorBar(this);pager->Parent=combinations;pager->Align=alBottom;
    auto prev=button(pager,wide(u8"‹ Стр."),PreviousPage,0,0,55);
    auto nextPage=button(pager,wide(u8"Стр. ›"),NextPage,0,0,55);
    pageLabel=new TLabel(this);pageLabel->Parent=pager;pageLabel->Caption="1 / 1";
    pager->configure({prev,pageLabel,nextPage});
    auto left=button(bar,wide(u8"← Столбец"),ColumnLeft,720,5,85);
    auto right=button(bar,wide(u8"Столбец →"),ColumnRight,810,5,85);
    auto up=button(bar,wide(u8"↑ Строка"),MoveUp,900,5,75);
    auto down=button(bar,wide(u8"Строка ↓"),MoveDown,980,5,75);
    columnColor=new InputColorBox(this,bar,0,25,settings);
    bar->configure({add,duplicate,remove,up,down,left,right,static_cast<InputColorBox*>(columnColor)->label,
                    columnColor,expandButton,compress});
    up->Hint=wide(u8"Переместить строку вверх (Alt+↑)");
    down->Hint=wide(u8"Переместить строку вниз (Alt+↓)");
    up->ShowHint=true;
    down->ShowHint=true;
    static_cast<CycleGrid *>(rules)->cellSelected=[this,left,right,up,down,duplicate](int col,int row) {
        if(rendering || state.is_null())return;
        int inputs=state.at("inputs").size();
        columnColor->Enabled=col>=1 && col<=inputs;
        columnColor->ItemIndex=columnColor->Enabled && state.contains("input_colors") ?
                              state["input_colors"][col-1].get<int>() : 0;
        left->Enabled=col>1 && col<=inputs;
        right->Enabled=col>=1 && col<inputs;
        int position=int(page)*256+row-1;
        int total=b2f::editor_row_count(state);
        bool exists=row>0 && position<total;
        duplicate->Enabled=exists && hasAppendRow(state);
        up->Enabled=exists && position>0;
        down->Enabled=exists && position+1<total;
    };
    rules->ShowHint=true;
    rules->Hint=wide(u8"Правый клик по входу — порядок и цвет столбца. Столбец: Alt+← / Alt+→. Строка: Alt+↑ / Alt+↓. Клик под последней строкой — добавить.");
    cancelButton=button(bottom,wide(u8"Отменить"),Cancel,720,7,170);cancelButton->Visible=false;
    poll=new TTimer(this);poll->Enabled=false;poll->Interval=50;poll->OnTimer=generationTick;
    state = b2f::fresh(5,false);
    clean = state;
    render();
    tabs->ActivePage = combinations;
}
void BooleanForm::caption() {
    Caption = (state != clean ? "* " : "") +
              (source.IsEmpty() ? wide(u8"Новая таблица") : ExtractFileName(source)) +
              wide(u8" — ") + Application->Title;
    undoItem->Enabled = !undo.empty();
    redoItem->Enabled = !redo.empty();
}
void BooleanForm::error(const U &message, bool notify) {
    if (notify && Parent) editorDialog(message, mtError, TMsgDlgButtons() << mbOK, 0);
    status->Font->Color = (TColor)RGB(175, 30, 30);
    status->Text = message;
}
void BooleanForm::validate() {
    if (state.is_null())
        return;
    invalidRow = -1;
    try {
        require(!macro->Text.Trim().IsEmpty(),u8"Введите имя макроса.");
        auto s = b2f::parse(state, utf8(macro->Text));
        require(fbd->Checked || st->Checked, u8"Выберите FBD и/или ST.");
        generateButton->Enabled = true;
        status->Font->Color = (TColor)RGB(32, 32, 32);
        status->Text = wide(state.contains("truth") ?
            u8"Все комбинации: редактируются Q и комментарий. Удалённая комбинация даёт Q=0.\r\n«К правилам» — возврат к редактированию входов. Alt+↑/↓ — переместить строку." :
            u8"0 / 1 — состояние входа; X — вход не влияет. Q=0, если ни одна строка не совпала.\r\nПересечения с одинаковым Q разрешены; с разным Q — ошибка. Клик или пробел: X → 1 → 0 → X.");
    } catch (std::exception &e) {
        generateButton->Enabled = false;
        S message = e.what();
        if (message.find(u8"Правило ") == 0)
            try {
                invalidRow = std::stoi(message.substr(S(u8"Правило ").size()));
            } catch (...) {
            }
        error(wide(message), false);
    }
    int n = state["inputs"].size();
    expandButton->Enabled = true;
    expandButton->Caption=wide(state.contains("truth") ? u8"К правилам" : u8"Все комбинации");
    count->Enabled = !state.contains("truth") && state.at("rules").size()<=4096;
    expandButton->ShowHint = true;
    expandButton->Hint = wide(u8"Плотная таблица до 65536 комбинаций, по 256 строк на странице. Входы задают индекс; редактируются Q и комментарий.");
    summary->Caption =
        wide(u8"Входов: " + std::to_string(n) + (state.contains("truth") ? u8"; строк: " : u8"; правил: ") +
             std::to_string(state.contains("truth") ? b2f::editor_row_count(state) : state["rules"].size()) + u8"; возможных комбинаций: " +
             std::to_string(uint64_t(1) << n) + u8". Инверсия выхода формируется автоматически.");
    caption();
    rules->Invalidate();
}
void BooleanForm::render(bool resetInputWidths) {
    rendering = true;
    int n = state["inputs"].size();
    count->ItemIndex = n - 1;
    output->Text = wide(state["output"]);
    inverted->Text = wide(state["inverted"]);
    fields->RowCount = n + 1;
    if(fields->ColCount!=3) {
        fields->ColCount = 3;
        fields->ColWidths[0] = 55;
        fields->ColWidths[1] = 180;
        fields->ColWidths[2] = 600;
    }
    fields->Cells[0][0] = wide(u8"№");
    fields->Cells[1][0] = wide(u8"Имя входа");
    fields->Cells[2][0] = wide(u8"Комментарий входа");
    for (int i = 0; i < n; ++i) {
        fields->Cells[0][i + 1] = IntToStr(i + 1);
        fields->Cells[1][i + 1] = wide(state["inputs"][i]);
        fields->Cells[2][i + 1] = state.contains("input_comments") ? wide(state["input_comments"][i]) : U("");
    }
    int previousInputs=rules->Cells[0][0].IsEmpty() ? 0 : rules->ColCount-3;
    int qWidth=previousInputs<1?70:rules->ColWidths[previousInputs+1];
    int commentWidth=previousInputs<1?270:rules->ColWidths[previousInputs+2];
    rules->ColCount = n + 3;
    bool dense=state.contains("truth");J visible;
    unsigned total=b2f::editor_row_count(state),pages=editorPages(state);
    page=std::min(page,pages-1);
    if(dense)visible=b2f::editor_page(state,page);
    unsigned shown=std::min(256U,total-page*256);
    rules->RowCount=std::max(2,int(shown)+1+(!dense && shown<256 ? 1 : 0));
    pageLabel->Parent->Visible=pages>1;
    pageLabel->Caption=IntToStr(int(page+1))+" / "+IntToStr(int(pages));
    for(int i=0;i<ComponentCount;++i)if(auto b=dynamic_cast<TButton *>(Components[i])) {
        if(b->Tag==Add)b->Enabled=hasAppendRow(state);
        if(b->Tag==Compact)b->Enabled=!dense && total>0 && total<=4096;
        if(b->Tag==RemoveRule)b->Enabled=total>0;
        if(b->Tag==PreviousPage)b->Enabled=page>0;
        if(b->Tag==NextPage)b->Enabled=page+1<pages;
    }
    rules->Cells[0][0] = wide(u8"№");
    rules->Options=rules->Col==n+2 ? rules->Options<<goEditing : rules->Options>>goEditing;
    rules->ColWidths[0] = dense ? MulDiv(80,CurrentPPI,96) : MulDiv(50,CurrentPPI,96);
    int firstNew=resetInputWidths ? 0 : std::max(0,previousInputs);
    for (int i = firstNew; i < n; ++i)
        rules->ColWidths[i + 1] = MulDiv(22,rules->PixelsPerInch,96);
    inputHeaders(rules,state,rules->PixelsPerInch,firstNew);
    rules->Cells[n + 1][0] = wide(state["output"]);
    if(previousInputs!=n)rules->ColWidths[n + 1] = qWidth;
    rules->Cells[n + 2][0] = wide(u8"Комментарий");
    if(previousInputs!=n)rules->ColWidths[n + 2] = commentWidth;
    for (int r = 1; r < rules->RowCount; ++r) {
        if(dense) {
            if(r>int(visible.size())) {for(int c=0;c<rules->ColCount;++c)rules->Cells[c][r]="";continue;}
            auto &v=visible[r-1];rules->Cells[0][r]=IntToStr(v["index"].get<int>());
            for(int i=0;i<n;++i)rules->Cells[i+1][r]=wide(v["inputs"][i]);
            rules->Cells[n+1][r]=wide(v["q"]);rules->Cells[n+2][r]=wide(v["comment"]);continue;
        }
        int position=page*256+r-1;
        bool exists = position < (int)state["rules"].size();
        rules->Cells[0][r] = exists ? IntToStr(position+1) : U("+");
        for (int i = 0; i < n; ++i)
            rules->Cells[i + 1][r] = exists ? wide(state["rules"][position]["inputs"][i]) : U("");
        rules->Cells[n + 1][r] = exists ? wide(state["rules"][position]["q"]) : U("");
        rules->Cells[n + 2][r] = exists ? wide(state["rules"][position].value("comment", S())) : wide(u8"Добавить строку");
    }
    rendering = false;
    validate();
    syncInputTools(fields->Row);
    static_cast<CycleGrid *>(rules)->cellSelected(rules->Col,rules->Row);
    resized(this);
}
void BooleanForm::rememberRemoved(const J &next) {
    size_t old=state.at("inputs").size(), kept=next.at("inputs").size();
    if(kept>=old) return;
    if(removedInputs.size()<old) removedInputs.resize(old);
    for(size_t i=kept;i<old;++i) {
        auto &slot=removedInputs[i];
        slot.name=state["inputs"][i].get<S>();
        slot.comment=state.contains("input_comments") ? state["input_comments"][i].get<S>() : S();
        slot.color=state.contains("input_colors") ? state["input_colors"][i].get<unsigned>() : 0;
    }
}
J BooleanForm::resizedInputs(int count) {
    J next=b2f::resize(state,count);
    int old=state.at("inputs").size();
    if(count<=old || removedInputs.size()<=size_t(old)) return next;
    std::set<S> used={lower(next["output"]),lower(next["inverted"])};
    for(int i=0;i<old;++i) used.insert(lower(next["inputs"][i]));
    // Reserve remembered names before assigning defaults to any new slots.
    for(int i=old;i<count;++i) next["inputs"][i]="";
    for(int i=old;i<count && i<int(removedInputs.size());++i) {
        const auto &slot=removedInputs[i];
        if(!slot.name.empty() && used.insert(lower(slot.name)).second) next["inputs"][i]=slot.name;
        if(!slot.comment.empty()) {
            if(!next.contains("input_comments")) next["input_comments"]=std::vector<S>(count);
            next["input_comments"][i]=slot.comment;
        }
        if(slot.color) {
            if(!next.contains("input_colors")) next["input_colors"]=std::vector<unsigned>(count);
            next["input_colors"][i]=slot.color;
        }
    }
    for(int i=old;i<count;++i) if(next["inputs"][i]=="") {
        S name="I"+std::to_string(i+1);
        while(!used.insert(lower(name)).second) name="I"+name;
        next["inputs"][i]=name;
    }
    return next;
}
void BooleanForm::commit(const J &next) {
    if (state == next)
        return;
    require(!worker,"Generation is running");
    ++revision;
    undo.push_back(state);
    if (undo.size() > 200)
        undo.erase(undo.begin());
    size_t historyBytes=0;
    for(auto &v:undo)historyBytes+=b2f::editor_state_bytes(v);
    while(historyBytes>16*1024*1024 && undo.size()>1) { historyBytes-=b2f::editor_state_bytes(undo.front());undo.erase(undo.begin()); }
    redo.clear();
    rememberRemoved(next);
    state = next;
    caption();
}
bool BooleanForm::discard() {
    if (state == clean)
        return true;
    int result = editorDialog(wide(u8"Сохранить изменения таблицы?"), mtConfirmation,
                            TMsgDlgButtons() << mbYes << mbNo << mbCancel, 0);
    return result == mrNo || (result == mrYes && save());
}
bool BooleanForm::save() {
    b2f::parse(state);
    std::unique_ptr<TSaveDialog> d(new EditorFileDialog<TSaveDialog>(this));
    d->Title=wide(u8"Сохранить таблицу");
    d->Filter = wide(u8"Таблица Excel или JSON (*.xlsx;*.json)|*.xlsx;*.json");
    d->DefaultExt = ExtractFileExt(source).LowerCase()==".json" ? "json" : "xlsx";
    d->FileName = source.IsEmpty() ? U("Bool.xlsx")
                  : owned          ? source
                                   : ChangeFileExt(source, U("_Bool")+ExtractFileExt(source));
    d->Options = d->Options << ofOverwritePrompt;
    if (!d->Execute())
        return false;
    S extension=lower(utf8(ExtractFileExt(d->FileName)));
    require(extension==".xlsx" || extension==".json",u8"Используйте .xlsx или .json.");
    require(owned || source.IsEmpty() ||
                ExpandFileName(source).LowerCase() != ExpandFileName(d->FileName).LowerCase(),
            u8"Исходная книга сохраняется под другим именем.");
    if(extension==".json")saveEditorJson(d->FileName,state);else b2f::save(d->FileName, state);
    source = d->FileName;
    owned = true;
    loadedSheet = extension==".json" ? "" : u8"Данные";
    rendering = true;
    sheet->Items->Clear();
    sheet->Items->Add(wide(loadedSheet));
    sheet->ItemIndex = 0;
    sheet->Visible = false;
    sheetLabel->Visible = false;
    rendering = false;
    path->Text = source;
    clean = state;
    caption();
    resized(this);
    return true;
}
void BooleanForm::load(const U &filename, const S &selected) {
    setDocument(b2f::load(filename, selected),filename);
}
void BooleanForm::setDocument(t2f::Book book,const U &filename) {
    auto parsed = b2f::parse(book.state);
    require(!worker,"Generation is running");
    J normalized=b2f::canonical(parsed);
    if(!parsed.dense) {
        // The grid edits strings. Preserve optional fields and document metadata.
        for(size_t i=0;i<parsed.rules.size();++i) {
            auto &row=book.state["rules"][i];
            row["inputs"]=std::move(normalized["rules"][i]["inputs"]);
            row["q"]=std::move(normalized["rules"][i]["q"]);
        }
    }
    removedInputs.clear();
    state=parsed.dense?std::move(normalized):std::move(book.state);
    ++revision;page=0;
    clean = state;
    source = filename;
    owned = false;
    loadedSheet = book.sheet;
    undo.clear();
    redo.clear();
    rendering = true;
    sheet->Items->Clear();
    for (auto &n : book.sheets)
        sheet->Items->Add(wide(n));
    sheet->ItemIndex = sheet->Items->IndexOf(wide(book.sheet));
    sheet->Visible = book.sheets.size() > 1;
    sheetLabel->Visible = sheet->Visible;
    rendering = false;
    path->Text = source;
    render(true);
    if(generateButton->Enabled)status->Text = wide(lower(utf8(ExtractFileExt(filename)))==".json" ? u8"Таблица JSON загружена." :
        u8"Книга загружена. Формулы читаются по сохранённым значениям: после изменения формул пересчитайте и сохраните книгу в Excel.");
}
void __fastcall BooleanForm::settings(TObject *sender) {
    if (worker || rendering || state.is_null())
        return;
    try {
        if (sender == sheet) {
            S selected = utf8(sheet->Text);
            if (selected == loadedSheet)
                return;
            U original = source;
            if (discard()) {
                try {
                    load(original, selected);
                } catch (...) {
                    rendering = true;
                    sheet->ItemIndex = sheet->Items->IndexOf(wide(loadedSheet));
                    rendering = false;
                    throw;
                }
            } else {
                rendering = true;
                sheet->ItemIndex = sheet->Items->IndexOf(wide(loadedSheet));
                rendering = false;
            }
            return;
        }
        if (sender == inputColor || sender == columnColor) {
            int i = (sender == columnColor ? rules->Col : fields->Row) - 1;
            int id = static_cast<TComboBox*>(sender)->ItemIndex;
            if (i < 0 || i >= int(state.at("inputs").size()) || id < 0) return;
            J next = state;
            auto colors = next.value("input_colors", std::vector<unsigned>(next.at("inputs").size()));
            colors[i] = unsigned(id);
            if (std::any_of(colors.begin(), colors.end(), [](unsigned c){ return c != 0; }))
                next["input_colors"] = colors;
            else
                next.erase("input_colors");
            commit(next);
            syncInputTools(fields->Row);
            static_cast<CycleGrid*>(rules)->cellSelected(rules->Col,rules->Row);
            fields->Invalidate();
            rules->Invalidate();
            return;
        }
        if (sender == count) {
            const int requested=count->ItemIndex+1;
            count->ItemIndex=int(state.at("inputs").size())-1;
            commit(resizedInputs(requested));
            render();
            return;
        }
        if (sender == output || sender == inverted) {
            J next = state;
            next[sender == output ? "output" : "inverted"] =
                utf8(static_cast<TEdit *>(sender)->Text);
            commit(next);
            rendering = true;
            rules->Cells[state["inputs"].size() + 1][0] = output->Text;
            rendering = false;
        }
        validate();
    } catch (const Exception &e) {
        error(e.Message);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
}
void __fastcall BooleanForm::edited(TObject *sender, int col, int row, const U value) {
    if (worker || rendering || row < 1 || col < 1)
        return;
    J next = state;
    int n = state["inputs"].size();
    bool added = false;
    if (sender == fields) {
        if(col==1)next["inputs"][row-1]=utf8(value);
        else {if(!next.contains("input_comments"))next["input_comments"]=std::vector<S>(n);next["input_comments"][row-1]=utf8(value);}
    }
    else if(next.contains("truth")) {
        if(page*256+row-1>=b2f::editor_row_count(next))return;
        b2f::edit_dense_cell(next,b2f::editor_index(next,page*256+row-1),col,utf8(value));
    } else {
        row+=page*256;
        require(row<=65536,"Row limit");
        while ((int)next["rules"].size() < row) {
            next["rules"].push_back(newEditorRule(next));
            added = true;
        }
        if (col <= n)
            next["rules"][row - 1]["inputs"][col - 1] = utf8(value);
        else
            next["rules"][row - 1][col == n + 1 ? "q" : "comment"] = utf8(value);
    }
    commit(next);
    if (added) {
        render();
        return;
    }
    if (sender == fields) {
        rendering=true;
        inputHeaders(rules,state,rules->PixelsPerInch);
        rendering=false;
    }
    validate();
}
void BooleanForm::setCellValue(int col, int row, const U &text) {
    if (worker || rendering || row < 1 || row >= rules->RowCount || col < 1 ||
        col > (int)state["inputs"].size() + 1)
        return;
    if(state.contains("truth") && (col<=int(state["inputs"].size()) ||
        page*256+row-1>=b2f::editor_row_count(state)))return;
    rendering = true;
    rules->Cells[col][row] = text;
    rendering = false;
    edited(rules, col, row, text);
}
void BooleanForm::cycleCell(int col, int row) {
    int n = state["inputs"].size();
    if (row < 1 || row >= rules->RowCount || col < 1 || col > n + 1)
        return;
    unsigned position=page*256+row-1;
    if(position>=b2f::editor_row_count(state)) {
        if(!state.contains("truth"))setCellValue(col,row,"1");
        return;
    }
    if(state.contains("truth") && col<=n)return;
    // Cycle the document value, so drawing/editor text cannot reset the sequence.
    S old=state.contains("truth") ? S(1,state["truth"].get_ref<const S &>()[b2f::editor_index(state,position)]) :
        (col<=n ? state["rules"][position]["inputs"][col-1].get<S>() : state["rules"][position]["q"].get<S>());
    U next=col<=n ? (old=="1"?U("0"):old=="0"?U("X"):U("1")) : (old=="1"?U("0"):U("1"));
    setCellValue(col,row,next);
}
void __fastcall BooleanForm::cellMouseUp(TObject *sender, TMouseButton button, TShiftState shift,
                                       int x, int y) {
    int col, row;
    if (sender == fields) {
        fields->MouseToCell(x, y, col, row);
        if (button == mbLeft && col == 0 && row >= 1 && row < fields->RowCount)
            fields->Row = row;
        return;
    }
    rules->MouseToCell(x, y, col, row);
    if (button == mbRight && !worker && col >= 1 && col <= int(state.at("inputs").size())) {
        fields->Row = col;
        rules->Col = col;
        auto menu = rules->PopupMenu;
        menu->Items->Items[0]->Enabled = col > 1;
        menu->Items->Items[1]->Enabled = col < int(state["inputs"].size());
        auto colors = menu->Items->Items[2];
        for (int i=0; i<colors->Count; ++i)
            colors->Items[i]->Checked = i == inputColor->ItemIndex;
        auto point = rules->ClientToScreen(Point(x,y));
        menu->Popup(point.x,point.y);
        return;
    }
    if (button != mbLeft || shift.Contains(ssShift) || shift.Contains(ssCtrl))
        return;
    if(col<0 || !static_cast<CycleGrid *>(rules)->pressedCell(col,row))return;
    if (row == 0 && col >= 1 && col <= int(state.at("inputs").size())) {
        rules->Col = col;
        return;
    }
    if(!worker && hasAppendRow(state) && row>0 && page*256+row-1==b2f::editor_row_count(state)) {
        std::unique_ptr<TComponent> command(new TComponent(this));command->Tag=Add;action(command.get());return;
    }
    if (col != rules->Col || row != rules->Row)
        return; // A drag selecting a range must not modify its last cell.
    try {
        cycleCell(col, row);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
}
static S copyGridRange(TStringGrid *grid,int lastDataRow) {
    auto sel=grid->Selection;
    int firstCol=std::max(1,sel.Left),firstRow=std::max(1,sel.Top);
    S text;
    for(int r=firstRow;r<=std::min(int(sel.Bottom),lastDataRow);++r) {
        if(r>firstRow)text+="\r\n";
        for(int c=firstCol;c<=sel.Right;++c) {
            if(c>firstCol)text+='\t';
            S value=utf8(grid->Cells[c][r]);
            bool quote=(value.empty() && firstCol==sel.Right) ||
                       value.find_first_of("\t\r\n\"")!=S::npos;
            if(quote)text+='"';
            for(char x:value) {
                text+=x;
                if(x=='"')text+='"';
            }
            if(quote)text+='"';
        }
    }
    return text;
}
static std::vector<std::vector<S>> clipboardRows(const S &text) {
    if(text.empty())return {};
    std::vector<std::vector<S>> rows(1, std::vector<S>(1));
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '"' && (quoted || rows.back().back().empty())) {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
                rows.back().back() += '"';
                ++i;
            } else
                quoted = !quoted;
        } else if (!quoted && c == '\t')
            rows.back().push_back("");
        else if (!quoted && (c == '\r' || c == '\n')) {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            rows.push_back(std::vector<S>(1));
        } else
            rows.back().back() += c;
    }
    require(!quoted, u8"Незавершённая кавычка при вставке.");
    if (text.back()=='\n' || text.back()=='\r')
        rows.pop_back();
    return rows;
}
void BooleanForm::clipboard(bool paste) {
    auto grid = tabs->ActivePageIndex == 0 ? fields : rules;
    int n = state["inputs"].size();
    int dataRows=grid==fields?n:int(b2f::editor_row_count(state)-page*256);
    if(!paste) {
        S text=copyGridRange(grid,dataRows);
        if(!text.empty())Clipboard()->AsText=wide(text);
        return;
    }
    auto rows=clipboardRows(utf8(Clipboard()->AsText));
    if(rows.empty())return;
    J next = state;
    bool dense=next.contains("truth");
    int row0 = std::max(1, grid->Row), col0 = std::max(1, grid->Col);
    for (size_t r = 0; r < rows.size(); ++r) {
        require(row0 + r <= (grid == fields ? n : dense ? dataRows : 65536-page*256),
                u8"Вставка выходит за допустимое число строк.");
        require(col0 + rows[r].size() <= (size_t)(grid == fields ? 3 : n + 3),
                u8"Вставка выходит за число столбцов.");
        if (grid == rules && !dense)
            while (next["rules"].size() < page*256+row0+r)
                next["rules"].push_back(newEditorRule(next));
        for (size_t c = 0; c < rows[r].size(); ++c) {
            int rr = row0 + r - 1, cc = col0 + c;
            if (grid == fields) {
                if(cc==1)next["inputs"][rr]=rows[r][c];
                else {if(!next.contains("input_comments"))next["input_comments"]=std::vector<S>(n);next["input_comments"][rr]=rows[r][c];}
                continue;
            }
            rr+=page*256;
            if(dense)b2f::edit_dense_cell(next,b2f::editor_index(next,rr),cc,rows[r][c]);
            else if (cc <= n)
                next["rules"][rr]["inputs"][cc - 1] = rows[r][c];
            else
                next["rules"][rr][cc == n + 1 ? "q" : "comment"] = rows[r][c];
        }
    }
    commit(next);
    render();
}
void BooleanForm::generate() {
    b2f::parse(state, utf8(macro->Text));
    std::unique_ptr<TSaveDialog> d(new EditorFileDialog<TSaveDialog>(this));
    d->Title=wide(u8"Сохранить макрос");
    d->Filter =
        fbd->Checked ? wide(u8"Макрос FBD (*.tple)|*.tple") : wide(u8"Макрос ST (*.fbst)|*.fbst");
    d->DefaultExt = fbd->Checked ? "tple" : "fbst";
    d->FileName = macro->Text + (fbd->Checked ? ".tple" : ".fbst");
    d->Options = d->Options << ofOverwritePrompt;
    if (!d->Execute())
        return;
    U other = ChangeFileExt(d->FileName, fbd->Checked ? ".fbst" : ".tple");
    U existing;
    if (fbd->Checked && st->Checked && FileExists(other))
        existing = other + "\n";
    if (!existing.IsEmpty() &&
        editorDialog(wide(u8"Заменить также файлы?\n") + existing, mtConfirmation,
                   TMsgDlgButtons() << mbYes << mbNo, 0) != mrYes)
        return;
    startGeneration(d->FileName);
}

bool BooleanForm::resetDocument(int tag, const U &label) {
    if (!discard())
        return false;
    if (tag == CodeExample || tag == CompareExample || tag == RowMaskExample) {
        std::unique_ptr<TResourceStream> data(new TResourceStream((NativeUInt)HInstance,
            tag == CodeExample ? L"BIT_CODE" : tag == CompareExample ? L"BIT_COMPARE" : L"ROW_MASKS", MAKEINTRESOURCEW(10)));
        auto bytes = static_cast<const char *>(data->Memory);
        state = b2f::canonical(b2f::parse(J::parse(bytes, bytes + data->Size)));
    } else
        state = b2f::fresh(5,false);
    removedInputs.clear();
    ++revision;page=0;
    if (tag == Example)
        state["rules"].push_back({{"inputs", J::array({"X", "X", "X", "1", "1"})},
                                  {"q", "1"},
                                  {"comment", u8"Q = I4 AND I5"}});
    clean = state;
    source = "";
    path->Text = label;
    owned = false;
    undo.clear();
    redo.clear();
    sheet->Visible = false;
    sheetLabel->Visible = false;
    render(true);
    tabs->ActivePageIndex = 1;
    return true;
}
void __fastcall BooleanForm::action(TObject *sender) {
    int tag = static_cast<TComponent *>(sender)->Tag;
    try {
        if(tag==Cancel) { if(worker)worker->control->cancel();return; }
        if(worker)return;
        if(tag>=FirstInputColor && tag<FirstInputColor+inputColor->Items->Count) {
            inputColor->ItemIndex=tag-FirstInputColor;
            settings(inputColor);
            return;
        }
        if(tag==InputUp || tag==InputDown) {
            moveInput(fields->Row,tag==InputUp ? -1 : 1);
            return;
        }
        if(tag==ColumnLeft || tag==ColumnRight) {
            moveInput(rules->Col,tag==ColumnLeft ? -1 : 1);
            return;
        }
        if(tag==PreviousPage || tag==NextPage) {
            unsigned pages=editorPages(state);
            if(tag==PreviousPage && page>0)--page;else if(tag==NextPage && page+1<pages)++page;render();return;
        }
        if (tag == New || tag == Example || tag == CodeExample || tag == CompareExample || tag == RowMaskExample) {
            resetDocument(tag, tag == New ? wide(u8"Новая таблица") :
                          wide(u8"Пример — ") + StripHotkey(dynamic_cast<TMenuItem*>(sender) ?
                          static_cast<TMenuItem*>(sender)->Caption : wide(u8"Bool")));
            return;
        }
        if (tag == Open) {
            std::unique_ptr<TOpenDialog> d(new EditorFileDialog<TOpenDialog>(this));
            d->Title=wide(u8"Открыть таблицу");
            d->Filter = wide(u8"Таблица Excel или JSON (*.xlsx;*.json)|*.xlsx;*.json");
            if (d->Execute() && discard())
                load(d->FileName);
            return;
        }
        if (tag == Save) {
            save();
            return;
        }
        if (tag == Reload) {
            U original = source;
            S selected=loadedSheet;
            bool own = owned;
            if (!original.IsEmpty() && discard()) {
                load(original, selected);
                owned = own;
            }
            return;
        }
        if (tag == Generate) {
            if(generateButton->Enabled)generate();
            return;
        }
        if (tag == Help) {
            editorHelp("Bool", wide(
                u8"Входы и выходы\r\n"
                u8"Задайте от 1 до 16 входов. В условии строки 0 и 1 задают состояние, X допускает оба состояния. "
                u8"Выход Q принимает 0 или 1. Если совпадений нет, Q = 0. Инверсный выход равен НЕ Q.\r\n\r\n"
                u8"Правила\r\n"
                u8"Пересекающиеся строки должны задавать одинаковый Q. Клик или пробел меняет состояние входа. "
                u8"«Все комбинации» показывает по 256 строк на странице. I1 — младший бит. "
                u8"«Сжать с X» объединяет совместимые правила.\r\n\r\n"
                u8"Книги Excel\r\n"
                u8"Первые три строки содержат комментарии, типы (BOOL_IN, BOOL_OUT, BOOL_NOT, COMMENT) и имена. "
                u8"Можно открыть и лист с колонками Dec, I1…In, Q. Для формул используется сохранённое значение.\r\n\r\n"
                u8"Генерация\r\n"
                u8"Выберите FBD, ST или оба формата. Программа использует текущую таблицу и проверяет оба выхода "
                u8"на всех комбинациях. Допускается до трёх уровней вложенности макросов.\r\n\r\n"
                u8"Клавиши\r\nCtrl+C и Ctrl+V — копирование и вставка. Ctrl+Z и Ctrl+Y — отмена и повтор."));
            return;
        }
        if (tag == Undo || tag == Redo) {
            auto &from = tag == Undo ? undo : redo;
            auto &to = tag == Undo ? redo : undo;
            if (!from.empty()) {
                to.push_back(state);
                rememberRemoved(from.back());
                ++revision;state = from.back();
                from.pop_back();
                render();
            }
            return;
        }
        if (tag == Expand || tag == Compact) {
            commit(tag == Expand ? (state.contains("truth") ? b2f::rules_editor_state(state) : b2f::dense_editor_state(state)) : b2f::compact(state));
            page=0;
            render();
            return;
        }
        int n = state["inputs"].size(), row = page*256+rules->Row-1;
        if(state.contains("truth") && (tag==Add || tag==Duplicate))return;
        J next = state;
        if (tag == Add || tag == Duplicate) {
            require(next["rules"].size() < 65536, u8"Не более 65536 полных строк.");
            if(tag==Duplicate && (row<0 || row>=int(state["rules"].size())))return;
            if (tag == Duplicate)
                next["rules"].push_back(state["rules"][row]);
            else
                next["rules"].push_back(newEditorRule(next));
        } else if (tag == RemoveRule) {
            auto sel=rules->Selection;
            for(int r=std::min(int(page*256)+sel.Bottom,int(b2f::editor_row_count(next)))-1;r>=int(page*256)+std::max(1,sel.Top)-1;--r)b2f::erase_editor_row(next,r);
        } else if(tag==MoveUp || tag==MoveDown) {
            int target=row+(tag==MoveUp?-1:1);
            if(row<0 || row>=int(b2f::editor_row_count(next)) || target<0 || target>=int(b2f::editor_row_count(next)))return;
            b2f::move_editor_row(next,row,target);commit(next);page=target/256;render();rules->Row=target%256+1;return;
        }
        commit(next);
        if(tag==Add || tag==Duplicate)page=(b2f::editor_row_count(state)-1)/256;
        render();
        if ((tag == Add || tag == Duplicate) && !state["rules"].empty())
            rules->Row = (state["rules"].size()-1)%256+1;
    } catch (const Exception &e) {
        error(e.Message);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
}
void __fastcall BooleanForm::resized(TObject *) {
    if(ComponentState.Contains(csDestroying))return;
    if (!top || !bottom || !generateButton || !path || !sheet || !summary)
        return;
    int margin = std::max(6, macro->Top);
    Canvas->Font->Assign(Font);
    int gap=MulDiv(4,CurrentPPI,96);
    int pad=MulDiv(14,CurrentPPI,96);
    int buttonHeight=std::max(25,Canvas->TextHeight("Ag")+gap);
    int fileX=gap;
    if(pageLabel && expandButton) {
        for(int i=0;i<ComponentCount;++i) {
            auto b=dynamic_cast<TButton *>(Components[i]);
            if(b && b->Parent==top) {
                int w=Canvas->TextWidth(b->Caption)+pad;
                b->SetBounds(fileX,gap,w,buttonHeight);fileX+=w+gap;
            }
        }
        static_cast<EditorBar*>(expandButton->Parent)->arrange();
        static_cast<EditorBar*>(count->Parent)->arrange();
        static_cast<EditorBar*>(pageLabel->Parent)->arrange();
        path->Left=fileX;
        top->Height=std::max(buttonHeight,path->Height)+2*gap;
        generateButton->Width=Canvas->TextWidth(generateButton->Caption)+pad;
        fitGridRows(fields);fitGridRows(rules);
    }
    sheet->Left = top->ClientWidth - sheet->Width - margin;
    sheetLabel->Left = sheet->Left - sheetLabel->Width - 5;
    path->Width =
        std::max(90, (sheet->Visible ? sheetLabel->Left : top->ClientWidth) - path->Left - margin);
    if(cancelButton)cancelButton->Left=bottom->ClientWidth-cancelButton->Width-margin;
    generateButton->Left = bottom->ClientWidth - generateButton->Width - margin;
    summary->Width = bottom->ClientWidth - summary->Left - margin;
    if(rules && !rendering && !state.is_null()) {
        rendering=true;
        inputHeaders(rules,state,rules->PixelsPerInch);
        rendering=false;
    }
}
void __fastcall BooleanForm::closing(TObject *, bool &can) {
    try {
        if(worker) { closePending=true;worker->control->cancel();can=false;return; }
        can = discard();
    } catch (std::exception &e) {
        error(wide(e.what()));
        can = false;
    }
}
void __fastcall BooleanForm::keyDown(TObject *, WORD &key, TShiftState shift) {
    if(worker) { key=0;return; }
    try {
        if (shift.Contains(ssAlt) && ((ActiveControl == fields && (key == VK_UP || key == VK_DOWN)) ||
                                      (ActiveControl == rules && (key == VK_LEFT || key == VK_RIGHT)))) {
            moveInput(ActiveControl == fields ? fields->Row : rules->Col,
                      key == VK_UP || key == VK_LEFT ? -1 : 1);
            key = 0;
            return;
        }
        if(ActiveControl==rules && shift.Contains(ssAlt) && (key==VK_UP || key==VK_DOWN)) {
            std::unique_ptr<TComponent> command(new TComponent(this));command->Tag=key==VK_UP?MoveUp:MoveDown;action(command.get());key=0;return;
        }
        if (shift.Contains(ssCtrl) &&
            (ActiveControl == rules || ActiveControl == fields)) {
            if(static_cast<TStringGrid *>(ActiveControl)->EditorMode)return;
            if (key == 'C' || key == 'V') {
                clipboard(key == 'V');
                key = 0;
            }
        } else if (ActiveControl == rules) {
            int c = rules->Col, r = rules->Row, n = state["inputs"].size();
            if (c > 0 && c <= n + 1 && r > 0) {
                if (key == VK_SPACE) {
                    cycleCell(c, r);
                    key = 0;
                } else if (key == '0' || key == '1' || key == 'X' ||
                           key == VK_NUMPAD0 || key == VK_NUMPAD1) {
                    if (key != 'X' || c <= n)
                        setCellValue(c, r, key == 'X' ? U("X") :
                            key == '1' || key == VK_NUMPAD1 ? U("1") : U("0"));
                    key = 0;
                }
            }
        }
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
}
void BooleanForm::syncInputTools(int row) {
    int n = state.at("inputs").size();
    inputUp->Enabled = row > 1 && row <= n;
    inputDown->Enabled = row >= 1 && row < n;
    inputColor->ItemIndex = row >= 1 && row <= n && state.contains("input_colors")
                          ? state["input_colors"][row-1].get<int>() : 0;
}
void __fastcall BooleanForm::selectInput(TObject *, int, int row, bool &) {
    if (!rendering && !state.is_null()) syncInputTools(row);
}
void BooleanForm::moveInput(int column, int delta) {
    int dest = column + delta, n = state.at("inputs").size();
    if (column < 1 || column > n || dest < 1 || dest > n) return;
    fields->EditorMode = false;
    auto next = state;
    b2f::swap_editor_inputs(next, column-1, dest-1);
    commit(next);
    int width = rules->ColWidths[column];
    rules->ColWidths[column] = rules->ColWidths[dest];
    rules->ColWidths[dest] = width;
    fields->Row = dest;
    rules->Col = dest;
    render();
}
void __fastcall BooleanForm::drawCell(TObject *sender, int col, int row, const TRect &rect,
                                      TGridDrawState drawState) {
    auto g = static_cast<TStringGrid *>(sender);
    auto canvas = g->Canvas;
    canvas->Brush->Color = row == 0 || col == 0                   ? (TColor)RGB(237, 237, 237)
                           : sender == rules && row == invalidRow-int(page*256) ? (TColor)RGB(255, 226, 226)
                           : drawState.Contains(gdSelected)       ? (TColor)RGB(221, 234, 246)
                                                                  : clWhite;
    int input = sender == rules ? col - 1 : row - 1;
    bool invalid = sender == rules && invalidRow > 0 && row == invalidRow-int(page*256);
    if (!invalid && state.contains("input_colors") && input >= 0 && input < int(state["input_colors"].size()))
        canvas->Brush->Color = groupFill(state["input_colors"][input].get<unsigned>(), canvas->Brush->Color);
    canvas->FillRect(rect);
    canvas->Font->Assign(g->Font);
    canvas->Font->Color = g->Cells[col][row].UpperCase() == "X" ? (TColor)RGB(110, 110, 110)
                                                                : (TColor)RGB(32, 32, 32);
    TRect text = rect;
    text.Left += sender==rules ? portInset(g->PixelsPerInch)+1 : MulDiv(7, CurrentPPI, 96);
    text.Right -= sender==rules ? portInset(g->PixelsPerInch) : MulDiv(5, CurrentPPI, 96);
    bool centered = row > 0 && g->DefaultColAlignment == taCenter;
    if (centered) text.Left = rect.Left + (rect.Right - text.Right);
    U value = g->Cells[col][row];
    if(sender==rules && row==0) drawPortComment(canvas,rect,text,value,g->PixelsPerInch);
    DrawTextW(canvas->Handle, value.c_str(), value.Length(), &text,
              (centered ? DT_CENTER : DT_LEFT) | DT_NOPREFIX | (row==0 && sender!=rules ? DT_WORDBREAK | DT_END_ELLIPSIS : DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS));
    canvas->Pen->Color = (TColor)RGB(221, 221, 221);
    canvas->MoveTo(rect.Left, rect.Bottom - 1);
    canvas->LineTo(rect.Right, rect.Bottom - 1);
    canvas->MoveTo(rect.Right - 1, rect.Top);
    canvas->LineTo(rect.Right - 1, rect.Bottom);
    if(g==rules && col==int(state.at("inputs").size())+1) inputDivider(canvas,rect,CurrentPPI);
}
void BooleanForm::smoke(const U &folder) {
    ForceDirectories(folder);
    if(b2f::arguments()[0]=="--smoke-editor") {
        state=b2f::fresh(2,false);
        state["rules"]={
            {{"inputs",{"0","X"}},{"q","0"},{"comment","stop"}},
            {{"inputs",{"0","X"}},{"q","0"},{"comment","stop"}},
            {{"inputs",{"0","X"}},{"q","0"},{"comment","interlock"}},
            {{"inputs",{"1","0"}},{"q","1"},{"comment","run"}},
            {{"inputs",{"1","0"}},{"q","1"},{"comment","run"}},
            {{"inputs",{"1","1"}},{"q","1"},{"comment","run"}}
        };
        state["input_comments"]={u8"Разрешение автоматического запуска",u8"Контроль готовности оборудования"};
        J original=state;render();tabs->ActivePageIndex=1;
        Position=poDesigned;Left=-32000;Top=-32000;Show();
        ShowWindow(Handle,SW_SHOWNOACTIVATE);Application->ProcessMessages();
        TButton *compress=nullptr;
        for(int i=0;i<ComponentCount;++i)if(auto b=dynamic_cast<TButton *>(Components[i]))
            if(b->Tag==Compact)compress=b;
        require(compress && compress->Enabled,"Compact button must be available");
        SendMessage(compress->Handle,BM_CLICK,0,0);Application->ProcessMessages();
        require(state["rules"].size()==2 && rules->RowCount==4,"Compact button must remove annotated Q=0 and Q=1 duplicates");
        require(state["rules"][0]["inputs"]==J::array({"0","X"}) && state["rules"][0]["q"]=="0" &&
                state["rules"][0]["comment"]=="interlock; stop","Zero duplicates must preserve distinct comments once");
        require(state["rules"][1]["inputs"]==J::array({"1","X"}) && state["rules"][1]["q"]=="1" &&
                state["rules"][1]["comment"]=="run","Positive duplicates and adjacent rows must merge with X");
        J compressed=state;
        SendMessage(compress->Handle,BM_CLICK,0,0);
        require(state==compressed,"Repeated compact must be stable");
        std::unique_ptr<TComponent> command(new TComponent(this));
        command->Tag=Undo;action(command.get());require(state==original,"Undo must restore all original rows and comments");
        command->Tag=Redo;action(command.get());require(state==compressed,"Redo must restore compacted rows");
        state["input_comments"][1]=S(2000,'W');render();
        for(int ppi:{96,144,192}) {
            inputHeaders(rules,state,ppi);
            int commentHeight=rules->RowHeights[0]-inputNameHeight(rules->Canvas,ppi);
            require(commentHeight>0 && commentHeight<=MulDiv(90,ppi,96),"Comment height must not exceed half the previous 180px limit");
        }
        Application->ProcessMessages();tabs->Realign();tabs->ActivePage->Realign();resized(this);rules->Repaint();
        for(int i=0;i<ComponentCount;++i)
            if(auto control=dynamic_cast<TWinControl *>(Components[i]))
                if(control->Visible)UpdateWindow(control->Handle);
        std::unique_ptr<Graphics::TBitmap> image(GetFormImage());
        image->SaveToFile(folder+"/headers-native.bmp");
        J imported=b2f::fresh(2,false);
        imported["rules"]={
            {{"inputs",{true,0}},{"q",1},{"comment",""}},
            {{"inputs",{u8"х",u8"истина"}},{"q",false},{"comment",""}},
            {{"inputs",{false,0}},{"q",0},{"comment",""}}
        };
        write_file(folder+"/typed.json",imported.dump());
        load(folder+"/typed.json");
        require(rules->Cells[1][1]=="1" && rules->Cells[2][1]=="0" && rules->Cells[3][1]=="1" &&
                rules->Cells[1][2]=="X" && rules->Cells[2][2]=="1" && rules->Cells[3][2]=="0" && state==clean,
                "JSON values must open as editable cells in a clean document");
        write_file(folder+"/broken.json","{");
        bool rejected=false;
        try {load(folder+"/broken.json");}catch(const std::exception &) {rejected=true;}
        require(rejected && state==clean && source==folder+"/typed.json","Invalid file must preserve the open document");
        count->ItemIndex=0;settings(count);
        require(count->ItemIndex==1 && rules->ColCount==5 && state==clean,
                "Rejected resize must leave input count and columns in sync");
        macro->Text="";require(!generateButton->Enabled,"Empty macro name must disable generation");
        load(folder+"/typed.json");
        require(status->Text.Pos(wide(u8"Введите имя"))>0,"Load must retain the validation message");
        macro->Text="Boolean";require(generateButton->Enabled,"Valid macro name must enable generation");
        rules->SetFocus();
        auto mouse=[&](UINT msg,int col,int row,WPARAM flags) {
            auto cell=rules->CellRect(col,row);
            POINT point={cell.Left+8,cell.Top+cell.Height()/2};
            HWND target=GetCapture();
            if(!target)target=ChildWindowFromPointEx(rules->Handle,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED);
            MapWindowPoints(rules->Handle,target,&point,1);
            SendMessage(target,msg,flags,MAKELPARAM(point.x,point.y));
            Application->ProcessMessages();
        };
        for(const char *value:{"1","0","X"}) {
            mouse(WM_MOUSEMOVE,1,2,0);
            mouse(WM_LBUTTONDOWN,1,2,MK_LBUTTON);
            mouse(WM_LBUTTONUP,1,2,0);
            require(state["rules"][1]["inputs"][0]==value,S("Repeated clicks expected ")+value+
                " got "+state["rules"][1]["inputs"][0].dump()+", col="+std::to_string(rules->Col)+
                " row="+std::to_string(rules->Row)+" pressed="+
                std::to_string(static_cast<CycleGrid *>(rules)->pressedCell(1,2)));
        }
        mouse(WM_LBUTTONDOWN,1,1,MK_LBUTTON);
        mouse(WM_MOUSEMOVE,1,4,MK_LBUTTON);
        mouse(WM_LBUTTONUP,1,4,0);
        require(state==clean,"Dragging to the append row must preserve the table");
        mouse(WM_LBUTTONDOWN,1,4,MK_LBUTTON);
        mouse(WM_LBUTTONUP,1,4,0);
        require(state["rules"].size()==4,"Clicking the append row must add one rule");
        command->Tag=Undo;action(command.get());
        S comment=u8"Датчик \"Старт\"\tA\r\nB";
        edited(rules,4,2,wide(comment));render();
        auto selection=rules->Selection;
        selection.Left=selection.Right=4;selection.Top=1;selection.Bottom=4;rules->Selection=selection;
        auto copied=clipboardRows(copyGridRange(rules,3));
        require(copied==std::vector<std::vector<S>>{{""},{comment},{""}},
                "TSV round trip must preserve empty edge rows and skip the append hint");
        require(clipboardRows("A \"B\"\r\n")==std::vector<std::vector<S>>{{"A \"B\""}},
                "Plain-text quotes must survive paste");
        rules->Col=4;rules->Row=2;rules->EditorMode=true;
        require(rules->EditorMode,"Comment must support text editing");
        WORD copyKey='C';keyDown(rules,copyKey,TShiftState()<<ssCtrl);
        require(copyKey=='C',"In-cell text editor must receive Ctrl+C");
        rules->EditorMode=false;
        b2f::save(folder+"/edited.xlsx",state);
        load(folder+"/edited.xlsx");
        require(state["rules"][1]["comment"]==comment || state["rules"][1]["comment"]==
                u8"Датчик \"Старт\"\tA\nB","Workbook comments must reopen in the editor");
        rules->ColWidths[1]=105;fields->ColWidths[2]=423;
        command->Tag=Expand;action(command.get());action(command.get());
        require(!state.contains("truth") && rules->ColWidths[1]==105 && fields->ColWidths[2]==423,
                "Mode switches must preserve column widths");
        state=b2f::fresh(9,false);page=0;render();action(command.get());
        command->Tag=NextPage;action(command.get());
        require(page==1 && rules->Cells[0][1]=="256" && rules->ColWidths[1]==105,"Next page must retain input width");
        command->Tag=PreviousPage;action(command.get());require(page==0,"Previous page must return to page one");
        J limit={{"rules",J::array_t(65536)}};
        require(editorPages(limit)==256 && !hasAppendRow(limit),"Full rules table must end at page 256");
        state=b2f::fresh(1,false);render();command->Tag=Expand;action(command.get());
        command->Tag=RemoveRule;action(command.get());action(command.get());
        setCellValue(2,1,"1");
        require(b2f::editor_row_count(state)==0 && rules->Cells[2][1].IsEmpty(),
                "Empty dense grid must ignore keyboard edits");
        command->Tag=Undo;action(command.get());require(b2f::editor_row_count(state)==1,"Undo must restore the last row");
        saveEditorJson(folder+"/saved.json",state);
        J saved=state;load(folder+"/saved.json");
        require(state==saved,"Edited JSON must reopen with its remaining row");
        state = b2f::fresh(3,false);
        state["input_comments"] = {"pump","level","alarm"};
        state["rules"].push_back({{"inputs",{"1","X","0"}},{"q","1"},{"comment","start"}});
        render(); tabs->ActivePageIndex = 0;
        fields->Row = 1;
        inputColor->ItemIndex = 7; settings(inputColor);
        fields->Row = 3;
        inputColor->ItemIndex = 7; settings(inputColor);
        J grouped = state;
        auto rowNumber = fields->CellRect(0,1);
        auto rowPoint = MAKELPARAM(rowNumber.Left+8,rowNumber.Top+rowNumber.Height()/2);
        SendMessage(fields->Handle,WM_LBUTTONDOWN,MK_LBUTTON,rowPoint);
        SendMessage(fields->Handle,WM_LBUTTONUP,0,rowPoint);
        require(inputColor->ItemIndex==7 && !inputUp->Enabled && inputDown->Enabled, "Input tools must follow selected row");
        SendMessage(inputDown->Handle,BM_CLICK,0,0);
        require(fields->Row==2 && state["inputs"]==J::array({"I2","I1","I3"}) &&
                state["input_comments"]==J::array({"level","pump","alarm"}) &&
                state["input_colors"]==J::array({0,7,7}) &&
                state["rules"][0]["inputs"]==J::array({"X","1","0"}), "Move the complete input column");
        J moved = state;
        command->Tag=Undo;action(command.get());require(state==grouped,"Undo input move");
        command->Tag=Redo;action(command.get());require(state==moved,"Redo input move");
        inputColor->ItemIndex=0;settings(inputColor);
        require(state["input_colors"]==J::array({0,0,7}),"Clear only the selected input color");
        command->Tag=Undo;action(command.get());
        b2f::save(folder+"/groups.xlsx",state); load(folder+"/groups.xlsx");
        require(state["input_colors"]==moved["input_colors"] && state["inputs"]==moved["inputs"],"XLSX input groups roundtrip");
        saveEditorJson(folder+"/groups.json",state);load(folder+"/groups.json");
        require(state["input_colors"]==moved["input_colors"],"JSON input groups roundtrip");
        J sparseExample=state;
        TButton *left=nullptr, *right=nullptr;
        for(int i=0;i<ComponentCount;++i)if(auto b=dynamic_cast<TButton *>(Components[i])) {
            if(b->Tag==ColumnLeft)left=b;
            if(b->Tag==ColumnRight)right=b;
        }
        require(left && right,"Column move buttons must be available on the combinations tab");
        tabs->ActivePageIndex=1;rules->SetFocus();
        mouse(WM_LBUTTONDOWN,3,0,MK_LBUTTON);
        mouse(WM_LBUTTONUP,3,0,0);
        require(rules->Col==3 && state==sparseExample && left->Enabled && !right->Enabled,
                "Clicking the final input header must select the column without editing data");
        SendMessage(left->Handle,BM_CLICK,0,0);
        require(rules->Col==2 && left->Enabled && right->Enabled &&
                state["inputs"][1]==sparseExample["inputs"][2],"Move the selected column left by button");
        SendMessage(right->Handle,BM_CLICK,0,0);
        require(rules->Col==3 && state==sparseExample,"Move the same column back by button");
        for(int col : {4,5}) {
            rules->Col=col;
            require(!left->Enabled && !right->Enabled,"Output and comments are fixed columns");
        }
        state.erase("rules"); state["truth"]="01000000";
        state["comments"]={"","source","","","","","",""};
        state["row_order"]={4,1,0};
        J sparseDense=state;
        J fullDense = state;
        fullDense.erase("row_order");
        J fullOriginal = fullDense;
        b2f::swap_editor_inputs(fullDense,0,1);
        require(fullDense["row_order"]==J::array({0,2,1,3,4,6,5,7}) &&
                fullDense["truth"]=="00100000","Preserve visible rows of a full dense table");
        b2f::swap_editor_inputs(fullDense,0,1);
        require(fullDense==fullOriginal,"Roundtrip full dense permutation");
        render();tabs->ActivePageIndex=1;rules->SetFocus();rules->Col=1;
        WORD rightKey=VK_RIGHT;keyDown(rules,rightKey,TShiftState()<<ssAlt);
        require(!rightKey && rules->Col==2 && state["row_order"]==J::array({4,2,0}) &&
                state["truth"]=="00100000" && state["comments"][2]=="source" &&
                state["input_colors"]==J::array({7,0,7}),"Reindex dense answers, comments and visible combinations");
        command->Tag=Undo;action(command.get());require(state==sparseDense,"Restore dense group edit");
        rules->Col=1;
        require(!left->Enabled && right->Enabled,"First dense input can move right on a single page");
        SendMessage(right->Handle,BM_CLICK,0,0);
        require(rules->Col==2 && state["truth"]=="00100000" && state["comments"][2]=="source",
                "Dense column button must move input and preserve answers");
        state=sparseExample;render();
        Width=Constraints->MinWidth;Height=Constraints->MinHeight;
        for(int tab:{0,1}) {
            tabs->ActivePageIndex=tab;Application->ProcessMessages();
            std::unique_ptr<Graphics::TBitmap> shot(GetFormImage());
            shot->SaveToFile(folder+(tab?"/minimum-rules.bmp":"/minimum-inputs.bmp"));
        }
        clean = state;
        for (int tag : {CodeExample, CompareExample}) {
            command->Tag = tag;
            action(command.get());
            require(state.at("truth").get<S>().size() == (tag == CodeExample ? 32 : 4096) &&
                    state == clean && source.IsEmpty(), "Open embedded ST example as a new table");
        }
        command->Tag = RowMaskExample;
        action(command.get());
        require(state.at("inputs").size()==12 && state.at("rules").size()==9 &&
                state==clean && source.IsEmpty(), "Open embedded row-mask rules");
        write_file(folder+"/result.json",J({{"compact_duplicates_both_outputs",true},
            {"distinct_comments_preserved",true},{"adjacent_rows_merge",true},
            {"compact_idempotent",true},{"undo_redo",true},{"comment_height_96dpi",90},
            {"input_reorder_and_colors",true},{"dense_input_reindex",true},{"xlsx_input_colors",true},{"typed_json_and_save",true},{"xlsx_open",true},{"invalid_file_keeps_document",true},
            {"in_cell_shortcut",true},{"rejected_resize",true},{"macro_validation",true},
            {"repeated_clicks_and_drag",true},{"clipboard_ranges",true},{"widths_across_pages_and_modes",true},
            {"page_limit",true},{"empty_dense_grid",true},{"header_layout_dpi_checked",{96,144,192}},{"screenshot_ppi",CurrentPPI}}).dump(2));
        clean=state;return;
    }
    require(state["rules"].empty() && rules->RowCount==2,"New document must have only an append row");
    state = b2f::fresh(5, false);
    state["rules"].push_back({{"inputs", J::array({"X", "X", "X", "1", "1"})},
                              {"q", "1"},
                              {"comment", u8"Q = I4 AND I5"}});
    b2f::save(folder + "/Boolean.xlsx", state);
    require(b2f::load(folder + "/Boolean.xlsx").state == state,
            u8"Не совпал Excel после сохранения.");
    auto small = b2f::generate_strategy(state, folder + "/Boolean", "Boolean",true,true,b2f::engine::default_policy());
    J wideState = b2f::resize(state, 16);
    auto wide = b2f::generate_strategy(wideState, folder + "/Wide", "WideBoolean",true,true,b2f::engine::default_policy());
    render();
    count->ItemIndex = 15;
    settings(count);
    require(state["inputs"].size() == 16 && state["rules"][0]["inputs"][15] == "X",
            u8"Не сработало добавление входов через окно.");
    std::unique_ptr<TComponent> command(new TComponent(this));
    command->Tag = Undo;
    action(command.get());
    require(state["inputs"].size() == 5, u8"Не сработала отмена изменения числа входов.");
    command->Tag = Redo;
    action(command.get());
    require(state["inputs"].size() == 16, u8"Не сработал повтор изменения числа входов.");
    count->ItemIndex = 4;
    settings(count);
    rules->Col = 1;
    rules->Row = 1;
    cycleCell(1, 1);
    cycleCell(1, 1);
    require(state["rules"][0]["inputs"][0] == "0", u8"Не сработал выбор состояния в ячейке.");
    command->Tag = Undo;
    action(command.get());
    require(state["rules"][0]["inputs"][0] == "1", u8"Не сработала отмена редактирования ячейки.");
    action(command.get());
    require(state["rules"][0]["inputs"][0] == "X", u8"Не сработала отмена редактирования ячейки.");
    J original = state, conflict = state;
    conflict["rules"].push_back({{"inputs", std::vector<S>(5, "X")}, {"q", "0"}, {"comment", ""}});
    commit(conflict);
    render();
    require(!generateButton->Enabled, u8"Конфликт не отключил генерацию.");
    state = b2f::compact(b2f::expand(original));
    clean = state;
    render();
    tabs->ActivePageIndex = 1;
    require(generateButton->Enabled, u8"Корректная таблица не разрешила генерацию.");
    Position = poDesigned;
    Left = -32000;
    Top = -32000;
    Show();
    ShowWindow(Handle, SW_SHOWNOACTIVATE);
    Update();
    Application->ProcessMessages();
    // Exercise the real grid message sequence, not just our event handlers.
    // Previously the smoke test only covered selecting a value in column I1.
    J cellBaseline = state;
    auto pressCell = [&](int col, int row,UINT message) {
        auto cell = rules->CellRect(col, row);
        POINT point={cell.Left+12,cell.Top+cell.Height()/2};
        auto sendMouse=[&](UINT msg,WPARAM flags) {
            HWND target=GetCapture();
            if(!target)target=ChildWindowFromPointEx(rules->Handle,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED);
            POINT local=point;MapWindowPoints(rules->Handle,target,&local,1);
            SendMessage(target,msg,flags,MAKELPARAM(local.x,local.y));
            Application->ProcessMessages();
        };
        sendMouse(WM_MOUSEMOVE,0);
        sendMouse(message,MK_LBUTTON);
        sendMouse(WM_LBUTTONUP,0);
        Application->ProcessMessages();
        if(col>0 && col<rules->ColCount-1 && row>0)
            require(!rules->EditorMode,"Toggle cells must stay out of inplace editing mode");
    };
    auto clickCell=[&](int col,int row){pressCell(col,row,WM_LBUTTONDOWN);};
    state=b2f::fresh(2,false);page=0;render();
    clickCell(1,1);
    require(state["rules"].size()==1 && rules->RowCount==3,"Click below last row must add exactly one rule");
    clickCell(1,2);
    edited(rules,4,2,"second");
    require(state["rules"].size()==2 && rules->RowCount==4,"Append row must remain after adding a rule");
    rules->Row=2;command->Tag=MoveUp;action(command.get());
    require(state["rules"][0]["comment"]=="second","Move must carry the whole rule");
    rules->SetFocus();WORD down=VK_DOWN;keyDown(rules,down,TShiftState()<<ssAlt);
    require(down==0 && state["rules"][1]["comment"]=="second","Alt+Down must move a rule");
    command->Tag=RemoveRule;action(command.get());
    require(state["rules"].size()==1,"Delete in rules mode");
    command->Tag=Undo;action(command.get());require(state["rules"].size()==2,"Undo rule delete");
    state=b2f::fresh(2,false);state["rules"].push_back({{"inputs",J::array({"0","1"})},{"q","1"},{"comment","marked"}});page=0;render();
    edited(fields,2,1,t2f::wide(u8"Разрешение пуска"));
    require(rules->ColWidths[2]<MulDiv(70,CurrentPPI,96),"Short inputs use compact columns");
    require(rules->RowHeights[0]>MulDiv(26,CurrentPPI,96),"Rotated comments need header height");
    rules->ColWidths[1]=51;
    edited(fields,2,1,t2f::wide(u8"Разрешение пуска"));
    require(rules->ColWidths[1]==51,"Editing a comment must retain the user's column width");
    require(state["inputs"][0]=="I1" && state["input_comments"][0]==u8"Разрешение пуска" && rules->Cells[1][0].Pos(t2f::wide(u8"Разрешение"))>0,"Input comments must not replace names");
    command->Tag=Expand;action(command.get());
    require(state.contains("truth") && expandButton->Enabled,"All combinations must offer a return action");
    J denseBeforeClicks=state;
    for(int i=0;i<4;++i) {
        clickCell(3,1);
        require(state["truth"].get_ref<const S &>()[0]==(i%2?'0':'1'),"Dense Q must toggle on every repeated click");
    }
    clickCell(1,1);
    require(state==denseBeforeClicks,"Dense input clicks must not change fixed combinations");
    rules->Row=4;command->Tag=MoveUp;action(command.get());
    require(b2f::editor_index(state,2)==3 && b2f::parse(state).truth.get(2),"Dense move must preserve logical indices");
    rules->Row=4;command->Tag=RemoveRule;action(command.get());
    require(b2f::editor_row_count(state)==3 && !b2f::parse(state).truth.get(2),"Deleted combination must disappear and yield zero");
    J sparse=state;saveEditorJson(folder+"/Sparse.json",state);load(folder+"/Sparse.json");require(state==sparse,"Sparse JSON roundtrip");
    b2f::save(folder+"/Sparse.xlsx",state);load(folder+"/Sparse.xlsx");
    require(b2f::editor_row_count(state)==3 && state["rules"][2]["inputs"]==J::array({"1","1"}) && state["input_comments"][0]==u8"Разрешение пуска","XLSX must retain row order, membership and input comments");
    state=sparse;render();command->Tag=Expand;action(command.get());
    require(!state.contains("truth") && state["rules"].size()==3,"Return to rules must retain remaining rows");
    action(command.get());require(b2f::editor_row_count(state)==4 && !b2f::parse(state).truth.get(2),"All combinations must restore absent rows with zero Q");
    for(int i=0;i<4;++i)b2f::erase_editor_row(state,0);render();
    require(b2f::editor_row_count(state)==0 && rules->RowCount==2,"Deleting all dense rows must leave an empty usable grid");
    command->Tag=Expand;action(command.get());clickCell(1,1);
    require(state["rules"].size()==1,"Return from empty dense table must permit new rules");
    state=cellBaseline;clean=state;page=0;undo.clear();redo.clear();render();
    for (int pass = 0; pass < 6; ++pass)
        for (int col = 1; col <= 6; ++col) {
            J expected = state;
            S old = col <= 5 ? state["rules"][0]["inputs"][col-1].get<S>() : state["rules"][0]["q"].get<S>();
            S value=col<=5 ? (old=="1"?"0":old=="0"?"X":"1") : (old=="1"?"0":"1");
            if (col <= 5) expected["rules"][0]["inputs"][col-1] = value;
            else expected["rules"][0]["q"] = value;
            clickCell(col, 1);
            require(state == expected, u8"Смена состояния затёрла данные: столбец " + std::to_string(col));
            for (int i = 1; i <= 6; ++i)
                require(utf8(rules->Cells[i][1]) ==
                            (i <= 5 ? state["rules"][0]["inputs"][i-1] : state["rules"][0]["q"]),
                        u8"Смена состояния затёрла отображение: столбец " + std::to_string(i));
        }
    require(state == cellBaseline, u8"Полный цикл кликов изменил исходную таблицу.");
    auto dragStart=rules->CellRect(1,1),dragEnd=rules->CellRect(3,1);
    SendMessage(rules->Handle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(dragStart.Left+12,dragStart.Top+12));
    SendMessage(rules->Handle,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(dragEnd.Left+12,dragEnd.Top+12));
    SendMessage(rules->Handle,WM_LBUTTONUP,0,MAKELPARAM(dragEnd.Left+12,dragEnd.Top+12));
    require(state==cellBaseline,"Dragging a selection must not toggle values");
    // Enter the actual row-comment editor before returning to a toggle cell.
    clickCell(7,1);clickCell(7,1);
    require(rules->EditorMode,"Row comments must still use the inplace editor");
    HWND editor=GetFocus();
    require(editor!=rules->Handle && IsChild(rules->Handle,editor),"Comment editor must own focus");
    SendMessageW(editor,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"edited comment"));
    clickCell(1,1);
    require(state["rules"][0]["comment"]=="edited comment" && state["rules"][0]["inputs"][0]=="1",
            "Leaving a comment editor must commit text and toggle on the first click");
    clickCell(1,1);clickCell(1,1);
    edited(rules,7,1,t2f::wide(cellBaseline["rules"][0]["comment"]));
    require(state==cellBaseline,"Comment editing followed by repeated clicks must retain the document");
    render();
    const char *inputCycle[]={"1","0","X","1","0","X"};
    for(int i=0;i<6;++i) {
        pressCell(1,1,i%2?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN);
        require(state["rules"][0]["inputs"][0]==inputCycle[i] && utf8(rules->Cells[1][1])==inputCycle[i],"Rapid clicks must cycle X -> 1 -> 0 -> X");
    }
    for(int i=0;i<6;++i) {
        pressCell(6,1,i%2?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN);
        require(state["rules"][0]["q"]==(i%2?"1":"0"),"Rapid Q clicks must cycle 1 -> 0 -> 1");
    }
    rules->Col=1;rules->Row=1;rules->SetFocus();
    for(int i=0;i<3;++i) {
        WORD key=VK_SPACE;keyDown(rules,key,TShiftState());
        require(key==0 && state["rules"][0]["inputs"][0]==inputCycle[i],"Space must use the same input cycle");
    }
    require(state==cellBaseline,"Rapid clicks and space must return to the initial document");
    clickCell(4, 1); // Repeated clicks on an already selected cell must also cycle.
    clickCell(4, 1);
    clickCell(4, 1);
    require(state == cellBaseline, u8"Повторный клик по ячейке не переключает значение.");
    clickCell(6, 1);
    command->Tag = Undo;
    action(command.get());
    require(state == cellBaseline, u8"Не сработала отмена клика по ячейке.");
    command->Tag = Redo;
    action(command.get());
    require(state["rules"][0]["q"] == "0", u8"Не сработал повтор клика по ячейке.");
    count->ItemIndex = 15;
    settings(count);
    rules->LeftCol = 16;
    clickCell(16, 1);
    require(state["rules"][0]["inputs"][15] == "1", u8"Не сработал клик по 16-му входу после прокрутки.");
    clickCell(16, 1);
    clickCell(16, 1);
    require(state["rules"][0]["inputs"][15] == "X", u8"Не сработал цикл значений 16-го входа.");
    state = cellBaseline; clean = state; render();
    auto capture = [&](const U &name) {
        resized(this);
        Application->ProcessMessages();
        tabs->Realign();
        tabs->ActivePage->Realign();
        rules->Repaint();
        for (int i = 0; i < ComponentCount; ++i) {
            auto control = dynamic_cast<TWinControl *>(Components[i]);
            if (control && control->Visible) UpdateWindow(control->Handle);
        }
        require(bottom->Top + bottom->Height <= messages->Top &&
                    messages->Top + messages->Height <= ClientHeight,
                u8"Панели перекрываются.");
        require(generateButton->Left + generateButton->Width <= bottom->ClientWidth,
                u8"Кнопка генерации выходит за окно.");
        std::unique_ptr<Graphics::TBitmap> image(GetFormImage());
        image->SaveToFile(folder + name);
        write_file(folder + name + ".json", J({{"rows",rules->RowCount},{"columns",rules->ColCount},
            {"ppi",CurrentPPI},{"first",utf8(rules->Cells[1][1])}}).dump(2));
    };
    state=sparse;page=0;render();capture("/input-comments.bmp");
    state["input_comments"][1]=S(2000,'W');render();capture("/long-input-comment.bmp");
    auto lines=headerLines(rules->Canvas,t2f::wide(state["input_comments"][1]),150);
    require(lines.size()==2 && lines[1].Pos(t2f::wide(u8"…"))>0,"Oversize comments must use two lines then an ellipsis");
    require(headerLines(rules->Canvas,"first\nsecond",150)==std::vector<U>({"first","second"}),"Explicit comment line break");
    require(headerLines(rules->Canvas,"first second third",rules->Canvas->TextWidth("first second")).front()=="first second",
            "Automatic wrapping must prefer a word boundary");
    require(rules->RowHeights[0]+3*rules->DefaultRowHeight<=rules->ClientHeight,"Long rotated comments must leave data rows visible");
    sparse["input_comments"][1]=u8"Контроль готовности оборудования к автоматическому запуску";
    state=sparse;render();
    tabs->ActivePageIndex=0;capture("/input-fields.bmp");
    tabs->ActivePageIndex=1;state=cellBaseline;clean=state;page=0;render();
    capture("/window.bmp");
    Width = 920;
    Height = 560;
    capture("/compact.bmp");
    ScaleForPPI(144);
    capture("/scale150.bmp");
    state=sparse;render();capture("/vertical150.bmp");
    ScaleForPPI(96);render();capture("/vertical100.bmp");
    ScaleForPPI(192);render();capture("/vertical200.bmp");
    state=cellBaseline;render();ScaleForPPI(144);
    J dense=b2f::dense_editor_state(b2f::fresh(16,false));
    state=dense;clean=state;page=0;render();
    require(rules->RowCount==257 && rules->ColCount==19,"dense grid must be paged");
    setCellValue(17,1,"1");edited(rules,18,1,t2f::wide(u8"Первый"));
    page=255;render();setCellValue(17,256,"1");edited(rules,18,256,t2f::wide(u8"Последний"));
    require(state["truth"].get_ref<const S &>()[0]=='1' && state["truth"].get_ref<const S &>()[65535]=='1',"dense boundary edits");
    J editedDense=state;command->Tag=Undo;action(command.get());command->Tag=Redo;action(command.get());
    require(state==editedDense && state!=clean,"dense undo/redo and dirty state");
    b2f::save(folder+"/Dense.xlsx",state);saveEditorJson(folder+"/Dense.json",state);
    S jsonBefore=read_file(folder+"/Dense.json");
    HANDLE locked=CreateFileW((folder+"/Dense.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    require(locked!=INVALID_HANDLE_VALUE,"JSON lock fixture");bool jsonRejected=false;
    try { saveEditorJson(folder+"/Dense.json",b2f::fresh(2)); }catch(...) { jsonRejected=true; }
    CloseHandle(locked);require(jsonRejected && read_file(folder+"/Dense.json")==jsonBefore,"failed JSON save changed previous file");
    load(folder+"/Dense.xlsx");require(state==editedDense,"dense XLSX reopened");
    load(folder+"/Dense.json");require(state==editedDense,"dense JSON reopened");
    J invalid=state;invalid["inputs"].push_back("I17");write_file(folder+"/Invalid.json",invalid.dump());
    bool rejected=false;try { load(folder+"/Invalid.json"); }catch(...) { rejected=true; }
    require(rejected && state==editedDense,"invalid import replaced current document");
    page=255;render();rules->LeftCol=16;capture("/dense-last.bmp");
    command->Tag=Expand;action(command.get());
    require(!state.contains("truth") && state["rules"].size()==65536 && rules->RowCount<=257,"Return from 16-input table must keep a bounded grid");
    page=1;render();rules->Row=1;command->Tag=MoveUp;action(command.get());
    require(page==0 && rules->Row==256 && state["rules"][255]["inputs"][8]=="1","Move across page boundary");
    command->Tag=RemoveRule;action(command.get());require(state["rules"].size()==65535,"Delete in large rules mode");
    page=255;render();rules->Row=256;command->Tag=RemoveRule;action(command.get());
    require(state["rules"].size()==65535,"Append row must not delete a data row");
    rules->Col=1;rules->LeftCol=1;rules->Row=256;
    clickCell(1,256);
    require(state["rules"].size()==65536 && state["rules"].back()["inputs"][8]=="1" && state["rules"].back()["q"]=="0","Adding a missing full row after returning from 16-input table");
    state=cellBaseline;clean=state;page=0;render();
    auto waitJob=[&]() { ULONGLONG end=GetTickCount64()+60000;while(worker && GetTickCount64()<end) { Application->ProcessMessages();generationTick(this);Sleep(1); } require(!worker,"worker timeout"); };
    auto hold=std::make_shared<b2f::engine::Control>();std::atomic<bool> entered{false};
    hold->observer=[&](int phase) { if(phase==2) { entered.store(true);while(!hold->cancelled.load())Sleep(1); } };
    startGeneration(folder+"/Cancelled",hold);
    ULONGLONG deadline=GetTickCount64()+10000;
    while(!entered.load() && GetTickCount64()<deadline) { Application->ProcessMessages();Sleep(1); }
    require(entered.load() && !tabs->Enabled && cancelButton->Enabled,"worker did not yield UI");
    J changed=state;changed["output"]="Changed";rejected=false;try { commit(changed); }catch(...) { rejected=true; }
    require(rejected && state==cellBaseline,"editing while generating was not blocked");
    bool canClose=true;closing(this,canClose);require(!canClose,"close did not wait for cancellation");closePending=false;waitJob();
    require(!lastJobError.empty() && tabs->Enabled && !FileExists(folder+"/Cancelled.st"),"cancel recovery");
    std::map<U,S> previous;
    for(auto ext:{".tple",".fbst",".st",".report.json"}) { U path=folder+"/Cancelled"+t2f::wide(ext);write_file(path,"previous");previous[path]="previous"; }
    auto staged=std::make_shared<b2f::engine::Control>();staged->observer=[&](int phase) { if(phase==5)staged->cancel(); };
    startGeneration(folder+"/Cancelled",staged);waitJob();
    for(auto &p:previous)require(read_file(p.first)==p.second,"cancel overwrote previous output");
    ForceDirectories(folder+"/Failure.fbst");startGeneration(folder+"/Failure");waitJob();
    require(!lastJobError.empty() && tabs->Enabled,"error did not restore controls");
    auto committing=std::make_shared<b2f::engine::Control>();bool commitCancel=true;
    committing->observer=[&](int phase) { if(phase==6)commitCancel=committing->cancel(); };
    startGeneration(folder+"/Async",committing);waitJob();
    require(!commitCancel && lastJobError.empty() && lastReport.at("engine")=="strategy","retry/commit boundary");
    require(state==cellBaseline && generateButton->Enabled,"generation changed the model");
    write_file(folder + "/result.json", J({{"small", small},
                                           {"wide", wide},
                                           {"conflict_blocked", true},
                                           {"excel_roundtrip", true},
                                           {"input_resize_undo_redo", true},
                                           {"cell_editor", true},
                                           {"empty_append_delete_move_both_modes",true},
                                           {"input_comments_sparse_roundtrip",true},
                                           {"return_16_inputs_and_cross_page_move",true},
                                           {"cell_editor_all_columns", true},
                                           {"click_cycle", true},
                                           {"rapid_double_click_cycle_and_space",true},
                                           {"toggle_cells_never_enter_text_editing",true},
                                           {"row_comment_editor_then_repeat_clicks",true},
                                           {"vertical_input_comments_two_lines",true},
                                           {"dense_boundary_edit_undo_save",true},
                                           {"json_save_failure_preserves_previous",true},
                                           {"grid_rows_max",257},
                                           {"worker_snapshot_and_revision_guard",true},
                                           {"cancel_close_error_retry",true},
                                           {"cancel_after_staging_preserves_files",true},
                                           {"commit_cannot_cancel",true},
                                           {"async_report",lastReport},
                                           {"ppi", CurrentPPI}})
                                            .dump(2));
    clean = state;
}
