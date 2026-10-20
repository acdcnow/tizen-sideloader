<#
.SYNOPSIS
    Builds tizen-sideloader for Windows x64 with MinGW-w64 (WinLibs).

.DESCRIPTION
    - finds g++ (PATH, or the winget WinLibs package installed user-scope)
    - vendors the official prebuilt GLFW into libs\glfw on first run
    - statically links GLFW, libgcc and libstdc++ -> dist\tizen-sideloader.exe
      is a single self-contained file (no DLLs to ship)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build_windows.ps1

.EXAMPLE
    # re-download GLFW and rebuild from scratch
    powershell -ExecutionPolicy Bypass -File .\build_windows.ps1 -Force
#>
[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
try {
    # ---- compiler ---------------------------------------------------------
    $gxx = Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($gxx) {
        $gxx = $gxx.Source
    }
    else {
        $gxx = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.*\mingw64\bin\g++.exe" `
            -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $gxx) {
        throw "g++ not found. Install the toolchain once with:`n  winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT"
    }
    Write-Host "compiler : $gxx"

    # ---- GLFW -------------------------------------------------------------
    $glfw = Join-Path (Get-Location) 'libs\glfw'
    if ($Force -or -not (Test-Path (Join-Path $glfw 'include\GLFW\glfw3.h'))) {
        Write-Host 'GLFW     : fetching the official Windows binaries ...'
        $headers = @{ 'User-Agent' = 'tizen-sideloader-build' }
        $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/glfw/glfw/releases/latest' -Headers $headers
        $asset = $rel.assets | Where-Object { $_.name -like '*WIN64.zip' } | Select-Object -First 1
        if (-not $asset) { throw 'no GLFW *WIN64.zip asset found in the latest release' }

        $zip = Join-Path $env:TEMP $asset.name
        (New-Object System.Net.WebClient).DownloadFile($asset.browser_download_url, $zip)
        New-Item -ItemType Directory -Force -Path $glfw | Out-Null
        Expand-Archive -Path $zip -DestinationPath $glfw -Force

        # the archive contains a single top folder (e.g. glfw-3.5.1.bin.WIN64) -> flatten
        $inner = Get-ChildItem $glfw -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'include') } | Select-Object -First 1
        if ($inner) {
            Get-ChildItem $inner.FullName -Force | Move-Item -Destination $glfw -Force
            Remove-Item $inner.FullName -Recurse -Force
        }
        Write-Host "           $($asset.name) -> libs\glfw"
    }
    Write-Host "GLFW     : $glfw"

    # ---- build ------------------------------------------------------------
    $sources = @(
        'main.cpp',
        'imgui.cpp', 'imgui_demo.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
        'backends\imgui_impl_glfw.cpp', 'backends\imgui_impl_opengl3.cpp',
        'misc\cpp\imgui_stdlib.cpp',
        'ImGuiFileDialog\ImGuiFileDialog.cpp'
    )
    $includes = @('-I.', '-I.\backends', "-I$glfw\include")
    $flags = @('-std=c++11', '-O2', '-Wall', '-Wformat', '-DNDEBUG')
    $libs = @(
        '-static', '-static-libgcc', '-static-libstdc++',
        "-L$glfw\lib-mingw-w64", '-lglfw3',
        '-lopengl32', '-lgdi32', '-luser32', '-lshell32', '-lwinmm', '-limm32', '-lole32', '-luuid'
    )

    # ---- compile objects in parallel, then link ---------------------------
    $root = (Get-Location).Path
    New-Item -ItemType Directory -Force -Path 'dist', 'build' | Out-Null
    $exe = 'dist\tizen-sideloader.exe'
    if (Test-Path $exe) { Remove-Item $exe -Force }

    # The file-dialog unit is pure UI code and by far the slowest one to
    # optimise; -O1 keeps the build quick with no measurable runtime difference.
    $perFileFlags = @{
        'ImGuiFileDialog\ImGuiFileDialog.cpp' = @('-std=c++11', '-O1', '-Wall', '-Wformat', '-DNDEBUG')
    }

    # incremental: rebuild an object only when its source or imgui.h is newer
    $stamp = (Get-Item 'imgui.h').LastWriteTime
    $queue = New-Object System.Collections.Queue
    foreach ($src in $sources) {
        $obj = Join-Path 'build' (($src -replace '[\\/]', '_') -replace '\.cpp$', '.o')
        $objItem = Get-Item $obj -ErrorAction SilentlyContinue
        $upToDate = $objItem -and -not $Rebuild -and
            $objItem.LastWriteTime -gt (Get-Item $src).LastWriteTime -and
            $objItem.LastWriteTime -gt $stamp
        if ($upToDate) { continue }
        $queue.Enqueue([pscustomobject]@{ Source = $src; Object = $obj })
    }
    $maxJobs = [Math]::Max(2, [Environment]::ProcessorCount - 1)
    Write-Host "building : $exe  ($($queue.Count) of $($sources.Count) units need compiling, up to $maxJobs in parallel)"

    $jobs = @()
    $failed = @()
    while ($queue.Count -gt 0 -or $jobs.Count -gt 0) {
        while ($jobs.Count -lt $maxJobs -and $queue.Count -gt 0) {
            $unit = $queue.Dequeue()
            $flagsFor = if ($perFileFlags.ContainsKey($unit.Source)) { $perFileFlags[$unit.Source] } else { $flags }
            $jobs += Start-Job -ArgumentList $root, $gxx, $flagsFor, $includes, $unit.Source, $unit.Object -ScriptBlock {
                param($root, $gxx, $flags, $includes, $src, $obj)
                Set-Location $root
                $text = & $gxx @flags @includes -c $src -o $obj 2>&1 | Out-String
                [pscustomobject]@{ source = $src; code = $LASTEXITCODE; text = $text }
            }
        }
        $finished = Wait-Job -Job $jobs -Any
        $result = Receive-Job -Job $finished
        Remove-Job -Job $finished -Force
        $jobs = @($jobs | Where-Object { $_.Id -ne $finished.Id })
        if ($result.code -ne 0) {
            $failed += $result
            Write-Host "           FAILED: $($result.source)"
        }
        else {
            Write-Host "           ok: $($result.source)"
        }
    }
    if ($failed.Count -gt 0) {
        foreach ($f in $failed) { Write-Host "--- $($f.source) ---"; Write-Host $f.text }
        throw "$($failed.Count) source file(s) failed to compile"
    }

    $objects = @(Get-ChildItem build -Filter *.o | Select-Object -ExpandProperty FullName)
    Write-Host "linking  : $($objects.Count) objects"
    & $gxx @objects -o $exe @libs
    if ($LASTEXITCODE -ne 0) { throw "link failed (exit code $LASTEXITCODE)" }

    $item = Get-Item $exe
    Set-Content -Path 'dist\.build-ok' -Value (Get-Date -Format o)
    Write-Host ("built    : {0}  ({1:N0} kB)" -f $item.FullName, ($item.Length / 1KB))
}
finally {
    Pop-Location
}
