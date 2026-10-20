@REM Build with the Visual Studio compiler.
@REM Run this from an "x64 Native Tools Command Prompt for VS" in the repository root.
@REM GLFW must be present in libs\glfw (build_windows.ps1 downloads it, or run it once
@REM with MinGW to vendor the folder). Sources are in the repo root, not in ..\..
@echo off
set OUT_DIR=dist
set OUT_EXE=tizen-sideloader
set INCLUDES=/I. /I.\backends /I.\libs\glfw\include
set SOURCES=main.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp backends\imgui_impl_glfw.cpp backends\imgui_impl_opengl3.cpp misc\cpp\imgui_stdlib.cpp ImGuiFileDialog\ImGuiFileDialog.cpp
set LIBS=/LIBPATH:.\libs\glfw\lib-vc2022 glfw3.lib opengl32.lib gdi32.lib user32.lib shell32.lib
if not exist %OUT_DIR% mkdir %OUT_DIR%
cl /nologo /EHsc /std:c++14 /O2 /MD /utf-8 %INCLUDES% %SOURCES% /Fe:%OUT_DIR%\%OUT_EXE%.exe /Fo:%OUT_DIR%\ /link %LIBS%
