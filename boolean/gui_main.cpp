#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
#include "window.hpp"
#include "version.hpp"
#include "../cpp/editor_dialog.hpp"
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int) {
    auto args = b2f::arguments();
    bool smoke = args.size() == 2 && (args[0] == "--smoke-test" || args[0] == "--smoke-editor");
    try {
        if (!args.empty() && !smoke)
            return b2f::cli(args);
        t2f::russianDialogLanguage();
        Application->Initialize();
        Application->Title = "Boolean2FBD " BOOLEAN_VERSION;
        Application->MainFormOnTaskBar = true;
        Application->Icon->LoadFromResourceName(reinterpret_cast<NativeUInt>(HInstance),
                                                "MAINICON");
        BooleanForm *form = nullptr;
        Application->CreateForm(__classid(BooleanForm), &form);
        form->Icon->Assign(Application->Icon);
        if (smoke) {
            form->smoke(t2f::wide(args[1]));
            delete form;
            return 0;
        }
        form->Show();
        Application->Run();
        delete form;
        return 0;
    } catch (const Exception &e) {
        if (smoke) {
            ForceDirectories(t2f::wide(args[1]));
            t2f::write_file(t2f::wide(args[1]) + "/error.txt", t2f::utf8(e.Message));
        } else
            MessageBoxW(nullptr, e.Message.c_str(), L"Boolean2FBD", MB_ICONERROR);
    } catch (std::exception &e) {
        if (smoke) {
            ForceDirectories(t2f::wide(args[1]));
            t2f::write_file(t2f::wide(args[1]) + "/error.txt", e.what());
        } else
            MessageBoxW(nullptr, t2f::wide(e.what()).c_str(), L"Boolean2FBD", MB_ICONERROR);
    }
    return 1;
}
