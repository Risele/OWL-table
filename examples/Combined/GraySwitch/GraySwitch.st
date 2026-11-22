///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Четыре положения переключателя.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:35.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Datadfe34fee
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Номер ступени</Description>
    LevelNo : UDINT;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Примечание: Стоп
    0:
        LevelNo := 0;
        Speed := 0.0;
        Valid := TRUE;
    // Примечание: Низкая
    2:
        LevelNo := 1;
        Speed := 18.0;
        Valid := TRUE;
    // Примечание: Средняя
    3:
        LevelNo := 2;
        Speed := 35.0;
        Valid := TRUE;
    // Примечание: Высокая
    1:
        LevelNo := 3;
        Speed := 50.0;
        Valid := TRUE;
    ELSE
        LevelNo := 0;
        Speed := 0.0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK GraySwitch
VAR_INPUT
    ///<Description>Контакт A</Description>
    A : BOOL;
    ///<Description>Контакт B</Description>
    B : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Номер ступени</Description>
    LevelNo : UDINT;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Datadfe34fee;
END_VAR
InputCode := 0;
InputCode.0 := A;
InputCode.1 := B;
RowKey := InputCode;
Lookup(RowKey := RowKey, LevelNo => LevelNo, Speed => Speed, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 425 2975929998 d58e5819b9b138b8e261d307cc74492a 3
// LWB1 1/3 eNplj8FKw0AQhl8lzHkP1VKQ3NJapNBGsfYUFrHNioFqSrIqIoU0gh48FPQmlOLJaw8GS7HtK8y-kbObBgIehl3--fabHQ8a4XU_uBE-MDiKLh6694EcXAF7hMu-D7aMbgXT1_NYihHYFQaxLOJYFulY84EY-jHYngf4gRtcqxQXuFKp5ZC75Z70zuh0gLN_
// LWB1 2/3 QL0E1HNgRsAvZiqx1ESl6gm3mOEal0T0DluuJtviTgzdMOe_cKFB3Ggps_BdPRNy2nTadHRHghY03FwluDTmFxpOQswIaBx3Ok0jBc4ZRGIQRrtdKhSWCz_1FNwanQ72ijrQ3RkZf3BFv5kaoujuU1Vr-fuEVvmm9aclRpuqVDUz4029qglN2Xn4mP8Bm2yt
// LWB1 3/3 OQ
