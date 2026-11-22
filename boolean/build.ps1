param([string]$Bds='C:/Program Files (x86)/Embarcadero/Studio/22.0', [switch]$CliOnly, [string]$OutputDir='', [string]$BuildDir='', [switch]$Development, [switch]$Unified)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$compiler=Join-Path $Bds 'bin/bcc32c.exe'
$build=if($BuildDir){[IO.Path]::GetFullPath($BuildDir)}else{Join-Path $PSScriptRoot 'build'}
$dist=if($OutputDir){[IO.Path]::GetFullPath($OutputDir)}elseif($Development){Join-Path $PSScriptRoot 'dist/Boolean2FBD-dev'}else{Join-Path $PSScriptRoot 'dist/Boolean2FBD'}
$defines=@()
if($Unified) {
    if(!$BuildDir){$build=Join-Path $repo 'unified/build'}
    if(!$OutputDir){$dist=Join-Path $repo 'unified/dist/OWL-Tables'}
    $defines=@('-DDISCRETE_TABLE','-DUNIFIED_EDITOR')
}
if(-not (Test-Path -LiteralPath $compiler)){throw 'C++Builder Win32 was not found. Specify -Bds.'}
New-Item -ItemType Directory -Force $build,$dist | Out-Null
Push-Location $build
try {
    $sources=[ordered]@{
        macro_document='cpp/macro_document.cpp'; shared_core='cpp/core.cpp'; shared_fbd='cpp/fbd.cpp'; pugixml='cpp/vendor/pugixml.cpp'; cache='boolean/engine/cache.cpp'; session_check='boolean/session_check.cpp';
        logic='boolean/logic.cpp'; editor_model='boolean/editor_model.cpp'; control='boolean/engine/control.cpp'; generation='boolean/generation.cpp'; generation_strategy='boolean/generation_strategy.cpp'; export_st='boolean/export_st.cpp'; export_fbd='boolean/export_fbd.cpp'; artifact_st='boolean/artifact_st.cpp'; artifact_fbd='boolean/artifact_fbd.cpp'; artifact_verify='boolean/artifact_verify.cpp'; cli='boolean/cli.cpp';
        contracts='boolean/engine/contracts.cpp'; ir='boolean/engine/ir.cpp'; target='boolean/engine/target.cpp'; package='boolean/engine/package.cpp'; package_search='boolean/engine/package_search.cpp';
        candidates='boolean/engine/candidates.cpp'; synth_basic='boolean/engine/synth_basic.cpp'; synth_cover='boolean/engine/synth_cover.cpp'; synth_advanced='boolean/engine/synth_advanced.cpp'; console_main='boolean/console_main.cpp'
    }
    if(-not $CliOnly){$sources['window']='boolean/window.cpp';$sources['gui_main']='boolean/gui_main.cpp'}
    if($Unified){
        foreach($unused in @('session_check','cli','console_main','gui_main')){$sources.Remove($unused)}
        $sources['window']='boolean/window.cpp'
        $sources['table_st']='cpp/st.cpp'
        $sources['table_window']='cpp/window.cpp'
        foreach($part in @('model','st','verify')){$sources['selector_'+$part]='selector/'+$part+'.cpp'}
        $sources['transfer']='unified/transfer.cpp'
        $sources['workbench']='unified/workbench.cpp'
    }
    foreach($name in $sources.Keys){
        $headers=@(Get-Item (Join-Path $repo 'cpp/core.hpp'))
        if($name -eq 'pugixml'){$headers=@(Get-ChildItem (Join-Path $repo 'cpp/vendor') -Filter 'pugi*.hpp')}
        elseif($name -eq 'shared_fbd'){$headers+=Get-Item (Join-Path $repo 'cpp/icon.hpp')}
        elseif(-not $name.StartsWith('shared_')){$headers+=Get-Item (Join-Path $PSScriptRoot 'logic.hpp')}
        if($name -eq 'logic'){$headers+=Get-Item (Join-Path $repo 'cpp/discrete_inputs.hpp')}
        if($name -in @('window','gui_main','workbench')){$headers+=Get-Item (Join-Path $PSScriptRoot 'window.hpp')}
        $headers+=Get-ChildItem $PSScriptRoot -Filter '*.hpp' | Where-Object Name -ne 'window.hpp'
        $headers+=Get-ChildItem (Join-Path $PSScriptRoot 'engine') -Filter '*.hpp' -Recurse
        $headers+=Get-ChildItem (Join-Path $PSScriptRoot 'verification') -Filter '*.hpp'
        if($Unified){
            $headers+=Get-Item (Join-Path $repo 'selector/selector.hpp'),(Join-Path $repo 'cpp/discrete_inputs.hpp')
            if($name -in @('table_window','workbench')){$headers+=Get-Item (Join-Path $repo 'cpp/window.hpp')}
            if($name -in @('transfer','workbench')){$headers+=Get-Item (Join-Path $repo 'unified/transfer.hpp')}
        }
        if($name -in @('shared_core','logic','window','table_window','transfer','selector_model','workbench')){$headers+=Get-Item (Join-Path $repo 'cpp/input_colors.hpp')}
        if($name -in @('window','table_window')){$headers+=Get-Item (Join-Path $repo 'cpp/input_color_box.hpp'),(Join-Path $repo 'cpp/port_headers.hpp'),(Join-Path $repo 'cpp/record_grid.hpp')}
        if($name -in @('window','table_window','workbench','gui_main')){$headers+=Get-Item (Join-Path $repo 'cpp/editor_bar.hpp'),(Join-Path $repo 'cpp/editor_dialog.hpp')}
        if($name -in @('macro_document','generation','generation_strategy','table_st','table_window','workbench')){$headers+=Get-Item (Join-Path $repo 'cpp/macro_document.hpp')}
        $source=Join-Path $repo $sources[$name]
        $object=Join-Path $build ($name+'.obj')
        $latest=(@(Get-Item -LiteralPath $source)+$headers | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
        if((Test-Path -LiteralPath $object) -and (Get-Item -LiteralPath $object).LastWriteTime -ge $latest){continue}
        & $compiler -c -O2 @defines ('-o'+$object) $source
        if($LASTEXITCODE -ne 0){throw "Compilation failed: $source"}
    }
    $common=@('macro_document','shared_core','shared_fbd','pugixml','cache','session_check','logic','editor_model','control','generation','generation_strategy','export_st','export_fbd','artifact_st','artifact_fbd','artifact_verify','cli','contracts','ir','target','package','package_search','candidates','synth_basic','synth_cover','synth_advanced') | ForEach-Object {Join-Path $build ($_+'.obj')}
    $resourceDefines=if($Unified){@('/d','UNIFIED_EDITOR')}else{@()}
    & (Join-Path $Bds 'bin/rc.exe') @resourceDefines '/i' $PSScriptRoot '/fo' (Join-Path $build 'app.res') (Join-Path $PSScriptRoot 'app.rc')
    if($LASTEXITCODE -ne 0){throw 'Resource compilation failed.'}
    if($Unified){
        $objects=@($sources.Keys | ForEach-Object {Join-Path $build ($_+'.obj')})
        & $compiler -tW -tM -tV ('-o'+(Join-Path $dist 'OWL-Tables.exe')) @objects (Join-Path $build 'app.res') vcl.lib rtl.lib
        if($LASTEXITCODE){throw 'Workbench link failed.'}
        Copy-Item -LiteralPath (Join-Path $repo 'assets/Template.xlsx'),(Join-Path $repo 'LICENSE'),(Join-Path $repo 'unified/THIRD_PARTY_NOTICES.md'),(Join-Path $repo 'README.md') -Destination $dist
        $guides = Join-Path $dist 'docs'
        New-Item -ItemType Directory -Force $guides | Out-Null
        foreach($guide in @('EDITOR.md','BUILD_UNIFIED.md','MACRO_IMPORT.md')){
            Copy-Item -LiteralPath (Join-Path $repo "docs/$guide") -Destination $guides
        }
        $workbooks=Join-Path $dist 'examples'
        New-Item -ItemType Directory -Force $workbooks | Out-Null
        Copy-Item (Join-Path $repo 'examples/*') -Destination $workbooks -Recurse -Force
        New-Item -ItemType Directory -Force (Join-Path $dist 'licenses') | Out-Null
        Copy-Item (Join-Path $repo 'cpp/vendor/*LICENSE*') (Join-Path $dist 'licenses')
        Write-Host "Built $dist";return
    }
    & $compiler -tC -tM -tJ ('-o'+(Join-Path $dist 'Boolean2FBD-cli.exe')) @common (Join-Path $build 'console_main.obj') (Join-Path $build 'app.res') rtl.lib
    if($LASTEXITCODE -ne 0){throw 'CLI link failed.'}
    if(-not $CliOnly){
        & $compiler -tW -tM -tV ('-o'+(Join-Path $dist 'Boolean2FBD.exe')) @common (Join-Path $build 'window.obj') (Join-Path $build 'gui_main.obj') (Join-Path $build 'app.res') vcl.lib rtl.lib
        if($LASTEXITCODE -ne 0){throw 'GUI link failed.'}
    }
    if($Development){Write-Host "Built development EXEs: $dist";return}
    Copy-Item -LiteralPath (Join-Path $repo 'LICENSE'),(Join-Path $repo 'THIRD_PARTY_NOTICES.md') -Destination $dist
    New-Item -ItemType Directory -Force (Join-Path $dist 'licenses') | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'cpp/vendor/json-LICENSE.txt'),(Join-Path $repo 'cpp/vendor/pugixml-LICENSE.txt') -Destination (Join-Path $dist 'licenses')
    if(Test-Path -LiteralPath (Join-Path $repo 'docs/BOOLEAN.md')){Copy-Item -LiteralPath (Join-Path $repo 'docs/BOOLEAN.md') -Destination (Join-Path $dist 'README.md')}
    Copy-Item -LiteralPath (Join-Path $repo 'docs/BOOLEAN_STRATEGY_CLI.md') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $repo 'docs/BOOLEAN_RELEASING.md') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'engine/policy.json') -Destination (Join-Path $dist 'policy.json')
    $example=Join-Path $build 'example.json'
    [IO.File]::WriteAllText($example,'{"inputs":["I1","I2","I3","I4","I5"],"output":"Q","inverted":"NQ","rules":[{"inputs":["X","X","X","1","1"],"q":"1","comment":"Q = I4 AND I5"}]}',[Text.UTF8Encoding]::new($false))
    $cli=Join-Path $dist 'Boolean2FBD-cli.exe'
    & $cli --save-xlsx $example --output (Join-Path $dist 'Boolean.xlsx')
    if($LASTEXITCODE -ne 0){throw 'Example workbook generation failed.'}
    & $cli --generate (Join-Path $dist 'Boolean.xlsx') --output (Join-Path $dist 'examples/Boolean') --report (Join-Path $dist 'examples/Boolean.report.json') --name Boolean | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Example macro generation failed.'}
    $mask=[uint32]0x96E8A57BL
    $rows=@(for($key=0;$key -lt 32;$key++){
        @{inputs=@(for($bit=0;$bit -lt 5;$bit++){[string](($key -shr $bit) -band 1)});q=[string](($mask -shr $key) -band 1);comment=''}
    })
    $exampleMask=Join-Path $build 'example-mask.json'
    $maskState=@{inputs=@('I1','I2','I3','I4','I5');output='Q';inverted='NQ';rules=$rows}
    [IO.File]::WriteAllText($exampleMask,($maskState | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    & $cli --save-xlsx $exampleMask --output (Join-Path $dist 'Boolean-mask.xlsx')
    if($LASTEXITCODE -ne 0){throw 'Mask workbook generation failed.'}
    & $cli --generate $exampleMask --output (Join-Path $dist 'examples/Boolean-mask') --report (Join-Path $dist 'examples/Boolean-mask.report.json') --name BooleanMask | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Mask macro generation failed.'}
    Write-Host "Built: $dist"
} finally {Pop-Location}
