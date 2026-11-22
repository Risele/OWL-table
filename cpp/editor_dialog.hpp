#pragma once
#include <vcl.h>
#include <Vcl.Dialogs.hpp>
#include <memory>
#include "core.hpp"
namespace t2f {
inline std::unique_ptr<TForm> editorMessage(const U &msg,TMsgDlgType type,TMsgDlgButtons buttons) {
    U captions[4]; int count=0; TMsgDlgBtn first=mbOK;
    for (auto b : {mbYes,mbNo,mbOK,mbCancel}) if(buttons.Contains(b)) {
        if(!count) first=b;
        captions[count++]=wide(b==mbYes?u8"Да":b==mbNo?u8"Нет":b==mbCancel?u8"Отмена":u8"ОК");
    }
    std::unique_ptr<TForm> form(CreateMessageDialog(msg,type,buttons,first,captions,count-1));
    form->Caption=wide(type==mtConfirmation?u8"Подтверждение":type==mtError?u8"Ошибка":
                       type==mtWarning?u8"Предупреждение":u8"Информация");
    form->Position=poMainFormCenter;
    return form;
}
inline int editorDialog(const U &msg,TMsgDlgType type,TMsgDlgButtons buttons,int = 0) {
    return editorMessage(msg,type,buttons)->ShowModal();
}
inline void editorHelp(const U &mode,const U &text) {
    auto dialog=editorMessage(text,mtInformation,TMsgDlgButtons()<<mbOK);
    dialog->Caption=wide(u8"Справка — ")+mode;
    dialog->ShowModal();
}
inline void editorError(const U &msg) { editorDialog(msg,mtError,TMsgDlgButtons()<<mbOK); }
template<class Dialog>
class EditorFileDialog : public Dialog {
    void __fastcall translateButtons(TObject *) {
        HWND window=this->Handle;
        if(window && !GetDlgItem(window,IDOK)) window=GetParent(window);
        if(!window) return;
        SetDlgItemTextW(window,IDOK,dynamic_cast<TSaveDialog*>(this) ? L"Сохранить" : L"Открыть");
        SetDlgItemTextW(window,IDCANCEL,L"Отмена");
    }
public:
    __fastcall EditorFileDialog(TComponent *owner) : Dialog(owner) {
        this->OnFolderChange=translateButtons;
    }
};
inline void russianDialogLanguage() {
    ULONG languages=0;
    SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME,L"ru-RU\0",&languages);
}
}
