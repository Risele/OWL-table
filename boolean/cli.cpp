#include <vcl.h>
#pragma hdrstop
#include "logic.hpp"
#include "version.hpp"
#include "editor_model.hpp"
#include "session_check.hpp"
#include "engine/contracts.hpp"
#include "engine/ir.hpp"
#include "engine/synthesis.hpp"
#include "engine/target.hpp"
#include "engine/package.hpp"
#include "engine/export.hpp"
#include "verification/native.hpp"
#include <shellapi.h>
#include <cstdio>
#pragma comment(lib, "shell32.lib")
namespace b2f {
std::vector<S> arguments() {
    int count = 0;
    auto a = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<S> result;
    for (int i = 1; i < count; ++i)
        result.push_back(utf8(a[i]));
    LocalFree(a);
    return result;
}
static void print(const S &s, bool error = false) {
    FILE *f = error ? stderr : stdout;
    fwrite(s.data(), 1, s.size(), f);
    fwrite("\n", 1, 1, f);
    fflush(f);
}
int cli(const std::vector<S> &args) {
    std::map<S, S> values;
    std::set<S> flags;
    try {
        for (size_t i = 0; i < args.size(); ++i) {
            S a = args[i];
            if (a == "--help" || a == "-h") {
                print("Boolean2FBD " BOOLEAN_VERSION "\nDefault engine: strategy. 1..16 BOOL inputs, macro depth <=3, independent ST/FBD packages.\n--generate file [--policy policy.json] [--report result.json] [--max-macro-levels 1|2|3]\n--engine legacy: explicit regression baseline only; no automatic fallback.\n");
                print(
                    u8"--generate файл.xlsx|json [--output путь] [--name Имя] [--fbd|--st]\n--validate файл | --evaluate файл --bits UDINT\n--save-xlsx файл --output книга.xlsx\n--expand файл --output книга.xlsx (до 12 входов)\n--compact файл --output книга.xlsx\n--sheet Лист; I1 — младший бит. По умолчанию создаются макросы FBD и ST. Для записи отчёта и листинга .st укажите --report имя.report.json. Оба выхода проверяются на всех комбинациях.");
                return 0;
            }
            if (a == "--version") {
                print("Boolean2FBD " BOOLEAN_VERSION);
                return 0;
            }
            if (a == "--fbd" || a == "--st") {
                flags.insert(a);
                continue;
            }
            require(a == "--session-check" || a == "--editor-check" || a == "--verify-export" || a == "--export-package" || a == "--package-search" || a == "--package-check" || a == "--target-check" || a == "--search-check" || a == "--ir-check" || a == "--legacy-ir" || a == "--truth" || a == "--engine-check" || a == "--generate" || a == "--validate" || a == "--evaluate" ||
                        a == "--save-xlsx" || a == "--expand" || a == "--compact" ||
                        a == "--engine" || a == "--policy" || a == "--report" || a == "--max-macro-levels" || a == "--output" || a == "--name" || a == "--sheet" || a == "--bits",
                    u8"Неизвестный параметр: " + a);
            require(i + 1 < args.size() && !values.count(a),
                    u8"Не задано значение или повторный параметр: " + a);
            values[a] = args[++i];
        }
        int commands = 0;
        for (auto a :
             {"--session-check", "--editor-check", "--verify-export", "--export-package", "--package-search", "--package-check", "--target-check", "--search-check", "--ir-check", "--legacy-ir", "--truth", "--engine-check", "--generate", "--validate", "--evaluate", "--save-xlsx", "--expand", "--compact"})
            commands += values.count(a);
        require(commands == 1, u8"Укажите одну команду. Справка: --help.");
        for(auto option:{"--engine","--policy","--report","--max-macro-levels"})if(values.count(option))
            require(values.count("--generate"),S(option)+" requires --generate");
        if(values.count("--engine"))require(values["--engine"]=="legacy" || values["--engine"]=="strategy","engine must be legacy or strategy");
        if(values.count("--generate") && !values.count("--engine"))values["--engine"]="strategy";
        for(auto option:{"--policy","--report","--max-macro-levels"})if(values.count(option))
            require(values["--engine"]=="strategy",S(option)+" requires --engine strategy");
        if(values.count("--max-macro-levels"))require(values["--max-macro-levels"]=="1" || values["--max-macro-levels"]=="2" || values["--max-macro-levels"]=="3","max-macro-levels must be 1, 2 or 3");
        for(auto option:{"--policy","--report"})if(values.count(option))require(!values[option].empty(),S(option)+" path is empty");
        if (values.count("--bits"))
            require(values.count("--evaluate"), u8"--bits используется с --evaluate.");
    } catch (std::exception &e) {
        print(u8"Ошибка: " + S(e.what()), true);
        return 2;
    }
    try {
        if(values.count("--session-check")) { print(session_check(J::parse(read_file(wide(values["--session-check"])))).dump());return 0; }
        if(values.count("--editor-check")) { print(editor_request(J::parse(read_file(wide(values["--editor-check"])))).dump());return 0; }
        if(values.count("--verify-export")) { print(verify_export_request(J::parse(read_file(wide(values["--verify-export"])))).dump());return 0; }
        if(values.count("--export-package")) { print(export_request(J::parse(read_file(wide(values["--export-package"])))).dump());return 0; }
        if(values.count("--package-search")) {
            J request=J::parse(read_file(wide(values["--package-search"])));
            require(request.is_object() && request.count("ir") && request.count("language"),"package search requires ir/language");
            for(auto i=request.begin();i!=request.end();++i)require(i.key()=="ir" || i.key()=="language" || i.key()=="external_levels","unknown package search field");
            if(request.count("external_levels"))require(request["external_levels"].is_number_integer() && request["external_levels"].get<int64_t>()>=0 && request["external_levels"].get<uint64_t>()<=2,"external levels must be 0..2");
            auto language = request["language"].get<S>();
            auto graph = engine::lower_target(engine::Graph::read(request["ir"]), language);
            print(engine::package_search(graph, language, request.value("external_levels",0U)).dump());return 0;
        }
        if(values.count("--package-check")) {
            J request=J::parse(read_file(wide(values["--package-check"])));
            require(request.is_object() && request.count("package"),"package request requires package");
            for(auto i=request.begin();i!=request.end();++i)require(i.key()=="package" || i.key()=="repair","unknown package request field");
            if(request.count("repair"))require(request["repair"].is_boolean(),"repair must be bool");
            print(engine::inspect_package(request["package"],request.value("repair",false)).dump());return 0;
        }
        if(values.count("--target-check")) {
            J request=J::parse(read_file(wide(values["--target-check"])));
            require(request.is_object() && request.size()==2 && request.count("ir") && request.count("language"),"target request requires ir/language");
            auto original=engine::Graph::read(request["ir"]);S language=request["language"].get<S>();auto target=engine::lower_target(original,language);
            auto tables=target.tables();require(tables==original.tables(),"target mismatch");
            print(J({{"ir",target.json()},{"tables",tables},{"metrics",engine::target_metrics(target,language)},{"verified",true}}).dump());return 0;
        }
        if(values.count("--search-check")) {
            J request=J::parse(read_file(wide(values["--search-check"])));
            require(request.is_object() && request.count("spec") && request.count("stage"),"search request requires spec/stage");
            for(auto i=request.begin();i!=request.end();++i)require(i.key()=="spec" || i.key()=="stage" || i.key()=="options","unknown search request field");
            require(request["stage"].is_number_integer() && request["stage"].get<int64_t>()>=5 && request["stage"].get<uint64_t>()<=10,"stage must be integer 5..10");
            auto spec=parse(request["spec"]);engine::Problem problem(spec.truth);
            for(auto &r:spec.rules)if(r.output)problem.original_on.push_back({r.care,r.bits});
            print(engine::search(problem,request["stage"].get<int>(),engine::Options::read(request.value("options",J::object()))).dump());return 0;
        }
        if(values.count("--ir-check")) {
            auto graph=engine::Graph::read(J::parse(read_file(wide(values["--ir-check"]))));
            print(J({{"ir",graph.json()},{"tables",graph.tables()},{"fanout",graph.fanout()}}).dump());return 0;
        }
        if(values.count("--engine-check")) {
            J document=J::parse(read_file(wide(values["--engine-check"])));
            engine::validate_contract(document);
            if(document["kind"]=="ir")engine::Graph::read(document);
            if(document["kind"]=="candidate")engine::Graph::read(document["ir"]);
            print(document.dump());
            return 0;
        }
        S command;
        for (auto a :
             {"--legacy-ir", "--truth", "--generate", "--validate", "--evaluate", "--save-xlsx", "--expand", "--compact"})
            if (values.count(a))
                command = a;
        auto book = load(wide(values[command]), values["--sheet"]);
        Spec s = parse(book.state);
        if(command=="--legacy-ir") {
            auto plan=compile(s);auto graph=engine::Graph::legacy(s.inputs.size(),plan.nodes,plan.root);
            print(J({{"ir",graph.json()},{"tables",graph.tables()},{"reference",s.truth.json()}}).dump());return 0;
        }
        if(command=="--truth") {
            auto support=engine::support(s.truth);
            print(J({{"spec",canonical(s)},{"truth",s.truth.json()},
                     {"local_to_external",support.local_to_external},{"external_to_local",support.external_to_local}}).dump());
            return 0;
        }
        if (command == "--evaluate") {
            require(values.count("--bits"), u8"Укажите --bits.");
            S text = values["--bits"];
            size_t used = 0;
            auto bits = std::stoull(text, &used);
            require(!text.empty() && text[0] != '-' && used == text.size() && bits <= 0xffffffffULL,
                    u8"--bits: целое число 0…4294967295.");
            bool value = evaluate(s, bits);
            print(J({{s.output, value}, {s.inverted, !value}}).dump());
            return 0;
        }
        if (command == "--validate") {
            print(J({{"inputs", s.inputs.size()},
                     {"rules", s.rules.size()},
                     {"representation",s.dense?"dense":"rules"},
                     {"values",1U<<s.inputs.size()},
                     {"valid",true}})
                      .dump());
            return 0;
        }
        U output = values.count("--output") && !values["--output"].empty()
                       ? wide(values["--output"])
                       : ChangeFileExt(wide(values[command]), "");
        if (command != "--generate") {
            require(values.count("--output") && lower(utf8(ExtractFileExt(output))) == ".xlsx",
                    u8"Укажите --output книга.xlsx.");
            require(ExpandFileName(output).LowerCase() !=
                        ExpandFileName(wide(values[command])).LowerCase(),
                    u8"Для преобразования укажите другой файл.");
            J state = command == "--expand"    ? expand(book.state)
                      : command == "--compact" ? compact(book.state)
                                               : book.state;
            save(output, state);
            print(utf8(output));
            return 0;
        }
        S name = values.count("--name") ? values["--name"] : "Boolean";
        if(values["--engine"]=="strategy") {
            J policy=values.count("--policy")?J::parse(read_file(wide(values["--policy"]))):engine::default_policy();
            engine::validate_contract(policy);require(policy.at("kind")=="policy","policy document required");
            if(values.count("--max-macro-levels"))policy["export"]["max_macro_levels"]=std::stoi(values["--max-macro-levels"]);
            U base=ChangeFileExt(ExpandFileName(output),"");
            U report=values.count("--report")?ExpandFileName(wide(values["--report"])):U();
            std::vector<U> destinations{base+".tple",base+".fbst"};
            if(!report.IsEmpty()) {
                destinations.push_back(base+".st");
                destinations.push_back(report);
            }
            for(auto source:{values[command],values["--policy"]})if(!source.empty()) {
                U original=ExpandFileName(wide(source)).LowerCase();
                for(const U &destination:destinations)require(original!=destination.LowerCase(),"output would overwrite source/policy file");
            }
            print(generate_strategy(book.state,output,name,!flags.count("--st") || flags.count("--fbd"),!flags.count("--fbd") || flags.count("--st"),policy,report).dump(2));return 0;
        }
        print(generate(book.state, output, name, !flags.count("--st") || flags.count("--fbd"),
                       !flags.count("--fbd") || flags.count("--st"))
                  .dump(2));
        return 0;
    } catch (const Exception &e) {
        print(u8"Ошибка: " + utf8(e.Message), true);
        return 1;
    } catch (std::exception &e) {
        print(u8"Ошибка: " + S(e.what()), true);
        return 1;
    }
}
} // namespace b2f
