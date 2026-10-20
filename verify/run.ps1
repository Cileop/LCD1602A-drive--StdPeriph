# =============================================================================
#  verify/run.ps1  --  one-command re-run of the host-side LCD1602A verification
#
#  Compiles the REAL driver sources (unmodified) as C++ against the fake
#  peripheral headers in verify/fake/, links them with verify/src/harness.cpp,
#  and runs:
#
#    1) the frozen BASELINE (git HEAD, copied into verify/baseline/)   -> expected FAIL
#    2) the CURRENT working-tree driver                               -> expected PASS
#
#  Each run is done with DB3..DB0 "undefined" in 8-bit mode forced LOW and
#  forced HIGH, plus a 40 ms HD44780 power-on-BUSY sensitivity run.  The logs
#  are written to verify/out/.
#
#  Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
#          pwsh -File verify/run.ps1            (PowerShell 7, optional)
#          powershell -File verify\run.ps1 -SkipCurrent
# =============================================================================
param(
    [switch]$SkipBaseline,
    [switch]$SkipCurrent
)

$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$verify = $PSScriptRoot
$outDir = Join-Path $verify 'out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# ---- locate g++ -------------------------------------------------------------
$gpp = 'C:\MinGWcui\MinGW\bin\g++.exe'
if (-not (Test-Path $gpp)) {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) { $gpp = $cmd.Source } else { throw "g++ not found (looked in C:\MinGWcui\MinGW\bin and PATH)" }
}
Write-Host "g++ : $gpp"

function Build-Harness([string]$name, [string]$incMokuai, [string]$incSystem, [string[]]$sources) {
    $exe = Join-Path $outDir "harness_$name.exe"
    $gargs = @('-std=c++11', '-O1', '-I', (Join-Path $verify 'fake'), '-I', $incMokuai, '-I', $incSystem,
               '-o', $exe, (Join-Path $verify 'src\harness.cpp')) + $sources
    Write-Host "`n=== build $name ==="
    & $gpp @gargs 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $name"; return $null }
    return $exe
}

# ---- build baseline ---------------------------------------------------------
$baselineExe = $null
if (-not $SkipBaseline) {
    $bm = Join-Path $verify 'baseline\mokuai'
    $bs = Join-Path $verify 'baseline\system'
    $baselineExe = Build-Harness 'baseline' $bm $bs @(
        (Join-Path $bm 'MyPCF8574.c'),
        (Join-Path $bm 'MyLCD1602A.c'),
        (Join-Path $bs 'Delay.c'))
}

# ---- build current working-tree driver --------------------------------------
$currentExe = $null
if (-not $SkipCurrent) {
    $cm = Join-Path $repo 'mokuai'
    $cs = Join-Path $repo 'system'
    $currentExe = Build-Harness 'current' $cm $cs @(
        (Join-Path $cm 'MyPCF8574.c'),
        (Join-Path $cm 'MyLCD1602A.c'),
        (Join-Path $cs 'Delay.c'))
}

# ---- source identity --------------------------------------------------------
$hashLines = @()
$hashLines += "# Source identity captured by verify/run.ps1"
$hashLines += "# time: $(Get-Date -Format o)"
$hashLines += ("# baseline: git HEAD blob of each file (frozen copies in verify/baseline/)")
$baselineMap = @(
    @('mokuai/MyPCF8574.c','MyPCF8574.c'), @('mokuai/MyPCF8574.h','MyPCF8574.h'),
    @('mokuai/MyLCD1602A.c','MyLCD1602A.c'), @('mokuai/MyLCD1602A.h','MyLCD1602A.h'),
    @('system/Delay.c','Delay.c'), @('system/Delay.h','Delay.h'))
foreach ($pair in $baselineMap) {
    $p = Join-Path $verify ("baseline\" + ($pair[0] -replace '/', '\'))
    if (Test-Path $p) {
        $h = (Get-FileHash $p -Algorithm SHA256).Hash.ToLower()
        $hashLines += "baseline/$($pair[0])  sha256=$h"
    }
}
$currentMap = @('mokuai/MyPCF8574.c','mokuai/MyPCF8574.h','mokuai/MyLCD1602A.c',
                'mokuai/MyLCD1602A.h','system/Delay.c','system/Delay.h','User/main.c')
foreach ($rel in $currentMap) {
    $p = Join-Path $repo ($rel -replace '/', '\')
    if (Test-Path $p) {
        $h = (Get-FileHash $p -Algorithm SHA256).Hash.ToLower()
        $hashLines += "current/$rel  sha256=$h"
    }
}
$hashLines | Set-Content -Encoding utf8 (Join-Path $outDir 'source-hashes.txt')

# ---- run matrix -------------------------------------------------------------
$runs = @()
if ($baselineExe) {
    $runs += @{ Exe = $baselineExe; Label = 'baseline_undef0x0'; Undef = '0x00'; PowerOn = '0' }
    $runs += @{ Exe = $baselineExe; Label = 'baseline_undef0xF'; Undef = '0x0F'; PowerOn = '0' }
    $runs += @{ Exe = $baselineExe; Label = 'baseline_undef0x0_poweron40ms'; Undef = '0x00'; PowerOn = '40000' }
}
if ($currentExe) {
    $runs += @{ Exe = $currentExe; Label = 'current_undef0x0'; Undef = '0x00'; PowerOn = '0' }
    $runs += @{ Exe = $currentExe; Label = 'current_undef0xF'; Undef = '0x0F'; PowerOn = '0' }
    $runs += @{ Exe = $currentExe; Label = 'current_undef0x0_poweron40ms'; Undef = '0x00'; PowerOn = '40000' }
}

$summary = @()
foreach ($r in $runs) {
    $log = Join-Path $outDir ($r.Label + '.log')
    Write-Host "`n=== run $($r.Label) ==="
    & $r.Exe --label $r.Label --undef $r.Undef --power-on-busy-us $r.PowerOn *> $log
    $code = $LASTEXITCODE
    $result = (Select-String -Path $log -Pattern '^RESULT=' | Select-Object -Last 1).Line
    $mode   = (Select-String -Path $log -Pattern '^LCD_MODE_FINAL=' | Select-Object -Last 1).Line
    $row1   = (Select-String -Path $log -Pattern '^DDRAM_ROW1_TRIMMED=' | Select-Object -Last 1).Line
    $viol   = (Select-String -Path $log -Pattern '^VIOLATION_COUNT=' | Select-Object -Last 1).Line
    $cmd28  = (Select-String -Path $log -Pattern '^HAS_INSTR_0x28=' | Select-Object -Last 1).Line
    $e      = (Select-String -Path $log -Pattern '^E_PULSES=' | Select-Object -Last 1).Line
    $clear  = (Select-String -Path $log -Pattern '^CLEAR_FOUND=' | Select-Object -Last 1).Line
    $summary += [pscustomobject]@{
        run = $r.Label; exit = $code; result = $result; mode = $mode;
        row1 = $row1; violations = $viol; cmd28 = $cmd28; epulse = $e; clear = $clear
    }
    Write-Host ("  {0}  {1}  {2}  {3}" -f $result, $mode, $row1, $viol)
}

Write-Host "`n===================== ALL RUNS ====================="
$summary | Format-Table -AutoSize -Wrap | Out-String -Width 220 | Write-Host
Write-Host "Logs: $outDir"
Write-Host "Baseline is EXPECTED to be FAIL; current is expected to be PASS."
exit 0
