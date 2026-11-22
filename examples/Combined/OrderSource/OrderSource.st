///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Приоритет источника задания.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:37.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Datab3b99e5f
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Источник задания</Description>
    SourceNo : UDINT;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Примечание: Местный
    8:
        SourceNo := 1;
        Speed := 12.0;
        Valid := TRUE;
    // Примечание: Сеть
    9:
        SourceNo := 2;
        Speed := 36.0;
        Valid := TRUE;
    // Примечание: Таймер
    4:
        SourceNo := 3;
        Speed := 24.0;
        Valid := TRUE;
    // Примечание: Нет команды
    0:
        SourceNo := 0;
        Speed := 0.0;
        Valid := TRUE;
    ELSE
        SourceNo := 0;
        Speed := 0.0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK OrderSource
VAR_INPUT
    ///<Description>Местный пост</Description>
    LocalCmd : BOOL;
    ///<Description>Сетевая команда</Description>
    NetCmd : BOOL;
    ///<Description>Команда таймера</Description>
    TimerCmd : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Источник задания</Description>
    SourceNo : UDINT;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Datab3b99e5f;
END_VAR
InputCode := 0;
InputCode.0 := LocalCmd;
InputCode.1 := NetCmd;
InputCode.2 := TimerCmd;
RowKey := InputCode;
IF (InputCode AND 1) = 1 THEN
    RowKey := 8;
END_IF;
IF (InputCode AND 3) = 2 THEN
    RowKey := 9;
END_IF;
Lookup(RowKey := RowKey, SourceNo => SourceNo, Speed => Speed, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 541 645259358 74f78f55203212ae064a362c812843d5 3
// LWB1 1/3 eNpVkM1Kw0AUhV8l3PUsWisuspPYRaFNRVsQShCbjBBoTZmkKyloF3ZRoQs3Ioroxm0oDbbFxle480beyU9NFvN35zvnXk4PDG_Yd2-4AwzawuHi3BsLmwO7heu-A3ogxpyp66Uf8BHoFQZ-kJf9IK9OFO_ygeOD3usBvmIk7-UUd3KOGw1_MVZP6tEwT7sd
// LWB1 2/3 OpuefTUwhg5YjPAPwqcY4RJDudBwizH-YIg7XGFYEJk82EteioxG6hA3VIjkXUnScYdc7EXPyVCxnJFsjVsNv0m2SlzWckF496RhKlmagumlsi8aKxVSG6bhk3wg5qx-3FToiPPM_p2ar5MhZqknRgQY7VarnriCZTEQ3PZEllOVihfZUvfqAW3l8BLnSvav
// LWB1 3/3 OIXUjhSXpCYf90RO1RR1qIjP_1RKVHHhm7IphS7nNOnE-gMheupT
