# Builds "Rave It Out Audio Sync.exe" (C# WinForms on .NET Framework 4.8, which Windows 10/11 already have).
#   .\build.ps1                 -> ..\..\Rave It Out Audio Sync.exe (sm-rio root, next to Program\ and Save\)
#   .\build.ps1 -OutDir <dir>   -> another folder (e.g. a release package)
#   .\build.ps1 -Test           -> also builds AudioSyncTests.exe and runs it on a copy of Save\Preferences.ini
param([string]$OutDir = "", [switch]$Test)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here "..\..")).Path
if (-not $OutDir) { $OutDir = $root }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# Roslyn csc from Visual Studio / Build Tools (any edition with MSBuild)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$csc = $null
if (Test-Path $vswhere) {
    $csc = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\Roslyn\csc.exe" | Select-Object -First 1
}
if (-not $csc) { throw "csc.exe not found: install Visual Studio 2022 Build Tools (MSBuild)." }

$fw = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
$refs = "System.dll", "System.Core.dll", "System.Drawing.dll", "System.Windows.Forms.dll" | ForEach-Object { "/r:$fw\$_" }

# Icon: wrap a 256px PNG of ../../../audio-sync/Icon/icon_1024.png in an .ico (Vista+ PNG icon)
$png = Join-Path $root "..\audio-sync\Icon\icon_1024.png"
$ico = Join-Path $here "AppIcon.ico"
if (-not (Test-Path $ico) -and (Test-Path $png)) {
    Add-Type -AssemblyName System.Drawing
    $ms = New-Object System.IO.MemoryStream
    $pngs = @()
    foreach ($s in 256, 48, 32, 16) {
        $src = [System.Drawing.Image]::FromFile((Resolve-Path $png).Path)
        $bmp = New-Object System.Drawing.Bitmap $s, $s
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = "HighQualityBicubic"
        $g.DrawImage($src, 0, 0, $s, $s)
        $g.Dispose(); $src.Dispose()
        $m = New-Object System.IO.MemoryStream
        $bmp.Save($m, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $pngs += , @($s, $m.ToArray())
    }
    $w = New-Object System.IO.BinaryWriter $ms
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$pngs.Count)
    $offset = 6 + 16 * $pngs.Count
    foreach ($p in $pngs) {
        $dim = if ($p[0] -ge 256) { 0 } else { $p[0] }
        $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$p[1].Length); $w.Write([uint32]$offset)
        $offset += $p[1].Length
    }
    foreach ($p in $pngs) { $w.Write($p[1]) }
    [System.IO.File]::WriteAllBytes($ico, $ms.ToArray())
}
$iconArg = if (Test-Path $ico) { @("/win32icon:$ico") } else { @() }

$exe = Join-Path $OutDir "Rave It Out Audio Sync.exe"
& $csc /nologo /noconfig /target:winexe /platform:anycpu /optimize+ /langversion:latest `
    $refs $iconArg "/win32manifest:$here\app.manifest" "/out:$exe" "$here\AudioSync.cs"
if ($LASTEXITCODE) { throw "build failed" }
Write-Host "Built $exe"

if ($Test) {
    $work = Join-Path $env:TEMP "rio-audiosync-test"
    $t = Join-Path $work "AudioSyncTests.exe"
    New-Item -ItemType Directory -Force $work | Out-Null
    & $csc /nologo /noconfig /target:exe /define:TESTING /langversion:latest $refs "/out:$t" "$here\AudioSync.cs" "$here\Tests.cs"
    if ($LASTEXITCODE) { throw "test build failed" }
    $targs = @((Join-Path $root "Save\Preferences.ini"), $work)
    $json = Join-Path $root "Save\AudioSyncProfiles.json"
    if (Test-Path $json) { $targs += $json }
    & $t @targs
    if ($LASTEXITCODE) { throw "tests failed" }
}
