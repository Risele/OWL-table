@echo off
setlocal DisableDelayedExpansion
for /f "tokens=2 delims=:" %%G in ('chcp') do set "_OLD_CODEPAGE=%%G"
chcp 65001 >nul

rem === Настройки. Файл BAT должен быть сохранён в UTF-8 без BOM. ===
rem Относительные пути отсчитываются от папки этого BAT, а не от папки терминала.
set "INPUT_FILE=Template.xlsx"
rem Пустой OUTPUT_FILE: имя входной книги, расширения .tple / .fbst, та же папка.
set "OUTPUT_FILE="
set "MACRO_NAME=TableParameters"
set "GROUP_STEP=10"
rem SINGLE_MACRO=1: единый макрос. SINGLE_MACRO=0: сводный макрос и группы.
set "SINGLE_MACRO=0"
rem Независимые параметры ST. Параметры выше задают только режим FBD.
set "ST_GROUP_STEP=10"
set "ST_SINGLE_MACRO=0"
rem BOTH, FBD или ST. Ключ --fbd / --st при запуске имеет приоритет.
set "OUTPUT_FORMAT=BOTH"
rem Пустой SHEET_NAME: лист Данные, если есть; иначе первый лист.
set "SHEET_NAME="
rem Для запуска двойным щелчком можно поставить PAUSE_AT_END=1.
set "PAUSE_AT_END=0"
rem При перетаскивании файла показать результат и дождаться нажатия клавиши.
rem Для автоматического вызова BAT с аргументом поставьте PAUSE_ON_DROP=0.
set "PAUSE_ON_DROP=1"
rem =============================================================

rem Аргумент имеет приоритет над INPUT_FILE. Полный путь получаем до pushd.
if "%~1"=="" goto input_ready
if /i "%~1"=="--fbd" goto key_only
if /i "%~1"=="--st" goto key_only
set "INPUT_FILE=%~f1"
if "%PAUSE_ON_DROP%"=="1" set "PAUSE_AT_END=1"
if "%~2"=="" goto input_ready
if not "%~3"=="" goto arguments_error
if /i "%~2"=="--fbd" set "OUTPUT_FORMAT=FBD"
if /i "%~2"=="--st" set "OUTPUT_FORMAT=ST"
if /i "%~2"=="--fbd" goto input_ready
if /i "%~2"=="--st" goto input_ready
goto arguments_error

:key_only
if not "%~2"=="" goto arguments_error
if /i "%~1"=="--fbd" set "OUTPUT_FORMAT=FBD"
if /i "%~1"=="--st" set "OUTPUT_FORMAT=ST"

:input_ready
pushd "%~dp0"
if errorlevel 1 goto folder_error
if not exist "%~dp0Table2FBD-cli.exe" goto program_error
if not defined INPUT_FILE goto input_error
set "_FORMAT_OPTION="
if /i "%OUTPUT_FORMAT%"=="FBD" set "_FORMAT_OPTION=--fbd"
if /i "%OUTPUT_FORMAT%"=="ST" set "_FORMAT_OPTION=--st"
if /i "%OUTPUT_FORMAT%"=="BOTH" goto format_ok
if defined _FORMAT_OPTION goto format_ok
echo Ошибка: OUTPUT_FORMAT должен быть BOTH, FBD или ST. 1>&2
set "_EXIT_CODE=2"
goto finish

:format_ok
set "_FBD_MODE="
set "_ST_MODE="
if "%SINGLE_MACRO%"=="0" set "_FBD_MODE=grouped"
if "%SINGLE_MACRO%"=="1" set "_FBD_MODE=single"
if "%ST_SINGLE_MACRO%"=="0" set "_ST_MODE=grouped"
if "%ST_SINGLE_MACRO%"=="1" set "_ST_MODE=single"
if not defined _FBD_MODE goto mode_error
if not defined _ST_MODE goto mode_error
"%~dp0Table2FBD-cli.exe" --generate "%INPUT_FILE%" --output "%OUTPUT_FILE%" --name "%MACRO_NAME%" --fbd-mode "%_FBD_MODE%" --fbd-step "%GROUP_STEP%" --st-mode "%_ST_MODE%" --st-step "%ST_GROUP_STEP%" --sheet "%SHEET_NAME%" %_FORMAT_OPTION%
set "_EXIT_CODE=%ERRORLEVEL%"
goto finish

:mode_error
echo Ошибка: SINGLE_MACRO и ST_SINGLE_MACRO должны быть равны 0 или 1. 1>&2
set "_EXIT_CODE=2"
goto finish

:program_error
echo Ошибка: Table2FBD-cli.exe не найден рядом с файлом BAT. 1>&2
set "_EXIT_CODE=1"
goto finish

:input_error
echo Ошибка: укажите INPUT_FILE в настройках BAT. 1>&2
set "_EXIT_CODE=2"
goto finish

:folder_error
echo Ошибка: не удалось перейти в папку файла BAT. 1>&2
set "_EXIT_CODE=1"
goto end

:arguments_error
echo Ошибка: передавайте один файл .xlsx и необязательный ключ --fbd или --st. 1>&2
set "_EXIT_CODE=2"
goto end

:finish
popd
:end
if "%PAUSE_AT_END%"=="1" pause
if defined _OLD_CODEPAGE chcp %_OLD_CODEPAGE% >nul
exit /b %_EXIT_CODE%
