#pragma once
#include "core.hpp"
namespace t2f {
struct InputColor {
    const char *rgb, *name;
};
inline const std::vector<InputColor> &inputPalette() {
    static const std::vector<InputColor> palette = {
        {"", u8"Без цвета"},        {"F8D9D8", u8"Розовый"},   {"FADFC6", u8"Персиковый"},
        {"F8F0BF", u8"Жёлтый"},     {"DDEDC8", u8"Салатовый"}, {"CBEADA", u8"Мятный"},
        {"C8EAEA", u8"Бирюзовый"},  {"D1E6F8", u8"Голубой"},   {"D9DCF7", u8"Сиреневый"},
        {"E5D8F4", u8"Лавандовый"}, {"F3D8EC", u8"Лиловый"},   {"E9DFD3", u8"Бежевый"},
        {"DEE3E7", u8"Серый"}};
    return palette;
}
inline unsigned colorFromFill(const S &rgb) {
    for (unsigned i = 1; i < inputPalette().size(); ++i)
        if (rgb == inputPalette()[i].rgb)
            return i;
    return 0;
}
inline unsigned fieldColor(const J &field) {
    return field.size() == 4 ? field.at(3).get<unsigned>() : 0;
}
// The optional fourth item travels with the contact when it is renamed or moved.
inline void setFieldColor(J &field, unsigned id) {
    if (id)
        field[3] = id;
    else if (field.size() == 4)
        field.erase(3);
}
} // namespace t2f
