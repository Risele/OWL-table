#pragma once
#include "core.hpp"
#include <System.Character.hpp>
namespace t2f {
inline S discrete_cell(const J &v) {
    if (v.is_string())
        return trim(v.get<S>());
    if (v.is_boolean())
        return v.get<bool>() ? "1" : "0";
    if (v.is_number_integer() || v.is_number_unsigned())
        return std::to_string(v.get<int64_t>());
    throw std::runtime_error(u8"Ячейка должна содержать текст или 0/1.");
}
inline S discrete_bit(const J &v, bool wildcard) {
    S s = lower(discrete_cell(v));
    if (s == "0" || s == "false" || s == u8"ложь")
        return "0";
    if (s == "1" || s == "true" || s == u8"истина")
        return "1";
    if (wildcard && (s == "x" || s == u8"х"))
        return "X";
    throw std::runtime_error(wildcard ? u8"Допустимы только 0, 1 и X (не влияет)."
                                      : u8"Для Q допустимы только 0 и 1.");
}
inline void check_port_name(const S &s, std::set<S> &used) {
    require(identifier(s) == s && System::Character::TCharacter::IsLetter(wide(s)[1]),
            u8"Имя должно начинаться с буквы: " + s);
    static const S words =
        " ABS ACTION AND ARRAY AT BOOL BY CASE CONFIGURATION CONSTANT CONTINUE DATE DINT DO DWORD "
        "ELSE ELSIF END END_CASE END_FOR END_FUNCTION END_FUNCTION_BLOCK END_IF END_PROGRAM "
        "END_REPEAT END_STRUCT END_TYPE END_VAR END_WHILE EXIT FALSE FOR FUNCTION FUNCTION_BLOCK "
        "IF INT LINT LREAL LWORD MOD NOT OF OR PROGRAM REAL REPEAT RETAIN RETURN SINT STRING "
        "STRUCT THEN TIME TO TRUE TYPE UDINT UINT ULINT UNTIL USINT VAR VAR_INPUT VAR_OUTPUT "
        "VAR_IN_OUT VAR_TEMP VAR_GLOBAL VAR_EXTERNAL WHILE WORD XOR ";
    require(lower(words).find(" " + lower(s) + " ") == S::npos,
            u8"Ключевое слово ST нельзя использовать как имя: " + s);
    require(used.insert(lower(s)).second, u8"Повторное имя (без учёта регистра): " + s);
}
} // namespace t2f
