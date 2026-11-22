#include <vcl.h>
#pragma hdrstop
#include "../cpp/macro_document.hpp"
#include "engine/export.hpp"
#include "engine/control.hpp"
#include "engine/cache.hpp"
#include "engine/synthesis.hpp"
#include "verification/native.hpp"
#include <algorithm>
namespace b2f {
namespace {
using engine::Graph;
struct Choice {
    J package, report;
};
Choice choose(const J &target, const J &search, const J &policy, engine::SearchBudget &budget,
              const Spec &spec, const S &name) {
    S language = target.at("language");
    J alternatives = J::array(), rejected = target.at("rejected");
    std::vector<J> packages;
    std::vector<std::vector<uint64_t>> ranks;
    unsigned levels = policy.at("export").at("max_macro_levels"),
             maxNodes = policy.at("search").at("max_ir_nodes");
    for (auto &candidate : target.at("candidates")) {
        unsigned source = candidate.at("source_candidate");
        const auto &origin = search.at("candidates").at(source);
        Graph graph = Graph::read(candidate.at("ir"));
        unsigned bytes = candidate.at("metrics").at("table_bytes");
        if (graph.nodes().size() > maxNodes) {
            rejected.push_back(
                {{"source_candidate", source}, {"reason", "max_ir_nodes after target lowering"}});
            continue;
        }
        J variants = J::array();
        auto flat = engine::inspect_package(engine::flat_package(graph, language, 0, bytes));
        flat.erase("tables");
        flat["method"] = "inline";
        variants.push_back(flat);
        if (levels > 1) {
            // One deterministic packaging unit per potential node in four variants.
            uint64_t work = 4 * graph.nodes().size() + 1;
            if (work <= budget.limit - budget.used) {
                budget.spend(work);
                auto packed = engine::package_search(graph, language, 0, bytes);
                for (size_t i = 1; i < packed.at("variants").size(); ++i)
                    variants.push_back(packed.at("variants")[i]);
                for (auto &r : packed.at("rejected"))
                    rejected.push_back({{"source_candidate", source}, {"packaging", r}});
            } else
                rejected.push_back({{"source_candidate", source},
                                    {"reason", "packaging work_limit; verified inline retained"}});
        }
        for (auto &v : variants) {
            const auto &m = v.at("metrics");
            if (m.at("height").get<unsigned>() > levels) {
                rejected.push_back({{"source_candidate", source},
                                    {"package_method", v.at("method")},
                                    {"reason", "max_macro_levels"}});
                continue;
            }
            size_t storedNodes = 0;
            for (auto &d : v.at("package").at("definitions"))
                storedNodes += d.at("nodes").size();
            if (storedNodes > maxNodes ||
                (language == "FBD" && m.at("max_definition_elements").get<unsigned>() > 20000)) {
                rejected.push_back({{"source_candidate", source},
                                    {"package_method", v.at("method")},
                                    {"reason", "stored node or native canvas limit"}});
                continue;
            }
            alternatives.push_back({{"source_candidate", source},
                                    {"method", origin.at("method")},
                                    {"inverted_first", origin.at("inverted_first")},
                                    {"package_method", v.at("method")},
                                    {"metrics", m},
                                    {"target_metrics", candidate.at("metrics")},
                                    {"pareto", true}});
            packages.push_back(v.at("package"));
        }
    }
    require(!packages.empty(),
            language + " has no admissible candidate under the requested limits");
    J ranking = J::array({"expanded_operations"}), excluded = J::object();
    if (language == "ST") ranking.insert(ranking.begin(), "cortex_m0_cost");
    if (language == "ST")
        ranking.push_back("declared_local_bytes");
    ranking.push_back(language == "ST" ? "st_dependency_depth" : "dependency_depth");
    if (language == "FBD")
        ranking.push_back("max_canvas_elements");
    ranking.push_back("table_bytes");
    if (language == "ST")
        ranking.push_back("source_bytes");
    else
        excluded["export_bytes"] = "compressed ZIP bytes not measured for every alternative";
    ranking.push_back("stable candidate and variant order");
    for (size_t i = 0; i < alternatives.size(); ++i) {
        auto &a = alternatives[i];
        const auto &m = a.at("metrics"), &t = a.at("target_metrics");
        std::vector<uint64_t> rank = {m.at("expanded_operations")};
        if (language == "ST") {
            rank.insert(rank.begin(), m.at("cortex_m0_cost").get<uint64_t>());
            auto data = export_st(packages[i], name, spec.inputs, {spec.output, spec.inverted}, spec.input_comments);
            const auto &source=data.at("source").get_ref<const S &>();
            a["source_bytes"] = source.size();
            a["declared_local_bytes"] = data.at("declared_local_bytes");
            rank.push_back(a.at("declared_local_bytes"));
            // Reassociation can give different source DAGs exactly the same ST body.
            auto emitted=read_st_artifact(data.at("documents"), data.at("metadata"), source);
            auto checked=engine::check_package(emitted);
            a["st_dependency_depth"]=engine::logical_metrics(checked.expanded).at("depth");
            rank.push_back(a.at("st_dependency_depth"));
        } else
            rank.push_back(t.at("dependency_depth"));
        if (language == "FBD")
            rank.push_back(m.at("max_definition_elements"));
        rank.push_back(m.at("table_bytes"));
        if (language == "ST")
            rank.push_back(a.at("source_bytes"));
        a["rank"] = rank;
        ranks.push_back(rank);
    }
    size_t best = 0;
    for (size_t i = 0; i < ranks.size(); ++i) {
        if (ranks[i] < ranks[best])
            best = i;
        for (size_t j = 0; j < ranks.size(); ++j)
            if (i != j) {
                bool strict = false, dominates = true;
                for (size_t k = 0; k < ranks[i].size(); ++k) {
                    if (ranks[j][k] > ranks[i][k])
                        dominates = false;
                    if (ranks[j][k] < ranks[i][k])
                        strict = true;
                }
                if (dominates && strict) {
                    alternatives[i]["pareto"] = false;
                    break;
                }
            }
    }
    return {packages[best],
            {{"language", language},
             {"ranking", ranking},
             {"excluded_ranking_metrics", excluded},
             {"selected", best},
             {"alternatives", alternatives},
             {"rejected", rejected}}};
}
struct Pending {
    U destination, temporary, backup;
    bool saved = false, installed = false;
};
void install(std::vector<Pending> &files) {
    try {
        for (auto &f : files) {
            if (FileExists(f.destination)) {
                require(
                    MoveFileExW(f.destination.c_str(), f.backup.c_str(), MOVEFILE_WRITE_THROUGH),
                    "cannot back up output: " + utf8(f.destination));
                f.saved = true;
            }
            require(MoveFileExW(f.temporary.c_str(), f.destination.c_str(), MOVEFILE_WRITE_THROUGH),
                    "cannot install output: " + utf8(f.destination));
            f.installed = true;
        }
    } catch (...) {
        S failures;
        for (auto i = files.rbegin(); i != files.rend(); ++i) {
            if (i->saved) {
                if (!MoveFileExW(i->backup.c_str(), i->destination.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    failures += " recovery backup: " + utf8(i->backup);
            } else if (i->installed && !DeleteFileW(i->destination.c_str()))
                failures += " cannot remove: " + utf8(i->destination);
        }
        require(failures.empty(), "output rollback incomplete;" + failures);
        throw;
    }
    for (auto &f : files)
        if (f.saved)
            require(DeleteFileW(f.backup.c_str()),
                    "output installed; cannot remove backup: " + utf8(f.backup));
}
} // namespace
J generate_strategy(const J &state, const U &target, const S &name, bool fbd, bool st,
                    const J &policy, const U &reportPath) {
    engine::progress(1);
    engine::validate_contract(policy);
    require(policy.at("kind") == "policy", "policy contract required");
    Spec spec = parse(state, name);
    require(fbd || st, "select FBD and/or ST");
    require(spec.dense || spec.rules.size() <= policy.at("source").at("max_rules").get<unsigned>(),
            "policy source rule limit");
    S ext = lower(utf8(ExtractFileExt(target)));
    require(!target.Trim().IsEmpty() && (ext.empty() || ext == ".tple" || ext == ".fbst"),
            "output must be .tple/.fbst or a basename");
    U base = ChangeFileExt(ExpandFileName(target), "");
    bool diagnostics = !reportPath.IsEmpty();
    if (diagnostics)
        require(lower(utf8(ExtractFileExt(reportPath))) == ".json", "report must be a JSON file");
    uint64_t work = policy.at("search").at("work_limit");
    engine::Options options;
    options.work_limit = work - work / 5;
    options.max_nodes = std::min(100000U, policy.at("search").at("max_ir_nodes").get<unsigned>());
    options.max_candidates =
        std::min(1024U, policy.at("search").at("max_candidates").get<unsigned>());
    options.allow_full_lut = policy.at("export").at("allow_full_lut");
    options.table_bytes = std::min(options.allow_full_lut ? 8192U : 256U,
                                   policy.at("export").at("table_bytes").get<unsigned>());
    engine::Problem problem(spec.truth);
    for (auto &r : spec.rules)
        if (r.output)
            problem.original_on.push_back({r.care, r.bits});
    engine::progress(2);
    J cache;
    auto search = engine::cached_search(problem, options, engine::cache_enabled(), cache,
                                        policy.at("native_fbd"));
    engine::progress(3);
    engine::SearchBudget packaging(work / 5);
    std::map<S, Choice> choices;
    for (auto &t : search.at("targets"))
        if (t.at("language") == "ST" ? (st || diagnostics) : fbd)
            choices[t.at("language").get<S>()] = choose(t, search, policy, packaging, spec, name);
    J report = {{"schema_version", 1},
                {"kind", "generation_report"},
                {"engine", "strategy"},
                {"name", name},
                {"cache",cache},
                {"inputs", spec.inputs.size()},
                {"policy", policy},
                {"effective_limits",
                 {{"max_ir_nodes", options.max_nodes},
                  {"max_candidates", options.max_candidates},
                  {"table_bytes", options.table_bytes}}},
                {"search",
                 {{"backends", search.at("backends")},
                  {"synthesis_work_used", search.at("work_used")},
                  {"packaging_work_used", packaging.used},
                  {"work_limit", work},
                  {"candidates_retained", search.at("candidates").size()},
                  {"baseline_reserved", true},
                  {"verification_reserved", true}}},
                {"ranking", "compact_logic; per-language order and exclusions in targets"},
                {"targets", J::array()},
                {"proofs", J::array()},
                {"files", J::array()},
                {"device_measurements", nullptr}};
    for (auto &c : choices)
        report["targets"].push_back(c.second.report);
    static LONG ticket = 0;
    S token =
        uuid5(ns, std::to_string(GetCurrentProcessId()) + ":" + std::to_string(GetTickCount64()) +
                      ":" + std::to_string(InterlockedIncrement(&ticket)));
    std::vector<Pending> pending;
    std::vector<U> temporary;
    auto stage = [&](const U &destination) {
        U absolute = ExpandFileName(destination);
        for (auto &p : pending)
            require(p.destination.LowerCase() != absolute.LowerCase(), "duplicate output path");
        require(!DirectoryExists(absolute), "output path is a directory: " + utf8(absolute));
        require(ForceDirectories(ExtractFileDir(absolute)) ||
                    DirectoryExists(ExtractFileDir(absolute)),
                "cannot create output directory");
        Pending p;
        p.destination = absolute;
        p.temporary = absolute + wide("." + token + ".tmp");
        p.backup = absolute + wide("." + token + ".bak");
        require(!FileExists(p.temporary) && !FileExists(p.backup), "staging name collision");
        pending.push_back(p);
        temporary.push_back(p.temporary);
        report["files"].push_back(utf8(absolute));
        return p.temporary;
    };
    try {
        engine::progress(4);
        MacroDocument document{"Bool",name,state,{{"fbd",fbd},{"st",st}}};
        S source;
        U fbdTemp, stTemp, reportTemp;
        if (fbd) {
            auto data = export_fbd(choices.at("FBD").package, name, spec.inputs,
                                   {spec.output, spec.inverted}, spec.input_comments);
            embedDocument(data["documents"],data["metadata"],document);
            fbdTemp = stage(base + ".tple");
            zip_write(fbdTemp, {{"Template", data.at("documents").dump(2)},
                                {"MetaData", data.at("metadata").dump(2)},
                                {"MinRequiredVersion", "2.0"}});
        }
        if (st || diagnostics) {
            auto stData = export_st(choices.at("ST").package, name, spec.inputs,
                                    {spec.output, spec.inverted}, spec.input_comments);
            embedDocument(stData["documents"],stData["metadata"],document);
            for (const auto &d : stData.at("documents"))
                source += d.at("SourceCode").get_ref<const S&>();
            if (st) {
                stTemp = stage(base + ".fbst");
            } else {
                stTemp = base + wide("." + token + ".proof.fbst");
                require(!FileExists(stTemp), "proof staging collision");
                temporary.push_back(stTemp);
            }
            zip_write(stTemp, {{"Template", stData.at("documents").dump(2)},
                               {"MetaData", stData.at("metadata").dump(2)},
                               {"MinRequiredVersion", "2.0"}});
        }
        if (diagnostics) {
            U sourceTemp = stage(base + ".st");
            write_file(sourceTemp, source);
            source = read_file(sourceTemp);
            reportTemp = stage(reportPath);
        }
        auto prove = [&](S language, const U &path, const S &listing) {
            auto proof = verify_export(spec, language, path, listing).at("proof");
            require(proof.at("verified").get<bool>(),
                    "export counterexample: " + proof.at("counterexample").dump());
            require(proof.at("height").get<unsigned>() <=
                        policy.at("export").at("max_macro_levels").get<unsigned>(),
                    "artifact exceeds requested macro height");
            report["proofs"].push_back(proof);
        };
        engine::progress(5);
        if (fbd)
            prove("FBD", fbdTemp, "");
        if (st || diagnostics)
            prove("ST", stTemp, source);
        if (diagnostics) {
            write_file(reportTemp, report.dump(2) + "\n");
            require(J::parse(read_file(reportTemp)) == report, "report write verification failed");
        }
        engine::progress(6);
        install(pending);
        engine::progress(7);
    } catch (...) {
        for (auto &p : temporary)
            DeleteFileW(p.c_str());
        throw;
    }
    for (auto &p : temporary)
        DeleteFileW(p.c_str());
    return report;
}
} // namespace b2f
