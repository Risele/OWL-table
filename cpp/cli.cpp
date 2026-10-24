#include <vcl.h>
#pragma hdrstop
#include "cli.hpp"
#include <shellapi.h>
#include <cstdio>
#include <algorithm>
#pragma comment(lib, "shell32.lib")
using namespace t2f;
std::vector<U> arguments() {
    int count = 0;
    LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<U> v;
    for (int i = 1; i < count; ++i)
        v.push_back(args[i]);
    LocalFree(args);
    return v;
}
static void message(const S &s, bool error = false) {
    FILE *f = error ? stderr : stdout;
    fwrite(s.data(), 1, s.size(), f);
    fwrite("\n", 1, 1, f);
    fflush(f);
}
int run_cli(const std::vector<U> &argv) {
    Options o;
    U source, output;
    S sheet;
    bool have_source = false, explicit_fbd = false, explicit_st = false, single = false;
    std::map<S, S> values;
    std::set<S> flags;
    try {
        for (size_t i = 0; i < argv.size(); ++i) {
            S a = utf8(argv[i]);
            if (a == "--help" || a == "-h") {
                message(
                    u8"Table2FBD C++ 2.0.6\nВызов: Table2FBD-cli.exe файл.xlsx [--output файл] [--fbd] [--st]\nПо умолчанию создаются FBD и ST.\n--fbd-single / --fbd-step N: независимый режим FBD\n--st-single / --st-step N: независимый режим ST\n--name ИМЯ, --sheet ЛИСТ\n--single / --step N: общий режим обоих форматов\nОтносительные пути отсчитываются от текущей папки.");
                return 0;
            }
            if (a == "--version") {
                message("Table2FBD C++ 2.0.6");
                return 0;
            }
            if (a == "--fbd" || a == "--st" || a == "--single" || a == "--fbd-single" ||
                a == "--st-single") {
                flags.insert(a);
                continue;
            }
            if (a == "--generate" || a == "--output" || a == "-o" || a == "--name" ||
                a == "--sheet" || a == "--step" || a == "--fbd-step" || a == "--st-step" ||
                a == "--fbd-mode" || a == "--st-mode") {
                require(i + 1 < argv.size(), u8"Не задано значение " + a);
                S value = utf8(argv[++i]);
                if (a == "--generate") {
                    require(!have_source, u8"Укажите входной файл один раз.");
                    source = wide(value);
                    have_source = true;
                } else
                    values[a == "-o" ? "--output" : a] = value;
                continue;
            }
            require(a.empty() || a[0] != '-', u8"Неизвестный параметр: " + a);
            require(!have_source, u8"Укажите входной файл один раз.");
            source = argv[i];
            have_source = true;
        }
        require(have_source && !source.IsEmpty(), u8"Укажите файл .xlsx.");
        for (auto lang : {S("fbd"), S("st")})
            require(!(flags.count("--" + lang + "-single") && values.count("--" + lang + "-step")),
                    u8"Несовместимые параметры режима " + lang);
        require(
            !(flags.count("--single") &&
              (values.count("--step") || values.count("--fbd-step") || values.count("--st-step"))),
            u8"--single несовместим с шагом групп.");
        for (auto mode : {S("--fbd-mode"), S("--st-mode")})
            if (values.count(mode))
                require(values[mode] == "single" || values[mode] == "grouped",
                        u8"Режим: single или grouped.");
    } catch (std::exception &e) {
        message(S(u8"Ошибка: ") + e.what(), true);
        return 2;
    }
    try {
        auto step = [&](const S &key) -> uint64_t {
            S value = values.count(key)        ? values[key]
                      : values.count("--step") ? values["--step"]
                                               : "10";
            size_t n = 0;
            uint64_t v = std::stoull(value, &n);
            require(n == value.size() && v >= 1 && v <= 4294967296ULL,
                    u8"Шаг должен быть от 1 до 4294967296.");
            return v;
        };
        o.fbd = !flags.count("--st") || flags.count("--fbd");
        o.st = !flags.count("--fbd") || flags.count("--st");
        o.name = values.count("--name") ? values["--name"] : "TableParameters";
        o.fbd_step = flags.count("--single") || flags.count("--fbd-single") ||
                             values["--fbd-mode"] == "single"
                         ? 0
                         : step("--fbd-step");
        o.st_step =
            flags.count("--single") || flags.count("--st-single") || values["--st-mode"] == "single"
                ? 0
                : step("--st-step");
        source = ExpandFileName(source);
        o.source_name = utf8(ExtractFileName(source));
        output = values["--output"].empty() ? ChangeFileExt(source, ".tple")
                                            : ExpandFileName(wide(values["--output"]));
        auto book = read_book(source, values["--sheet"]);
        auto results = generate(parse_table(book.state), output, o);
        for (auto &r : results) {
            message(u8"Создан файл: " + r["path"].get<S>());
            message(r["format"].get<S>() + ": " + o.name + u8"; записей: " +
                    std::to_string(r["records"].get<int>()) + u8"; групп: " +
                    std::to_string(r["groups"].get<int>()));
            if (r["groups"].get<int>())
                message(u8"На схему следует поместить сводный макрос: " + o.name);
        }
        return 0;
    } catch (const Exception &e) {
        message(u8"Ошибка: " + utf8(e.Message), true);
        return 1;
    } catch (std::exception &e) {
        message(S(u8"Ошибка: ") + e.what(), true);
        return 1;
    }
}
#ifdef TABLE2FBD_CONSOLE
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        return run_cli(arguments());
    } catch (...) {
        return 1;
    }
}
#endif
