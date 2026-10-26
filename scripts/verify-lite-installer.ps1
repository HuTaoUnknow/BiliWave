param([string]$Installer = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (-not $Installer) { $Installer = Join-Path $project 'dist\BiliWave-Lite-1.11.0-Windows-x64-Setup.exe' }
$Installer = [IO.Path]::GetFullPath($Installer)
$verificationRoot = Join-Path $project ('.cache\ivl110-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$destination = Join-Path $verificationRoot 'App'
if (Test-Path -LiteralPath $destination) { throw 'Verification destination already exists; use a fresh verification directory.' }
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
function Invoke-CheckedProcess([string]$File, [string[]]$Arguments, [int]$Timeout = 180000) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -WorkingDirectory $verificationRoot -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($Timeout)) { throw "Verification process timed out: $File" }
    if ($process.ExitCode -ne 0) { throw "Verification process failed ($($process.ExitCode)): $File" }
}
$installLog = Join-Path $verificationRoot 'install.log'
Invoke-CheckedProcess $Installer @('/VERYSILENT','/SUPPRESSMSGBOXES','/SP-','/NORESTART','/VERIFYINSTALL=1',
    '/LANG=chinesesimplified',("/DIR=`"$destination`""),("/LOG=`"$installLog`""))
$installedExe = Join-Path $destination 'BiliWave.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'release-lite\BiliWave.exe')).Hash) {
    throw 'Installed application hash mismatch.'
}
foreach ($forbidden in @('models','workers','tools\lyrics-runtime','tools\ocr-gpu','tools\whisper-cli.exe')) {
    if (Test-Path -LiteralPath (Join-Path $destination $forbidden)) { throw "Forbidden Lite payload: $forbidden" }
}
$payloadBytes = (Get-ChildItem -LiteralPath $destination -File -Recurse | Measure-Object -Property Length -Sum).Sum
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Invoke-CheckedProcess $installedExe @('--capture',("`"$(Join-Path $verificationRoot 'screenshots')`""),'--capture-delay','800') 20000
Write-Output 'Lite installed and started with system-only PATH; lyric payload is absent.'
$fixture = Join-Path $verificationRoot 'test audio.flac'
Invoke-CheckedProcess (Join-Path $destination 'tools\ffmpeg.exe') @('-hide_banner','-loglevel','error','-y','-f','lavfi','-i','sine=frequency=440:duration=12','-c:a','flac',("`"$fixture`""))
$smokePath = Join-Path $verificationRoot 'playback.json'
Invoke-CheckedProcess $installedExe @('--import',("`"$fixture`""),'--smoke',("`"$smokePath`"")) 30000
$smoke = Get-Content -LiteralPath $smokePath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $smoke.decoded -or -not $smoke.seeked -or $smoke.tracks -ne 1) { throw 'Installed Lite playback check failed.' }
Write-Output 'Installed Lite directly imported, decoded and seeked the local track.'
$data = Join-Path $destination 'data'
$sentinel = Join-Path $data 'installer-test-user-data.txt'
Set-Content -LiteralPath $sentinel -Value 'Preserve user data on uninstall.' -Encoding utf8
Invoke-CheckedProcess (Join-Path $destination 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstaller did not remove the installed executable.' }
if (-not (Test-Path -LiteralPath $sentinel)) { throw 'Uninstaller removed user data.' }
$report = [ordered]@{
    version = '1.11.0'; edition = 'Lite'; installer = $Installer
    installer_bytes = (Get-Item -LiteralPath $Installer).Length; installed_bytes = $payloadBytes
    sha256 = (Get-FileHash -LiteralPath $Installer).Hash.ToLowerInvariant()
    installed = $true; application_startup = $true; system_only_path = $true; lyric_payload_absent = $true
    direct_library_import = $true; decoded = $smoke.decoded; seeked = $smoke.seeked
    uninstall = $true; user_data_preserved = $true
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project 'docs\lite-installer-validation.json') -Encoding utf8
Write-Output 'Lite installer verification completed; uninstall preserved user data.'
