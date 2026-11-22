///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Выбор станции по адресу.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:39.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Datacf05d445
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Номер станции</Description>
    StationNo : UDINT;
    ///<Description>Тайм-аут, мс</Description>
    TimeoutMs : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Примечание: Станция 1
    1:
        StationNo := 101;
        TimeoutMs := 250;
        Valid := TRUE;
    // Примечание: Станция 6
    6:
        StationNo := 106;
        TimeoutMs := 400;
        Valid := TRUE;
    // Примечание: Станция 9
    9:
        StationNo := 109;
        TimeoutMs := 750;
        Valid := TRUE;
    ELSE
        StationNo := 0;
        TimeoutMs := 0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK StationCode
VAR_INPUT
    ///<Description>Разряд 0</Description>
    B0 : BOOL;
    ///<Description>Разряд 1</Description>
    B1 : BOOL;
    ///<Description>Разряд 2</Description>
    B2 : BOOL;
    ///<Description>Разряд 3</Description>
    B3 : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Номер станции</Description>
    StationNo : UDINT;
    ///<Description>Тайм-аут, мс</Description>
    TimeoutMs : UDINT;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Datacf05d445;
END_VAR
InputCode := 0;
InputCode.0 := B0;
InputCode.1 := B1;
InputCode.2 := B2;
InputCode.3 := B3;
RowKey := InputCode;
Lookup(RowKey := RowKey, StationNo => StationNo, TimeoutMs => TimeoutMs, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 508 1348304959 87fcac96e6d9302c71349f2e3a4baece 2
// LWB1 1/2 eNptkM9Kw0AQxl8lzHmFTauV5mj00EOiYHoKQaxZIWC7kmxPUkgr6LFPIP55gh4sVog-w8wbuZu0dLE97O4w3--b2ZkYfDkcZCORAoNLda0yOfJlKoA9wO0gBU_lY8FMeFUocQ8eZ1CoTbpQm-zE8Jm4Swvw4hjwDRf4RSXN8dPhunQvvOhH-j3hkLB_umvr
// LWB1 2/2 7q7esvXWrt629Xajv-AvVrik0qEpzTT8Q0-4wpVG-qe9MNpOG8rG8aGhb6wOcEGPNGMOVjS16CgbCjlWQdHQr1TqcqbDsymu46WG_PMgOKtxSBIGubiR-XojZkhuHZebTOvIxPi-_SLNnWYFNbQ-jaGj70O-x9CpDXaH2si7-j7e16GrvzdJ_gDgbsZu
