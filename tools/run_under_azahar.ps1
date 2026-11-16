param(
    [Parameter(Mandatory=$true)][string]$Emulator,
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [int]$TimeoutSeconds = 45,
    [Alias('Floor')][int[]]$Floors = (@(0x2f..0x37) + @(0x39..0x40) + @(0x79,0x7a,0x7d,0x7f,0x8a,0x8b,0xd3,0xd4,0xd5)),
    [string]$Save
)
$ErrorActionPreference = 'Stop'
# Portable SD containment is mandatory; stop only the PID launched below.
$emuPath = (Resolve-Path -LiteralPath $Emulator).Path
$emuDirectory = Split-Path -Parent $emuPath
$portableSd = Join-Path $emuDirectory 'user/sdmc'
$rootPath = (Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\','/')
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $portableSd '3ds/Colosseum3DS')).TrimEnd('\','/')
if (!(Test-Path -LiteralPath $portableSd -PathType Container) -or $rootPath -ine $expectedRoot) {
    throw 'GameRoot must be the emulator portable user/sdmc/3ds/Colosseum3DS directory.'
}
if ((Split-Path -Leaf $emuDirectory) -notlike 'lane-*') { throw 'Use an isolated lane-* portable emulator copy.' }
foreach ($part in @($emuDirectory, (Join-Path $emuDirectory 'user'), $portableSd, $rootPath)) {
    if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Portable path must not redirect: $part" }
}
$binary = Join-Path $rootPath 'ColosseumRT.3dsx'
$binaryHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
$saveTarget = Join-Path $rootPath 'probe-save.gci'
if ($Save) {
    $saveSource = (Resolve-Path -LiteralPath $Save).Path
    if ($saveSource -ine $saveTarget) { Copy-Item -LiteralPath $saveSource -Destination $saveTarget }
}
$saveHash = if (Test-Path -LiteralPath $saveTarget) { (Get-FileHash -LiteralPath $saveTarget -Algorithm SHA256).Hash } else { $null }
$evidencePath = [IO.Path]::GetFullPath($Evidence)
New-Item -ItemType Directory -Force -Path $evidencePath | Out-Null
function Test-Frame([string]$Path, [int]$Width) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 54 -or $bytes[0] -ne 66 -or $bytes[1] -ne 77) { return $false }
        $offset = [BitConverter]::ToUInt32($bytes,10)
        $w = [BitConverter]::ToInt32($bytes,18)
        $h = [Math]::Abs([BitConverter]::ToInt32($bytes,22))
        $bits = [BitConverter]::ToUInt16($bytes,28)
        return $w -eq $Width -and $h -eq 240 -and $bits -in @(24,32) -and $offset -ge 54 -and $bytes.Length -ge ($offset + ([Math]::Floor(($w * $bits + 31)/32)*4)*$h)
    } catch { return $false }
}
$results = @()
foreach ($floor in $Floors) {
    $hex = '{0:x3}' -f $floor
    $trace = Join-Path $rootPath 'boot-trace.log'
    $top = Join-Path $rootPath "probe/area_${hex}_top.bmp"
    $bottom = Join-Path $rootPath "probe/area_${hex}_bottom.bmp"
    $process = $null
    $captured = $false
    $errors = @()
    $started = [DateTime]::UtcNow
    try {
        foreach ($old in @($trace,$top,$bottom,(Join-Path $evidencePath "$hex.log"),(Join-Path $evidencePath "${hex}_top.bmp"),(Join-Path $evidencePath "${hex}_bottom.bmp"))) {
            if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old }
        }
        [IO.File]::WriteAllText((Join-Path $rootPath 'probe-route.txt'), "start $hex`n")
        $process = Start-Process -FilePath $emuPath -WorkingDirectory $emuDirectory -ArgumentList ('"' + $binary + '"') -WindowStyle Hidden -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 500
            if ((Test-Frame $top 400) -and (Test-Frame $bottom 320) -and (Test-Path -LiteralPath $trace)) {
                $log = [IO.File]::ReadAllText($trace)
                if ($log -match "story_prepared floor=$floor(?:\s|$)" -and $log -match "field_tick floor=$floor\s[^\r\n]*control=1(?:\s|$)") { $captured = $true; break }
            }
            if ($process.HasExited) { break }
        }
        if (!$captured) { $errors += 'Missing current valid top/bottom BMP or matching story_prepared/control trace before timeout/process exit.' }
    } catch { $errors += $_.Exception.Message }
    finally {
        if ($process) {
            if (!$process.HasExited) { Stop-Process -Id $process.Id }
            $process.WaitForExit()
        }
    }
    if (Test-Path -LiteralPath $trace) { Copy-Item -LiteralPath $trace -Destination (Join-Path $evidencePath "$hex.log") }
    foreach ($screen in @('top','bottom')) {
        $frame = Join-Path $rootPath "probe/area_${hex}_${screen}.bmp"
        if (Test-Path -LiteralPath $frame) { Copy-Item -LiteralPath $frame -Destination (Join-Path $evidencePath "${hex}_${screen}.bmp") }
    }
    $results += [PSCustomObject]@{ floor=$hex; captured=$captured; pid=$(if ($process) {$process.Id} else {$null}); started_utc=$started.ToString('o'); binary_sha256=$binaryHash; save_sha256=$saveHash; errors=$errors }
    $json = ConvertTo-Json -InputObject @($results) -Depth 5
    [IO.File]::WriteAllText((Join-Path $evidencePath 'loads.json'), $json + "`n")
    Write-Output "under_azahar floor=$hex captured=$captured"
}
if (@($results | Where-Object { !$_.captured }).Count) { exit 1 }
