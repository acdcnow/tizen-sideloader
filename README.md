#### Sideload apps on Samsung Tizen TVs without messing around with Tizen Studio.

![](https://files.rickroll.space/content/cdn/eiqDEZeCFill.png) 

*Beautiful ImGui UI*

A tiny Dear ImGui front-end for the Tizen CLI: connect to the TV with `sdb`, then
`tizen install` your `.wgt`. This fork adds a **working Windows build** - upstream is
Linux/macOS only.

## Download (Windows)

Grab `tizen-sideloader.exe` from the [latest release](../../releases/latest). It is a
single self-contained x64 executable (GLFW, libgcc and libstdc++ linked statically), so
there is nothing to install beside it.

> It runs as a **console** application on purpose: `sdb` and `tizen` write their progress
to the console, and the dialogs tell you to check it when a step fails.

> Commands run in the background and everything they print is captured in the in-app
> **Log** window (`Show log`). Errors open a dialog with the exact command line, the
> full output and the exit code - no separate console window to hunt for.

## Usage

1. Install the **Tizen Studio CLI** (the "CLI" installer - no IDE needed) and note the
   folder that contains `tools\sdb.exe` and `tools\ide\bin\tizen.bat`, e.g. `C:\tizen-studio`.
2. Put the TV into **developer mode** and register this PC's IP as the host. Depending on
   the model that lives under *Apps* (enter `12345` on the remote) or
   *Settings \u25b8 General \u25b8 External Device Manager \u25b8 Device Manager*.
3. Start `tizen-sideloader.exe`, press **Configure tizen-studio** and enter that folder
   (without a trailing backslash).
4. Enter the TV's **IP address** and press **Connect!** - port **26101** is used
   automatically (`IP:port` is only needed if you changed it).
5. **Choose WGT** \u2192 pick your `.wgt` \u2192 **Sideload!**. The result appears in a dialog,
   the full transcript in the log window.

# How to Build
Tested on Linux and Windows. Other Unixes are untested (the Makefile has a MinGW branch,
but it expects GLFW via `pkg-config`).

## Windows

Both routes run from the repository root.

**MinGW-w64 (recommended, no Visual Studio required)**
```
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
powershell -ExecutionPolicy Bypass -File .\build_windows.ps1
```
The script fetches the official prebuilt GLFW into `libs\glfw` on first run and links
GLFW, libgcc and libstdc++ statically, so `dist\tizen-sideloader.exe` is a single
self-contained file - no DLLs to copy next to it.

**Visual Studio**\
Open an *x64 Native Tools Command Prompt*, make sure `libs\glfw` exists (the script
above creates it) and run `build_win32.bat`.

Details, requirements and troubleshooting: [BUILD_WINDOWS.md](BUILD_WINDOWS.md)

## Linux and similar Unixes

Install glfw from your distro's package manager and then:
```
make
```
## macOS

```
brew install glfw
make
```

## Changes in this fork

See [CHANGELOG.md](CHANGELOG.md). In short: platform-aware `sdb`/`tizen` invocation, a
real Windows build script, a corrected MSVC batch file and Windows documentation.

## Credits & License

MIT - see [LICENSE.txt](LICENSE.txt). Original project by
[Joshua](https://github.com/TheGithubJoshua)
([upstream](https://github.com/TheGithubJoshua/tizen-sideloader)).