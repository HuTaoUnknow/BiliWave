param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (-not $Compiler) { $Compiler = Join-Path $project '.cache\installer-tools\inno\ISCC.exe' }
$runId = [guid]::NewGuid().ToString('N')
$root = Join-Path $project ('.cache\install-choice-' + $runId)
$registryPath = 'Software\BiliWave\InstallerTests\' + $runId
$registryRoot = $registryPath + '\Uninstall'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$results = @()

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Register-Fixture([string]$Id, [string]$Directory, [string]$Name) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($registryRoot + '\' + $Id + '_is1')
    try {
        $key.SetValue('InstallLocation', $Directory)
        $key.SetValue('DisplayVersion', '1.11.0')
        $key.SetValue('BiliWaveInstanceName', $Name)
    } finally { $key.Dispose() }
}
function Read-Plan([string]$File) {
    $plan = @{}
    foreach ($line in Get-Content -LiteralPath $File) {
        if ($line -match '^([^=]+)=(.*)$') { $plan[$matches[1]] = $matches[2] }
    }
    return $plan
}
function Invoke-Setup([string]$Installer, [string]$Name, [string]$Target, [string[]]$Extra, [bool]$Fail = $false) {
    $log = Join-Path $root ($Name + '.log')
    $plan = Join-Path $root ($Name + '.ini')
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/SP-','/NORESTART','/VERIFYINSTALL=1',
        '/LANG=chinesesimplified',('/DIR="' + $Target + '"'),('/LOG="' + $log + '"'),
        ('/TESTREGISTRYROOT="' + $registryRoot + '"'),('/TESTSIDEBASE="' + $sideBase + '"'),('/TESTPLAN="' + $plan + '"')) + $Extra
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw ('Test installer timed out: ' + $Name) }
    if ($Fail) {
        Assert-True ($process.ExitCode -ne 0) ($Name + ' should have refused installation')
    } else {
        Assert-True ($process.ExitCode -eq 0) ($Name + ' failed; see ' + $log)
        Assert-True (Test-Path -LiteralPath $plan) ($Name + ' did not save an installation plan')
        return Read-Plan $plan
    }
}

try {
    foreach ($lite in @(0,1)) {
        $name = if ($lite) { 'BiliWave Lite' } else { 'BiliWave' }
        $id = if ($lite) { '{EE37A2DA-4D0B-4BCD-97E3-3B3B6141F838}' } else { '{B1A2A5B4-8F18-449F-8E87-DCB27977DDE2}' }
        $otherId = if ($lite) { '{B1A2A5B4-8F18-449F-8E87-DCB27977DDE2}' } else { '{EE37A2DA-4D0B-4BCD-97E3-3B3B6141F838}' }
        $tag = if ($lite) { 'lite' } else { 'screen' }
        $editionRoot = Join-Path $root $tag
        $payload = Join-Path $editionRoot 'payload'
        $output = Join-Path $editionRoot 'test-installer'
        $production = Join-Path $editionRoot 'production-compile'
        $sideBase = Join-Path $editionRoot 'side-by-side'
        New-Item -ItemType Directory -Force -Path $payload,$output,$production,$sideBase | Out-Null
        Copy-Item -LiteralPath (Join-Path $project 'build\import_fixture.exe') -Destination (Join-Path $payload 'BiliWave.exe')
        Set-Content -LiteralPath (Join-Path $payload 'version.txt') -Value 'new payload'
        $prefix = if ($lite) { 'BiliWave-Lite' } else { 'BiliWave' }
        foreach ($test in @(0,1)) {
            $destination = if ($test) { $output } else { $production }
            $compileLog = Join-Path $editionRoot ('compile-' + $test + '.log')
            & $Compiler '/DAppVersion=9.9.9' "/DLiteEdition=$lite" "/DUpgradeTestMode=$test" "/DPayloadDir=$payload" "/DPackageOutputDir=$destination" (Join-Path $project 'packaging\BiliWave.iss') *> $compileLog
            Assert-True ($LASTEXITCODE -eq 0) ('Installer compilation failed; see ' + $compileLog)
        }
        $installer = Join-Path $output ($prefix + '-9.9.9-Windows-x64-Setup.exe')
        $fresh = Join-Path $editionRoot 'fresh'
        $plan = Invoke-Setup $installer ($tag+'-fresh') $fresh @()
        Assert-True ($plan.Mode -eq 'fresh' -and $plan.AppId -eq $id -and $plan.Detected -eq '0') 'Fresh installation identity mismatch'
        $old = Join-Path $editionRoot 'old'
        $oldSide = Join-Path $editionRoot 'old-side'
        $other = Join-Path $editionRoot 'other-edition'
        foreach ($dir in @($old,$oldSide,$other)) {
            New-Item -ItemType Directory -Force -Path (Join-Path $dir 'data') | Out-Null
            Set-Content -LiteralPath (Join-Path $dir 'BiliWave.exe') -Value 'old application fixture'
            Set-Content -LiteralPath (Join-Path $dir 'data\library.sqlite') -Value 'synthetic library sentinel'
            Set-Content -LiteralPath (Join-Path $dir 'data\settings.ini') -Value 'synthetic settings sentinel'
        }
        Register-Fixture $id $old $name
        Register-Fixture ($id+'-side-9.9.9-1') $oldSide ($name+' old side')
        Register-Fixture $otherId $other 'Other edition'
        $beforeData = (Get-FileHash -LiteralPath (Join-Path $old 'data\library.sqlite')).Hash
        $beforeSettings = (Get-FileHash -LiteralPath (Join-Path $old 'data\settings.ini')).Hash
        $oldSideHash = (Get-FileHash -LiteralPath (Join-Path $oldSide 'BiliWave.exe')).Hash
        Invoke-Setup $installer ($tag+'-no-choice') (Join-Path $editionRoot 'no-choice') @() $true
        Invoke-Setup $installer ($tag+'-ambiguous') $old @('/INSTALLMODE=upgrade') $true
        Invoke-Setup $installer ($tag+'-wrong-edition') $other @('/INSTALLMODE=upgrade',('/UPGRADETARGET="'+$other+'"')) $true
        $plan = Invoke-Setup $installer ($tag+'-upgrade') $old @('/INSTALLMODE=upgrade',('/UPGRADETARGET="'+$old+'"'))
        Assert-True ($plan.Mode -eq 'upgrade' -and $plan.AppId -eq $id -and $plan.Detected -eq '2') 'Upgrade detection/identity mismatch'
        Assert-True ([int]$plan.PickerTop -gt [int]$plan.ChoiceBottom -and [int]$plan.PickerBottom -le [int]$plan.SurfaceHeight) 'Installation choice controls overlap or exceed the page'
        Assert-True ($plan.Directory -eq $old -and $plan.Name -eq $name) 'Upgrade changed installation identity or path'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $old 'BiliWave.exe')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $payload 'BiliWave.exe')).Hash) 'Upgrade did not replace application'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $old 'data\library.sqlite')).Hash -eq $beforeData) 'Upgrade modified existing library'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $old 'data\settings.ini')).Hash -eq $beforeSettings) 'Upgrade modified settings'
        New-Item -ItemType Directory -Force -Path (Join-Path $sideBase ($name+' 9.9.9-2')) | Out-Null
        $side = Join-Path $sideBase ($name+' 9.9.9-3')
        $plan = Invoke-Setup $installer ($tag+'-separate') $side @('/INSTALLMODE=separate')
        Assert-True ($plan.Mode -eq 'separate' -and $plan.AppId -eq ($id+'-side-9.9.9-3')) 'Side-by-side identity collision'
        Assert-True ($plan.Name -ne $name -and $plan.Directory -eq $side) 'Side-by-side path/name collision'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $oldSide 'BiliWave.exe')).Hash -eq $oldSideHash) 'Separate installation modified existing side copy'
        Invoke-Setup $installer ($tag+'-side-collision') $old @('/INSTALLMODE=separate') $true
        $nonempty = Join-Path $editionRoot 'nonempty'
        New-Item -ItemType Directory -Force -Path $nonempty | Out-Null
        Set-Content -LiteralPath (Join-Path $nonempty 'data.txt') -Value 'keep existing files'
        Invoke-Setup $installer ($tag+'-nonempty') $nonempty @('/INSTALLMODE=separate') $true
        Register-Fixture $plan.AppId $side $plan.Name
        $plan2 = Invoke-Setup $installer ($tag+'-upgrade-side') $side @('/INSTALLMODE=upgrade',('/UPGRADETARGET="'+$side+'"'))
        Assert-True ($plan2.AppId -eq $plan.AppId -and $plan2.Name -eq $plan.Name) 'Updating a separate copy lost its identity'
        $data = Join-Path $side 'data'
        New-Item -ItemType Directory -Force -Path $data | Out-Null
        $sentinel = Join-Path $data 'preserve.txt'
        Set-Content -LiteralPath $sentinel -Value 'keep after uninstall'
        $uninstaller = Join-Path $side 'unins000.exe'
        $process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -PassThru
        Assert-True ($process.WaitForExit(30000) -and $process.ExitCode -eq 0) 'Test uninstaller failed'
        Assert-True ((Test-Path -LiteralPath $sentinel) -and -not (Test-Path -LiteralPath (Join-Path $side 'BiliWave.exe'))) 'Uninstall did not preserve data or remove application'
        $results += [ordered]@{edition=$tag;production_compile=$true;fresh=$true;discovery=$true;explicit_choice_required=$true;upgrade=$true;library_preserved=$true;settings_preserved=$true;separate_identity=$true;separate_upgrade=$true;directory_collision_rejected=$true;cross_edition_rejected=$true;uninstall_preserves_data=$true;wizard_control_bounds=$true}
        # Reset only this run's fixtures before checking the other edition.
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registryRoot)
    }
    $report = [ordered]@{date=(Get-Date -Format 'yyyy-MM-dd');fixture_payload=$true;real_install_registration_modified=$false;results=$results;interactive_screenshot_verified=$false;interactive_screenshot_note='Computer Use window approval timed out; native wizard control bounds verified through fixture plans.'}
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $project 'docs\installer-choice-validation.json') -Encoding utf8
    Write-Output ('Installer choice verification passed for '+$results.Count+' editions. Fixture: '+$root)
} finally {
    if ($registryPath -notmatch '^Software\\BiliWave\\InstallerTests\\[0-9a-f]{32}$') { throw 'Unexpected test registry cleanup path' }
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registryPath, $false)
}
