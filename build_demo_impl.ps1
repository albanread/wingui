param(
    [Parameter(Position = 0)]
    [string]$DemoSource = "demo.cpp",

    [Parameter(Position = 1)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$Run
)

$ErrorActionPreference = 'Stop'

function Resolve-DemoSourcePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InputPath,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $candidates = @()

    if ([System.IO.Path]::IsPathRooted($InputPath)) {
        $candidates += $InputPath
    } else {
        $candidates += (Join-Path $RepoRoot $InputPath)
        $candidates += (Join-Path (Join-Path $RepoRoot 'src') $InputPath)
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Demo source not found: $InputPath"
}

function Import-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe not found at $vswhere"
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
    }

    $devCmd = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devCmd -PathType Leaf)) {
        throw "VsDevCmd.bat not found at $devCmd"
    }

    $envDump = cmd /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul & set"
    foreach ($line in $envDump) {
        if ($line -match '^(.*?)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

function Invoke-BuildStep {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Write-Host "==> $Description"
    Write-Host ("cl " + ($Arguments -join ' '))
    & cl @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build step failed: $Description"
    }
}

function Copy-ShaderAssets {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$OutputDir
    )

    $sourceDir = Join-Path $RepoRoot 'shaders'
    $targetDir = Join-Path $OutputDir 'shaders'
    if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
        throw "Shader source directory not found: $sourceDir"
    }

    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    Copy-Item -Path (Join-Path $sourceDir '*') -Destination $targetDir -Force
}

function Get-ShaderCompileJobs {
    @(
        @{ Source = 'text_grid.hlsl'; Entry = 'glyph_vertex'; Profile = 'vs_4_0' },
        @{ Source = 'text_grid.hlsl'; Entry = 'glyph_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'graphics.hlsl'; Entry = 'graphics_vertex'; Profile = 'vs_4_0' },
        @{ Source = 'graphics.hlsl'; Entry = 'graphics_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'graphics.hlsl'; Entry = 'rgba_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'sprite.hlsl'; Entry = 'sprite_vertex'; Profile = 'vs_4_0' },
        @{ Source = 'sprite.hlsl'; Entry = 'sprite_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'rgba_blit.hlsl'; Entry = 'rgba_blit_vertex'; Profile = 'vs_4_0' },
        @{ Source = 'rgba_blit.hlsl'; Entry = 'rgba_blit_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'vector.hlsl'; Entry = 'vector_vertex'; Profile = 'vs_4_0' },
        @{ Source = 'vector.hlsl'; Entry = 'vector_fragment'; Profile = 'ps_4_0' },
        @{ Source = 'indexed_fill.hlsl'; Entry = 'indexed_fill_cs'; Profile = 'cs_5_0' },
        @{ Source = 'indexed_fill.hlsl'; Entry = 'indexed_line_cs'; Profile = 'cs_5_0' }
    )
}

function Compile-ShaderAssets {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputDir
    )

    $fxc = Get-Command fxc.exe -ErrorAction SilentlyContinue
    if (-not $fxc) {
        throw 'fxc.exe not found in the MSVC/Windows SDK environment; cannot build packaged precompiled shaders.'
    }

    $shaderDir = Join-Path $OutputDir 'shaders'
    foreach ($job in Get-ShaderCompileJobs) {
        $sourcePath = Join-Path $shaderDir $job.Source
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($job.Source)
        $outputPath = Join-Path $shaderDir ("{0}.{1}.{2}.cso" -f $baseName, $job.Entry, $job.Profile)
        Write-Host ("==> Precompiling {0} [{1} {2}]" -f $job.Source, $job.Entry, $job.Profile)
        & $fxc.Source /nologo /T $job.Profile /E $job.Entry /Fo $outputPath $sourcePath
        if ($LASTEXITCODE -ne 0) {
            throw "Shader compile failed for $($job.Source) [$($job.Entry) $($job.Profile)]"
        }
    }
}

function Reset-OutputDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputDir
    )

    if (Test-Path -LiteralPath $OutputDir -PathType Container) {
        Get-ChildItem -LiteralPath $OutputDir -Force | Remove-Item -Recurse -Force
    } else {
        New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    }
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $repoRoot

$demoSourcePath = Resolve-DemoSourcePath -InputPath $DemoSource -RepoRoot $repoRoot
$demoName = [System.IO.Path]::GetFileNameWithoutExtension($demoSourcePath)
$outputDir = Join-Path $repoRoot 'manual_build\out'
$dllPath = Join-Path $outputDir 'wingui.dll'
$libPath = Join-Path $outputDir 'wingui.lib'
$demoExePath = Join-Path $outputDir ("wingui_{0}.exe" -f $demoName)
$demoPdbPath = Join-Path $outputDir ("wingui_{0}.pdb" -f $demoName)
$manifestPath = Join-Path $repoRoot 'app.manifest'

Import-MsvcEnvironment
Reset-OutputDirectory -OutputDir $outputDir

$commonCompilerArgs = @(
    '/nologo', '/std:c++20', '/permissive-', '/EHsc', '/W3', '/sdl',
    '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS'
)

$configurationCompilerArgs = if ($Configuration -eq 'Release') {
    @('/O2', '/Ob2', '/DNDEBUG', '/MD')
} else {
    @('/MDd', '/Zi')
}

$configurationLinkArgs = if ($Configuration -eq 'Release') {
    @()
} else {
    @("/PDB:$outputDir\wingui.pdb")
}

$demoConfigurationLinkArgs = if ($Configuration -eq 'Release') {
    @()
} else {
    @("/PDB:$demoPdbPath")
}

$dllArgs = @()
$dllArgs += $commonCompilerArgs
$dllArgs += $configurationCompilerArgs
$dllArgs += @(
    '/D_WINDOWS', '/D_USRDLL', '/DWINGUI_BUILD_DLL',
    '/I', 'include', '/LD',
    'src\abc_player.cpp', 'src\audio.cpp', 'src\native_ui.cpp', 'src\SoundBank.cpp',
    'src\SynthEngine.cpp', 'src\terminal.cpp', 'src\ui_model.cpp', 'src\wingui.cpp',
    '/link', '/NOLOGO',
    "/OUT:$dllPath",
    "/IMPLIB:$libPath"
)
$dllArgs += $configurationLinkArgs
$dllArgs += @(
    '/SUBSYSTEM:WINDOWS',
    "/MANIFEST:EMBED",
    "/MANIFESTINPUT:$manifestPath",
    'd3d11.lib', 'd3dcompiler.lib', 'dxgi.lib', 'windowscodecs.lib', 'winmm.lib',
    'Comctl32.lib', 'Msimg32.lib', 'Ole32.lib', 'user32.lib', 'gdi32.lib'
)

$demoArgs = @()
$demoArgs += $commonCompilerArgs
$demoArgs += $configurationCompilerArgs
$demoArgs += @(
    '/D_WINDOWS',
    '/I', 'include',
    $demoSourcePath, 'src\ui_model.cpp',
    '/link', '/NOLOGO',
    "/OUT:$demoExePath"
)
$demoArgs += $demoConfigurationLinkArgs
$demoArgs += @(
    '/SUBSYSTEM:WINDOWS',
    '/ENTRY:mainCRTStartup',
    "/MANIFEST:EMBED",
    "/MANIFESTINPUT:$manifestPath",
    $libPath,
    'user32.lib'
)

Invoke-BuildStep -Arguments $dllArgs -Description 'Building wingui.dll'
Invoke-BuildStep -Arguments $demoArgs -Description ("Building {0}" -f [System.IO.Path]::GetFileName($demoExePath))
Copy-ShaderAssets -RepoRoot $repoRoot -OutputDir $outputDir
Compile-ShaderAssets -OutputDir $outputDir

Write-Host "Built DLL : $dllPath"
Write-Host "Built EXE : $demoExePath"
Write-Host "Copied shaders to : $(Join-Path $outputDir 'shaders')"
Write-Host "Build configuration : $Configuration"

if ($Run) {
    $env:Path = "$outputDir;$env:Path"
    Write-Host "==> Running $demoExePath"
    & $demoExePath
    exit $LASTEXITCODE
}