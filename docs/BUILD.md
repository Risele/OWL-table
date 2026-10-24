# Сборка Table2FBD

Все команды выполняются из корня рабочей копии или распакованного архива
исходников. Инструкция по использованию находится в `docs/USAGE.txt`.

## C++

Проверенная среда: Windows, C++Builder 11, Studio 22.0, компилятор bcc32c 7.60.
Установите компоненты Win32 и стандартные библиотеки C++Builder.
Для самой сборки C++ не нужны Python, Qt, Excel и OwenLogic.
Используются статические библиотеки VCL и RTL. Компилятор ресурсов `rc.exe`
входит в C++Builder; он поддерживает PNG внутри ICO.

```powershell
powershell -ExecutionPolicy Bypass -File cpp/build.ps1
```

Если C++Builder установлен в другой папке:

```powershell
powershell -ExecutionPolicy Bypass -File cpp/build.ps1 -Bds "D:\Embarcadero\Studio\22.0"
```

Результат: `cpp/dist/Table2FBD/`. В этой папке находятся `Table2FBD.exe`,
`Table2FBD-cli.exe`, BAT, пример таблицы и лицензии. Оба EXE имеют разрядность
Win32 и предназначены для Windows 10/11. Файлы `.tds` нужны для отладки
и не входят в архив программы.

Сторонние исходники JSON и XML уже находятся в `cpp/vendor/`.
Файлы `.cpp` и `.hpp` можно открыть в RAD Studio. Готового проекта `.cbproj`
и форм `.dfm` нет: окно создаётся в `window.cpp`, сборка выполняется скриптом.

## Python

Этот раздел относится к полной рабочей копии или архиву исходников Python.

Проверенная среда: Python 3.14.6 x64, Windows, PySide6 6.11.1,
PyInstaller 6.21.0. Точные версии зависимостей заданы в `python/requirements.txt`.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r python/requirements.txt
.\.venv\Scripts\python.exe python/app.py
```

Сборка EXE является необязательной. Она не входит в подготовленный релиз GitHub:

```powershell
powershell -ExecutionPolicy Bypass -File python/build.ps1 -Python .\.venv\Scripts\python.exe
```

Результат: `python/dist/Table2FBD/`. Распространяйте всю папку, включая `_internal`
и лицензии. Чтение и запись XLSX выполняются без Microsoft Excel.

## Проверки

Проверки Python с установленными зависимостями:

```powershell
python -m unittest discover -s python -v
```

Сравнение C++ с независимыми проверками Python после сборки C++:

```powershell
python cpp/test_native.py
$env:TABLE2FBD_TEST_EXE = (Resolve-Path cpp/dist/Table2FBD/Table2FBD-cli.exe).Path
python -m unittest discover -s python -p test_cli.py -v
Remove-Item Env:TABLE2FBD_TEST_EXE
```

Эти проверки C++ используют стандартную библиотеку Python; Qt не требуется.
Архив C++ содержит необходимые модули Python, поэтому объединять два архива
исходников не нужно.

Проверка обычного запуска и кнопки в панели задач:

```powershell
python cpp/check_taskbar.py
```

Она требует рабочего стола Windows и закрывает только запущенный ею экземпляр.
Проверка редактора и генерации из окна:

```powershell
cpp/dist/Table2FBD/Table2FBD.exe --smoke-test .agents/window-check
```

Если установлены OwenLogic, PowerShell 7 и .NET SDK 6 или новее, доступны
дополнительные проверки чтения и создания блоков без открытия окна OwenLogic:

```powershell
pwsh -NoProfile -File tools/check_st_import.ps1 -Archive "Результат.fbst"
python python/owen_checks.py "Результат.tple"
```

Это не симуляция проекта и не испытание на ПЛК. Библиотеки OwenLogic
не копируются в репозиторий и релизы.

## Подготовка релиза

Порядок упаковки и загрузки архивов описан отдельно в `docs/RELEASING.md`.
