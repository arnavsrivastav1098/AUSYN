# Ausyn

Your PC shouldn't just show numbers. It should explain what they mean.

Ausyn is a native Windows desktop companion built with C++20 and Qt 6.
It monitors your PC, understands resource behaviour, explains unusual
conditions, and provides cautious, user-controlled recommendations.

## What Ausyn can do

- Live CPU, memory, GPU, storage, battery and network monitoring
- Process-level resource analysis
- Detect sustained CPU and memory pressure
- Explain findings using actual system evidence
- Local performance history and trends
- Windows event intelligence
- Storage and short-term resource forecasting
- Startup and application analysis
- Gaming readiness checks
- Battery and power insights
- Ask Ausyn — a local PC assistant
- Optional cloud AI with explicit user approval
- User-controlled recommendations and actions
- Local-first privacy

## Current release

Ausyn v0.6.0

Available as:

- Windows x64 installer
- Portable Windows x64 ZIP

Go to GitHub Releases to download the latest build.

## Privacy & safety

Ausyn is local-first.

System telemetry and findings remain on your PC by default.
Cloud AI is optional and disabled by default.

Ausyn does not automatically:

- delete files
- install Windows updates
- change important system settings without confirmation
- claim that correlation proves the cause of a problem

## Windows Smart App Control / unsigned build

Ausyn is currently independently distributed and the executable is
not code-signed with a commercial certificate.

Because of this, Windows Smart App Control / SmartScreen may warn
about or block the application.

This warning does not by itself mean that Ausyn contains malware.
It means Windows cannot verify a trusted publisher signature.

Do not weaken your security settings just because an application asks
you to. Review the source and release information before deciding
whether to run an unsigned build.

## Build from source

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 C++ Build Tools
- CMake 3.24+
- Qt 6 MSVC x64

PowerShell:

.\scripts\build.ps1 -Configuration Release

Package:

.\scripts\package.ps1 -Configuration Release

## Documentation

Detailed documentation is available in the `docs` directory.

## Current status

Ausyn is actively under development.

Current public version: v0.6.0

Expect features, behaviour and interfaces to evolve as development continues.

