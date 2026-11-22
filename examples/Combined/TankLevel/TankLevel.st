///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Состояние по датчикам уровня.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:41.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Datad1d65e1e
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Уровень</Description>
    LevelNo : UDINT;
    ///<Description>Бак заполнен</Description>
    FullTank : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Примечание: Пусто
    0:
        LevelNo := 0;
        FullTank := FALSE;
        Valid := TRUE;
    // Примечание: Низкий
    1:
        LevelNo := 1;
        FullTank := FALSE;
        Valid := TRUE;
    // Примечание: Средний
    3:
        LevelNo := 2;
        FullTank := FALSE;
        Valid := TRUE;
    // Примечание: Полный
    7:
        LevelNo := 3;
        FullTank := TRUE;
        Valid := TRUE;
    ELSE
        LevelNo := 0;
        FullTank := FALSE;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK TankLevel
VAR_INPUT
    ///<Description>Нижний датчик</Description>
    LowSw : BOOL;
    ///<Description>Средний датчик</Description>
    MidSw : BOOL;
    ///<Description>Верхний датчик</Description>
    TopSw : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Уровень</Description>
    LevelNo : UDINT;
    ///<Description>Бак заполнен</Description>
    FullTank : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Datad1d65e1e;
END_VAR
InputCode := 0;
InputCode.0 := LowSw;
InputCode.1 := MidSw;
InputCode.2 := TopSw;
RowKey := InputCode;
Lookup(RowKey := RowKey, LevelNo => LevelNo, FullTank => FullTank, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 508 3764941309 dcba48dd90a8112c1ea0649de58efa82 3
// LWB1 1/3 eNp9kM1Kw0AQx1-lzHkPVW85WhUK-RBMT2ERa1YIxm5JUnuQQq1ID3rRuxTxBYoYW6OtrzD7Rs4mXbEeetid2Znf_ucjgIa8aEcdEQID_6RzbotLEQO7grN2CFaW9ATT7nGaiS5YdQZpZsJpZqIDzUciDlOwggDwCef4jgu6P2r4hlM1UmN6FFSj6R62fLK2
// LWB1 2/3 7B_1gTOin9UQc8I28k4UGv4RczVUt5t5X3YN_0L6S3ylGgt1T6nWXtMtW9CjurKCHnCKRQ1nZL6J_iR14ona9TybzEEvjvV-KnpCknP8okbG9EE3khPT8Bxnv5QGzhkk4lQmq5XUKfj_kMqNulYjXJaiW39Sxi83OcNCT_oLmeS2gdYWuIbps7OyOKnmUnca
// LWB1 3/3 4gP-A-cz2Og
