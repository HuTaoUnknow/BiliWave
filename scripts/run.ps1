param([switch]$Development)
$project=Split-Path $PSScriptRoot -Parent
$release=Join-Path $project 'release\BiliWave.exe'
if((Test-Path -LiteralPath $release) -and -not $Development){
    Start-Process -FilePath $release -WorkingDirectory (Split-Path $release -Parent) -WindowStyle Hidden
} else {
    $env:Path='D:\PC\EXE\QtCreator\6.11.1\mingw_64\bin;D:\PC\EXE\QtCreator\Tools\mingw1310_64\bin;'+$env:Path
    Start-Process -FilePath (Join-Path $project 'build\BiliWave.exe') -WorkingDirectory $project -WindowStyle Hidden
}
