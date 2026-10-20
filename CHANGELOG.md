# Changelog

Notable changes to this fork. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); tags keep the upstream style
(**no `v` prefix**).

## [0.3.0] - 2026-10-20

Verified against a real TV (Samsung UE40NU7190, Tizen 4.0, `10.0.1.236:26101`):
connect and file transfer work; the install is refused by the TV because of the
package certificate.

### Added
- **Installer error explanation** in the failure dialog: `install failed[118019]` is
  decoded as "the TV rejected the certificate inside this .wgt" and names the fix
  (Samsung distributor certificate containing the TV's DUID, created in Tizen Studio
  › Certificate Manager). Other `118xxx` codes, `offline`/`unauthorized` and a closed
  connection get their own hints. Codes that were not reproduced on hardware are not
  claimed to mean anything specific.
- Failure diagnostics now also run `sdb -s <ip>:26101 capability`, so the log shows the
  TV's `platform_version`, `vendor_name` and `sdk_toolpath` next to the error.

### Changed
- The hint block is rendered above the captured output and highlighted, instead of only
  listing generic "developer mode / same network" advice.

### Notes
- A `.wgt` whose `config.xml` asks for a higher `required_version` than the TV's
  `platform_version` will still fail; the check above makes that visible in the log.

## [0.2.0] - 2026-10-20

### Added
- **In-app log window** (`Show log`) that captures stdout **and** stderr of every
  `sdb`/`tizen` call, with Copy, Clear and auto-scroll.
- **Detailed failure dialog**: the exact command line, the captured output and the exit
  code, plus "Copy to clipboard" and "Show log". A failed connect additionally runs
  `sdb devices` and appends its output, so `offline`/`unauthorized` becomes visible.
- **Preflight check**: when `tools\sdb.exe` (or `tools\ide\bin\tizen.bat`) is missing under
  the configured tizen-studio folder, a dialog names the expected paths instead of running
  a doomed command; the main window shows a red hint as well.
- Commands run on a worker thread, so the window keeps rendering while `sdb`/`tizen` run.

### Changed
- **Only the TV IP is entered.** Port `26101` is appended automatically; `IP:port` is still
  accepted for unusual setups. The separate port field is gone.
- Success is reported as a "Command finished" dialog with the output tail; the generic
  "An error occured" popups were removed.
- `build_windows.ps1` now fails with a clear message when the executable is still running
  and cannot be replaced.

### Fixed
- Errors used to say "Check the console for more info" while the output went to a console
  window; everything is captured in the app now.

## [0.1.0] - 2026-10-20

First release of the `acdcnow` fork. Baseline: upstream `0.0.1` (`98c3e68`).

### Added
- **Windows support.** `build_windows.ps1` locates MinGW-w64 `g++` (on `PATH` or in the
  winget WinLibs package), vendors the official prebuilt GLFW into `libs\glfw`, compiles
  the ten translation units in parallel, reuses objects newer than their source and
  `imgui.h` (`-Rebuild` forces a full build) and links GLFW, libgcc and libstdc++
  statically.
- `BUILD_WINDOWS.md` — requirements, both build routes, what changed for Windows, usage
  notes and a troubleshooting table.
- Release asset `tizen-sideloader.exe` (x64, console subsystem, self-contained: imports
  only `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `OPENGL32` and the `api-ms-win-crt-*`
  UCRT stubs).
- Usage and download sections in the README.

### Changed
- `main.cpp`: invoking the Tizen CLI is now platform-aware. On Windows the tools are
  `sdb.exe` and `tizen.bat` and the tool path is quoted, so installations under
  `C:\Program Files\...` work. The Unix command line is unchanged.
- `.gitignore`: ignores `libs/`, `dist/` and `build/` (vendored GLFW and build output).

### Fixed
- `build_win32.bat` was an unmodified Dear ImGui sample: its include paths pointed at
  `..\..` and it linked `libs\glfw\lib-vc2010-32`, neither of which exists here, so it
  could not build this repository. It now targets the real layout,
  `libs\glfw\lib-vc2022` and produces `dist\tizen-sideloader.exe`.

### Verified
- Windows 11 x64, GCC 16.2.0 (MinGW-w64 UCRT) + GLFW 3.5.1: 6.13 MB executable, PE32+
  x86-64, starts and keeps its render loop running. The `sdb`/`tizen` round trip has **not**
  been tested against a real TV yet.
