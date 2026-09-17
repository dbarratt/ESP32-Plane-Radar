# Project Instructions

## PlatformIO

Run firmware builds, uploads, and monitor commands from the workspace's dedicated PlatformIO CLI terminal. In a generic PowerShell terminal, `pio` may not be on `PATH`, and `py -m platformio` may fail because PlatformIO is not installed in that Python environment.

Use the project environment explicitly:

```powershell
pio run -e supermini
pio run -e supermini -t upload --upload-port COM5
pio device monitor -b 115200
```

If `pio` is unavailable, do not retry through a different Python interpreter. Switch to the PlatformIO CLI terminal or locate the PlatformIO executable through the VS Code PlatformIO extension. Check the terminal's working directory is the repository root before running commands.
