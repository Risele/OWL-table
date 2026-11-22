///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Выбор направления.xlsx. Время генерации (местное время ПК): 22.11.2026 23:51:31.</Description>
///<GroupName>OWL_Tables_auto</GroupName>
FUNCTION_BLOCK Data1d03327f
VAR_INPUT
    ///<Description>Внутренний номер строки</Description>
    RowKey : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Реверс</Description>
    Reverse : BOOL;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE RowKey OF
    // Примечание: Стоп
    0:
        Reverse := FALSE;
        Speed := 0.0;
        Valid := TRUE;
    // Примечание: Вперёд
    1:
        Reverse := FALSE;
        Speed := 30.0;
        Valid := TRUE;
    // Примечание: Назад
    2:
        Reverse := TRUE;
        Speed := 15.0;
        Valid := TRUE;
    ELSE
        Reverse := FALSE;
        Speed := 0.0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

///<Description>Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK Direction
VAR_INPUT
    ///<Description>Прямой ход</Description>
    Fwd : BOOL;
    ///<Description>Обратный ход</Description>
    Rev : BOOL;
END_VAR
VAR_OUTPUT
    ///<Description>Реверс</Description>
    Reverse : BOOL;
    ///<Description>Частота, Гц</Description>
    Speed : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR
VAR
    InputCode : UDINT;
    RowKey : UDINT;
    Lookup : Data1d03327f;
END_VAR
InputCode := 0;
InputCode.0 := Fwd;
InputCode.1 := Rev;
RowKey := InputCode;
Lookup(RowKey := RowKey, Reverse => Reverse, Speed => Speed, Valid => Valid);
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 384 2622215970 4db835fff9516f885bb4d724ff8ad8d8 2
// LWB1 1/2 eNptkD9LA0EQxb9KmHqLBLG5TmMEweTEP9VxCPFGONBcuF21kEAiqIWFqLX_KttTPEzAJF9h9hv59i4HKSyGWd78eG9mA2omp924xxEp2ohTPjJx0iN1ScfdiDyTnrFyz0NtuE9eXZE2laxNpQ4cH_NJpMkLApJXO7T38iszmdTsNdo33Lc6Owf76JsXEYUK
// LWB1 2/2 1It82qFk9kqm9u4_cpfPS_JNcvmSHK4jyOu-v11OOdVcEh_wGcFphspUTZ7sjUNaa47c6zMvMrGZjLFZbm8lkyneOYCm3263Oi6SwlARfiFJF7fUIS6XvLsUmRd2jaXBSjF9lHmx5wMOcYQTG1WtOuIZuT8ozMNB-AeWhpzN
