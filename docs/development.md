# Development and packaging

## Toolchain

- Windows x64
- Visual Studio 2022 with the C++ desktop workload and x64 MSVC tools
- CMake 3.24 or newer
- Qt 6.6 or newer, MSVC x64 kit, with Concurrent, Core, Gui, Network, Sql, and Widgets
- Inno Setup 6 for the installer output (optional; the portable ZIP does not require it)

The application code is C++20. PowerShell scripts in `scripts/` configure, compile, stage Qt/compiler runtime files, and produce distributables. The Qt development kit is needed only on the build machine.

## Build

Open PowerShell in the repository root and run:

```powershell
.\scripts\build.ps1 -Configuration Release
```

The script locates the newest Qt MSVC kit under `C:\Qt` and the installed Visual Studio toolchain. Set `QTDIR` to a Qt kit directory if it is installed elsewhere.

## Package

```powershell
.\scripts\package.ps1 -Configuration Release
```

The script builds again, deploys the Qt runtime and x64 Visual C++ runtime, checks required runtime files, and writes:

- `dist/Ausyn-0.4.0-Windows-x64.zip`
- `dist/Ausyn-Setup-0.4.0-Windows-x64.exe` when Inno Setup 6 is available

The package staging directory is `build-nmake/package-stage`. Do not distribute that staging folder as a release; use the ZIP or installer.

## Code map

- `src/monitoring/`: Windows collectors, telemetry scheduling, sample validation, and change tracking.
- `src/intelligence/`: findings, gaming readiness, and conservative history-backed predictions.
- `src/data/`: SQLite local-history repository and schema migrations.
- `src/assistant/`: offline intent-based answers and the optional reviewed AI-provider client.
- `src/settings/`: per-user preferences and Windows DPAPI credential protection.
- `src/ui/`: Qt Widgets application pages, dashboard, and user workflows.
- `docs/`: architecture, feature behavior, privacy, installation, and troubleshooting.

## Validation boundary

The Release build and packaging scripts validate compilation, Qt/runtime deployment, required staged files, and Inno Setup compilation. The app's `--ui-check <folder>` mode validates page routes, detail windows, title-bar controls, pending alerts, quiet hours, critical memory detection, inline checks and local replies using fixtures without collectors or settings writes. It renders both the main UI and alert cards with `WA_DontShowOnScreen`. Use `QT_QPA_PLATFORM=windows` for native font rendering. `--collector-check <folder>` makes three real Windows collections while measuring a UI heartbeat, without using history storage. See [interface guide](ui-guide.md). These smoke checks do not establish sustained workload stability or sensor coverage across different PCs.
