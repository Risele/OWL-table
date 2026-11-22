#include <vcl.h>
#pragma hdrstop
#include "verification/native.hpp"
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
namespace b2f {
S sha256(const S &data) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    require(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT),
            "SHA256 provider unavailable");
    try {
        require(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash),
                "SHA256 initialization failed");
        require(CryptHashData(hash, reinterpret_cast<const BYTE *>(data.data()), data.size(), 0),
                "SHA256 update failed");
        BYTE digest[32];
        DWORD size = 32;
        require(CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32,
                "SHA256 finalization failed");
        const char *hex = "0123456789abcdef";
        S out;
        for (auto b : digest) {
            out += hex[b >> 4];
            out += hex[b & 15];
        }
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return out;
    } catch (...) {
        if (hash)
            CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        throw;
    }
}
J verify_export(const Spec &spec, const S &language, const U &archive, const S &sourceBytes) {
    require(language == "ST" || language == "FBD", "unknown artifact language");
    S archiveBytes = read_file(archive);
    J docs = J::parse(zip_read(archive, "Template")),
      meta = J::parse(zip_read(archive, "MetaData"));
    require(zip_read(archive, "MinRequiredVersion") == "2.0", "unsupported archive version");
    J package;
    if (language == "ST") {
        package = read_st_artifact(docs, meta, sourceBytes);
        require(meta.at("InputVariableNames") == J(spec.inputs) &&
                    meta.at("OutputVariableNames") == J::array({spec.output, spec.inverted}),
                "ST public port order differs from spec");
    } else {
        package = read_fbd_artifact(docs, meta);
        J inputs = J::array(), outputs = J::array();
        for (auto &p : meta.at("MetaDataInputs"))
            inputs.push_back(p.at("Name"));
        for (auto &p : meta.at("MetaDataOutputs"))
            outputs.push_back(p.at("Name"));
        require(inputs == J(spec.inputs) && outputs == J::array({spec.output, spec.inverted}),
                "FBD public port order differs from spec");
    }
    auto checked = engine::check_package(package);
    auto tables = checked.expanded.tables();
    require(tables.size() == 2 && tables[0].at("type") == "BOOL" && tables[1].at("type") == "BOOL",
            "Boolean artifact outputs required");
    S truth;
    unsigned count = 1U << spec.inputs.size();
    J counterexample = nullptr;
    for (unsigned k = 0; k < count; ++k) {
        bool reference = evaluate(spec, k);
        truth += reference ? '1' : '0';
        bool q = tables[0].at("values")[k].get<unsigned>() != 0,
             qn = tables[1].at("values")[k].get<unsigned>() != 0;
        if ((q != reference || qn == reference) && counterexample.is_null())
            counterexample = {{"input_index", k},
                              {"expected", J::array({reference, !reference})},
                              {"actual", J::array({q, qn})}};
    }
    J proof = {{"schema_version", 1},
               {"language", language},
               {"verified", counterexample.is_null()},
               {"reference_hash", sha256(truth)},
               {"artifact_hash", sha256(archiveBytes + sourceBytes)},
               {"archive_hash", sha256(archiveBytes)},
               {"source_hash", language == "ST" ? J(sha256(sourceBytes)) : J(nullptr)},
               {"assignments_checked", count},
               {"counterexample", counterexample},
               {"height", checked.report.at("height")},
               {"definition_count", checked.report.at("definition_count")}};
    return {{"proof", proof}, {"ir", checked.expanded.json()}, {"package", package}};
}
J verify_export_request(const J &r) {
    for (auto i = r.begin(); i != r.end(); ++i)
        require(i.key() == "spec" || i.key() == "language" || i.key() == "archive" ||
                    i.key() == "source",
                "unknown verify request field");
    S source;
    if (r.at("language") == "ST") {
        require(!r.value("source", S()).empty(), "standalone ST text required");
        source = read_file(wide(r.at("source")));
    }
    return verify_export(parse(r.at("spec")), r.at("language"), wide(r.at("archive")), source);
}
} // namespace b2f
