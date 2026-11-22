///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Параметры шагов промывки.xlsx. Время генерации (местное время ПК): 22.11.2026 22:57:06.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK Sequence
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Открыть впуск</Description>
    Inlet : BOOL;
    ///<Description>Открыть слив</Description>
    Outlet : BOOL;
    ///<Description>Выдержка, с</Description>
    HoldSec : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Ожидание
    0:
        Inlet := FALSE;
        Outlet := FALSE;
        HoldSec := 0;
        Valid := TRUE;
    // Примечание: Наполнение
    1:
        Inlet := TRUE;
        Outlet := FALSE;
        HoldSec := 30;
        Valid := TRUE;
    // Примечание: Слив
    2:
        Inlet := FALSE;
        Outlet := TRUE;
        HoldSec := 15;
        Valid := TRUE;
    // Примечание: Пауза
    3:
        Inlet := FALSE;
        Outlet := FALSE;
        HoldSec := 5;
        Valid := TRUE;
    ELSE
        Inlet := FALSE;
        Outlet := FALSE;
        HoldSec := 0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 445 1624022885 1d1105e17cf7c2f6b1592e2426fb382e 3
// LWB1 1/3 eNpt0MFOwkAQANBfIXPeA0i49KiY2MTaA3hqGiPdNTFpKLbbkyFBOHjAk3cg_kFDrDZF4Bdm_8jZtdUePLSdzLzpzK4Hw9tRKIDBQDykYhxQ-Ah3Iw6WjFPBdHiTSDEBq80gkXU6kXV2qv29CHkClucBrvCAX5irWQs_McMjFuoJCxpg9-nlRFzYHHxGcq3m
// LWB1 2/3 WKqZWqq5emnhFo9qQbYkduq6l7plHAr5Hya2wwK3f9RN5a99VUt81yvgB5aYMeIkrvv21ZC-F1HIByL4oRtChdn3mZbdU5wTOXMd59xg8H0GsQiiuDpfm5LNB9c0pKBxdbf-bYcKnQp0jVqZqzjQ1nvMG_KkUqajp-VbdTJd7TYGmeIGM7XQ90p7Tf1vORm-
// LWB1 3/3 Jw
