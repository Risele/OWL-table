///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Две скорости привода.xlsx. Время генерации (местное время ПК): 22.11.2026 22:57:01.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK DriveModes
VAR_INPUT
    ///<Description>Номер режима</Description>
    Mode : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Разрешение</Description>
    Enable : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE Mode OF
    // Режим: Останов
    0:
        Speed := 0.0;
        Enable := FALSE;
        Valid := TRUE;
    // Режим: Работа
    1:
        Speed := 25.5;
        Enable := TRUE;
        Valid := TRUE;
    ELSE
        Speed := 0.0;
        Enable := FALSE;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 307 673750750 3db51ed0be97a4731a56ac35493a5e5d 2
// LWB1 1/2 eNo1TrFqAkEU_BV59SIasLkuQYtARNB0xxHUewFBPbldbURQC7FPHUmXdhM4tUjuG97-kbPnXfH2zZsdZiak1-FoyqSonU5W3E1i1qTW9D6KKTDpkpWHb9rwgoKGIm0qWpuK3Xj9hKexpiAMST4llz_J3LbmtpLJWa44LSKe23h8BEUKum-xbuf2kmOsqsmH
// LWB1 2/2 O-C_33l8wRosmOO77kusXLyVO8LuH3YZBE-9ntd15kX_UlimgR8nsxnPDRBFkaKUx0laFmyArEZORQUL21x-C5sm6IdWvYXlYRH_cy8Jq010A-ihgQ8
