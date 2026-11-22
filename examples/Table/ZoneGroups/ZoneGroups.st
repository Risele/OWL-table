///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Уставки двух зон.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:28.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK ZoneGroups_10_19
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Температура, °C</Description>
    SetTemp : REAL;
    ///<Description>Скорость вентилятора, %</Description>
    FanPct : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Зона 1, ночь
    10:
        SetTemp := 18.0;
        FanPct := 25;
        Valid := TRUE;
    // Примечание: Зона 1, день
    11:
        SetTemp := 22.0;
        FanPct := 60;
        Valid := TRUE;
    ELSE
        SetTemp := 0.0;
        FanPct := 0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Уставки двух зон.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:28.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK ZoneGroups_20_29
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Температура, °C</Description>
    SetTemp : REAL;
    ///<Description>Скорость вентилятора, %</Description>
    FanPct : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Зона 2, ночь
    20:
        SetTemp := 16.0;
        FanPct := 20;
        Valid := TRUE;
    // Примечание: Зона 2, день
    21:
        SetTemp := 24.0;
        FanPct := 75;
        Valid := TRUE;
    ELSE
        SetTemp := 0.0;
        FanPct := 0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Уставки двух зон.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:28.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK ZoneGroups
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Температура, °C</Description>
    SetTemp : REAL;
    ///<Description>Скорость вентилятора, %</Description>
    FanPct : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    Group0 : ZoneGroups_10_19;
    Group1 : ZoneGroups_20_29;
END_VAR

CASE ModeId OF
    10..19:
        Group0(ModeId := ModeId, SetTemp => SetTemp, FanPct => FanPct, Valid => Valid);
    20..29:
        Group1(ModeId := ModeId, SetTemp => SetTemp, FanPct => FanPct, Valid => Valid);
    ELSE
        SetTemp := 0.0;
        FanPct := 0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 450 62899448 adff63d96348b84a004952f38eab7e8f 3
// LWB1 1/3 eNpdj8FKw0AQhl8lDHjbQzbYKrlJWyVgVDReDEHaZgWhNiFJT1Kw9dBLwaMnEX2CUAwGNSn4BLNv5GyiBXP5_2H2m39nXHD6g5EABhfBWBxEwSSMgd3C1cAHM4kmgqnyMk5ECCbXGcTJXz9ONu2pmrgWIz8G03UBn7DEL8zknYbvmOIacznDnD6xuiR24AvL
// LWB1 2/3 B48R-YoZkWvFYirn8l45077TDoGnvb1DsjOROOImrAde8ANLgko5I3yp4YoSCjnHHD_lA3lZJ2zR4HnXOnLI9_vjk2FSzz_Tc14tt6DNCqozIjrHtt2rWPA8BpEYBtHvMVynLt8lMVok-Ei3FZhqnGnkpVzIZZXMuUIMkrbe5N6qHWvOqPLaCv7HGY08o8rb
// LWB1 3/3 JtlpNblNnjf1fgA6GMLd
