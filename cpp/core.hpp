#pragma once
#include <System.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include "vendor/json.hpp"
namespace t2f {
using J = nlohmann::json;
using S = std::string;
using U = System::UnicodeString;
U wide(const S &);
S utf8(const U &);
S lower(const S &);
S trim(const S &);
void require(bool, const S &);
S identifier(const S &);
S uuid5(const S &, const S &);
S read_file(const U &);
void write_file(const U &, const S &);
S zip_read(const U &, const S &);
void zip_write(const U &, const std::map<S, S> &);
struct Field {
    S title, kind, name;
    int column;
};
struct Table {
    std::vector<Field> fields;
    J rows;
    std::vector<std::pair<S, int>> outputs;
    S input;
    std::map<S, S> titles;
};
struct Book {
    J state;
    S sheet;
    std::vector<S> sheets;
};
Book read_book(const U &, const S &sheet = "");
Table parse_table(const J &);
void write_book(const U &, const J &);
J build_fbd(const Table &, const S &, uint64_t);
J build_st(const Table &, const S &, uint64_t, const S &source_name = "");
J evaluate_fbd(const J &, const S &, uint32_t);
int verify_fbd(const J &, const Table &, const S &);
J evaluate_st(const J &, const S &, uint32_t, int depth = 0);
int verify_st(const J &, const Table &, const S &);
struct Options {
    S name = "TableParameters";
    S source_name;
    bool fbd = true, st = true;
    uint64_t fbd_step = 10, st_step = 10;
};
J generate(const Table &, const U &, const Options &);
S icon();
S now_iso(S *local_text = nullptr);
S number(double, int);
S xml_escape(const S &);
const S ns = "a2072054-7813-4acc-8513-00f35030cac8";
const S zero = "00000000-0000-0000-0000-000000000000";
} // namespace t2f
