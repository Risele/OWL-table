///<Description>Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: Рецепты дозирования.xlsx. Время генерации (местное время ПК): 22.11.2026 22:57:01.</Description>
///<GroupName>Таблицы параметров</GroupName>
FUNCTION_BLOCK Recipe
VAR_INPUT
    ///<Description>Номер записи</Description>
    ModeId : UDINT;
END_VAR
VAR_OUTPUT
    ///<Description>Масса порции, кг</Description>
    Mass : REAL;
    ///<Description>Число циклов</Description>
    Cycles : UDINT;
    ///<Description>Промывка</Description>
    Rinse : BOOL;
    ///<Description>Строка существует</Description>
    Valid : BOOL;
END_VAR

CASE ModeId OF
    // Примечание: Проба
    1:
        Mass := 0.5;
        Cycles := 1;
        Rinse := FALSE;
        Valid := TRUE;
    // Примечание: Малая серия
    2:
        Mass := 1.25;
        Cycles := 3;
        Rinse := FALSE;
        Valid := TRUE;
    // Примечание: Большая серия
    3:
        Mass := 2.75;
        Cycles := 4;
        Rinse := TRUE;
        Valid := TRUE;
    ELSE
        Mass := 0.0;
        Cycles := 0;
        Rinse := FALSE;
        Valid := FALSE;
END_CASE;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 427 2649456230 d8cd62aa026181c52bf7cd6579680018 3
// LWB1 1/3 eNplj8FKw0AQhl8lzHkJNSpCbmp7CBgDpZ5CkDZZIRBsycaDSCG9qKBQvCsevcZKaGg1vsLsGzkTEzx42N2fmW_--deH0XiSSBAwlGE8I3EDF5MI7Cy9koLlucrkDOyeAJV1ZZV11TnzsUwiBbbvA75gjZ9Y6tzANRb4jZVeYEX2Tp8udxpJJ4JAEPmMhV5Q
// LWB1 2/3 szCIqnWub7HCShi4wQ-OMzg84YmxUr_8W2O1xdpoyA1LXBFy1ndOR_QeX4eJbOFXnXMQ_YArIgvqHnke-w3jSyX_mKoJe0dJv0iX7OK57qDxgyAQkMpwmraf26Fiz9ynu1F02j3vtIEtLW6ZFhO7HUHfpKSFXhqUvuSVetnATFjmAcN7rSU-kdlWP-r7fwPB
// LWB1 3/3 PPgBjpC5vA
