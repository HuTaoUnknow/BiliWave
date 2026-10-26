param([string]$QtRoot='D:\PC\EXE\QtCreator\6.11.1\mingw_64',[string]$QtTools='D:\PC\EXE\QtCreator\Tools')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$debugBuild=Join-Path $project 'build-debug'
$env:Path="$QtTools\mingw1310_64\bin;$QtRoot\bin;"+$env:Path
$cmake=Join-Path $QtTools 'CMake_64\bin\cmake.exe'
& $cmake -S $project -B $debugBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Debug -DBILI_ENABLE_LYRICS=ON -DBILI_ENABLE_RECOGNITION=ON -DBILI_ENABLE_ALIGNMENT=OFF -DBUILD_TESTING=OFF
if($LASTEXITCODE){throw 'Debug configure failed'}
& $cmake --build $debugBuild --target BiliWave -j 4
if($LASTEXITCODE){throw 'Debug build failed'}
# This installed Qt kit provides RelWithDebInfo libraries. Deploy its matching
# release-named runtime DLLs; the application itself remains a Debug build.
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --compiler-runtime "$debugBuild\BiliWave.exe"
if($LASTEXITCODE){throw 'Debug runtime deployment failed'}
