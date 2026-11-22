#include <vcl.h>
#pragma hdrstop
#include "../cpp/macro_document.hpp"
#include <tchar.h>
#include <Vcl.Dialogs.hpp>
#include <System.IOUtils.hpp>
#include "../cpp/window.hpp"
#include "../boolean/window.hpp"
#include "../boolean/editor_model.hpp"
#include "../boolean/engine/export.hpp"
#include <functional>
#include "../selector/selector.hpp"
#include "transfer.hpp"
#include "../cpp/editor_bar.hpp"
#include "../cpp/editor_dialog.hpp"
#include "../cpp/input_colors.hpp"
#include <algorithm>
#include <memory>

using namespace t2f;
using unified::Mode;
namespace {
const U names[] = {wide(u8"Таблицы"), wide(u8"Bool"), wide(u8"Комбинированный")};
constexpr int table = 0, logic = 1, combined = 2;
const char *const exampleFolders[] = {"Table", "Bool", "Combined"};
struct Page {
    TForm *form;
    TPanel *top, *bottom, *messages;
    TEdit *path, *macro;
    TComboBox *sheet;
    TCheckBox *fbd, *st;
    TButton *generate;
    TMemo *status;
    TMainMenu *menu;
};
} // namespace
class Workbench : public TForm {
    EditorForm *tables[3] = {};
    BooleanForm *boolean = nullptr;
    Page pages[3] = {};
    J defaults[3];
    TPageControl *modes;
    TPanel *settings;
    TEdit *macro;
    TMenuItem *examples;
    TMenuItem *sheetMenus[3];
    TCheckBox *fbd, *st;
    TButton *generate;
    struct TransferDecision {
        uint64_t source = UINT64_MAX;
        uint64_t destination = 0;
        bool declined = false;
    };
    TransferDecision decisions[3][3];
    uint64_t revision(int i) const { return i == logic ? boolean->revision : tables[i]->revision; }
    int active = 0;
    bool syncing = false, ready = false, closeWhenIdle = false;

    J &state(int i) { return i == logic ? boolean->state : tables[i]->state; }
    bool pristine(int i) { return state(i) == defaults[i]; }
    bool freshTarget(int dest, int from) {
        if (dest != combined)
            return pristine(dest);
        const auto &document = state(dest);
        if (!document.at("records").empty()) {
            if (from != logic)
                return false;
            // A parameter transfer can create records before the first contact is defined.
            for (size_t i = 0; i < document.at("fields").size(); ++i)
                if (lower(trim(document.at("fields")[i][1])) == "input")
                    for (const auto &row : document.at("records"))
                        if (!trim(row.at(i)).empty())
                            return false;
        }
        auto fields = [from](const J &document) {
            J result = J::array();
            for (const auto &field : document.at("fields"))
                if ((lower(trim(field.at(1))) == "input") == (from == logic))
                    result.push_back(field);
            return result;
        };
        return fields(document) == fields(defaults[dest]);
    }
    bool dirty(int i) {
        return i == logic ? boolean->state != boolean->clean : tables[i]->state != tables[i]->clean;
    }
    bool running() const { return boolean && boolean->worker; }
    TPanel *panel(TAlign align, int height) {
        auto p = new EditorBar(this);
        p->Parent = this;
        p->Align = align;
        p->Height = height;
        p->BevelOuter = bvNone;
        return p;
    }
    void __fastcall openExample(TObject *sender) {
        if (running()) return;
        auto item=static_cast<TMenuItem*>(sender);
        int mode=item->Parent->Tag;
        flush(active);
        try {
            if (!item->Hint.IsEmpty()) {
                auto book = mode == logic ? b2f::load(item->Hint) : read_book(item->Hint);
                if (!(mode == logic ? boolean->discard() : tables[mode]->discard())) return;
                if (mode == logic) {
                    boolean->setDocument(std::move(book), "");
                    boolean->tabs->ActivePageIndex = 1;
                } else {
                    auto t = tables[mode];
                    t->setDocument(book, "", false);
                    t->tabs->ActivePage = static_cast<TTabSheet*>(t->records->Parent);
                }
                pages[mode].path->Text = wide(u8"Пример — ") + StripHotkey(item->Caption);
                selectPage(mode);
                return;
            }
            bool opened=mode==logic ? boolean->resetDocument(item->Tag,wide(u8"Пример — ")+StripHotkey(item->Caption))
                                    : tables[mode]->openExample();
            if(opened) selectPage(mode);
        } catch(const std::exception &e) { editorError(wide(e.what())); }
          catch(const Exception &e) { editorError(e.Message); }
    }
    void __fastcall listSheets(TObject *) {
        auto menu=sheetMenus[active];
        auto sheet=pages[active].sheet;
        menu->Clear();
        if (!sheet->Visible) return;
        for(int i=0;i<sheet->Items->Count;++i) {
            auto entry=new TMenuItem(menu);
            entry->Caption=StringReplace(sheet->Items->Strings[i],"&","&&",TReplaceFlags()<<rfReplaceAll);
            entry->Tag=i;
            entry->RadioItem=true;
            entry->Checked=i==sheet->ItemIndex;
            entry->OnClick=chooseSheet;
            menu->Add(entry);
        }
    }
    void __fastcall chooseSheet(TObject *sender) {
        auto sheet=pages[active].sheet;
        flush(active);
        sheet->ItemIndex=static_cast<TMenuItem*>(sender)->Tag;
        if(sheet->OnChange) sheet->OnChange(sheet);
        refresh();
    }
    U documentTitle() const {
        const U file=active==logic ? boolean->source : tables[active]->source;
        U label=file.IsEmpty() ? pages[active].path->Text : file.SubString(LastDelimiter(L"/\\",file)+1,file.Length());
        if(label.IsEmpty()) label=wide(u8"Новая таблица");
        return Application->Title + wide(u8" — ") + label +
               (pages[active].form->Caption.Pos("*") ? U(" *") : U());
    }
    void mount(int i) {
        Page &p = pages[i];
        auto page = new TTabSheet(this);
        page->PageControl = modes;
        page->Caption = names[i];
        p.menu = p.form->Menu;
        auto importItem=new TMenuItem(p.menu);
        importItem->Caption=wide(u8"Открыть макрос для редактирования…");
        importItem->OnClick=openMacro;
        auto file=p.menu->Items->Items[0];
        file->Add(importItem);
        sheetMenus[i]=new TMenuItem(file);
        sheetMenus[i]->Caption=wide(u8"Лист книги");
        file->Add(sheetMenus[i]);
        file->OnClick=listSheets;
        auto group=i==logic ? boolean->examples : tables[i]->examples;
        group->Parent->Remove(group);
        group->Caption=names[i];
        group->Tag=i;
        for(int j=0;j<group->Count;++j) group->Items[j]->OnClick=openExample;
        U folder = ExtractFilePath(Application->ExeName) + "examples/" + exampleFolders[i];
        if (DirectoryExists(folder)) {
            auto books = System::Ioutils::TDirectory::GetFiles(folder, "*.xlsx", System::Ioutils::TSearchOption::soAllDirectories);
            if (books.Length > 1) std::sort(books.begin(), books.end());
            for (const auto &book : books) {
                U title = ChangeFileExt(ExtractFileName(book), "");
                if (title.Pos("~$") == 1) continue;
                auto entry = new TMenuItem(group);
                entry->Caption = StringReplace(title, "&", "&&", TReplaceFlags() << rfReplaceAll);
                entry->Hint = book;
                entry->OnClick = openExample;
                group->Add(entry);
            }
        }
        examples->Add(group);
        p.form->Menu = nullptr;
        p.form->BorderStyle = bsNone;
        p.form->BorderIcons = TBorderIcons();
        p.form->Constraints->MinHeight = 0;
        p.form->Constraints->MinWidth = 0;
        p.form->Parent = page;
        p.form->Align = alClient;
        p.form->Visible = true;
        // Keep editor handlers and per-document state. Present their common controls once.
        p.top->Visible = false;
        p.messages->Visible = false;
        p.bottom->Visible = false;
        if (i!=logic) {
            auto t=tables[i];
            auto options=new TTabSheet(p.form);options->PageControl=t->tabs;
            options->Caption=wide(u8"Настройки");
            for(int row=1;row>=0;--row) {
                auto bar=new EditorBar(options);bar->Parent=options;bar->Align=alTop;bar->Top=0;
                auto language=new TLabel(bar);language->Parent=bar;language->Caption=row?"ST":"FBD";
                auto mode=row?t->stMode:t->fbdMode;mode->Parent=bar;language->FocusControl=mode;
                auto step=row?t->stStep:t->fbdStep;step->Parent=bar;
                auto label=new TLabel(bar);label->Parent=bar;label->FocusControl=step;
                label->Caption=wide(t->discrete?u8"Шаг группы":u8"Шаг по ID");
                bar->configure({language,mode,label,step});
            }
        }
        defaults[i] = state(i);
    }
    void selectPage(int index) {
        active = index;
        if (modes->ActivePageIndex != index)
            modes->ActivePageIndex = index;
        auto &p = pages[index];
        auto help=p.menu->Items->Items[2];
        if(examples->Parent!=help) {
            if(examples->Parent) examples->Parent->Remove(examples);
            help->Add(examples);
        }
        if (Menu != p.menu)
            Menu = p.menu;
        p.fbd->Checked = fbd->Checked;
        p.st->Checked = st->Checked;
        refresh();
    }
    void flush(int i) {
        if (i == logic) {
            boolean->rules->EditorMode = false;
            boolean->fields->EditorMode = false;
        } else {
            tables[i]->records->EditorMode = false;
            tables[i]->fields->EditorMode = false;
            if (tables[i]->contacts)
                tables[i]->contacts->EditorMode = false;
        }
    }
    void switchTo(int dest, int answer = 0) {
        if (dest == active)
            return;
        if (running()) {
            modes->ActivePageIndex = active;
            return;
        }
        flush(active);
        auto &decision = decisions[active][dest];
        if (decision.source == revision(active) &&
            (decision.declined || decision.destination == revision(dest))) {
            selectPage(dest);
            return;
        }
        bool relevant = (active == combined && !freshTarget(combined, dest)) || dest == combined;
        bool declined = false;
        if (relevant && !pristine(active)) {
            try {
                J next = unified::transfer(Mode(active), state(active), Mode(dest), state(dest),
                                           freshTarget(dest, active));
                if (next != state(dest)) {
                    U scope = (active == logic || dest == logic)
                                  ? wide(u8"имена, комментарии и цвета дискретных входов")
                                  : wide(u8"выходные параметры, их значения, описания и "
                                         u8"комментарии к режимам");
                    U prompt = wide(u8"Перенести ") + scope + wide(u8" из вкладки «") +
                               names[active] + wide(u8"» во вкладку «") + names[dest] +
                               wide(u8"»?\n\n");
                    prompt += active == logic || dest == logic
                                  ? wide(u8"Совпадающие входы сохранят своё место. Заполненные "
                                         u8"комментарии и назначенные цвета обновятся.")
                                  : wide(u8"Параметры объединятся по имени, записи — по порядку. "
                                         u8"Заполненные значения обновят совпадающие ячейки.");
                    prompt += wide(u8"\n\n«Нет» — перейти без переноса. «Отмена» — остаться на "
                                   u8"текущей вкладке.");
                    int choice = answer
                                     ? answer
                                     : editorDialog(prompt, mtConfirmation,
                                                  TMsgDlgButtons() << mbYes << mbNo << mbCancel, 0);
                    if (choice == mrCancel) {
                        modes->ActivePageIndex = active;
                        return;
                    }
                    declined = choice == mrNo;
                    if (choice == mrYes) {
                        if (dest == logic) {
                            boolean->commit(next);
                            boolean->render();
                        } else {
                            tables[dest]->commit(next);
                            tables[dest]->render(true);
                        }
                    }
                }
            } catch (const std::exception &e) {
                if (answer)
                    throw;
                if (editorDialog(wide(u8"Перенос невозможен. ") + wide(e.what()) +
                                   wide(u8"\nПерейти без переноса?"),
                               mtWarning, TMsgDlgButtons() << mbYes << mbNo, 0) != mrYes) {
                    modes->ActivePageIndex = active;
                    return;
                }
                declined = true;
            }
        }
        decision = {revision(active), revision(dest), declined};
        selectPage(dest);
    }
    void refresh() {
        if (!ready || syncing)
            return;
        syncing = true;
        try {
            auto &p = pages[active];
            bool busy = running();
            if (macro->Text != p.macro->Text)
                macro->Text = p.macro->Text;
            sheetMenus[active]->Visible=p.sheet->Visible && p.sheet->Items->Count>1;
            U actionCaption=wide(busy ? u8"Отменить расчёт" : u8"Создать макросы…");
            if(generate->Caption!=actionCaption) {
                generate->Caption=actionCaption;
                static_cast<EditorBar*>(settings)->arrange();
            }
            generate->Enabled = !busy || boolean->cancelButton->Enabled;
            macro->Enabled = !busy;
            fbd->Enabled = !busy;
            st->Enabled = !busy;
            for (int i = 0; i < p.menu->Items->Count; ++i)
                if (p.menu->Items->Items[i]->Enabled == busy)
                    p.menu->Items->Items[i]->Enabled = !busy;
            U title = documentTitle();
            if (Caption != title)
                Caption = title;
        } __finally {
            syncing = false;
        }
    }
    void __fastcall idle(TObject *, bool &) {
        refresh();
        if (closeWhenIdle && !running()) {
            closeWhenIdle = false;
            Close();
        }
    }
    void __fastcall changing(TObject *, bool &allow) { allow = !running(); }
    void __fastcall changed(TObject *) {
        if (ready)
            switchTo(modes->ActivePageIndex);
    }
    bool importMacro(const U &file) {
        auto doc = readMacroDocument(file);
        int mode = doc.mode == "Bool" ? logic : doc.mode == "Table" ? table : combined;
        require(mode != combined || doc.mode == "Combined", u8"Неизвестный режим макроса.");
        identifier(doc.name);
        bool wantFbd = doc.settings.at("fbd").get<bool>(), wantSt = doc.settings.at("st").get<bool>();
        require(wantFbd || wantSt, u8"В комментарии не выбран формат генерации.");
        uint64_t fstep = 0, sstep = 0;
        if (mode == logic)
            b2f::parse(doc.state, doc.name);
        else {
            parse_table(doc.state, mode == combined);
            fstep = doc.settings.at("fbd_step").get<uint64_t>();
            sstep = doc.settings.at("st_step").get<uint64_t>();
            require(fstep <= 4294967296ULL && sstep <= 4294967296ULL, u8"Некорректный шаг группировки.");
        }
        if (doc.logicChanged &&
            editorDialog(wide(u8"Содержимое макроса изменилось после генерации. "
                              u8"Открыть копию таблицы, сохранённой на момент генерации?"),
                         mtWarning, TMsgDlgButtons() << mbYes << mbNo) != mrYes)
            return false;
        flush(active);
        if (!(mode == logic ? boolean->discard() : tables[mode]->discard()))
            return false;
        Book book{doc.state, "", {}};
        if (mode == logic)
            boolean->setDocument(std::move(book), "");
        else {
            auto t = tables[mode];
            t->setDocument(book, "", false);
            t->fbdMode->ItemIndex = fstep ? 0 : 1;
            t->stMode->ItemIndex = sstep ? 0 : 1;
            t->fbdStep->Text = wide(std::to_string(fstep ? fstep : 10));
            t->stStep->Text = wide(std::to_string(sstep ? sstep : 10));
            t->modeChanged(nullptr);
        }
        selectPage(mode);
        pages[mode].macro->Text = wide(doc.name);
        fbd->Checked = wantFbd;
        st->Checked = wantSt;
        pages[mode].path->Text = wide(u8"Из макроса: ") + ExtractFileName(file);
        refresh();
        return true;
    }
    void __fastcall openMacro(TObject *) {
        if (running())
            return;
        std::unique_ptr<TOpenDialog> dialog(new EditorFileDialog<TOpenDialog>(this));
        dialog->Title = wide(u8"Открыть макрос для редактирования");
        dialog->Filter = wide(u8"Макросы OwenLogic (*.tple;*.fbst)|*.tple;*.fbst");
        if (!dialog->Execute())
            return;
        try {
            importMacro(dialog->FileName);
        } catch (const std::exception &e) {
            editorError(wide(e.what()));
        } catch (const Exception &e) {
            editorError(e.Message);
        }
    }
    void __fastcall command(TObject *) {
        if (running()) { boolean->cancelButton->Click(); return; }
        flush(active);
        auto &p=pages[active];
        if (p.generate->Enabled) p.generate->Click();
        else {
            if (active==logic) boolean->validate(); else tables[active]->validate();
            U reason=!fbd->Checked && !st->Checked ? wide(u8"Выберите FBD или ST.") : p.status->Text;
            editorDialog(reason,mtError,TMsgDlgButtons()<<mbOK,0);
        }
        refresh();
    }
    void __fastcall changeSettings(TObject *sender) {
        if (!ready || syncing || running())
            return;
        auto &p = pages[active];
        if (sender == macro)
            p.macro->Text = macro->Text;
        if (sender == fbd || sender == st) {
            p.fbd->Checked = fbd->Checked;
            p.st->Checked = st->Checked;
        }
        refresh();
    }
    void __fastcall closing(TObject *, bool &allow) {
        if (running()) {
            closeWhenIdle = true;
            boolean->cancelButton->Click();
            allow = false;
            return;
        }
        flush(active);
        int first = active;
        for (int offset = 0; offset < 3; ++offset) {
            int i = (first + offset) % 3;
            if (!dirty(i))
                continue;
            selectPage(i);
            auto r =
                editorDialog(wide(u8"Сохранить изменения на вкладке «") + names[i] + wide(u8"»?"),
                           mtConfirmation, TMsgDlgButtons() << mbYes << mbNo << mbCancel, 0);
            if (r == mrCancel) {
                allow = false;
                return;
            }
            if (r == mrYes) {
                try {
                    if (!(i == logic ? boolean->save() : tables[i]->save())) {
                        allow = false;
                        return;
                    }
                } catch (const std::exception &e) {
                    editorError(wide(e.what()));
                    allow = false;
                    return;
                }
            }
        }
        allow = true;
    }

  protected:
    void __fastcall CreateParams(TCreateParams &params) override {
        TForm::CreateParams(params);
        params.WndParent = 0;
        params.ExStyle = (params.ExStyle | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW;
    }

  public:
    __fastcall Workbench(TComponent *owner) : TForm(owner, 0) {
        Caption = Application->Title;
        Position = poScreenCenter;
        Width = 1150;
        Height = 780;
        Constraints->MinWidth = 960;
        Constraints->MinHeight = 630;
        Font->Name = "Segoe UI";
        Font->Size = 9;
        Canvas->Font->Assign(Font);
        examples=new TMenuItem(this);
        examples->Caption=wide(u8"Примеры");
        settings = panel(alBottom, 40);
        auto l = new TLabel(this);
        l->Parent = settings;
        l->Caption = wide(u8"Имя макроса");
        l->SetBounds(8, 9, Canvas->TextWidth(l->Caption) + 8, 24);
        macro = new TEdit(this);
        macro->Parent = settings;
        macro->SetBounds(l->Left + l->Width + 6, 4, 245, 28);
        macro->MaxLength = 64;
        macro->OnChange = changeSettings;
        for (int i = 0; i < 2; ++i) {
            auto c = new TCheckBox(this);
            c->Parent = settings;
            c->SetBounds(macro->Left + macro->Width + 12 + 78 * i, 5, 70, 26);
            c->Caption = i ? "ST" : "FBD";
            c->Checked = true;
            c->OnClick = changeSettings;
            if (i)
                st = c;
            else
                fbd = c;
        }
        generate = new TButton(settings);
        generate->Parent=settings;
        generate->Caption=wide(u8"Создать макросы…");
        generate->OnClick=command;
        generate->Left = ClientWidth - generate->Width - 8;
        l->FocusControl=macro;
        static_cast<EditorBar*>(settings)->configure({l,macro,fbd,st,generate},macro);
        modes = new TPageControl(this);
        modes->Parent = this;
        modes->Align = alClient;
        for (int i = 0; i < 3; ++i) {
            if (i == logic) {
                auto b = boolean = new BooleanForm(this);
                pages[i] = {b,        b->top, b->bottom, b->messages,       b->path,   b->macro,
                            b->sheet, b->fbd, b->st,     b->generateButton, b->status, nullptr};
            } else {
                auto t = tables[i] = new EditorForm(this, i == combined);
                pages[i] = {t,        t->top, t->bottom, t->messages,       t->path,   t->macroName,
                            t->sheet, t->fbd, t->st,     t->generateButton, t->status, nullptr};
            }
            mount(i);
        }
        ready = true;
        selectPage(table);
        modes->OnChanging = changing;
        modes->OnChange = changed;
        OnCloseQuery = closing;
        Application->OnIdle = idle;
    }
    __fastcall ~Workbench() {
        ready = false;
        Application->OnIdle = nullptr;
    }
    void checkMacros(const U &dir);
    void smoke(const U &dir);
    void checkEditorUI(const U &dir, bool snapshots);
    void checkLayout(const U &dir);
};

void Workbench::checkMacros(const U &dir) {
    ForceDirectories(dir);
    for (int mode : {logic, table, combined}) {
        bool isBool = mode == logic;
        S name = isBool ? "EditableBool" : mode == table ? "EditableTable" : "EditableCombined";
        U base = dir + "/" + wide(name);
        U xlsx = ExtractFilePath(Application->ExeName) + "examples/" +
                 (isBool          ? wide(u8"Bool/StartPermit/Разрешение пуска.xlsx")
                  : mode == table ? wide(u8"Table/DriveModes/Две скорости привода.xlsx")
                                  : wide(u8"Combined/DrivePermit/Пуск по готовности.xlsx"));
        J source = isBool ? b2f::load(xlsx).state : read_book(xlsx).state;
        if (isBool) source["rules"][0]["inputs"][1] = "X";
        for (int formats : {1, 2, 3}) {
            U target = formats == 3 ? base : base + (formats == 1 ? "_Fbd" : "_St");
            bool fbd = (formats & 1) != 0, st = (formats & 2) != 0;
            if (isBool) {
                b2f::generate_strategy(source, target, name, fbd, st, b2f::engine::default_policy());
            } else {
                Options opts;
                opts.name = name;
                opts.fbd = fbd;
                opts.st = st;
                opts.fbd_step = opts.st_step = 1;
                t2f::generate(parse_table(source, mode == combined), target, opts, source);
            }
            require(FileExists(target + ".tple") == fbd && FileExists(target + ".fbst") == st &&
                    !FileExists(target + ".st") && !FileExists(target + ".report.json"),
                    "Generate only the selected macro files");
        }
        for (auto ext : {".tple", ".fbst"}) {
            U path = base + wide(ext);
            auto doc = readMacroDocument(path);
            require(!doc.logicChanged && doc.state == source, "Embedded document round trip");
            J meta = J::parse(zip_read(path, "MetaData"));
            J blocks = J::parse(zip_read(path, "Template"));
            S publicGroup = isBool ? "Boolean" : table_macro_group;
            if (S(ext) == ".fbst") {
                require(meta.at("MetaData").at("GroupName") == publicGroup, "ST public group");
                for (const auto &block : blocks) {
                    if (block.at("Id") == meta.at("MetaData").at("FunctionBlockUniqueId")) continue;
                    const S &body = block.at("SourceCode").get_ref<const S &>();
                    require(body.find(st_annotation("GroupName", internal_macro_group)) < body.find("FUNCTION_BLOCK"),
                            "ST internal group");
                }
                const S code = blocks.back().at("SourceCode");
                auto heading = code.find(u8"\r\n// Сгенерировано OWL-Tables\r\n// LWB1 ");
                require(heading != S::npos && code.rfind("END_FUNCTION_BLOCK") < heading,
                        "Signed ST trailer follows the last block");
            } else {
                const J &component = meta.at("ComponentMetaData");
                require(component.at("GroupName") == publicGroup, "FBD public group");
                for (const auto &block : blocks) {
                    const auto &part = block.at("MetaData").at("ComponentMetaData");
                    require(part.at("GroupName") == (part.at("UniqueId") == component.at("UniqueId")
                                                     ? publicGroup : S(internal_macro_group)), "FBD group");
                }
                const S description = component.at("Description");
                require(description.find(u8"\r\nСгенерировано OWL-Tables\r\nLWB1 ") != S::npos &&
                        blocks.at(component.at("UniqueId").get<S>()).at("MetaData").at("ComponentMetaData").at("Description") == description,
                        "FBD trailer heading in the root and catalogue");
            }
            require(importMacro(path) && active == mode && state(mode) == source &&
                        utf8(pages[mode].macro->Text) == name,
                    "Open embedded document in editor");
        }
    }
    const U original = dir + "/EditableBool.fbst", changed = dir + "/changed.fbst";
    J blocks = J::parse(zip_read(original, "Template"));
    S &source = blocks.back().at("SourceCode").get_ref<S &>();
    auto save = [&]() {
        zip_write(changed, {{"Template", blocks.dump()},
                            {"MetaData", zip_read(original, "MetaData")},
                            {"MinRequiredVersion", "2.0"}});
    };
    const S heading = u8"// Сгенерировано OWL-Tables\r\n";
    const size_t headingAt = source.find(heading);
    source.erase(headingAt, heading.size());
    save();
    require(!readMacroDocument(changed).logicChanged, "Read ST trailer without a heading");
    source.insert(headingAt, heading);
    source.insert(source.find("END_FUNCTION_BLOCK"), "Q := FALSE;\r\n");
    save();
    require(readMacroDocument(changed).logicChanged, "Detect edited ST");
    source.erase(source.rfind("// LWB1 "));
    save();
    auto expectUnrecognized = [&](const U &file) {
        const int page = active;
        const J before = {state(logic), state(table), state(combined)};
        S message;
        try {
            importMacro(file);
        } catch (const std::exception &e) {
            message = e.what();
        }
        require(message == u8"Не удалось распознать макрос", "Unexpected macro import result");
        require(active == page && before == J::array({state(logic), state(table), state(combined)}),
                "Rejected import changed the editor");
    };
    expectUnrecognized(changed);
    source.erase(source.find("// LWB1 "));
    save();
    expectUnrecognized(changed);
    // Test storage capacity independently of the cost of synthesizing a random function.
    U originalFbd = dir + "/EditableBool.tple", denseFile = dir + "/storage-limit.tple";
    J drawings = J::parse(zip_read(originalFbd, "Template")),
      meta = J::parse(zip_read(originalFbd, "MetaData"));
    const S rootId = meta.at("ComponentMetaData").at("UniqueId");
    auto &description = drawings[rootId]["MetaData"]["ComponentMetaData"]["Description"];
    description = "FBD";
    U noComment = dir + "/no-comment.tple";
    zip_write(noComment, {{"Template", drawings.dump()}, {"MetaData", meta.dump()}, {"MinRequiredVersion", "2.0"}});
    expectUnrecognized(noComment);
    J dense = b2f::fresh(16, false);
    dense.erase("rules");
    dense["input_comments"] = std::vector<S>(16, u8"Обратная связь привода");
    S truth(65536, '0');
    uint32_t seed = 1;
    for (auto &bit : truth) {
        seed = 1664525u * seed + 1013904223u;
        bit = (seed >> 31) ? '1' : '0';
    }
    dense["truth"] = truth;
    MacroDocument packed{"Bool", "Storage", dense, {{"st", false}, {"fbd", true}}};
    description = "";
    auto probe = drawings;
    embedDocument(probe, meta, packed);
    size_t trailer =
        probe[rootId]["MetaData"]["ComponentMetaData"]["Description"].get_ref<const S &>().size();
    description = S(65536 - trailer, 'a');
    J tooLarge = drawings;
    tooLarge[rootId]["MetaData"]["ComponentMetaData"]["Description"].get_ref<S &>() += "a";
    bool rejected = false;
    try {
        embedDocument(tooLarge, meta, packed);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Reject FBD description over 64 KiB");
    embedDocument(drawings, meta, packed);
    zip_write(denseFile,
              {{"Template", drawings.dump()}, {"MetaData", meta.dump()}, {"MinRequiredVersion", "2.0"}});
    auto restored = readMacroDocument(denseFile);
    require(!restored.logicChanged && restored.state == dense, "16-input truth table at FBD size limit");
    write_file(
        dir + "/result.txt",
        "OK: six editor imports, ST trailer, edited logic, truncated data, 16 inputs and FBD limit");
}
void Workbench::checkEditorUI(const U &dir, bool snapshots) {
    ForceDirectories(dir);
    Show();
    auto t = tables[combined];
    J source = {{"fields",
                 {{"a", "INPUT", "A"},
                  {"speed", "REAL", "Speed"},
                  {"b", "INPUT", "B"},
                  {"on", "BOOL", "On"},
                  {u8"Описание", "comment", ""}}},
                {"records", {{"0", "3.5", "1", "1", u8"Пуск"}}}};
    t->commit(source);
    require(t->state["fields"][1][2]=="B" &&
            t->state["records"][0]==J::array({"0","1","3.5","1",u8"Пуск"}), "Inputs first with matching values");
    source=t->state;
    t->render(true);
    auto open = [&](TStringGrid *view) {
        t->tabs->ActivePage = static_cast<TTabSheet *>(view->Parent);
        require(t->grid() == view, "Active field list");
    };
    auto click = [&](const U &caption) {
        for (int i = 0; i < t->ComponentCount; ++i) {
            auto b = dynamic_cast<TButton *>(t->Components[i]);
            if (b && b->Caption == caption && b->Showing) {
                b->Click();
                return;
            }
        }
        throw std::runtime_error("Missing button: " + utf8(caption));
    };
    selectPage(combined);
    open(t->contacts);
    require(t->contacts->ColCount == 2 && t->contacts->Cells[1][2] == "B", "Input list");
    t->contacts->Row = 2;
    require(t->inputColor->Items->Equals(boolean->inputColor->Items), "Shared input palette");
    t->inputColor->ItemIndex = 7;
    t->inputColor->OnChange(t->inputColor);
    require(fieldColor(t->state["fields"][1]) == 7, "Color selected input only");
    t->records->PopupMenu->Items->Items[0]->Items[0]->Click();
    require(t->state == source, "Clear input color");
    t->undoItem->Click();
    require(t->inputColor->ItemIndex == 7, "Undo input color change");
    click(wide(u8"Выше"));
    require(t->state["records"][0] == J::array({"1", "0", "3.5", "1", u8"Пуск"}) &&
                t->contacts->Cells[1][1] == "B",
            "Move input and its record values");
    require(fieldColor(t->state["fields"][0]) == 7 && t->inputColor->ItemIndex == 7 &&
            fieldColor(t->state["fields"][1]) == 0, "Input color follows reordered data");
    switchTo(logic, mrYes);
    require(boolean->state.at("inputs")[0] == "B" && boolean->state.at("input_colors")[0] == 7,
            "Combined color transfers to Boolean");
    boolean->fields->Row = 2;
    boolean->inputColor->ItemIndex = 3;
    boolean->inputColor->OnChange(boolean->inputColor);
    U booleanWorkbook = dir + "/BooleanColors.xlsx";
    b2f::save(booleanWorkbook, boolean->state);
    require(b2f::load(booleanWorkbook).state.at("input_colors") == J::array({7,3}),
            "Shared workbook writer preserves Boolean colors");
    switchTo(combined, mrYes);
    require(fieldColor(t->state["fields"][1]) == 3, "Boolean color merges by input name");
    J uncolored = t->state;
    setFieldColor(uncolored["fields"][0], 0);
    require(unified::transfer(Mode::Combined, uncolored, Mode::Boolean, boolean->state, false)
                .at("input_colors") == J::array({7,3}), "Merge preserves a destination-only color");
    auto workbook = dir + "/Colors.xlsx";
    write_book(workbook, t->state, true);
    require(read_book(workbook).state == t->state, "Input colors round trip through Excel");
    open(t->records);
    Application->ProcessMessages();
    require(tables[table]->records->DefaultColAlignment == taCenter &&
            t->records->DefaultColAlignment == taCenter && boolean->rules->DefaultColAlignment == taCenter,
            "Center values in all three record grids");
    t->records->Col = t->records->ColCount - 1;
    t->records->Row = 1;
    t->records->SetFocus();
    t->records->EditorMode = true;
    HWND editor = ::GetFocus();
    require(editor && ::IsChild(t->records->Handle, editor) &&
            (::GetWindowLong(editor, GWL_STYLE) & (ES_CENTER | ES_RIGHT)) == ES_CENTER,
            "Center the record text while editing");
    t->records->EditorMode = false;
    if (snapshots) {
        std::unique_ptr<Graphics::TBitmap> colored(GetFormImage());
        colored->SaveToFile(dir + "/InputColors.bmp");
    }
    J originalOrder=t->state;
    t->records->Col=1;
    click(wide(u8"Столбец →"));
    require(t->state==originalOrder,"Input cannot cross the separator");
    t->records->Col=2;
    click(wide(u8"Столбец →"));
    require(t->state["fields"][3][2]=="Speed" && t->state["records"][0][3]=="3.5","Move a record column");
    click(wide(u8"← Столбец"));
    require(t->state==originalOrder,"Move the parameter and its value back");
    open(t->contacts);
    click(wide(u8"Добавить вход"));
    require(t->contacts->Cells[1][3] == "I1", "Add input");
    click(wide(u8"Удалить вход"));
    require(t->contacts->RowCount == 3 && t->fields->RowCount == 4,
            "Remove only the selected input");
    t->contacts->Col = 0;
    t->contacts->Row = 1;
    t->pasteCells("start\tStart\nstop\tStop");
    require(t->state["fields"][1][2] == "Stop" && t->state["fields"][2][2] == "Speed",
            "Paste into inputs");
    open(t->fields);
    require(t->fields->Cells[2][1] == "Speed" && t->fields->Cells[2][2] == "On", "Output list");
    t->fields->Row = 2;
    click(wide(u8"Выше"));
    require(t->state["records"][0] == J::array({"1", "0", "1", "3.5", u8"Пуск"}),
            "Move output with values");
    click(wide(u8"Добавить выход"));
    require(t->fields->Cells[2][4] == "Field1", "Add output");
    click(wide(u8"Удалить выход"));
    t->undoItem->Click();
    require(t->fields->Cells[2][4] == "Field1", "Undo output deletion");
    t->fields->Col = 1;
    t->fields->Row = 2;
    t->typeEditor->ItemIndex = t->typeEditor->Items->IndexOf("UDINT");
    t->selectType(t->typeEditor);
    require(t->state["fields"][3][1] == "UDINT", "Change selected output type");
    int footerY = settings->Top;
    for (int i = 0; i < 3; ++i) {
        selectPage(i);
        auto inner = i == logic ? boolean->tabs : tables[i]->tabs;
        inner->ActivePageIndex = i == logic ? 0 : inner->PageCount - 1;
        Application->ProcessMessages();
        require(settings->Top == footerY &&
                    !pages[i].messages->Showing && !pages[i].bottom->Showing &&
                    (i == logic ? boolean->output->Showing : tables[i]->fbdMode->Showing),
                "Shared footer and settings");
        inner->ActivePageIndex = 0;
        Application->ProcessMessages();
        if (snapshots) {
            std::unique_ptr<Graphics::TBitmap> shot(GetFormImage());
            shot->SaveToFile(dir + "/Editor" + IntToStr(i) + ".bmp");
        }
    }
    // A forced Cancel makes an unexpected repeated offer observable without a modal window.
    auto editDocument = [&](int mode, const J &doc) {
        if (mode == logic) { boolean->commit(doc); boolean->render(); }
        else { tables[mode]->commit(doc); tables[mode]->render(true); }
    };
    for (int from : {logic,table,combined}) {
        for (int to : {logic,table,combined}) {
            if ((from == combined) == (to == combined)) continue;
            J source=defaults[from];
            if (from==logic) source["inputs"][0]="Start";
            else {
                source["fields"][0][2]=from==combined ? "Start" : "Mode";
                source["fields"][1][2]="Speed";
                source["records"]=J::array({J::array({"1","10"})});
            }
            editDocument(from,source);
            editDocument(to,defaults[to]);
            selectPage(from);
            switchTo(to,mrCancel);
            require(active==from,"Cancel leaves source open");
            switchTo(to,mrNo);
            require(active==to && state(to)==defaults[to],"Decline preserves target");
            switchTo(from,mrNo);
            switchTo(to,mrCancel);
            require(active==to && state(to)==defaults[to],"Unchanged source must skip declined offer");
            switchTo(from,mrNo);
            if (from==logic) source["inputs"][0]="Run";
            else source["fields"][to==logic ? 0 : 1][0]="Updated";
            editDocument(from,source);
            switchTo(to,mrCancel);
            require(active==from,"Edited source offers transfer again");
            switchTo(to,mrYes);
            require(active==to && state(to)!=defaults[to],"Accept after edit");
        }
    }
    J books[3];
    books[logic]={{"inputs", {"Start","Ready"}},{"input_comments",{u8"Команда пуска",u8"Привод готов"}},
                  {"input_colors",{2,4}},{"output","Run"},{"inverted","NoRun"},
                  {"rules", {{{"inputs",{"1","1"}},{"q","1"},{"comment",u8"Пуск разрешён"}}}}};
    books[table]={{"fields",{{u8"Номер режима","ID","Mode"},{u8"Частота, Гц","REAL","Speed"},
                             {u8"Разрешение","BOOL","Enable"},{u8"Режим","comment",""}}},
                  {"records",{{"0","0","0",u8"Останов"},{"1","25.5","1",u8"Работа"}}}};
    books[combined]={{"fields",{{u8"Команда пуска","INPUT","Start"},{u8"Привод готов","INPUT","Ready"},
                                {u8"Частота, Гц","REAL","Speed"},{u8"Разрешение","BOOL","Enable"},
                                {u8"Режим","comment",""}}},
                     {"records",{{"0","X","0","0",u8"Останов"},{"1","1","25.5","1",u8"Работа"}}}};
    setFieldColor(books[combined]["fields"][0],2);
    U files[]={dir+"/Table.xlsx",dir+"/Bool.xlsx",dir+"/Combined.xlsx"};
    for (int i : {table,logic,combined}) {
        J before[]={state(table),state(logic),state(combined)};
        if(i==logic) {
            b2f::save(files[i],books[i]);
            boolean->load(files[i]);
        } else {
            write_book(files[i],books[i],i==combined);
            require(tables[i]->load(files[i]),"Import independent workbook");
        }
        for(int j=0;j<3;++j)
            require(state(j)==(j==i ? books[i] : before[j]),"Excel import affects only its own document");
    }
    require(t->records->Cells[0][0]==wide(u8"Start\nКоманда пуска") &&
            t->records->Cells[2][0]==wide(u8"Speed\nЧастота, Гц") &&
            t->records->Cells[4][0]==wide(u8"Режим") &&
            t->records->RowHeights[0]>t->records->DefaultRowHeight,"Combined port comments");
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) if(i!=j) {
        bool rejected=false;
        try {
            if(i==logic) boolean->load(files[j]);
            else tables[i]->setDocument(read_book(files[j]),files[j],false);
        } catch(const std::exception &) {rejected=true;}
        require(rejected && state(i)==books[i],"Wrong workbook preserves document");
    }
    for(int mode : {logic,combined,table}) {
        selectPage(mode);
        U filename=mode==logic ? U("Bool.xlsx") : mode==table ? U("Table.xlsx") : U("Combined.xlsx");
        require(Caption==Application->Title+wide(u8" — ")+filename,
                "Workbook name on every tab: "+utf8(Caption)+" expected "+utf8(filename));
        require(!sheetMenus[mode]->Visible,"Single-sheet workbook has no sheet menu");
    }
    for(int mode : {combined,logic,table}) {
        auto group=examples->Items[mode];
        require(examples->Parent==Menu->Items->Items[2] && group->Count>0,"Examples available from Help");
        int workbooks = 0;
        for(int i=0;i<group->Count;++i) {
            J before[]={state(table),state(logic),state(combined)};
            auto entry = group->Items[i];
            entry->Click();
            require(active==mode && !dirty(mode) && Caption.Pos(wide(u8"Пример"))>0,
                    "Open named example in its own mode");
            if (!entry->Hint.IsEmpty()) {
                ++workbooks;
                require(Caption.Pos(StripHotkey(entry->Caption)) > 0 &&
                        (mode == logic ? boolean->source : tables[mode]->source).IsEmpty(),
                        "Open the selected workbook as an independent example");
            }
            for(int j=0;j<3;++j) if(j!=mode) require(state(j)==before[j],"Example changed another tab");
        }
        require(workbooks > 0, "Packaged examples missing from this mode");
    }
    write_file(dir + "/result.txt",
               "OK: independent Table, Bool and Combined workbooks, port headers, transfer and editor controls");
}

void Workbench::checkLayout(const U &dir) {
    selectPage(logic);
    auto doc=b2f::fresh(5,false);
    doc["inputs"][4]="Permit";
    doc["input_comments"]={"","","","",u8"Разрешение пуска"};
    doc["input_colors"]={0,0,0,0,4};
    boolean->commit(doc);
    boolean->render();
    require(boolean->rules->ColWidths[3]<MulDiv(70,boolean->CurrentPPI,96),"Compact new Bool input");
    for(int n : {2,3,5}) {
        boolean->count->ItemIndex=n-1;
        boolean->settings(boolean->count);
        require(boolean->state.at("inputs").size()==unsigned(n),"Input count change");
    }
    require(boolean->state==doc,"Restore hidden input name, comment and color");
    auto pager=boolean->pageLabel->Parent;
    require(!pager->Visible,"Single page navigation hidden");
    boolean->commit(b2f::dense_editor_state(b2f::fresh(9,false)));
    boolean->render();
    require(pager->Visible && pager->Align==alBottom,"Multiple pages have bottom navigation");
    require(boolean->count->Parent==boolean->output->Parent &&
            boolean->count->Parent==boolean->inverted->Parent,"Output names on input toolbar");
    auto dialog=editorMessage(L"?",mtConfirmation,TMsgDlgButtons()<<mbYes<<mbNo<<mbCancel);
    int translated=0;
    for(int i=0;i<dialog->ComponentCount;++i) if(auto b=dynamic_cast<TButton*>(dialog->Components[i])) {
        require(b->Caption==wide(b->ModalResult==mrYes?u8"Да":b->ModalResult==mrNo?u8"Нет":u8"Отмена"),
                "Dialog button translation");
        ++translated;
    }
    require(translated==3 && dialog->Caption==wide(u8"Подтверждение"),"Dialog title and choices");
    auto compactBool=b2f::fresh(2,false);
    compactBool["input_comments"]={u8"Пуск\nнасоса",""};
    boolean->setDocument({compactBool,"",{}}, "");
    auto compactTable=defaults[combined];
    compactTable["fields"][0][0]=u8"Пуск\nнасоса";
    tables[combined]->setDocument({compactTable,"",{}}, "",false);
    S report;
    for(int dpi : {96,120,144,192,96}) {
        ScaleForPPI(dpi);
        for(int width : {960,1200}) {
            Width=MulDiv(width,dpi,96);
            int inspected=0;
            for(int mode=0; mode<3; ++mode) {
                selectPage(mode);
                require(Caption==documentTitle() && Caption.Pos(wide(u8"OWL-Tables — "))==1,"Document title");
                auto inner=mode==logic ? boolean->tabs : tables[mode]->tabs;
                for(int page=0;page<inner->PageCount;++page) {
                    inner->ActivePageIndex=page;
                    Application->ProcessMessages();
                    // Showing a native tab may dispatch a DPI notification from the physical monitor.
                    // Apply the simulated monitor change after its window has been created.
                    ScaleForPPI(dpi);
                    Width=MulDiv(width,dpi,96);
                    std::function<void(TWinControl*)> inspect=[&](TWinControl *parent) {
                        auto bar=dynamic_cast<EditorBar*>(parent);
                        for(int i=0;i<parent->ControlCount;++i) {
                            auto c=parent->Controls[i];
                            if(!c->Visible) continue;
                            if(bar) {
                                S where="at "+std::to_string(dpi)+" DPI, "+std::to_string(width)+
                                    ", "+utf8(inner->ActivePage->Caption)+", "+utf8(c->ClassName());
                                require(bar->PixelsPerInch==dpi,"Toolbar DPI "+where+", actual "+std::to_string(bar->PixelsPerInch)+", form "+std::to_string(PixelsPerInch)+", editor "+std::to_string(pages[mode].form->PixelsPerInch));
                                auto r=c->BoundsRect;
                                require(r.Left>=0 && r.Top>=0 && r.Right<=bar->ClientWidth &&
                                        r.Bottom<=bar->ClientHeight,"Toolbar bounds "+where+" rect "+
                                        std::to_string(r.Left)+","+std::to_string(r.Top)+","+std::to_string(r.Right)+","+
                                        std::to_string(r.Bottom)+" parent "+std::to_string(bar->ClientWidth)+"x"+
                                        std::to_string(bar->ClientHeight));
                                Canvas->Font->Assign(bar->Font);
                                int textHeight=Canvas->TextHeight(L"Ау");
                                require(c->Height>=textHeight,"Control height "+where);
                                if(auto button=dynamic_cast<TButton*>(c))
                                    require(button->Width>=Canvas->TextWidth(button->Caption)+MulDiv(8,dpi,96),
                                            "Button caption width "+where);
                                if(auto label=dynamic_cast<TLabel*>(c))
                                    require(label->Width>=Canvas->TextWidth(label->Caption),"Label width "+where);
                                if(c==boolean->count) {
                                    auto middle=boolean->count->Top+boolean->count->Height/2;
                                    for(auto edit : {boolean->output,boolean->inverted})
                                        require(edit->Top<=middle && middle<edit->Top+edit->Height,"Q/NQ on count row");
                                }
                                for(int j=0;j<i;++j) if(parent->Controls[j]->Visible) {
                                    RECT overlap;
                                    auto other=parent->Controls[j]->BoundsRect;
                                    require(!::IntersectRect(&overlap,&r,&other),
                                            "Overlapping controls "+where);
                                }
                                if(auto combo=dynamic_cast<TComboBox*>(c)) {
                                    int extra=MulDiv(combo->Style==csOwnerDrawFixed?54:24,dpi,96);
                                    for(int k=0;k<combo->Items->Count;++k)
                                        require(combo->Width>=Canvas->TextWidth(combo->Items->Strings[k])+extra,
                                                "Combo text width "+where);
                                }
                                ++inspected;
                            }
                            if(auto g=dynamic_cast<TStringGrid*>(c))
                                require(g->DefaultRowHeight>=g->Canvas->TextHeight(L"Ау"),"Grid row height");
                            if(auto child=dynamic_cast<TWinControl*>(c)) inspect(child);
                        }
                    };
                    if(mode==logic || mode==combined) {
                        auto grid=mode==logic ? boolean->rules : tables[combined]->records;
                        if(grid->Showing) {
                            // Simulate opening the document on this monitor with the editor's 9 pt font.
                            grid->ScaleForPPI(dpi);
                            require(grid->PixelsPerInch==dpi,"Grid layout DPI");
                            grid->Font->Height=-MulDiv(9,dpi,72);
                            if(mode==logic) boolean->render(true); else tables[combined]->render(true);
                            const int w=grid->ColWidths[mode==logic ? 1 : 0];
                            require(w<=MulDiv(40,dpi,96),utf8(names[mode])+" two-line port width: "+
                                    std::to_string(w)+" at "+std::to_string(dpi));
                            report+=utf8(names[mode])+" "+std::to_string(dpi)+" DPI: "+std::to_string(w)+" px\n";
                        }
                    }
                    inspect(this);
                }
            }
            report+=std::to_string(dpi)+" DPI, width "+std::to_string(width)+": "+
                    std::to_string(inspected)+" control placements OK\n";
        }
    }
    write_file(dir+"/layout.txt",report);
}

void Workbench::smoke(const U &dir) {
    ForceDirectories(dir);
    auto a = tables[table], c = tables[combined];
    J src = {{"fields",
              {{u8"Номер", "ID", "Mode"},
               {u8"Давление", "REAL", "P"},
               {u8"Режим", u8"Комментарий", ""}}},
             {"records", {{"7", "12.5", u8"Пуск"}, {"3", "20", u8"Работа"}}}};
    a->commit(src);
    a->render(true);
    switchTo(combined, mrYes);
    require(freshTarget(combined, logic) && c->state.at("records").size() == 2,
            "Table-first transfer must leave contact selection open");
    c->undoItem->Click();
    switchTo(table, mrNo);
    switchTo(logic);
    require(a->state == src && pristine(logic), "Table to Boolean modified a document");
    J signals = {{"inputs", {"Start", "Ready"}},
                 {"input_comments", {u8"Пуск", u8"Готовность"}},
                 {"output", "Q"},
                 {"inverted", "NQ"},
                 {"rules", {{{"inputs", {"1", "X"}}, {"q", "1"}, {"comment", "keep"}}}}};
    boolean->commit(signals);
    boolean->render();
    switchTo(combined, mrCancel);
    require(active == logic && pristine(combined), "Cancelled switch");
    switchTo(combined, mrNo);
    require(active == combined && pristine(combined), "Declined transfer");
    switchTo(logic, mrNo);
    switchTo(combined, mrYes);
    require(pristine(combined), "A declined offer must not recur for an unchanged source");
    switchTo(logic, mrNo);
    signals["input_comments"][0]=u8"Пуск установки";
    boolean->commit(signals);
    boolean->render();
    switchTo(combined, mrYes);
    require(c->state["fields"][0][2] == "Start" && c->state["fields"][1][0] == u8"Готовность" &&
                c->state["records"].empty(),
            "Boolean inputs and comments");
    switchTo(table, mrNo);
    switchTo(combined, mrYes);
    auto rows = c->state.at("records");
    require(rows.size() == 2 && rows[1].back() == u8"Работа" && rows[0][0] == "",
            "Incorrect merged records: " + c->state.dump());
    // Accepting the transfer is a single undoable document edit.
    c->undoItem->Click();
    require(c->state["records"].empty(), "Undo transfer");
    c->redoItem->Click();
    require(c->state["records"] == rows, "Redo transfer");
    J changed = c->state;
    for (size_t i = 0; i < changed["fields"].size(); ++i)
        if (changed["fields"][i][2] == "P")
            changed["records"][0][i] = "19.5";
    changed["records"][0][0] = "1";
    changed["records"][1][0] = "0";
    changed["records"][0][1] = "X";
    changed["records"][1][1] = "1";
    changed["fields"][0][0] = u8"Пуск двигателя";
    c->commit(changed);
    c->render(true);
    J dense = {{"inputs", {"Start"}}, {"output", "Q"},           {"inverted", "NQ"},
               {"truth", "01"},       {"comments", {"", "hit"}}, {"row_order", {1}}};
    J expanded = unified::transfer(Mode::Combined, changed, Mode::Boolean, dense, false);
    require(expanded.at("truth") == "0101" && expanded.at("row_order") == J::array({1, 3}) &&
                expanded.at("comments") == J::array({"", "hit", "", "hit"}),
            "Dense Boolean rows and comments");
    J extra = src;
    extra["fields"].push_back({u8"Местный выход", "BOOL", "Local"});
    for (auto &row : extra["records"])
        row.push_back("1");
    extra["records"].push_back({"9", "15", u8"Резерв", "1"});
    J merged = unified::transfer(Mode::Combined, changed, Mode::Table, extra, false);
    require(merged.at("records")[2] == extra.at("records")[2] &&
                merged.at("records")[0][3] == "1" &&
                unified::transfer(Mode::Combined, changed, Mode::Table, merged, false) == merged,
            "Destination-only fields and rows; repeated transfer");
    J limit = signals;
    while (limit["inputs"].size() < 16)
        limit["inputs"].push_back("I" + std::to_string(limit["inputs"].size()));
    limit.erase("input_comments");
    J incoming = changed;
    incoming["fields"][0][2] = "Another";
    bool rejected = false;
    try {
        unified::transfer(Mode::Combined, incoming, Mode::Boolean, limit, false);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected && limit.at("inputs").size() == 16, "Input limit on merge");
    switchTo(logic, mrYes);
    require(boolean->state["rules"] == signals["rules"] &&
                boolean->state["input_comments"][0] == u8"Пуск двигателя",
            "Return comments without Boolean rules");
    switchTo(table);
    require(a->state == src, "Boolean to table should preserve data");
    switchTo(combined, mrNo);
    switchTo(table, mrYes);
    require(a->state["records"][0][0] == "7" && a->state["records"][1][0] == "3" &&
                a->state["records"][0][1] == "19.5",
            "Merge parameters, retain IDs");
    Options opts;
    opts.name = "Settings";
    opts.fbd_step = opts.st_step = 0;
    auto tab = parse_table(a->state, false);
    auto mix = parse_table(c->state, true);
    require(tab.contacts.empty() && mix.contacts.size() == 2, "Table backend dispatch");
    t2f::generate(tab, dir + "/Table.tple", opts,a->state);
    t2f::generate(mix, dir + "/Combined.tple", opts,c->state);
    write_book(dir + "/Table.xlsx", a->state, false);
    write_book(dir + "/Combined.xlsx", c->state, true);
    require(parse_table(read_book(dir + "/Combined.xlsx").state, true).contacts == mix.contacts,
            "Workbook round trip");
    fbd->Checked = false;
    switchTo(logic);
    require(!boolean->fbd->Checked && boolean->st->Checked, "Shared formats");
    require(boolean->state["rules"] == signals["rules"] && c->state == changed,
            "Inactive document changed");
    boolean->startGeneration(dir + "/Boolean.fbst");
    while (running()) {
        Application->ProcessMessages();
        Sleep(10);
    }
    require(boolean->lastJobError.empty() && FileExists(dir + "/Boolean.fbst"),
            "Boolean backend dispatch");
    Show();
    for (int i = 0; i < 3; ++i) {
        selectPage(i);
        Application->ProcessMessages();
        std::unique_ptr<Graphics::TBitmap> shot(GetFormImage());
        shot->SaveToFile(dir + "/Workbench" + IntToStr(i) + ".bmp");
    }
    write_file(dir + "/result.txt", "OK: six transitions, accept/decline/cancel, undo/redo, shared "
                                    "formats, Excel and three generators");
}

int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int) {
    U folder;
    try {
        if (ParamCount() == 2 && (ParamStr(1) == "--smoke-test" || ParamStr(1) == "--ui-smoke" || ParamStr(1) == "--layout-check" || ParamStr(1) == "--macro-check"))
            folder = ExpandFileName(ParamStr(2));
        russianDialogLanguage();
        Application->Initialize();
        Application->MainFormOnTaskBar = true;
        Application->Title = "OWL-Tables";
        Application->Icon->LoadFromResourceName(reinterpret_cast<NativeUInt>(HInstance),
                                                "MAINICON");
        Workbench *window = nullptr;
        Application->CreateForm(__classid(Workbench), &window);
        window->Icon->Assign(Application->Icon);
        if (!folder.IsEmpty()) {
            if (ParamStr(1) == "--macro-check") window->checkMacros(folder);
            else if (ParamStr(1) == "--layout-check") {
                window->checkEditorUI(folder,false);
                window->checkLayout(folder);
            } else if (ParamStr(1) == "--ui-smoke")
                window->checkEditorUI(folder,true);
            else
                window->smoke(folder);
            delete window;
            return 0;
        }
        Application->Run();
        return 0;
    } catch (const Exception &e) {
        if (folder.IsEmpty())
            editorError(e.Message);
        else
            write_file(folder + "/error.txt", utf8(e.Message));
    } catch (const std::exception &e) {
        if (folder.IsEmpty())
            editorError(wide(e.what()));
        else
            write_file(folder + "/error.txt", e.what());
    }
    return 1;
}
