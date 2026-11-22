///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Пуск по готовности.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:33.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Data3085e757
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Разрешение</Description>
    Enable : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Режим: Останов
    4:
        Speed := 0.0;
        Enable := FALSE;
        Valid := TRUE;
    // Режим: Работа
    3:
        Speed := 25.5;
        Enable := TRUE;
        Valid := TRUE;
    ELSE
        Speed := 0.0;
        Enable := FALSE;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK DrivePermit
VAR_INPUT
    ///<Description>Команда пуска</Description>
    Start : BOOL;
    ///<Description>Привод готов</Description>
    Ready : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Разрешение</Description>
    Enable : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Data3085e757;
END_VAR
InputCode := 0;
InputCode.0 := Start;
InputCode.1 := Ready;
RowKey := InputCode;
IF (InputCode AND 1) = 0 THEN
    RowKey := 4;
END_IF;
Lookup(RowKey := RowKey, Speed => Speed, Enable => Enable, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 371 4028606415 34dba1073eb06b0f1e1b81bc9aee2c23 2
// LWB1 1/2 eNpNj81Kw1AQhV8lzPoisdBNdv50IYgt_oAQgiTNFAJNUm6ugkjBKoh7dy7UndtYGy1S6yvMfSNnYgIuDhnO_XLmjA87eRolGcagYFcnFzhAnSYG1BWMohg8o89RyXhWGJyA5yooTGsXpnWnwic4jgvwfB_okda0opK-aUGlQz_21s7oi0resncwODnm75EJ
// LWB1 2/2 Ne_pBIr5Z3tNS5rzXwuH3mltb3ic_6MPMYwvoWZfqbQzAVilcujB3gnQ29qX1AnyLTX3wvs_Obey91RxkyVVDGz3-8L1sjAaYwtW9MHPK_aHeZpixr0AgkCBxmGum6NcNk9ZbiN6qmvIkdJVojbZFnW6G91mrGu8_ZXlyGnwC_PjnZg
