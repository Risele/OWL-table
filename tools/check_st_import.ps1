param([Parameter(Mandatory=$true)][string]$Archive,
      [string]$OwenPath='C:/Program Files/Owen/Owen Logic')
$ErrorActionPreference='Stop'
# Проверка чтения моделей и создания блоков без открытия проекта или окна OwenLogic.
$assembly=[Reflection.Assembly]::LoadFrom((Join-Path $OwenPath 'ProgramRelayFBD.dll'))
$repository=$assembly.GetType('ProgramRelayFBD.InfrastructureLayer.Adapters.ST.FunctionBlockTemplateRepository',$true)
$wrapperType=$repository.GetNestedType('FunctionBlockStoreModel',[Reflection.BindingFlags]'NonPublic')
$from=$repository.GetMethod('From',[Reflection.BindingFlags]'NonPublic,Static')
$blockType=$assembly.GetType('ProgramRelayFBD.DomainLayer.ST.FunctionBlock',$true)
$ctor=$blockType.GetConstructors([Reflection.BindingFlags]'NonPublic,Instance') | Where-Object { $_.GetParameters().Count -eq 1 }
$modelType=$assembly.GetType('ProgramRelayFBD.InfrastructureLayer.Adapters.Serialization.StoreModels.ST.FunctionBlockStoreModel',$true)
$zip=[IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Archive))
try {
    $reader=[IO.StreamReader]::new($zip.GetEntry('Template').Open(),[Text.UTF8Encoding]::new($false,$true))
    try { $json=$reader.ReadToEnd() } finally { $reader.Dispose() }
    $documents=[Text.Json.JsonDocument]::Parse($json)
    foreach($document in $documents.RootElement.EnumerateArray()) {
        $model=[Text.Json.JsonSerializer]::Deserialize($document.GetRawText(),$modelType,[Text.Json.JsonSerializerOptions]::new())
        $wrapper=[Activator]::CreateInstance($wrapperType,$true)
        $wrapperType.GetProperty('StoreModel').SetValue($wrapper,$model)
        try {
            $snapshot=$from.Invoke($null,[object[]]@($wrapper))
            $block=$ctor.Invoke([object[]]@($snapshot))
            '{0}: OWEN_BLOCK_CREATED; variables={1}' -f $model.Name,$block.Variables.Count
        } catch { throw "Блок $($model.Name): $($_.Exception.ToString())" }
    }
} finally { $zip.Dispose() }
