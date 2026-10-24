param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$Python = (Get-Command $Python -ErrorAction Stop).Source
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $PSScriptRoot
try {
    & $Python -m unittest -v test_generator test_app_core test_app_gui test_st test_cli.ConsoleTests
    if ($LASTEXITCODE -ne 0) { throw 'Проверки не пройдены.' }
    $templateFile = Join-Path $repo 'assets/Template.xlsx'
    $iconFile = Join-Path $repo 'assets/Table2FBD.ico'
    & $Python -m PyInstaller --noconfirm --clean --windowed --name Table2FBD `
        --add-data "$templateFile;assets" --icon $iconFile `
        --distpath dist --workpath .build --specpath .build app.py
    if ($LASTEXITCODE -ne 0) { throw 'Не удалось собрать программу.' }
    & $Python -m PyInstaller --noconfirm --clean --onefile --console --name Table2FBD-cli `
        --icon $iconFile --distpath dist/Table2FBD --workpath .build/cli --specpath .build cli.py
    if ($LASTEXITCODE -ne 0) { throw 'Не удалось собрать консольную программу.' }
    Copy-Item -LiteralPath (Join-Path $repo 'Generate.bat') -Destination dist/Table2FBD/Generate.bat
    $previousTestExe = $env:TABLE2FBD_TEST_EXE
    try {
        $env:TABLE2FBD_TEST_EXE = Join-Path $PSScriptRoot 'dist/Table2FBD/Table2FBD-cli.exe'
        & $Python -m unittest -v test_cli
        if ($LASTEXITCODE -ne 0) { throw 'Проверки консольной сборки и BAT не пройдены.' }
    } finally { $env:TABLE2FBD_TEST_EXE = $previousTestExe }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/USAGE.txt') -Destination dist/Table2FBD/README.txt
    Copy-Item -LiteralPath (Join-Path $repo 'LICENSE'),(Join-Path $repo 'THIRD_PARTY_NOTICES.md') -Destination dist/Table2FBD
    Copy-Item -LiteralPath $templateFile -Destination dist/Table2FBD/Template.xlsx
    & $Python app.py --generate $templateFile --output dist/Table2FBD/examples/FanModes.tple --name FanModes --step 10
    if ($LASTEXITCODE -ne 0) { throw 'Не удалось сформировать пример макроса.' }
    & $Python package_licenses.py dist/Table2FBD
    if ($LASTEXITCODE -ne 0) { throw 'Не удалось скопировать сведения о библиотеках.' }
    Write-Host 'Готово: dist/Table2FBD/Table2FBD.exe. Распространяйте папку целиком.'
} finally { Pop-Location }
