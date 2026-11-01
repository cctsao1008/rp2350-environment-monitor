# RP2350 ELOG — Windows host-side read-only backups

The RP2350 has a 12 MiB circular Flash logger (at most 460,800 1 Hz
samples, about 5.33 days). This tool copies only existing local USB
CDC-NCM HTTP GET endpoints. It never flashes, erases, resets, or changes
MCU firmware. It does not use the public Cloudflare Tunnel.

## One-time manual verification

Open PowerShell in the repository on the Windows PC physically connected
to the RP2350:

~~~powershell
git pull --ff-only
py -3 .\tools\envlog_backup.py --outdir "$env:USERPROFILE\RP2350-Log-Backups"
~~~

Requires Python 3.9+; uses only Python standard library and the existing
tools/envlog_parse.py. The default URL is http://192.168.7.1.

Success: a 12,582,912-byte BIN, a JSON sidecar, a SHA-256 digest and
sample/UTC counts with zero Parser warnings. Existing snapshots are
preserved. You may pass the saved BIN path to envlog_parse.py for CSV export.

## Consistency and safety contract

1. GET status and verify geometry, format and zero unknown headers.
2. GET all 48 manifest pages (64 slots per page).
3. GET only listed 4096-byte sectors by slot/generation/header CRC,
   verify sector header CRC/identity and fill other slots with FF.
4. Re-fetch the manifest. Abort and retry the whole capture if an
   original sector was overwritten during the transfer.
5. Run the offline Parser; reject any corrupted pages or unknown sectors.
6. Publish a complete binary and JSON report only after verification.
   No failed or incomplete result is published as a completed backup.

This is an incremental, non-atomic snapshot. New pages or sectors
created during transfer may be absent. Later backups include them.
The tool never invents UTC or modifies ELOG data.

## Windows Task Scheduler — optional after first device PASS

Create a task in Windows Task Scheduler under the normal user account:

- Trigger: Daily when Windows is awake and the RP2350 USB cable connected,
  for example 20:00 (change this to suit the actual setup).
- Program: full path found using Get-Command py.exe.
- Arguments: -3 tools\envlog_backup.py --outdir "C:\Users\<USERNAME>\RP2350-Log-Backups"
- Start in: D:\my-github\rp2350-environment-monitor
- Settings: do not run overlapping copies. Optionally run as soon as
  practical after a missed start.

This repository change does not install a local scheduled task. If the PC
or RP2350 is unavailable, backup fails without modifying existing files.
Do not configure auto-deletion without a separate retention and offline
copy policy.

## Host regression tests (no board needed)

~~~powershell
py -3 -m unittest discover -s tests -p test_envlog_backup.py -v
~~~

Mock HTTP tests check successful capture, a recycled sector, invalid
page CRC, rejection of the public URL, and manifest re-reads.
The client bypasses system HTTP proxies and refuses HTTP redirects.

## Remaining real-world acceptance

- [x] First physical Windows host-side USB-NCM CLI capture: 60,950 samples, 1,890 UTC, 412 sectors, 0 warnings, reported 2026-11-01; BIN+JSON created
- [ ] Historical record preservation check against an earlier backup
- [ ] Two scheduled daily runs from Task Scheduler
- [ ] Retention, off-device copies and missed-run detection
- [ ] Full Flash wraparound and unexpected power loss (separate reliability)

## First physical Windows host backup — 2026-11-01

Owner executed the CLI from the Windows project root against the physically
USB-NCM-connected board. It reported 60,950 samples, 1,890 UTC mapped
samples, 412 valid sectors and **zero Parser warnings**. A full BIN and a
SHA-256 JSON report were saved in the requested host backup directory.
The reported digest was
`c85aadea48b20a2b37a51054eef30039d2e6378455c63a114d01a629f9a7ac0c`.
This is on-target evidence (owner console), not merely CI simulation.

The 59,690-record previous snapshot has 1,260 fewer records than this
backup; exact record-by-record historical preservation is pending
comparison. No local Task Scheduler job was installed by GitHub changes.
