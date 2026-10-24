#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
#include "window.hpp"
#include "cli.hpp"
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int) {
    auto args = arguments();
    auto report = [&](const t2f::S &message) {
        if (args.size() == 2 && args[0] == "--smoke-test") {
            ForceDirectories(args[1]);
            t2f::write_file(args[1] + "/error.txt", message);
        } else
            MessageBoxW(nullptr, t2f::wide(message).c_str(), L"Table2FBD", MB_ICONERROR);
    };
    try {
        if (!args.empty() && args[0] != "--smoke-test")
            return run_cli(args);
        Application->Initialize();
        Application->Icon->LoadFromResourceName(reinterpret_cast<NativeUInt>(HInstance),
                                                "MAINICON");
        Application->MainFormOnTaskBar = true;
        EditorForm *form = nullptr;
        Application->CreateForm(__classid(EditorForm), &form);
        form->Icon->Assign(Application->Icon);
        if (args.size() == 2 && args[0] == "--smoke-test") {
            form->smoke(args[1]);
            delete form;
            return 0;
        }
        form->Show();
        Application->Run();
        delete form;
        return 0;
    } catch (const Exception &e) {
        report(t2f::utf8(e.Message));
        return 1;
    } catch (std::exception &e) {
        report(e.what());
        return 1;
    }
}
