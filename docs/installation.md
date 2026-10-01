# Install Ausyn

Ausyn is installed as a standard Windows desktop application under Program Files. The installer requests administrator approval for the application files and Start Menu shortcut. Ausyn does not install a Windows service.

## Installer

1. Quit an already-running Ausyn from its notification-area menu before upgrading.
2. Run `Ausyn-Setup-0.4.0-Windows-x64.exe` and approve the Windows administrator prompt.
3. Choose whether to create a desktop shortcut.
4. Leave “Launch Ausyn” selected to open it after setup.

The installer places the application in `C:\Program Files\Ausyn` and registers it in **Settings → Apps → Installed apps** and **Control Panel → Programs and Features**. Use either Windows page to uninstall it. Uninstalling removes the application and its optional sign-in entry; Ausyn's per-user history and settings are kept so they are not silently erased.

## Portable ZIP

1. Extract `Ausyn-0.4.0-Windows-x64.zip` to a folder.
2. Run `ausyn.exe` from that folder.

The portable app still stores settings and history in the current Windows account's local application-data location. It does not write its database beside the executable.

## First launch

Ausyn shows a short guide explaining local monitoring and privacy defaults. Live monitoring and the local written assistant work without an internet connection. The optional cloud assistant is off by default. It requires an endpoint configuration and confirmation of each request preview before sending the displayed context.

The dashboard may initially show that history-based trends are unavailable. Ausyn needs enough consistent local samples before producing a forecast; it does not fill missing history with estimates.

## Requirements

- 64-bit Windows capable of running the included Qt 6 and Visual C++ runtime libraries.
- The installer and portable ZIP contain their application runtime dependencies. Visual Studio, CMake, Qt development files, and Inno Setup are not required on a user PC.

## Local data

Settings, the optional conversation history, and the SQLite history database are stored under the current user's Windows local application-data location. The exact database path is shown in **History & reports**. Local telemetry can be exported or cleared from that page. Uninstall does not delete user data; remove it separately only if you no longer need it.
