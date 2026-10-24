#include <vcl.h>
#pragma hdrstop
#include "cli.hpp"
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        return run_cli(arguments());
    } catch (...) {
        return 1;
    }
}
