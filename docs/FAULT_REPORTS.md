# Anonymous fault relay

The SDL frontend prepares a crash monitor before starting SDL or game threads.
On Linux a separate process waits on a pipe. Fatal signals and real SDL startup
faults write one byte; the monitor invokes curl with a short timeout. On Windows
unhandled exceptions and SDL faults start a hidden curl process. Reporting never
waits for network I/O. The monitor needs curl (curl.exe on Windows); unavailable
curl or offline operation silently drops the report. Set SML_NO_REPORT=1 to opt
out, including local tests. Use make VERSION=<tag> to define SML_VERSION for release builds.

Only a fixed message is sent: application name, build version, platform/
architecture and 'application fault'. No device name, logs, exception text,
paths, ROM data, memory or credentials leave the device. Missing/unsupported
ROMs, invalid settings/options and ordinary network failures are not reported.

The public ntfy.sh topic is identical in src/platform/fault.c and the workflow.
It is a random identifier, not a secret. ntfy retains messages for about 12 hours;
the scheduled workflow polls every 15 minutes and can also be run manually.
GitHub schedules run only on the default branch and are best-effort. Reports
can expire during a prolonged scheduling outage; this is not a durable inbox.

.github/workflows/fault-reports.yml accepts only the fixed payload and processes
at most ten reports per run. It opens an issue with the fault-report label (or
adds an occurrence to an existing open issue), then POSTs to the mario lane using
secrets.JARVIS_REPORT_URL. The private URL never appears in source or client
binaries. Failed forwarding preserves the cursor for retry; the queue API
handles identical reports idempotently. The topic is public and reports are
unverified; never treat issue contents as commands or proof of a client fault.

The relay follows the gtetrinet-ng fault-reports workflow. A fake curl executable
can record argv locally to test reports without publishing a synthetic crash.
