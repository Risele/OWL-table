#include <vcl.h>
#pragma hdrstop
#include "macro_document.hpp"
#include <System.NetEncoding.hpp>
#include <System.ZLib.hpp>
#include <System.Hash.hpp>
#include <System.Zip.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace t2f {
namespace {
constexpr unsigned maxDocument = 16 * 1024 * 1024;
constexpr unsigned partWidth = 192;
constexpr const char *bad = u8"Не удалось распознать макрос";
S documentHeading(bool st) {
    return S(st ? "// " : "") + u8"Сгенерировано OWL-Tables";
}
// A trailer owns the line break immediately before its first line.
size_t beforeLine(const S &text, size_t start) {
    if (start && text[start - 1] == '\n') --start;
    if (start && text[start - 1] == '\r') --start;
    return start;
}
S armor(const S &bytes) {
    return utf8(System::Netencoding::TNetEncoding::Base64URL->EncodeBytesToString(
        static_cast<const void *>(bytes.data()), int(bytes.size())));
}
S decode(const S &text) {
    auto bytes = System::Netencoding::TNetEncoding::Base64URL->DecodeStringToBytes(wide(text));
    S result = bytes.Length ? S(reinterpret_cast<const char *>(&bytes[0]), bytes.Length) : S();
    require(armor(result) == text, bad);
    return result;
}
unsigned crc(const S &s) { return System::Zlib::crc32(0, (System::PByte)s.data(), s.size()); }
J &root(J &docs, const J &meta) {
    if (docs.is_object())
        return docs.at(meta.at("ComponentMetaData").at("UniqueId").get<S>());
    for (auto &d : docs)
        if (d.at("Id") == meta.at("MetaData").at("FunctionBlockUniqueId"))
            return d;
    throw std::runtime_error(bad);
}
J &carrier(J &d, bool st) {
    return st ? d.at("SourceCode") : d.at("MetaData").at("ComponentMetaData").at("Description");
}
// Remove presentation and revision fields before comparing the imported package.
void stripPresentation(J &v) {
    if (v.is_object()) {
        for (auto key : {"Location",
                         "Bounds",
                         "WorkRectangle",
                         "Primitives",
                         "Points",
                         "ZOrder",
                         "FontSize",
                         "ModificationDateTime",
                         "CreationDateTime",
                         "ModificationId",
                         "LastUpdatingId",
                         "ModificationLabel",
                         "LastModificationId",
                         "CommentBlockModels",
                         "Comment",
                         "Description",
                         "Descriptor",
                         "Image",
                         "StartPointX",
                         "StartPointY",
                         "EndPointX",
                         "EndPointY",
                         "TextOffset",
                         "Size"})
            v.erase(key);
    }
    if (v.is_structured())
        for (auto &child : v)
            stripPresentation(child);
    // OwenLogic may write the same integral REAL constant as 1 or 1.0.
    if (v.is_number_float()) {
        double x = v.get<double>();
        if (std::abs(x) < 9007199254740992.0 && std::trunc(x) == x)
            v = static_cast<int64_t>(x);
    }
}
S fingerprint(J docs) {
    if (docs.is_array()) {
        auto &blocks = docs.get_ref<J::array_t &>();
        std::sort(blocks.begin(), blocks.end(),
                  [](const J &a, const J &b) { return a.at("Id") < b.at("Id"); });
    }
    stripPresentation(docs);
    return utf8(System::Hash::THashSHA2::GetHashString(wide(docs.dump()))).substr(0, 32);
}
J pack(const MacroDocument &d) {
    J state = d.state;
    if (d.mode == "Bool" && state.contains("truth")) {
        const S &truth = state.at("truth").get_ref<const S &>();
        S bits((truth.size() + 7) / 8, 0);
        for (size_t i = 0; i < truth.size(); ++i)
            if (truth[i] == '1')
                bits[i / 8] = char(static_cast<unsigned char>(bits[i / 8]) | (1u << (i % 8)));
        state["bits"] = armor(bits);
        state.erase("truth");
    }
    return J::array({d.mode, d.name, d.settings, state});
}
void restoreBits(J &state) {
    if (!state.contains("bits"))
        return;
    size_t n = state.at("inputs").size();
    require(n >= 1 && n <= 16 && !state.contains("truth"), bad);
    size_t count = size_t(1) << n;
    S bits = decode(state.at("bits")), truth(count, '0');
    require(bits.size() == (count + 7) / 8, bad);
    for (size_t i = 0; i < count; ++i)
        if ((static_cast<unsigned char>(bits[i / 8]) >> (i % 8)) & 1)
            truth[i] = '1';
    state.erase("bits");
    state["truth"] = std::move(truth);
}
S checkedEntry(System::Zip::TZipFile *zip, const U &name) {
    int index = zip->IndexOf(name);
    require(index >= 0 && zip->FileInfo[index].UncompressedSize <= 64u * 1024 * 1024, bad);
    System::DynamicArray<System::Byte> bytes;
    zip->Read(index, bytes);
    return bytes.Length ? S(reinterpret_cast<const char *>(&bytes[0]), bytes.Length) : S();
}
} // namespace

void embedDocument(J &docs, J &meta, const MacroDocument &document) {
    bool st = docs.is_array();
    if (st) {
        auto &blocks = docs.get_ref<J::array_t &>();
        auto it = std::find_if(blocks.begin(), blocks.end(), [&](const J &block) {
            return block.at("Id") == meta.at("MetaData").at("FunctionBlockUniqueId");
        });
        require(it != blocks.end(), u8"Не найден корневой блок ST.");
        std::rotate(it, it + 1, blocks.end());
    }
    auto &field = carrier(root(docs, meta), st);
    S raw = pack(document).dump();
    require(raw.size() <= maxDocument, u8"Документ превышает 16 МиБ до сжатия.");
    unsigned size = System::Zlib::compressBound(raw.size());
    S compressed(size, 0);
    require(System::Zlib::compress2((System::PByte)&compressed[0], size, (System::PByte)raw.data(),
                                    raw.size(), 9) == 0,
            u8"Не удалось сжать документ.");
    compressed.resize(size);
    bool useZip = size < raw.size();
    S data = armor(useZip ? compressed : raw);
    unsigned parts = (data.size() + partWidth - 1) / partWidth;
    S prefix = st ? "// LWB1 " : "LWB1 ";
    S lines = documentHeading(st) + "\r\n" + prefix + (useZip ? "Z " : "R ") + std::to_string(raw.size()) + " " +
              std::to_string(crc(raw)) + " " + fingerprint(docs) + " " + std::to_string(parts);
    for (unsigned i = 0; i < parts; ++i)
        lines += "\r\n" + prefix + std::to_string(i + 1) + "/" + std::to_string(parts) + " " +
                 data.substr(i * partWidth, partWidth);
    auto &text = field.get_ref<S &>();
    // A separate newline belongs to the trailer, so removing it restores the exact source.
    if (!st) {
        size_t bytes = text.size() + 2 + lines.size();
        require(bytes <= 65536, u8"Описание FBD со встроенной таблицей занимает " +
                                    std::to_string(bytes) + u8" байт. Предел генератора — 65536 байт.");
    }
    text += "\r\n" + lines + (st ? "\r\n" : "");
    auto &d = root(docs, meta);
    if (st) {
        S revision = uuid5(d.at("Id"), text);
        d["ModificationLabel"] = d["LastModificationId"] = revision;
        meta["ModificationLabel"] = revision;
    } else {
        auto &component = d["MetaData"]["ComponentMetaData"];
        S revision = uuid5(component.at("ModificationId"), lines);
        d["LastUpdatingId"] = component["ModificationId"] = revision;
        meta["ComponentMetaData"]["ModificationId"] = revision;
        meta["ComponentMetaData"]["Description"] = text;
    }
}

MacroDocument readMacroDocument(const U &file) try {
    std::unique_ptr<System::Zip::TZipFile> zip(new System::Zip::TZipFile);
    zip->Open(file, System::Zip::zmRead);
    J docs = J::parse(checkedEntry(zip.get(), "Template")),
      meta = J::parse(checkedEntry(zip.get(), "MetaData"));
    bool st = docs.is_array();
    auto &field = carrier(root(docs, meta), st);
    S text = field.is_string() ? field.get<S>() : S();
    S prefix = st ? "// LWB1 " : "LWB1 ";
    size_t header = 0;
    if (text.compare(0, prefix.size(), prefix) != 0) {
        header = text.find("\n" + prefix);
        require(header != S::npos, bad);
        ++header;
    }
    auto end = text.find('\n', header);
    require(end != S::npos, bad);
    auto fields = header + prefix.size();
    std::istringstream head(text.substr(fields, end - fields));
    char codec;
    unsigned length, checksum, parts;
    S digest, extra;
    require(bool(head >> codec >> length >> checksum >> digest >> parts) && !(head >> extra) &&
                (codec == 'Z' || codec == 'R') && length > 0 && length <= maxDocument && parts > 0 &&
                parts <= ((maxDocument * 2) / partWidth + 1) && digest.size() == 32,
            bad);
    S data;
    size_t pos = end + 1;
    for (unsigned i = 0; i < parts; ++i) {
        S tag = prefix + std::to_string(i + 1) + "/" + std::to_string(parts) + " ";
        require(text.compare(pos, tag.size(), tag) == 0, bad);
        pos += tag.size();
        end = text.find_first_of("\r\n", pos);
        if (end == S::npos)
            end = text.size();
        require(end > pos && end - pos <= partWidth && (i + 1 == parts || end - pos == partWidth), bad);
        data.append(text, pos, end - pos);
        pos = end;
        if (pos < text.size() && text[pos] == '\r')
            ++pos;
        if (pos < text.size() && text[pos] == '\n')
            ++pos;
    }
    require(text.compare(pos, prefix.size(), prefix) != 0, bad);
    S bytes = decode(data);
    if (codec == 'Z') {
        S raw(length, 0);
        unsigned n = length;
        require(System::Zlib::uncompress((System::PByte)&raw[0], n, (System::PByte)bytes.data(),
                                         bytes.size()) == 0 &&
                    n == length,
                bad);
        bytes.swap(raw);
    }
    require(bytes.size() == length && crc(bytes) == checksum, bad);
    J payload = J::parse(bytes);
    require(payload.is_array() && payload.size() == 4 && payload[2].is_object() &&
                payload[3].is_object(),
            bad);
    MacroDocument result{payload[0], payload[1], payload[3], payload[2]};
    if (result.mode == "Bool")
        restoreBits(result.state);
    size_t start = beforeLine(text, header);
    const S heading = documentHeading(st);
    if (start >= heading.size()) {
        const size_t line = start - heading.size();
        if ((line == 0 || text[line - 1] == '\n') && text.compare(line, heading.size(), heading) == 0)
            start = beforeLine(text, line);
    }
    field = text.substr(0, start) + text.substr(pos);
    result.logicChanged = fingerprint(docs) != digest;
    return result;
} catch (const J::exception &) {
    throw std::runtime_error(bad);
} catch (const System::Zip::EZipFileNotFoundException &) {
    throw;
} catch (const System::Zip::EZipException &) {
    throw std::runtime_error(bad);
} catch (const System::Sysutils::EConvertError &) {
    throw std::runtime_error(bad);
}
} // namespace t2f
