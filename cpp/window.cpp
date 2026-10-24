#include <vcl.h>
#pragma hdrstop
#include "window.hpp"
#include <Vcl.Clipbrd.hpp>
#include <Vcl.Dialogs.hpp>
#include <shellapi.h>
#include <algorithm>
#include <sstream>
#include <memory>
using namespace t2f;
enum {
    New = 1,
    Open,
    Save,
    Reload,
    Example,
    OpenFolder,
    Undo,
    Redo,
    Help,
    AddField,
    RemoveField,
    Up,
    Down,
    AddRow,
    Duplicate,
    RemoveRow,
    Generate
};
TButton *EditorForm::button(TWinControl *parent, const U &caption, int tag, int x, int y,
                            int width) {
    auto b = new TButton(this);
    b->Parent = parent;
    b->Caption = caption;
    b->Tag = tag;
    b->SetBounds(x, y, width, 25);
    b->OnClick = action;
    return b;
}
TMenuItem *EditorForm::menu(TMenuItem *parent, const U &caption, int tag, Word shortcut) {
    auto m = new TMenuItem(this);
    m->Caption = caption;
    m->Tag = tag;
    m->ShortCut = shortcut;
    m->OnClick = action;
    parent->Add(m);
    return m;
}
void __fastcall EditorForm::CreateParams(TCreateParams &params) {
    TForm::CreateParams(params);
    // CreateNew без DFM не устанавливает признак главной формы до создания HWND.
    // Главное окно редактора должно иметь собственную кнопку на панели задач.
    params.WndParent = 0;
    params.ExStyle = (params.ExStyle | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW;
}
__fastcall EditorForm::EditorForm(TComponent *owner) : TForm(owner, 0) {
    Caption = "Table2FBD C++ 2.0.6";
    Width = 1020;
    Height = 700;
    Constraints->MinWidth = 850;
    Constraints->MinHeight = 500;
    Position = poScreenCenter;
    Font->Name = "Segoe UI";
    Font->Size = 9;
    Color = (TColor)RGB(246, 246, 246);
    DoubleBuffered = true;
    KeyPreview = true;
    OnKeyDown = keyDown;
    OnCloseQuery = closing;
    OnResize = resized;
    auto main = new TMainMenu(this);
    Menu = main;
    auto file = new TMenuItem(main);
    file->Caption = wide(u8"Файл");
    main->Items->Add(file);
    menu(file, wide(u8"Новая"), New, TextToShortCut("Ctrl+N"));
    menu(file, wide(u8"Открыть Excel…"), Open, TextToShortCut("Ctrl+O"));
    menu(file, wide(u8"Сохранить в Excel…"), Save, TextToShortCut("Ctrl+S"));
    reloadItem = menu(file, wide(u8"Перечитать файл"), Reload);
    menu(file, wide(u8"Открыть пример"), Example);
    folderItem = menu(file, wide(u8"Открыть папку результата"), OpenFolder);
    folderItem->Enabled = false;
    auto edit = new TMenuItem(main);
    edit->Caption = wide(u8"Правка");
    main->Items->Add(edit);
    undoItem = menu(edit, wide(u8"Отменить"), Undo, TextToShortCut("Ctrl+Z"));
    redoItem = menu(edit, wide(u8"Повторить"), Redo, TextToShortCut("Ctrl+Y"));
    auto help = new TMenuItem(main);
    help->Caption = wide(u8"Справка");
    main->Items->Add(help);
    menu(help, wide(u8"Формат таблицы"), Help, VK_F1);
    top = new TPanel(this);
    top->Parent = this;
    top->Align = alTop;
    top->Height = 35;
    top->BevelOuter = bvNone;
    top->ParentBackground = false;
    top->Color = Color;
    button(top, wide(u8"Новая"), New, 6, 5, 60);
    button(top, wide(u8"Открыть…"), Open, 70, 5, 82);
    button(top, wide(u8"Сохранить…"), Save, 156, 5, 96);
    path = new TEdit(this);
    path->Parent = top;
    path->ReadOnly = true;
    path->SetBounds(258, 5, 460, 25);
    sheetLabel = new TLabel(this);
    sheetLabel->Parent = top;
    sheetLabel->Caption = wide(u8"Лист:");
    sheetLabel->SetBounds(730, 9, 35, 20);
    sheet = new TComboBox(this);
    sheet->Parent = top;
    sheet->Style = csDropDownList;
    sheet->SetBounds(770, 5, 180, 25);
    sheet->OnChange = sheetChanged;
    messages = new TPanel(this);
    messages->Parent = this;
    messages->Align = alBottom;
    messages->Height = 62;
    messages->BevelOuter = bvNone;
    messages->ParentBackground = false;
    messages->Color = Color;
    messages->Padding->SetBounds(8, 4, 8, 8);
    status = new TMemo(this);
    status->Parent = messages;
    status->Align = alClient;
    status->ReadOnly = true;
    status->TabStop = false;
    status->BorderStyle = bsNone;
    status->Color = Color;
    status->ScrollBars = ssVertical;
    status->WordWrap = true;
    bottom = new TPanel(this);
    bottom->Parent = this;
    bottom->Align = alBottom;
    bottom->Height = 114;
    bottom->BevelOuter = bvNone;
    bottom->ParentBackground = false;
    bottom->Color = Color;
    bottom->Top = messages->Top - bottom->Height;
    auto label = new TLabel(this);
    label->Parent = bottom;
    label->Caption = wide(u8"Макрос:");
    label->SetBounds(6, 8, 54, 20);
    macroName = new TEdit(this);
    macroName->Parent = bottom;
    macroName->Text = "TableParameters";
    macroName->MaxLength = 64;
    macroName->SetBounds(65, 5, 500, 24);
    auto controls = [&](const U &language, int y, TCheckBox *&check, TComboBox *&mode,
                        TEdit *&step) {
        check = new TCheckBox(this);
        check->Parent = bottom;
        check->Caption = language;
        check->Checked = true;
        check->SetBounds(6, y, 60, 24);
        check->OnClick = modeChanged;
        mode = new TComboBox(this);
        mode->Parent = bottom;
        mode->Style = csDropDownList;
        mode->Items->Add(wide(u8"По группам"));
        mode->Items->Add(wide(u8"Всё в один"));
        mode->ItemIndex = 0;
        mode->SetBounds(70, y, 140, 24);
        mode->OnChange = modeChanged;
        auto l = new TLabel(this);
        l->Parent = bottom;
        l->Caption = wide(u8"Шаг по ID:");
        l->SetBounds(222, y + 4, 72, 20);
        step = new TEdit(this);
        step->Parent = bottom;
        step->Text = "10";
        step->SetBounds(298, y, 100, 24);
    };
    controls("FBD", 34, fbd, fbdMode, fbdStep);
    controls("ST", 63, st, stMode, stStep);
    generateButton = button(bottom, wide(u8"Создать макросы…"), Generate, 770, 5, 160);
    summary = new TLabel(this);
    summary->Parent = bottom;
    summary->SetBounds(6, 94, 800, 18);
    tabs = new TPageControl(this);
    tabs->Parent = this;
    tabs->Align = alClient;
    auto makeGrid = [&](const U &caption, bool isFields) -> TStringGrid * {
        auto page = new TTabSheet(this);
        page->PageControl = tabs;
        page->Caption = caption;
        auto bar = new TPanel(this);
        bar->Parent = page;
        bar->Align = alTop;
        bar->Height = 33;
        bar->BevelOuter = bvNone;
        bar->ParentBackground = false;
        bar->Color = Color;
        if (isFields) {
            button(bar, wide(u8"Добавить поле"), AddField, 2, 4, 110);
            button(bar, wide(u8"Удалить поле"), RemoveField, 116, 4, 110);
            button(bar, wide(u8"Выше"), Up, 230, 4, 65);
            button(bar, wide(u8"Ниже"), Down, 299, 4, 65);
        } else {
            button(bar, wide(u8"Добавить строку"), AddRow, 2, 4, 124);
            button(bar, wide(u8"Копировать строку"), Duplicate, 130, 4, 142);
            button(bar, wide(u8"Удалить строки"), RemoveRow, 276, 4, 124);
        }
        auto g = new TStringGrid(this);
        g->Parent = page;
        g->Align = alClient;
        g->ColCount = isFields ? 3 : 2;
        g->RowCount = 2;
        g->FixedCols = 0;
        g->FixedRows = 1;
        g->DefaultRowHeight = 26;
        g->DefaultColWidth = 130;
        g->BorderStyle = bsNone;
        g->Color = clWhite;
        g->FixedColor = (TColor)RGB(237, 237, 237);
        g->DefaultDrawing = false;
        g->Options = (g->Options << goEditing << goColSizing << goRangeSelect) >> goVertLine >>
                     goHorzLine >> goFixedVertLine >> goFixedHorzLine;
        g->OnDrawCell = drawCell;
        g->OnSetEditText = edited;
        g->OnSelectCell = selectCell;
        return g;
    };
    fields = makeGrid(wide(u8"Поля"), true);
    records = makeGrid(wide(u8"Записи"), false);
    tabs->ActivePageIndex = 0;
    typeEditor = new TComboBox(this);
    typeEditor->Parent = fields;
    typeEditor->Style = csDropDownList;
    for (auto s : {"ID", u8"Комментарий", "REAL", "UDINT", "BOOL"})
        typeEditor->Items->Add(wide(s));
    typeEditor->Visible = false;
    typeEditor->OnChange = selectType;
    newTable();
    resized(this);
}
void EditorForm::updateCaption() {
    Caption = "Table2FBD C++ 2.0.6" + U(state == clean ? "" : " *") +
              wide(u8" — редактор таблиц для OwenLogic");
    undoItem->Enabled = !undo.empty();
    redoItem->Enabled = !redo.empty();
    reloadItem->Enabled = !source.IsEmpty();
}
void EditorForm::error(const U &message) {
    status->Font->Color = clMaroon;
    status->Text = wide(u8"Ошибка: ") + message;
}
void EditorForm::validate() {
    updateCaption();
    try {
        auto t = parse_table(state);
        summary->Caption = wide(u8"Записей: " + std::to_string(t.rows.size()) + u8" · Полей: " +
                                std::to_string(t.fields.size()) + u8" · Выходов: " +
                                std::to_string(t.outputs.size()) + u8" с Valid");
        status->Text = "";
        generateButton->Enabled = fbd->Checked || st->Checked;
    } catch (std::exception &e) {
        summary->Caption = wide(u8"Записей: " + std::to_string(state["records"].size()) +
                                u8" · Полей: " + std::to_string(state["fields"].size()));
        error(wide(e.what()));
        generateButton->Enabled = false;
    }
    status->Font->Color = status->Text.IsEmpty() ? clWindowText : status->Font->Color;
}
void EditorForm::render(bool resizeColumns) {
    rendering = true;
    typeEditor->Visible = false;
    fields->RowCount = std::max(2, (int)state["fields"].size() + 1);
    fields->Cells[0][0] = wide(u8"Название поля");
    fields->Cells[1][0] = wide(u8"Тип");
    fields->Cells[2][0] = wide(u8"Имя входа / выхода");
    for (int r = 1; r < fields->RowCount; ++r)
        for (int c = 0; c < 3; ++c)
            fields->Cells[c][r] =
                r <= (int)state["fields"].size() ? wide(state["fields"][r - 1][c].get<S>()) : U();
    records->ColCount = std::max(1, (int)state["fields"].size());
    records->RowCount = std::max(2, (int)state["records"].size() + 1);
    for (int c = 0; c < records->ColCount; ++c) {
        if (c < (int)state["fields"].size()) {
            S n = state["fields"][c][2];
            records->Cells[c][0] = wide(n.empty() ? state["fields"][c][0].get<S>() : n);
        } else
            records->Cells[c][0] = "";
        for (int r = 1; r < records->RowCount; ++r)
            records->Cells[c][r] =
                (r <= (int)state["records"].size() && c < (int)state["records"][r - 1].size())
                    ? wide(state["records"][r - 1][c].get<S>())
                    : U();
    }
    if (resizeColumns) {
        fields->ColWidths[0] = 230;
        fields->ColWidths[1] = 110;
        fields->ColWidths[2] = 280;
        for (int c = 0; c < records->ColCount; ++c)
            records->ColWidths[c] =
                std::max(90, std::min(300, records->Canvas->TextWidth(records->Cells[c][0]) + 20));
    }
    rendering = false;
    validate();
}
void EditorForm::commit(const J &next) {
    if (next == state)
        return;
    undo.push_back(state);
    redo.clear();
    state = next;
    validate();
}
void EditorForm::newTable() {
    source = "";
    loadedSheet = u8"Данные";
    owned = false;
    state = {{"fields", J::array({J::array({u8"Номер записи", "ID", "ID"}),
                                  J::array({u8"Параметр", "REAL", "Value"})})},
             {"records", J::array()}};
    clean = state;
    undo.clear();
    redo.clear();
    path->Text = wide(u8"Новая таблица");
    sheet->Visible = false;
    sheetLabel->Visible = false;
    render(true);
}
bool EditorForm::discard() {
    if (state == clean)
        return true;
    int result = MessageDlg(wide(u8"Сохранить изменения таблицы?"), mtConfirmation,
                            TMsgDlgButtons() << mbYes << mbNo << mbCancel, 0);
    return result == mrYes ? save() : result == mrNo;
}
void EditorForm::setDocument(const Book &book, const U &filename, bool own) {
    state = book.state;
    clean = state;
    undo.clear();
    redo.clear();
    source = filename;
    loadedSheet = book.sheet;
    owned = own;
    path->Text = filename.IsEmpty() ? wide(u8"Пример: режимы вентилятора") : filename;
    rendering = true;
    sheet->Items->Clear();
    for (auto &s : book.sheets)
        sheet->Items->Add(wide(s));
    sheet->ItemIndex = sheet->Items->IndexOf(wide(book.sheet));
    sheet->Visible = book.sheets.size() > 1;
    sheetLabel->Visible = sheet->Visible;
    rendering = false;
    render(true);
    resized(this);
}
bool EditorForm::load(const U &filename, const S &selected) {
    try {
        auto book = read_book(filename, selected);
        setDocument(book, ExpandFileName(filename), false);
        return true;
    } catch (const Exception &e) {
        error(e.Message);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
    return false;
}
bool EditorForm::save() {
    std::unique_ptr<TSaveDialog> d(new TSaveDialog(this));
    d->Filter = wide(u8"Книги Excel (*.xlsx)|*.xlsx");
    d->DefaultExt = "xlsx";
    d->Options = d->Options << ofOverwritePrompt;
    d->FileName = source.IsEmpty() ? U("Table.xlsx") : ChangeFileExt(source, "") + "_edited.xlsx";
    if (!d->Execute())
        return false;
    try {
        if (!owned && !source.IsEmpty() &&
            ExpandFileName(source).LowerCase() == ExpandFileName(d->FileName).LowerCase())
            throw std::runtime_error(u8"Выберите другое имя: сохраняется только текущий лист.");
        write_book(d->FileName, state);
        Book b{state, u8"Данные", {u8"Данные"}};
        auto past = undo, future = redo;
        setDocument(b, d->FileName, true);
        undo = past;
        redo = future;
        updateCaption();
        status->Font->Color = clWindowText;
        status->Text = wide(u8"Таблица сохранена: ") + d->FileName;
        return true;
    } catch (const Exception &e) {
        error(e.Message);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
    return false;
}
TStringGrid *EditorForm::grid() { return tabs->ActivePageIndex == 0 ? fields : records; }
void __fastcall EditorForm::edited(TObject *sender, int c, int r, const U value) {
    if (rendering || r < 1)
        return;
    J next = state;
    S key = sender == fields ? "fields" : "records";
    if (sender == fields) {
        if (c >= 3)
            return;
        while ((int)next[key].size() < r) {
            next[key].push_back(J::array({"", "", ""}));
            for (auto &row : next["records"])
                row.push_back("");
        }
    } else {
        if (c >= (int)next["fields"].size())
            return;
        while ((int)next[key].size() < r)
            next[key].push_back(std::vector<S>(next["fields"].size(), ""));
    }
    next[key][r - 1][c] = utf8(value);
    commit(next);
    if (sender == fields) {
        rendering = true;
        records->ColCount = std::max(1, (int)state["fields"].size());
        for (size_t i = 0; i < state["fields"].size(); ++i) {
            S n = state["fields"][i][2];
            records->Cells[i][0] = wide(n.empty() ? state["fields"][i][0].get<S>() : n);
        }
        rendering = false;
    }
}
void __fastcall EditorForm::selectCell(TObject *sender, int c, int r, bool &can) {
    typeEditor->Visible = false;
    if (sender == fields && c == 1 && r > 0 && r <= (int)state["fields"].size()) {
        auto rc = fields->CellRect(c, r);
        typeEditor->SetBounds(rc.Left, rc.Top, rc.Width(), rc.Height() + 2);
        typeEditor->Tag = r;
        S kind = state["fields"][r - 1][1];
        if (lower(kind) == "comment")
            kind = u8"Комментарий";
        typeEditor->ItemIndex = typeEditor->Items->IndexOf(wide(kind));
        typeEditor->Visible = true;
    }
}
void __fastcall EditorForm::selectType(TObject *) {
    if (typeEditor->ItemIndex < 0)
        return;
    fields->Cells[1][typeEditor->Tag] = typeEditor->Text;
    edited(fields, 1, typeEditor->Tag, typeEditor->Text);
}
void __fastcall EditorForm::modeChanged(TObject *) {
    if (!fbdMode || !stMode)
        return;
    fbdMode->Enabled = fbd->Checked;
    fbdStep->Enabled = fbd->Checked && fbdMode->ItemIndex == 0;
    stMode->Enabled = st->Checked;
    stStep->Enabled = st->Checked && stMode->ItemIndex == 0;
    validate();
}
void __fastcall EditorForm::sheetChanged(TObject *) {
    if (rendering || source.IsEmpty() || sheet->ItemIndex < 0)
        return;
    S selected = utf8(sheet->Text);
    if (selected == loadedSheet)
        return;
    U original = source;
    bool own = owned;
    if (!discard() || !load(original, selected)) {
        rendering = true;
        sheet->ItemIndex = sheet->Items->IndexOf(wide(loadedSheet));
        rendering = false;
    } else
        owned = own;
}
void __fastcall EditorForm::resized(TObject *) {
    if (!top || !bottom || !path || !sheet || !macroName || !generateButton || !status || !summary)
        return;
    int margin = std::max(6, macroName->Top);
    sheet->Left = top->ClientWidth - sheet->Width - margin;
    sheetLabel->Left = sheet->Left - sheetLabel->Width - margin;
    path->Width =
        std::max(100, (sheet->Visible ? sheetLabel->Left : top->ClientWidth) - path->Left - margin);
    generateButton->Left = bottom->ClientWidth - generateButton->Width - margin;
    macroName->Width = std::max(180, generateButton->Left - macroName->Left - margin);
    summary->Width = bottom->ClientWidth - summary->Left - margin;
}
void __fastcall EditorForm::drawCell(TObject *sender, int col, int row, const TRect &rect,
                                     TGridDrawState cellState) {
    auto g = static_cast<TStringGrid *>(sender);
    auto canvas = g->Canvas;
    bool selected = row > 0 && cellState.Contains(gdSelected);
    canvas->Brush->Color = row == 0   ? (TColor)RGB(237, 237, 237)
                           : selected ? (TColor)RGB(221, 234, 246)
                                      : clWhite;
    canvas->FillRect(rect);
    canvas->Font->Assign(g->Font);
    canvas->Font->Color = (TColor)RGB(32, 32, 32);
    TRect textRect = rect;
    textRect.Left += MulDiv(7, CurrentPPI, 96);
    textRect.Right -= MulDiv(5, CurrentPPI, 96);
    U value = g->Cells[col][row];
    DrawTextW(canvas->Handle, value.c_str(), value.Length(), &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    canvas->Pen->Color = (TColor)RGB(221, 221, 221);
    canvas->MoveTo(rect.Left, rect.Bottom - 1);
    canvas->LineTo(rect.Right, rect.Bottom - 1);
    canvas->MoveTo(rect.Right - 1, rect.Top);
    canvas->LineTo(rect.Right - 1, rect.Bottom);
    if (cellState.Contains(gdFocused)) {
        canvas->Brush->Style = bsClear;
        canvas->Pen->Color = (TColor)RGB(90, 130, 165);
        canvas->Rectangle(rect);
        canvas->Brush->Style = bsSolid;
    }
}
void __fastcall EditorForm::closing(TObject *, bool &can) { can = !busy && discard(); }
void EditorForm::copyCells() {
    auto g = grid();
    auto rect = g->Selection;
    S text;
    for (int r = std::max(1, rect.Top); r <= rect.Bottom; ++r) {
        if (!text.empty())
            text += '\n';
        for (int c = rect.Left; c <= rect.Right; ++c) {
            if (c > rect.Left)
                text += '\t';
            S value = utf8(g->Cells[c][r]);
            if (value.find_first_of("\t\r\n\"") != S::npos) {
                text += '"';
                for (char ch : value) {
                    text += ch;
                    if (ch == '"')
                        text += '"';
                }
                text += '"';
            } else
                text += value;
        }
    }
    Clipboard()->AsText = wide(text);
}
void EditorForm::pasteCells() {
    S text = utf8(Clipboard()->AsText);
    if (text.empty())
        return;
    std::vector<std::vector<S>> rows(1, std::vector<S>(1));
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
                rows.back().back() += '"';
                ++i;
            } else
                quoted = !quoted;
        } else if (!quoted && c == '\t')
            rows.back().push_back("");
        else if (!quoted && (c == '\n' || c == '\r')) {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            rows.push_back(std::vector<S>(1));
        } else
            rows.back().back() += c;
    }
    require(!quoted, u8"Незавершённая кавычка при вставке.");
    if (rows.back().size() == 1 && rows.back()[0].empty())
        rows.pop_back();
    auto g = grid();
    int r0 = std::max(0, g->Row - 1), c0 = g->Col, width = g == fields ? 3 : state["fields"].size();
    J next = state;
    S key = g == fields ? "fields" : "records";
    for (size_t r = 0; r < rows.size(); ++r) {
        require(c0 + rows[r].size() <= (size_t)width,
                u8"Вставляемые данные выходят за число столбцов.");
        while (next[key].size() <= r0 + r) {
            next[key].push_back(std::vector<S>(width, ""));
            if (g == fields)
                for (auto &row : next["records"])
                    row.push_back("");
        }
        for (size_t c = 0; c < rows[r].size(); ++c)
            next[key][r0 + r][c0 + c] = rows[r][c];
    }
    commit(next);
    render();
}
void __fastcall EditorForm::keyDown(TObject *, WORD &key, TShiftState shift) {
    if (!shift.Contains(ssCtrl))
        return;
    if (ActiveControl == fields || ActiveControl == records) {
        try {
            if (key == 'C') {
                copyCells();
                key = 0;
            } else if (key == 'V') {
                pasteCells();
                key = 0;
            }
        } catch (std::exception &e) {
            error(wide(e.what()));
        }
    }
}
void EditorForm::generate() {
    Options o;
    o.source_name = utf8(ExtractFileName(source));
    o.name = identifier(utf8(macroName->Text));
    o.fbd = fbd->Checked;
    o.st = st->Checked;
    auto step = [](TEdit *e) {
        S s = trim(utf8(e->Text));
        size_t used = 0;
        uint64_t n = std::stoull(s, &used);
        require(used == s.size() && n >= 1 && n <= 4294967296ULL,
                u8"Шаг должен быть от 1 до 4294967296.");
        return n;
    };
    o.fbd_step = !o.fbd || fbdMode->ItemIndex == 1 ? 0 : step(fbdStep);
    o.st_step = !o.st || stMode->ItemIndex == 1 ? 0 : step(stStep);
    auto table = parse_table(state);
    std::unique_ptr<TSaveDialog> d(new TSaveDialog(this));
    d->Filter = o.fbd ? wide(u8"Макрос FBD (*.tple)|*.tple") : wide(u8"Макрос ST (*.fbst)|*.fbst");
    d->DefaultExt = o.fbd ? "tple" : "fbst";
    d->FileName = wide(o.name) + (o.fbd ? ".tple" : ".fbst");
    d->Options = d->Options << ofOverwritePrompt;
    if (!d->Execute())
        return;
    U target = d->FileName;
    U extra = ChangeFileExt(target, o.fbd ? ".fbst" : ".tple");
    if (o.fbd && o.st && FileExists(extra) &&
        MessageDlg(wide(u8"Заменить также файл?\n") + extra, mtConfirmation,
                   TMsgDlgButtons() << mbYes << mbNo, 0) != mrYes)
        return;
    busy = true;
    Enabled = false;
    Screen->Cursor = crHourGlass;
    try {
        auto results = t2f::generate(table, target, o);
        S message;
        for (auto &r : results) {
            if (!message.empty())
                message += "\n";
            message += r["format"].get<S>() + ": " +
                       utf8(ExtractFileName(wide(r["path"].get<S>()))) + u8"; групп: " +
                       std::to_string(r["groups"].get<int>());
        }
        status->Font->Color = clWindowText;
        status->Text = wide(message);
        lastOutput = target;
        folderItem->Enabled = true;
    } catch (...) {
        busy = false;
        Enabled = true;
        Screen->Cursor = crDefault;
        throw;
    }
    busy = false;
    Enabled = true;
    Screen->Cursor = crDefault;
}
void __fastcall EditorForm::action(TObject *sender) {
    int tag = static_cast<TComponent *>(sender)->Tag;
    try {
        if (tag == New) {
            if (discard())
                newTable();
            return;
        }
        if (tag == Open) {
            std::unique_ptr<TOpenDialog> d(new TOpenDialog(this));
            d->Filter = wide(u8"Книги Excel (*.xlsx)|*.xlsx");
            if (d->Execute() && discard())
                load(d->FileName);
            return;
        }
        if (tag == Save) {
            save();
            return;
        }
        if (tag == Reload) {
            if (discard()) {
                U original = source;
                bool own = owned;
                load(original, loadedSheet);
                owned = own;
            }
            return;
        }
        if (tag == Example) {
            if (discard()) {
                auto b = read_book(ExtractFilePath(Application->ExeName) + "Template.xlsx");
                setDocument(b, "", false);
            }
            return;
        }
        if (tag == OpenFolder) {
            ShellExecuteW(Handle, L"open", ExtractFileDir(lastOutput).c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            return;
        }
        if (tag == Help) {
            MessageDlg(
                wide(
                    u8"Строки Excel: 1 — название, 2 — тип, 3 — имя входа/выхода; далее записи.\nРовно один ID и хотя бы один REAL, UDINT или BOOL. Для комментария имя пустое.\nID и UDINT: 0..4294967295. BOOL: 0/1, TRUE/FALSE.\nFBD и ST выбираются независимо. Без найденного ID выходы равны нулю, Valid=FALSE.\nВ режиме групп подключайте сводный макрос без суффикса диапазона.\nCtrl+C/V — ячейки, Ctrl+Z/Y — отмена/повтор. Формулы Excel не поддерживаются."),
                mtInformation, TMsgDlgButtons() << mbOK, 0);
            return;
        }
        if (tag == Undo) {
            if (!undo.empty()) {
                redo.push_back(state);
                state = undo.back();
                undo.pop_back();
                render();
            }
            return;
        }
        if (tag == Redo) {
            if (!redo.empty()) {
                undo.push_back(state);
                state = redo.back();
                redo.pop_back();
                render();
            }
            return;
        }
        if (tag == Generate) {
            generate();
            return;
        }
        J next = state;
        if (tag == AddField) {
            std::set<S> names;
            for (auto &f : next["fields"])
                names.insert(lower(f[2]));
            int n = 1;
            while (names.count("field" + std::to_string(n)))
                ++n;
            next["fields"].push_back(
                J::array({u8"Поле " + std::to_string(n), "REAL", "Field" + std::to_string(n)}));
            for (auto &r : next["records"])
                r.push_back("0");
        } else if (tag == RemoveField) {
            auto sel = fields->Selection;
            for (int r = std::min(sel.Bottom, (int)next["fields"].size());
                 r >= std::max(1, sel.Top); --r) {
                next["fields"].erase(r - 1);
                for (auto &row : next["records"])
                    row.erase(r - 1);
            }
        } else if (tag == Up || tag == Down) {
            int r = fields->Row - 1, to = r + (tag == Up ? -1 : 1);
            if (to < 0 || to >= (int)next["fields"].size() || r < 0)
                return;
            std::swap(next["fields"][r], next["fields"][to]);
            for (auto &row : next["records"])
                std::swap(row[r], row[to]);
            commit(next);
            render();
            fields->Row = to + 1;
            return;
        } else if (tag == AddRow || tag == Duplicate) {
            require(!next["fields"].empty(), u8"Сначала добавьте поля.");
            std::set<uint32_t> used;
            int idcol = -1;
            for (size_t c = 0; c < next["fields"].size(); ++c)
                if (lower(next["fields"][c][1]) == "id")
                    idcol = c;
            if (idcol >= 0)
                for (auto &r : next["records"]) {
                    try {
                        used.insert(std::stoul(r[idcol].get<S>()));
                    } catch (...) {
                    }
                }
            uint64_t id = used.empty() ? 0 : (uint64_t)*used.rbegin() + 1;
            if (id > 4294967295ULL) {
                id = 0;
                while (used.count(id))
                    ++id;
            }
            J row = J::array();
            int selected = records->Row - 1;
            if (tag == Duplicate && selected >= 0 && selected < (int)next["records"].size())
                row = next["records"][selected];
            else
                for (auto &f : next["fields"]) {
                    S kind = lower(f[1]);
                    row.push_back(kind == "comment" || kind == u8"комментарий" ? "" : "0");
                }
            if (idcol >= 0)
                row[idcol] = std::to_string(id);
            next["records"].push_back(row);
            tabs->ActivePageIndex = 1;
        } else if (tag == RemoveRow) {
            auto sel = records->Selection;
            for (int r = std::min(sel.Bottom, (int)next["records"].size());
                 r >= std::max(1, sel.Top); --r)
                next["records"].erase(r - 1);
        }
        commit(next);
        render();
    } catch (const Exception &e) {
        error(e.Message);
    } catch (std::exception &e) {
        error(wide(e.what()));
    }
}
void EditorForm::smoke(const U &folder) {
    ForceDirectories(folder);
    require(load(ExtractFilePath(Application->ExeName) + "Template.xlsx"), u8"Не открыт пример.");
    records->Cells[2][1] = "37.5";
    edited(records, 2, 1, "37.5");
    require(parse_table(state).rows[0]["values"]["Speed"] == 37.5, u8"Не изменена ячейка.");
    auto saved = state;
    auto fake = new TComponent(this);
    fake->Tag = Undo;
    action(fake);
    require(parse_table(state).rows[0]["values"]["Speed"] == 0, u8"Не выполнена отмена.");
    fake->Tag = Redo;
    action(fake);
    require(state == saved, u8"Не выполнен повтор.");
    fake->Tag = AddField;
    action(fake);
    require(state["fields"].size() == saved["fields"].size() + 1, u8"Не добавлено поле.");
    fake->Tag = Undo;
    action(fake);
    require(state == saved, u8"Добавление поля не отменено.");
    fake->Tag = Duplicate;
    action(fake);
    require(state["records"].size() == saved["records"].size() + 1, u8"Не скопирована строка.");
    parse_table(state);
    fake->Tag = Undo;
    action(fake);
    require(state == saved, u8"Копирование не отменено.");
    stMode->ItemIndex = 1;
    modeChanged(this);
    require(fbdStep->Enabled && !stStep->Enabled, u8"Режимы FBD и ST связаны.");
    fbd->Checked = false;
    st->Checked = false;
    modeChanged(this);
    require(!generateButton->Enabled, u8"Разрешена генерация без формата.");
    fbd->Checked = true;
    st->Checked = true;
    modeChanged(this);
    J withBlank = state;
    withBlank["records"].push_back(std::vector<S>(state["fields"].size(), ""));
    write_book(folder + "/Blank.xlsx", withBlank);
    require(parse_table(read_book(folder + "/Blank.xlsx").state).rows == parse_table(state).rows,
            u8"Пустая строка нарушила сохранение.");
    delete fake;
    write_book(folder + "/Edited.xlsx", state);
    require(parse_table(read_book(folder + "/Edited.xlsx").state).rows == parse_table(state).rows,
            u8"Не совпала сохранённая книга.");
    Options o;
    o.st_step = 0;
    auto result = t2f::generate(parse_table(state), folder + "/Smoke.tple", o);
    write_file(folder + "/result.json", result.dump(2));
    tabs->ActivePageIndex = 1;
    Position = poDesigned;
    Left = -32000;
    Top = -32000;
    Show();
    ShowWindow(Handle, SW_SHOWNOACTIVATE);
    Update();
    Application->ProcessMessages();
    for (int i = 0; i < ComponentCount; ++i) {
        auto c = dynamic_cast<TWinControl *>(Components[i]);
        if (c && c->Visible) {
            auto h = c->Handle;
            ShowWindow(h, SW_SHOWNOACTIVATE);
            UpdateWindow(h);
        }
    }
    status->Text =
        wide(u8"FBD: сводный макрос и 3 группы; Smoke.tple\r\nST: единый макрос; Smoke.fbst");
    auto capture = [&](const U &filename) {
        Application->ProcessMessages();
        resized(this);
        tabs->Realign();
        tabs->ActivePage->Realign();
        records->Repaint();
        require(status->Top >= 0 && status->Top + status->Height <= messages->ClientHeight,
                u8"Область сообщений выходит за границу панели.");
        require(messages->Top + messages->Height <= ClientHeight &&
                    bottom->Top + bottom->Height <= messages->Top,
                u8"Нижние панели перекрываются или выходят за окно.");
        require(generateButton->Left + generateButton->Width <= bottom->ClientWidth,
                u8"Кнопка генерации выходит за окно.");
        J geometry = {{"ppi", CurrentPPI},
                      {"width", ClientWidth},
                      {"bottomWidth", bottom->ClientWidth},
                      {"buttonLeft", generateButton->Left},
                      {"buttonWidth", generateButton->Width},
                      {"rows", records->RowCount},
                      {"gridWidth", records->Width}};
        write_file(folder + filename + ".json", geometry.dump(2));
        std::unique_ptr<Graphics::TBitmap> image(GetFormImage());
        for (int i = 0; i < ComponentCount; ++i) {
            auto control = dynamic_cast<TWinControl *>(Components[i]);
            if (!control || !control->Visible ||
                !(dynamic_cast<TButton *>(control) || dynamic_cast<TEdit *>(control) ||
                  dynamic_cast<TComboBox *>(control) || dynamic_cast<TCheckBox *>(control) ||
                  dynamic_cast<TMemo *>(control)))
                continue;
            auto pt = ScreenToClient(control->ClientToScreen(TPoint(0, 0)));
            int dc = SaveDC(image->Canvas->Handle);
            SetViewportOrgEx(image->Canvas->Handle, pt.x, pt.y, nullptr);
            PrintWindow(control->Handle, image->Canvas->Handle, 1);
            RestoreDC(image->Canvas->Handle, dc);
        }
        image->SaveToFile(folder + filename);
    };
    capture("/window.bmp");
    Width = 850;
    Height = 500;
    capture("/compact.bmp");
    ScaleForPPI(144);
    capture("/scale150.bmp");
    clean = state;
}
