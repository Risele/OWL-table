///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Разреженные коды диагностики.xlsx. Время генерации (местное время ПК): 22.11.2026 22:57:06.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK FaultCodes
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Номер канала</Description>
    LineNo : UDINT;
    ///<Description>Требуется обслуживание</Description>
    Service : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Норма
    0:
        LineNo := 0;
        Service := FALSE;
        Valid := TRUE;
    // Примечание: Предупреждение
    101:
        LineNo := 1;
        Service := FALSE;
        Valid := TRUE;
    // Примечание: Осмотр
    240:
        LineNo := 2;
        Service := TRUE;
        Valid := TRUE;
    // Примечание: Ремонт
    503:
        LineNo := 3;
        Service := TRUE;
        Valid := TRUE;
    ELSE
        LineNo := 0;
        Service := FALSE;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 421 1708053405 884cf71281db93c6a2a70f6d4621ab21 3
// LWB1 1/3 eNpVT01OwkAUvgp56y5a0E2XgiYklC7EVdMYoGPSpLGmLW4MSakLl55AMZ4ADMUGilzhzY38OqWJLmbm5Xvf3zg0Gk8CQRpdjWdB0g09EZP2RHcTj8wkmgmtGm_jRDyQqWsUJw0cJw06r_i-CLyYTMchfuMfLjmXaYu_ecVHLuSCC0T0e7gsRPQ9crX_zB2Y
// LWB1 2/3 B5w9r0C66fWHI7wD_14Mw5r8KVPOeS2fIcjkQr62oF7Deg9oywV_KYuCcwgvbHuA51pEj_5U1AZLGBQq7-UPs2tb1qUKI9fVKBLTMDr9RAfYnKor9CXqVWaGbgA0muVSddugyFFNW97griMqevusorVPEn5H6xJ-mUzV-lzvAO406w9oseaDzNBp7v4CI7i9
// LWB1 3/3 -A
