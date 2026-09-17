# Project Instructions

## PlatformIO

Run firmware builds, uploads, and monitor commands with the PlatformIO executable directly. Do not invoke `pio` through `PATH`, and do not use `py -m platformio`, because the active terminal or Python environment may not have PlatformIO installed.

Use the project environment explicitly:

```powershell
& "$HOME\.platformio\penv\Scripts\pio.exe" run -e supermini
& "$HOME\.platformio\penv\Scripts\pio.exe" run -e supermini -t upload --upload-port COM5
& "$HOME\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

Check the terminal's working directory is the repository root before running commands. If this executable path does not exist, locate the PlatformIO installation through the VS Code PlatformIO extension rather than trying another Python interpreter.
