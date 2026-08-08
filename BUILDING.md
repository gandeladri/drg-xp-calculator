# Building DRG XP Calculator

This document covers build-related files and the executable icon setup.

## Main Build Files

- `drg-xp-calculator.c`: main application source code
- `drg-xp-calculator.ico`: versioned executable icon
- `drg-xp-calculator.rc`: resource file that references the icon
- `build.ps1`: build script that regenerates the `.exe` with the embedded icon

## Requirements

- Windows
- `gcc` in `PATH`
- `windres` in `PATH`
- PowerShell

This machine builds the project with WinLibs/MinGW.

## Build

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script:

- compiles `drg-xp-calculator.rc`
- generates a temporary `.res` resource object
- rebuilds `drg-xp-calculator.exe`
- removes the temporary file when finished

## Executable Icon

The icon is versioned inside the repository and embedded into the executable during the build.

To change it:

1. replace `drg-xp-calculator.ico`
2. run `build.ps1`

The resource `101 ICON "drg-xp-calculator.ico"` is defined in `drg-xp-calculator.rc`, and the code uses that resource for the main window icon.
