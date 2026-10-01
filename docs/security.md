# Security boundary

Ausyn is a per-user monitoring and guidance app. It does not install a privileged service, request administrator access, run a custom antivirus scan, execute assistant-generated commands, close applications, delete files, or install Windows updates.

## Local collection

Ausyn reads Windows-reported system and hardware measurements, current-account-visible process information, selected Windows event-log records, and read-only startup/software inventory metadata. It does not inspect personal document contents, browser contents, keystrokes, passwords, or command-line arguments. Process details are transient and are not written to telemetry history.

Telemetry history stays in the current Windows user's local application-data directory. Users control retention, CSV/report export, history clearing, and optional conversation saving. Installation does not imply consent to cloud AI.

## Optional AI provider

Cloud AI is off by default. Each request requires a configured provider and explicit approval of the request preview. Ausyn sends selected evidence and optionally up to eight recent conversation messages if separately enabled. Process names require the sharing setting; session goals are generic when that setting is off. Database contents, inventory, process paths/IDs and command lines are not automatically sent as device evidence. User-written conversation may contain personal information, so it is included in the preview.

Web search is separately off by default and requires a second explicit opt-in and approval of each request. It uses the OpenAI `gpt-5-search-api` model and the official HTTPS API endpoint; model and search usage may incur provider charges. The preview covers selected evidence and any separately enabled recent conversation/session context. Citation URLs are accepted only when they use HTTPS and open in the system browser.

Remote endpoints must use HTTPS. Plain HTTP is allowed only for loopback local-model endpoints. TLS verification stays enabled and redirects are not followed. API keys are protected with Windows DPAPI for the current user account. A provider's own handling, retention, and billing terms still apply to requests the user approves.

## Findings and limitations

The workload companion does not interpret continued foreground activity as consent. Temporary background relief requires a user-confirmed app goal and explicit approval of the specific process. Ausyn accepts only a fresh, verified, same-account, non-critical process outside the Windows directory whose priority is Normal and which is not the current/kept app. It changes one process to Below Normal, verifies it, retains the same process handle to avoid PID reuse, and restores after ten minutes or at session end/pause/normal exit/observed foreground use. Restoration does not override a different externally selected priority. A crash can leave Below Normal priority until the app restarts. It does not terminate a process, free RAM, elevate permissions or modify a persistent OS setting. Windows documents the effect in [SetPriorityClass](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass).

Readings, Windows event messages, and process associations are evidence, not proof of root cause. Missing sensors remain unavailable. Security status is what Windows Security Center reports; Ausyn does not certify that the PC is malware-free. Disk reliability indicators come from Windows/storage drivers and are not a failure-date prediction.

Recommendations remain user-controlled. Ausyn can run the fixed, read-only `powercfg /requests` check when asked. Its power-plan action enumerates plans currently exposed by Windows. After the user chooses a plan, Ausyn previews its Windows-managed effect, asks for confirmation, saves the active scheme, verifies the new scheme through the Windows power API, and offers an undo that restores and verifies the saved scheme. Ausyn does not run assistant-generated commands or claim to repair detected issues automatically. Undo is available during the current app session; if Ausyn closes, the user can restore a plan from Windows Settings.
