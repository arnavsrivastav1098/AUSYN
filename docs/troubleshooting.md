# Troubleshooting

## Ausyn says data is delayed

Check the status at the top of the app and open **Agent health**. The status is based on the timestamp of the latest validated sample and becomes delayed after three selected sampling intervals, with a ten-second minimum. Wait briefly for the next sample. If it remains delayed, close and reopen Ausyn; live monitoring may recover even if local history is unavailable.

## CPU, GPU, temperature, battery, or drive fields are unavailable

Windows and device drivers expose different sensors on different PCs. Ausyn labels unsupported or inaccessible values as unavailable rather than treating them as zero. ACPI thermal zones may not represent CPU or GPU die temperatures. Ausyn shows whether Windows denied access to the ACPI sensor provider or whether the device exposed no thermal zones. It stays at normal user privilege and will not install a hardware driver to force a reading; CPU and GPU die temperatures require a supported hardware-specific source.

## Windows Firewall asks whether Ausyn can access the network

Ausyn does not accept incoming network connections. You can keep it blocked in Windows Firewall; local monitoring, alerts, and recommendations continue to work. Optional cloud AI makes outbound requests only after you enable it and approve a request. Blocking inbound connections does not prevent that feature.

## Ausyn is installed under my user profile or disappeared from Apps

The per-user installer used the current Windows account's Programs folder. Release 0.1.1 and later use a standard administrator-approved install under `C:\Program Files\Ausyn`, so Windows shows a normal uninstall entry. If you uninstall Ausyn, its entry disappearing from Installed apps / Programs and Features is expected. Run Ausyn 0.1.2 or later setup to install the updated build.

## The main window disappears when I click X

When a Windows notification-area icon is available, X hides Ausyn to the tray so its monitoring can continue. Right-click the Ausyn tray icon and choose **Open Ausyn** to bring back the same page, or **Quit Ausyn** to stop it completely. If Windows has no notification area, closing the window exits Ausyn.

## History or forecasts are unavailable

Open **History & reports** and read the status and database path. Forecasts need enough fresh, consistent history; a new install or long monitoring gap can leave them unavailable. If live readings work but history does not, the database may be locked, inaccessible, or damaged. Ausyn keeps live monitoring available and retries local storage. Export any data that remains accessible before attempting manual recovery.

## Cloud AI does not respond

Confirm Cloud AI is enabled, the endpoint and model are correct, and the request preview was approved. Remote endpoints need HTTPS; HTTP only works for loopback local services. Check internet connectivity, provider availability, and API-key validity. Web search is a separate opt-in; it requires `https://api.openai.com/v1`, OpenAI API access to `gpt-5-search-api`, and can incur API charges. Ausyn still returns its local answer when the provider request fails; cloud access is not needed for monitoring or diagnostics.

## Notifications do not appear

Open **Settings → Notifications**. Check the global switch, the relevant category, quiet hours, and Windows notification settings. Dashboard findings continue to update even when desktop notifications are disabled or suppressed by quiet hours.

## The installer is blocked or Ausyn will not start

Use the installer or portable ZIP from the same release build. The portable package requires extraction before running `ausyn.exe`. If Windows blocks the download, review Windows' file properties and security prompt; do not bypass a warning unless you have verified the file source and integrity yourself. Reinstalling does not remove local user data.

## Report a problem

Record the Ausyn version, Windows version/build, the relevant page, and the exact status text. Avoid posting API keys, process names, event message text, or exported system reports publicly unless you have reviewed and intentionally removed private details.
