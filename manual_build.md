# Manual Demo Build

This project can be built and run directly with MSVC command lines without using the Visual Studio solution or project files.

The commands below were validated on Windows with the Visual Studio C++ toolchain from `VsDevCmd.bat`.

## Quick path

Use the repository script from the repo root:

```powershell
.\build_demo demo.cpp
```

Release build:

```powershell
.\build_demo demo.cpp Release
```

Notes:

- `build_demo.cmd` is a thin wrapper around `build_demo_impl.ps1`, so the normal PowerShell execution policy does not block the command.
- Passing `demo.cpp` resolves to `src\demo.cpp` automatically.
- Passing `Release` as the second argument changes compiler/linker flags, but both configurations now write to the same canonical output folder: `manual_build\out`.
- Each build clears `manual_build\out` first, so there is only one current packaged build at a time.
- The script always rebuilds `wingui.dll` first, then writes the demo executable to `manual_build\out\wingui_<name>.exe`.
- The script also copies `shaders\*.hlsl` into `manual_build\out\shaders`, so launching the built `.exe` directly still finds its shader assets.
- The script also precompiles shader blobs (`*.cso`) into `manual_build\out\shaders`, and the runtime loads those first before falling back to `D3DCompileFromFile`.
- Demos are linked as windowed apps, so double-click launch does not open a transient console window.
- The build embeds `app.manifest`, so Common Controls v6 and PerMonitorV2 DPI awareness are enabled at process scope as well.
- Add `-Run` to launch the built demo after compilation.

## 1. Open an x64 MSVC environment

From PowerShell, load the latest installed Visual Studio C++ environment:

```powershell
Set-Location "c:\projects\wingui"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'

$envDump = cmd /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul & set"
foreach ($line in $envDump) {
    if ($line -match '^(.*?)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
```

## 2. Create an output directory

```powershell
New-Item -ItemType Directory -Force -Path .\manual_build\out | Out-Null
```

## 3. Build `wingui.dll`

```powershell
cl /nologo /std:c++20 /permissive- /EHsc /W3 /sdl /MDd /Zi \
  /D_WINDOWS /D_USRDLL /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DWINGUI_BUILD_DLL \
  /I include /LD \
  src\abc_player.cpp src\audio.cpp src\native_ui.cpp src\SoundBank.cpp \
  src\SynthEngine.cpp src\terminal.cpp src\ui_model.cpp src\wingui.cpp \
  /link /NOLOGO \
  /OUT:manual_build\out\wingui.dll \
  /IMPLIB:manual_build\out\wingui.lib \
  /PDB:manual_build\out\wingui.pdb \
  /SUBSYSTEM:WINDOWS \
  d3d11.lib d3dcompiler.lib dxgi.lib windowscodecs.lib winmm.lib \
  Comctl32.lib Msimg32.lib Ole32.lib user32.lib gdi32.lib
```

Notes:

- `user32.lib` and `gdi32.lib` are required for the direct link even though they were not listed in the project metadata.
- The build produces `manual_build\out\wingui.dll` and `manual_build\out\wingui.lib`.

## 4. Build `wingui_demo.exe`

```powershell
cl /nologo /std:c++20 /permissive- /EHsc /W3 /sdl /MDd /Zi \
  /D_WINDOWS /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS \
  /I include src\demo.cpp src\ui_model.cpp \
  /link /NOLOGO \
  /OUT:manual_build\out\wingui_demo.exe \
  /PDB:manual_build\out\wingui_demo.pdb \
  /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup \
  manual_build\out\wingui.lib
```

Important:

- Link against the explicit path `manual_build\out\wingui.lib`.
- There is also a `wingui.lib` in the repository root, and using just `wingui.lib` can cause the linker to pick the wrong import library.

## 5. Run the demo

The build script copies the shaders beside the executable, so the demo can be launched either from the repo root or by opening the `.exe` directly:

```powershell
Set-Location "c:\projects\wingui"
$env:Path = "c:\projects\wingui\manual_build\out;" + $env:Path
& ".\manual_build\out\wingui_demo.exe"
```

Notes:

- The demo depends on `wingui.dll`, so `manual_build\out` must be on `PATH` or the DLL must be beside the executable.