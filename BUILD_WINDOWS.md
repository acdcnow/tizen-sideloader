# Building Tizen Sideloader for Windows

The upstream build files were Unix-only in practice: `Makefile` handles Linux/macOS
(and a MinGW branch that still relies on `pkg-config`), while `build_win32.bat` was an
unmodified Dear ImGui example — its include paths pointed at `..\..` and it linked
`libs\glfw\lib-vc2010-32`, neither of which exists in this repository.

This document describes the working Windows route and what changed.

## Requirements

| | |
|---|---|
| Compiler | MinGW-w64 GCC (x86_64) — no Visual Studio needed |
| Install (once, user scope, no admin) | `winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT` |
| GLFW | fetched automatically into `libs\glfw` by the build script (official `glfw-*.bin.WIN64.zip`) |
| OS | Windows 10/11 x64 with an OpenGL 3.0 capable GPU driver |

Verified with **GCC 16.2.0** (`MinGW-W64 x86_64-ucrt-posix-seh`, WinLibs) and
**GLFW 3.5.1**.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\build_windows.ps1
```

What the script does:

1. finds `g++` (on `PATH`, otherwise the winget WinLibs package),
2. downloads the latest official GLFW Windows binaries into `libs\glfw` if the headers
   are missing (`-Force` re-downloads),
3. compiles `main.cpp`, the five `imgui*.cpp`, both GLFW/OpenGL3 backends,
   `imgui_stdlib.cpp` and `ImGuiFileDialog.cpp` with `-std=c++11 -O2`
   (`ImGuiFileDialog.cpp` at `-O1`: pure dialog UI code, and by far the slowest
   unit to optimise). Objects that are newer than their source and `imgui.h` are
   reused — pass `-Rebuild` to force a full rebuild,
4. links `libglfw3.a` plus `opengl32`, `gdi32`, `user32`, `shell32`, `winmm`, `imm32`,
   `ole32`, `uuid` and links the C/C++ runtimes statically.

Result: **`dist\tizen-sideloader.exe`** — one file, nothing to install beside it.

Verified on this machine (GCC 16.2.0 + GLFW 3.5.1): 6.1 MB `tizen-sideloader.exe`,
PE32+ x86-64, console subsystem, and an import table containing only `KERNEL32`,
`USER32`, `GDI32`, `SHELL32`, `OPENGL32` and the `api-ms-win-crt-*` UCRT stubs —
no `glfw3.dll`, `libstdc++-6.dll`, `libgcc_s_*.dll` or `libwinpthread-1.dll`.
The binary starts and keeps its render loop alive (process stays responsive); the
GUI was not inspected interactively.

The executable is built as a **console** application on purpose: it keeps stray `printf`
output visible, but all `sdb`/`tizen` output is captured by the app itself and shown in the
**Log** window (`Show log`) and in the error dialogs.

`imgui.ini` is written next to the executable (Dear ImGui default), which is why
`dist/` is git-ignored.

## Code change required for Windows

`main.cpp` shelled out to the Tizen CLI with hard-coded Unix naming:

```cpp
command << tizenloc << "/tools/sdb connect " << str2.str();
sideloadcommand << tizenloc << "/tools/ide/bin/tizen install -n " << filePathName;
```

That cannot work on Windows for two reasons: the tools are `sdb.exe` and `tizen.bat`,
and Tizen Studio is usually installed under `C:\Program Files\...`, so an unquoted path
splits at the space and `system()` fails.

Two small helpers now build the command line per platform (everything else is
unchanged, so the Linux/macOS build behaves exactly as before):

| | Windows | Unix |
|---|---|---|
| connect | `"<loc>\tools\sdb.exe" connect <ip:port>` | `<loc>/tools/sdb connect <ip:port>` |
| install | `"<loc>\tools\ide\bin\tizen.bat" install -n "<file.wgt>"` | `<loc>/tools/ide/bin/tizen install -n "<file.wgt>"` |

## Using it

1. Start `dist\tizen-sideloader.exe`.
2. **Configure tizen-studio** → enter the folder that contains `tools\sdb.exe` and
   `tools\ide\bin\tizen.bat` (the *Tizen Studio CLI* installation, e.g.
   `C:\tizen-studio`). No trailing backslash.
3. Put the TV in developer mode, then enter its **IP** and **port** (default `26101`)
   and press **Connect!** — the console shows the `sdb connect` output.
4. **Choose WGT** → pick your `.wgt`/`.tpk` → **Sideload!** — the console shows the
   `tizen install` output; success/failure is also reported in a dialog.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `g++ not found` | toolchain not installed — run the `winget` line above, then open a new shell |
| GLFW download fails | offline/proxy; download `glfw-*.bin.WIN64.zip` manually and extract it so that `libs\glfw\include\GLFW\glfw3.h` exists |
| `dist\tizen-sideloader.exe is running (pid …)` | close the app before building — Windows locks a running executable |
| Dialog "Tizen Studio CLI not found" | the configured folder has no `tools\sdb.exe` / `tools\ide\bin\tizen.bat`; install the CLI-only Tizen Studio and point "Configure tizen-studio" at that folder |
| Dialog "Command failed" when connecting | read the dialog: it shows the exact command, the full output and the exit code. Typical: TV not in developer mode, PC IP not registered, wrong IP, TV offline |
| `sdb` says the device is `offline` / `unauthorized` | developer mode not active on the TV, or the host IP was changed and the TV was not rebooted |
| `app_id[...] install failed[118019]` | the TV rejects the certificate in the `.wgt`. Verified on a Samsung UE40NU7190 (Tizen 4.0): an unsigned test widget and a third-party-signed `.wgt` both fail with this code after `push` succeeded. Samsung TVs need a distributor certificate bound to the TV's DUID (Tizen Studio › Certificate Manager, Samsung account); re-sign the widget with that profile |
| `install failed[118xxx]` (other codes) | also signature/certificate family — sign the package for this TV before retrying |
| Sideload says the app is missing on the TV although install worked | check `required_version` in the widget's `config.xml` against the TV's `platform_version` (logged by the failure diagnostics) |
| Blank/black ImGui window | GPU driver without OpenGL 3.0 — update the driver |
