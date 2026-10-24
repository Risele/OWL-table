#include <vcl.h>
#pragma hdrstop
#include "core.hpp"
#include <System.Zip.hpp>
#include <System.Hash.hpp>
#include <System.Character.hpp>
#include <System.IOUtils.hpp>
#include <System.DateUtils.hpp>
#include "vendor/pugixml.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <memory>
#include <fstream>
namespace t2f {
U wide(const S &s) { return U(UTF8String(s.c_str())); }
S utf8(const U &s) {
    UTF8String x(s);
    return S(x.c_str(), x.Length());
}
S lower(const S &s) { return utf8(wide(s).LowerCase()); }
S trim(const S &s) { return utf8(wide(s).Trim()); }
void require(bool ok, const S &msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
S identifier(const S &value) {
    S s = trim(value);
    U u = wide(s);
    require(u.Length() > 0 && u.Length() <= 64, u8"Имя должно содержать от 1 до 64 знаков.");
    for (int i = 1; i <= u.Length(); ++i)
        require(u[i] == '_' || System::Character::TCharacter::IsLetter(u[i]) ||
                    (i > 1 && System::Character::TCharacter::IsDigit(u[i])),
                u8"Недопустимое имя: " + s);
    return s;
}
S number(double x, int precision) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::setprecision(precision) << x;
    return s.str();
}
S read_file(const U &path) {
    std::unique_ptr<TFileStream> f(new TFileStream(path, fmOpenRead | fmShareDenyWrite));
    S s((size_t)f->Size, '\0');
    if (!s.empty())
        f->ReadBuffer(&s[0], s.size());
    return s;
}
void write_file(const U &path, const S &s) {
    std::unique_ptr<TFileStream> f(new TFileStream(path, fmCreate));
    if (!s.empty())
        f->WriteBuffer(s.data(), s.size());
}
S zip_read(const U &path, const S &entry) {
    std::unique_ptr<System::Zip::TZipFile> z(new System::Zip::TZipFile());
    z->Open(path, System::Zip::zmRead);
    System::DynamicArray<System::Byte> b;
    z->Read(wide(entry), b);
    return b.Length ? S((char *)&b[0], b.Length) : S();
}
void zip_write(const U &path, const std::map<S, S> &files) {
    std::unique_ptr<System::Zip::TZipFile> z(new System::Zip::TZipFile());
    z->Open(path, System::Zip::zmWrite);
    for (auto &kv : files) {
        System::DynamicArray<System::Byte> b;
        b.Length = kv.second.size();
        if (b.Length)
            memcpy(&b[0], kv.second.data(), b.Length);
        z->Add(b, wide(kv.first));
    }
    z->Close();
}
S uuid5(const S &space, const S &name) {
    S bytes;
    for (size_t i = 0; i < space.size();) {
        if (space[i] == '-') {
            ++i;
            continue;
        }
        bytes.push_back((char)std::stoul(space.substr(i, 2), nullptr, 16));
        i += 2;
    }
    bytes += name;
    std::unique_ptr<TMemoryStream> stream(new TMemoryStream());
    stream->WriteBuffer(bytes.data(), bytes.size());
    stream->Position = 0;
    auto hash = System::Hash::THashSHA1::GetHashBytes(stream.get());
    hash[6] = (hash[6] & 15) | 80;
    hash[8] = (hash[8] & 63) | 128;
    const char *hex = "0123456789abcdef";
    S out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        out += hex[hash[i] >> 4];
        out += hex[hash[i] & 15];
    }
    return out;
}
S now_iso(S *local_text) {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char buf[40];
    if (local_text) {
        SYSTEMTIME local;
        require(SystemTimeToTzSpecificLocalTime(nullptr, &t, &local) != 0,
                u8"Не удалось определить местное время ПК.");
        sprintf(buf, "%02d.%02d.%04d %02d:%02d:%02d", local.wDay, local.wMonth, local.wYear,
                local.wHour, local.wMinute, local.wSecond);
        *local_text = buf;
    }
    sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
            t.wSecond);
    return buf;
}
S xml_escape(const S &s) {
    S r;
    for (char c : s) {
        require((unsigned char)c >= 32 || c == '\t' || c == '\n' || c == '\r',
                u8"Недопустимый управляющий знак в тексте Excel.");
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        default:
            r += c;
        }
    }
    return r;
}
static S local(const char *n) {
    S s = n;
    auto p = s.find(':');
    return p == S::npos ? s : s.substr(p + 1);
}
static pugi::xml_node child(pugi::xml_node n, const char *key) {
    for (auto c : n.children())
        if (local(c.name()) == key)
            return c;
    return {};
}
static S attribute(pugi::xml_node n, const char *key) {
    for (auto a : n.attributes())
        if (local(a.name()) == key)
            return a.value();
    return {};
}
static S texts(pugi::xml_node n) {
    S s;
    for (auto c : n.children()) {
        if (local(c.name()) == "t")
            s += c.child_value();
        else
            s += texts(c);
    }
    return s;
}
static void parse_xml(pugi::xml_document &d, const S &s) {
    auto r = d.load_buffer(s.data(), s.size());
    require(r, u8"Повреждён XML в книге Excel.");
}
static S cell_name(int col, int row) {
    S s;
    for (++col; col; col = (col - 1) / 26)
        s = char('A' + (col - 1) % 26) + s;
    return s + std::to_string(row);
}
static double numeric(S s) {
    s = trim(s);
    std::replace(s.begin(), s.end(), ',', '.');
    size_t used = 0;
    double v;
    try {
        v = std::stod(s, &used);
    } catch (...) {
        throw std::runtime_error(u8"Требуется числовое значение: " + s);
    }
    require(used == s.size() && std::isfinite(v), u8"Требуется конечное число: " + s);
    return v;
}
Book read_book(const U &path, const S &selected) {
    require(lower(utf8(ExtractFileExt(path))) == ".xlsx", u8"Поддерживается только .xlsx.");
    pugi::xml_document rel, book;
    parse_xml(rel, zip_read(path, "xl/_rels/workbook.xml.rels"));
    parse_xml(book, zip_read(path, "xl/workbook.xml"));
    std::map<S, S> targets;
    for (auto r : rel.document_element().children())
        targets[attribute(r, "Id")] = attribute(r, "Target");
    Book result;
    std::map<S, S> sheets;
    for (auto sh : child(book.document_element(), "sheets").children()) {
        S name = attribute(sh, "name"), target = targets[attribute(sh, "id")];
        if (target.empty())
            throw std::runtime_error(u8"Не найден лист.");
        if (target[0] == '/')
            target.erase(0, 1);
        else
            target = "xl/" + target;
        std::vector<S> parts;
        std::istringstream in(target);
        S part;
        while (std::getline(in, part, '/')) {
            if (part == "..") {
                require(!parts.empty(), u8"Неверный путь листа.");
                parts.pop_back();
            } else if (part != ".")
                parts.push_back(part);
        }
        target.clear();
        for (auto &p : parts) {
            if (!target.empty())
                target += '/';
            target += p;
        }
        sheets[name] = target;
        result.sheets.push_back(name);
    }
    require(!result.sheets.empty(), u8"Книга не содержит листов.");
    result.sheet =
        selected.empty() ? (sheets.count(u8"Данные") ? u8"Данные" : result.sheets[0]) : selected;
    require(sheets.count(result.sheet), u8"Лист не найден: " + result.sheet);
    std::vector<S> strings;
    std::unique_ptr<System::Zip::TZipFile> z(new System::Zip::TZipFile());
    z->Open(path, System::Zip::zmRead);
    if (z->IndexOf("xl/sharedStrings.xml") >= 0) {
        pugi::xml_document ss;
        parse_xml(ss, zip_read(path, "xl/sharedStrings.xml"));
        for (auto si : ss.document_element().children())
            strings.push_back(texts(si));
    }
    z->Close();
    pugi::xml_document ws;
    parse_xml(ws, zip_read(path, sheets[result.sheet]));
    std::map<std::pair<int, int>, S> cells;
    std::set<int> columns, rows;
    for (auto row : child(ws.document_element(), "sheetData").children())
        for (auto c : row.children()) {
            S a = attribute(c, "r");
            int col = 0;
            size_t i = 0;
            while (i < a.size() && a[i] >= 'A' && a[i] <= 'Z')
                col = col * 26 + a[i++] - 'A' + 1;
            require(col > 0 && i < a.size(), u8"Неверный адрес ячейки.");
            int rn = std::stoi(a.substr(i));
            S type = attribute(c, "t");
            require(!child(c, "f") && type != "e",
                    result.sheet + "!" + a + u8": замените формулу или ошибку Excel значением.");
            S value = child(c, "v").child_value();
            if (type == "s") {
                size_t index = std::stoul(value);
                require(index < strings.size(), u8"Неверная строка Excel.");
                value = strings[index];
            } else if (type == "inlineStr")
                value = texts(c);
            else if (!value.empty() && (type.empty() || type == "n" || type == "b"))
                value = number(numeric(value), 17);
            if (value.empty())
                continue;
            cells[{rn, col}] = value;
            columns.insert(col);
            if (rn >= 4)
                rows.insert(rn);
        }
    result.state = {{"fields", J::array()}, {"records", J::array()}};
    for (int c : columns)
        result.state["fields"].push_back(J::array({cells[{1, c}], cells[{2, c}], cells[{3, c}]}));
    for (int r : rows) {
        J row = J::array();
        for (int c : columns)
            row.push_back(cells[{r, c}]);
        result.state["records"].push_back(row);
    }
    return result;
}
Table parse_table(const J &state) {
    Table t;
    t.rows = J::array();
    std::set<S> names = {"valid"};
    int idcount = 0, col = 0;
    for (auto &row : state.at("fields")) {
        require(row.size() == 3, u8"Поле должно иметь название, тип и имя.");
        Field f{trim(row[0].get<S>()), lower(trim(row[1].get<S>())), trim(row[2].get<S>()), ++col};
        require(!f.title.empty(), u8"Укажите название поля " + std::to_string(col));
        if (f.kind == "float")
            f.kind = "real";
        if (f.kind == u8"комментарий")
            f.kind = "comment";
        require(f.kind == "id" || f.kind == "comment" || f.kind == "real" || f.kind == "udint" ||
                    f.kind == "bool",
                u8"Неизвестный тип: " + f.kind);
        if (f.kind == "comment")
            require(f.name.empty(), u8"Для комментария оставьте имя пустым.");
        else {
            if (f.name.empty() && f.kind == "id")
                f.name = "ID";
            f.name = identifier(f.name);
            require(names.insert(lower(f.name)).second,
                    u8"Повторное или зарезервированное имя: " + f.name);
            t.titles[f.name] = f.title;
            if (f.kind == "id") {
                t.input = f.name;
                ++idcount;
            } else
                t.outputs.push_back({f.name, f.kind == "real" ? 4 : f.kind == "udint" ? 3 : 0});
        }
        t.fields.push_back(f);
    }
    require(idcount == 1, u8"Требуется ровно один ID.");
    require(!t.outputs.empty(), u8"Добавьте хотя бы один REAL, UDINT или BOOL.");
    t.outputs.push_back({"Valid", 0});
    std::set<uint32_t> keys;
    int rn = 3;
    for (auto &row : state.at("records")) {
        ++rn;
        require(row.size() == t.fields.size(), u8"Число значений отличается от числа полей.");
        bool empty = true;
        for (auto &v : row)
            if (!trim(v.get<S>()).empty())
                empty = false;
        if (empty)
            continue;
        J values = J::object(), labels = J::object();
        uint32_t key = 0;
        for (size_t i = 0; i < t.fields.size(); ++i) {
            auto &f = t.fields[i];
            S s = trim(row[i].get<S>());
            if (f.kind == "comment") {
                if (!s.empty())
                    labels[f.title + "_" + std::to_string(i + 1)] =
                        f.title + ": " + row[i].get<S>();
                continue;
            }
            S where = cell_name(i, rn) + " (" + f.title + "): ";
            double v;
            try {
                S folded = lower(s);
                v = f.kind == "bool" && (folded == "true" || folded == u8"истина")  ? 1
                    : f.kind == "bool" && (folded == "false" || folded == u8"ложь") ? 0
                                                                                    : numeric(s);
            } catch (std::exception &e) {
                throw std::runtime_error(where + e.what());
            }
            if (f.kind == "id" || f.kind == "udint") {
                require(v >= 0 && v <= 4294967295.0 && v == std::floor(v),
                        where + u8"Требуется целое 0..4294967295.");
                if (f.kind == "id")
                    key = (uint32_t)v;
                else
                    values[f.name] = (uint32_t)v;
            } else if (f.kind == "bool") {
                require(v == 0 || v == 1, where + u8"BOOL: только 0 и 1.");
                values[f.name] = (v != 0);
            } else {
                float real = (float)v;
                require(std::isfinite(real) && (v == 0 || real != 0),
                        where + u8"Число не представимо REAL.");
                values[f.name] = real;
            }
        }
        require(keys.insert(key).second, u8"Повторный ID: " + std::to_string(key));
        t.rows.push_back({{"key", key}, {"values", values}, {"labels", labels}});
    }
    require(!t.rows.empty(), u8"Нет записей.");
    std::sort(t.rows.begin(), t.rows.end(), [](const J &a, const J &b) {
        return a["key"].get<uint32_t>() < b["key"].get<uint32_t>();
    });
    return t;
}
void write_book(const U &path, const J &state) {
    auto t = parse_table(state);
    require(lower(utf8(ExtractFileExt(path))) == ".xlsx", u8"Требуется расширение .xlsx.");
    S xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"3\" topLeftCell=\"A4\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData>";
    J matrix = J::array();
    for (int h = 0; h < 3; ++h) {
        J row = J::array();
        for (auto &f : state["fields"])
            row.push_back(f[h]);
        matrix.push_back(row);
    }
    for (auto &r : state["records"]) {
        bool empty = true;
        for (auto &v : r)
            if (!trim(v.get<S>()).empty())
                empty = false;
        if (!empty)
            matrix.push_back(r);
    }
    int rn = 0;
    for (auto &r : matrix) {
        ++rn;
        xml += "<row r=\"" + std::to_string(rn) + "\">";
        for (size_t c = 0; c < r.size(); ++c) {
            S value = r[c];
            S a = cell_name(c, rn);
            if (rn <= 3 || t.fields[c].kind == "comment") {
                xml += "<c r=\"" + a + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" +
                       xml_escape(value) + "</t></is></c>";
            } else {
                S kind = t.fields[c].kind;
                S folded = lower(trim(value));
                double v = kind == "bool" && (folded == "true" || folded == u8"истина") ? 1
                           : kind == "bool" && (folded == "false" || folded == u8"ложь")
                               ? 0
                               : numeric(value);
                xml += "<c r=\"" + a + "\"" + (kind == "bool" ? " t=\"b\"" : "") + "><v>" +
                       number(v, 17) + "</v></c>";
            }
        }
        xml += "</row>";
    }
    xml += "</sheetData></worksheet>";
    std::map<S, S> files = {
        {"[Content_Types].xml",
         "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>"},
        {"_rels/.rels",
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"},
        {"xl/workbook.xml",
         u8"<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Данные\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
        {"xl/_rels/workbook.xml.rels",
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>"},
        {"xl/worksheets/sheet1.xml", xml}};
    U tmp = path + ".tmp";
    try {
        zip_write(tmp, files);
        if (!MoveFileExW(tmp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error(u8"Не удалось сохранить книгу.");
    } catch (...) {
        DeleteFileW(tmp.c_str());
        throw;
    }
}
} // namespace t2f
