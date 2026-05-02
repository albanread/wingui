param(
    [Parameter(Position = 0)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$Run
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $repoRoot

$outputDir = Join-Path $repoRoot 'manual_build\out'
$exePath = Join-Path $outputDir 'wingui_demo_zig_spec_bind.exe'
$repoExePath = Join-Path $repoRoot 'demo_zig_spec_bind.exe'
$repoPdbPath = Join-Path $repoRoot 'demo_zig_spec_bind.pdb'
$outPdbPath = Join-Path $outputDir 'wingui_demo_zig_spec_bind.pdb'

if (-not (Test-Path -LiteralPath (Join-Path $outputDir 'wingui.lib') -PathType Leaf)) {
    throw "manual_build\out\wingui.lib was not found. Build any packaged Wingui demo first, for example: powershell -NoProfile -ExecutionPolicy Bypass -File .\build_demo_impl.ps1 demo_c_spec_bind.c Release"
}

if (-not (Test-Path -LiteralPath (Join-Path $outputDir 'shaders') -PathType Container)) {
    throw "manual_build\out\shaders was not found. Build any packaged Wingui demo first so the runtime assets are copied beside the DLL."
}

$zigArgs = @(
    'build-exe',
    'src/demo_zig_spec_bind.zig',
    'app_manifest.rc',
    '-I', 'include',
    '-L', $outputDir,
    '-lwingui',
    '-luser32',
    '-lc',
    '--subsystem', 'windows',
    '-O', $(if ($Configuration -eq 'Release') { 'ReleaseFast' } else { 'Debug' })
)

Remove-Item -LiteralPath $repoExePath -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $repoPdbPath -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $exePath -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $outPdbPath -ErrorAction SilentlyContinue

Write-Host '==> Building wingui_demo_zig_spec_bind.exe'
Write-Host ('zig ' + ($zigArgs -join ' '))
& zig @zigArgs
if ($LASTEXITCODE -ne 0) {
    throw 'Zig build failed.'
}

if (-not (Test-Path -LiteralPath $repoExePath -PathType Leaf)) {
    throw "Expected Zig to emit $repoExePath but it was not created."
}

Move-Item -LiteralPath $repoExePath -Destination $exePath -Force
if (Test-Path -LiteralPath $repoPdbPath -PathType Leaf) {
    Move-Item -LiteralPath $repoPdbPath -Destination $outPdbPath -Force
}

Write-Host "Built EXE : $exePath"

if ($Run) {
    $env:Path = "$outputDir;$env:Path"
    Write-Host "==> Running $exePath"
    & $exePath
    exit $LASTEXITCODE
}