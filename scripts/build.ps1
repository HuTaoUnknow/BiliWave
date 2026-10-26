param([string]$QtRoot='D:\PC\EXE\QtCreator\6.11.1\mingw_64',[string]$QtTools='D:\PC\EXE\QtCreator\Tools',[switch]$Deploy)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$env:Path="$QtTools\mingw1310_64\bin;$QtRoot\bin;"+$env:Path
$cmake=Join-Path $QtTools 'CMake_64\bin\cmake.exe'
& $cmake -S $project -B "$project\build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBILI_ENABLE_LYRICS=ON -DBILI_ENABLE_RECOGNITION=ON -DBILI_ENABLE_ALIGNMENT=OFF
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build "$project\build" -j 4
if($LASTEXITCODE){throw 'Build failed'}
& "$QtTools\CMake_64\bin\ctest.exe" --test-dir "$project\build" --output-on-failure --output-junit "$project\docs\test-results.xml"
if($LASTEXITCODE){Get-Content "$project\build\core-results.txt","$project\build\integration-results.txt","$project\build\redesign-results.txt","$project\build\lyric-workflow-results.txt" -ErrorAction SilentlyContinue; throw 'Tests failed'}
if($Deploy){
    & "$PSScriptRoot\deploy-screen.ps1" -QtRoot $QtRoot
}
