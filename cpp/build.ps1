param([string]$Bds='C:/Program Files (x86)/Embarcadero/Studio/22.0')
$ErrorActionPreference='Stop'
$compiler=Join-Path $Bds 'bin/bcc32c.exe'
$build=Join-Path $PSScriptRoot 'build'
$dist=Join-Path $PSScriptRoot 'dist/Table2FBD'
New-Item -ItemType Directory -Force $build,$dist | Out-Null
Push-Location $build
try {
    # rc.exe из C++Builder поддерживает PNG-изображения внутри ICO.
    & (Join-Path $Bds 'bin/rc.exe') '/i' $PSScriptRoot '/fo' (Join-Path $build 'app.res') (Join-Path $PSScriptRoot 'app.rc')
    if($LASTEXITCODE -ne 0){throw 'Ошибка сборки ресурсов окна.'}
    $common=@('core','fbd','st','cli')
    foreach($name in ($common+@('window','gui_main','console_main'))) {
        $dependencies=@(Get-Item (Join-Path $PSScriptRoot ($name+'.cpp'))) + @(Get-ChildItem $PSScriptRoot -Filter '*.hpp')
        $latest=($dependencies | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
        $object=Join-Path $build ($name+'.obj')
        if((Test-Path -LiteralPath $object) -and (Get-Item -LiteralPath $object).LastWriteTime -ge $latest){continue}
        & $compiler -c -O2 ('-o'+(Join-Path $build ($name+'.obj'))) (Join-Path $PSScriptRoot ($name+'.cpp'))
        if($LASTEXITCODE -ne 0){throw "Ошибка компиляции: $name"}
    }
    & $compiler -c -O2 ('-o'+(Join-Path $build 'pugixml.obj')) (Join-Path $PSScriptRoot 'vendor/pugixml.cpp')
    if($LASTEXITCODE -ne 0){throw 'Ошибка компиляции XML.'}
    $objects=@($common | ForEach-Object {Join-Path $build ($_+'.obj')}) + @(Join-Path $build 'pugixml.obj')
    & $compiler -tC -tM -tJ ('-o'+(Join-Path $dist 'Table2FBD-cli.exe')) @objects (Join-Path $build 'console_main.obj') rtl.lib
    if($LASTEXITCODE -ne 0){throw 'Ошибка компоновки консольной программы.'}
    & $compiler -tW -tM -tV ('-o'+(Join-Path $dist 'Table2FBD.exe')) @objects (Join-Path $build 'window.obj') (Join-Path $build 'gui_main.obj') (Join-Path $build 'app.res') vcl.lib rtl.lib
    if($LASTEXITCODE -ne 0){throw 'Ошибка компоновки редактора.'}
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../Generate.bat') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../assets/Template.xlsx') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../docs/USAGE.txt') -Destination (Join-Path $dist 'README.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../LICENSE'),(Join-Path $PSScriptRoot '../THIRD_PARTY_NOTICES.md') -Destination $dist
    New-Item -ItemType Directory -Force (Join-Path $dist 'licenses') | Out-Null
    Copy-Item (Join-Path $PSScriptRoot 'vendor/*LICENSE*') (Join-Path $dist 'licenses')
} finally { Pop-Location }
