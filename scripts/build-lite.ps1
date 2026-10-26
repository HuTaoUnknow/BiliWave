param([string]$QtRoot='D:\PC\EXE\QtCreator\6.11.1\mingw_64', [string]$QtTools='D:\PC\EXE\QtCreator\Tools')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$env:PATH = "$QtTools\mingw1310_64\bin;$QtRoot\bin;" + $env:PATH
$cmake = Join-Path $QtTools 'CMake_64\bin\cmake.exe'
$build = Join-Path $project 'build-lite'
$release = Join-Path $project 'release-lite'
& $cmake -S $project -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBILI_ENABLE_LYRICS=ON -DBILI_ENABLE_RECOGNITION=OFF -DBILI_ENABLE_ALIGNMENT=OFF
if ($LASTEXITCODE) { throw 'Lite configure failed' }
& $cmake --build $build -j 4
if ($LASTEXITCODE) { throw 'Lite build failed' }
& "$QtTools\CMake_64\bin\ctest.exe" --test-dir $build --output-on-failure --output-junit "$project\docs\lite-test-results.xml"
if ($LASTEXITCODE) { Get-Content "$build\lite-results.txt" -ErrorAction SilentlyContinue; throw 'Lite tests failed' }
New-Item -ItemType Directory -Force -Path $release | Out-Null
Copy-Item -LiteralPath "$build\BiliWave.exe" -Destination $release -Force
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --compiler-runtime "$release\BiliWave.exe"
if ($LASTEXITCODE) { throw 'Lite Qt deployment failed' }
$toolDirectory = Join-Path $release 'tools'
New-Item -ItemType Directory -Force -Path $toolDirectory | Out-Null
foreach ($tool in @('yt-dlp.exe','ffmpeg.exe','ffprobe.exe')) {
    Copy-Item -LiteralPath (Join-Path $project "tools\$tool") -Destination $toolDirectory -Force
}
$licenses = Join-Path $release 'docs\licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -LiteralPath "$project\docs\LITE.md" -Destination "$release\README.md" -Force
Copy-Item -LiteralPath "$project\THIRD_PARTY_NOTICES.md" -Destination $release -Force
& robocopy.exe "$project\docs\licenses" $licenses /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XJ
if ($LASTEXITCODE -gt 7) { throw 'Lite license deployment failed' }
foreach ($forbidden in @('models','workers','tools\lyrics-runtime','tools\ocr-gpu','tools\whisper-cli.exe')) {
    if (Test-Path -LiteralPath (Join-Path $release $forbidden)) { throw "Forbidden Lite payload exists: $forbidden" }
}
$global:LASTEXITCODE = 0
