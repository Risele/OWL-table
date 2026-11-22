///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Поправки измерительных каналов.xlsx. Время генерации (местное время ПК): 22.11.2026 22:57:07.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK Calibration
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Коэффициент</Description>
    Gain : REAL;
    ///<Description>Смещение</Description>
    OffsetVal : REAL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Эталонный канал
    1:
        Gain := 1.0;
        OffsetVal := 0.0;
        Valid := TRUE;
    // Примечание: Датчик температуры
    2:
        Gain := 1.01999998;
        OffsetVal := -0.75;
        Valid := TRUE;
    // Примечание: Резервный канал
    3:
        Gain := 0.985000014;
        OffsetVal := 0.125;
        Valid := TRUE;
    ELSE
        Gain := 0.0;
        OffsetVal := 0.0;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 430 2115075757 bf0500963a46407a7af2d25c2b490447 3
// LWB1 1/3 eNptj8FKw0AQhl8lzHkNaVW0vUktUjAWpHgJQRKzgUBoJFlPUoixKFK8eRJEfQChCKGx2vYVZt_I2a1BBBd2Z_n3--efdWDg-TEHBh0vjvzUE1EyBHYJoR9AW6QXnKnraSb4ObQtBpmo5UzU6kjxEY-DDNqOA_iES_zCUuYGznCKK6zkFVaU0dunw04C3gvA
// LWB1 2/3 ZUQ-4lLey7EcE3KDFZa4kAUxx929QyoHXjRcg6-64Z0CFPaL9MMw4-LEi9fcs8zpXbG3lFyznb5td48GdAPXZZDysyT9GbZBotoWbXyTBbk-afwFDTLBDwPnug2Jun9T0aalyoZl7mwr0wNOZUFxFc4N8peUvlKf1_K1zOVEWzdViNna3Wr9WVptNHWnFzLP
// LWB1 3/3 tPX9n3h35H4DduvBbg
