# Changelog

Notable changes to this fork. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); tags keep the upstream style
(**no `v` prefix**).

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
