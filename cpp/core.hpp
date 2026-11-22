#pragma once
#include <System.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include "vendor/json.hpp"
namespace t2f {
#ifdef DISCRETE_TABLE
constexpr bool discrete_build = true;
constexpr const char *table_product = "DiscreteTable 0.1";
#else
constexpr bool discrete_build = false;
constexpr const char *table_product = "Table2FBD C++ 2.0.6";
#endif
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
#ifdef DISCRETE_TABLE
    std::vector<S> contacts;
    std::vector<int> matching_rows; // For discrete tables, source row for each input code, or -1.
#endif
};
struct Book {
    J state;
    S sheet;
    std::vector<S> sheets;
};
Book read_book(const U &, const S &sheet = "", bool raw_matrix = false);
Table parse_table(const J &, bool discrete = discrete_build);
void write_book(const U &, const J &, bool discrete = discrete_build);
void write_matrix(const U &, const J &, const std::vector<S> &column_fills = {});
J build_boolean_package_fbd(const J &, const S &, const std::vector<S> &, const std::vector<S> &, const std::vector<S> & = {});
J build_boolean_fbd(const std::vector<S> &, const J &, int, const S &, const S &, const S &, const std::vector<S> & = {});
constexpr char table_macro_group[] = u8"Таблицы параметров";
constexpr char internal_macro_group[] = "OWL_Tables_auto";
S one_line(S);
S st_annotation(const S &tag, const S &value);
J build_fbd(const Table &, const S &, uint64_t, const S &root_group = table_macro_group);
J build_st(const Table &, const S &, uint64_t, const S &source_name = "", const S &root_group = table_macro_group);
J evaluate_fbd(const J &, const S &, uint32_t);
int verify_fbd(const J &, const Table &, const S &);
J evaluate_st(const J &, const S &, uint32_t, int depth = 0);
int verify_st(const J &, const Table &, const S &);
struct Options {
    S name = "TableParameters";
    S source_name;
    bool fbd = true, st = true;
    uint64_t fbd_step = 10, st_step = 10;
    bool write_st_listing = false;
};
J generate(const Table &, const U &, const Options &, const J &editorState);
S icon();
S now_iso(S *local_text = nullptr);
S number(double, int);
S xml_escape(const S &);
const S ns = "a2072054-7813-4acc-8513-00f35030cac8";
const S zero = "00000000-0000-0000-0000-000000000000";
} // namespace t2f
