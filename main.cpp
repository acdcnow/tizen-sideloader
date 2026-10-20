// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ImGuiFileDialog/ImGuiFileDialog.h"

#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <string>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <stdlib.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <cctype>
#include <cstdio>
#ifndef _WIN32
#include <sys/wait.h>
#endif

void setImGuiWindowSizeToOSWindow(GLFWwindow* window);

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ---------------------------------------------------------------------------
// Tizen CLI invocation and logging.
//
// On Windows the tools are sdb.exe / tizen.bat and tizen-studio is commonly
// installed under "C:\Program Files\...", so the tool path is quoted; on Unix
// the original unquoted form is used.
//
// Every command runs on a worker thread so the window keeps rendering, and its
// stdout+stderr is captured into an in-app log window. That log is what the
// dialogs point at now - a GUI user never sees the console.
// ---------------------------------------------------------------------------
#ifdef _WIN32
#define TIZEN_POPEN  _popen
#define TIZEN_PCLOSE _pclose
#else
#define TIZEN_POPEN  popen
#define TIZEN_PCLOSE pclose
#endif

static const char* kDefaultSdbPort = "26101";

// Shared between the UI thread and the command workers.
struct LogState
{
    std::mutex mutex;
    std::string text;
    bool busy = false;
    bool has_unseen_failure = false;
    bool has_unseen_success = false;
    std::string title;
    std::string transcript;
};

static std::shared_ptr<LogState> g_log = std::make_shared<LogState>();
static bool g_show_log = false;

static std::string trim_path(std::string path)
{
    while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
        path.pop_back();
    return path;
}

static std::string tizen_tool_path(const std::string& tizenloc,
                                   const char* relative_windows,
                                   const char* relative_unix)
{
    const std::string base = trim_path(tizenloc);
#ifdef _WIN32
    return std::string("\"") + base + "\\" + relative_windows + "\"";
#else
    (void)relative_windows;
    return base + "/" + relative_unix;
#endif
}

static std::string sdb_path(const std::string& tizenloc)
{
    return tizen_tool_path(tizenloc, "tools\\sdb.exe", "tools/sdb");
}

static std::string tizen_cli_path(const std::string& tizenloc)
{
    return tizen_tool_path(tizenloc, "tools\\ide\\bin\\tizen.bat", "tools/ide/bin/tizen");
}

// Bare paths (no quoting) used for the existence check and the error dialog.
static std::string sdb_executable(const std::string& tizenloc)
{
#ifdef _WIN32
    return trim_path(tizenloc) + "\\tools\\sdb.exe";
#else
    return trim_path(tizenloc) + "/tools/sdb";
#endif
}

static std::string tizen_executable(const std::string& tizenloc)
{
#ifdef _WIN32
    return trim_path(tizenloc) + "\\tools\\ide\\bin\\tizen.bat";
#else
    return trim_path(tizenloc) + "/tools/ide/bin/tizen";
#endif
}

static bool file_exists(const std::string& path)
{
    std::FILE* handle = std::fopen(path.c_str(), "rb");
    if (handle == nullptr)
        return false;
    std::fclose(handle);
    return true;
}

static bool sdb_available(const std::string& tizenloc)
{
    return file_exists(sdb_executable(tizenloc));
}

// "192.168.1.50" -> "192.168.1.50:26101". A value that already carries a port
// is passed through unchanged, so an unusual setup still works.
static std::string with_default_port(const std::string& ip)
{
    std::string target = ip;
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())))
        target.erase(target.begin());
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
        target.pop_back();
    if (target.empty())
        return target;
    if (target.find(':') == std::string::npos)
        target += std::string(":") + kDefaultSdbPort;
    return target;
}

static std::string sdb_connect_command(const std::string& tizenloc, const std::string& target)
{
    return sdb_path(tizenloc) + " connect " + target;
}

static std::string sdb_devices_command(const std::string& tizenloc)
{
    return sdb_path(tizenloc) + " devices";
}

// Diagnostics shown when a command fails: the device list plus the TV's
// capabilities, so the log states the platform version the TV reports.
static std::string sdb_diagnostics_command(const std::string& tizenloc, const std::string& target)
{
#ifdef _WIN32
    const char* joiner = " & ";
#else
    const char* joiner = " ; ";
#endif
    std::string command = sdb_devices_command(tizenloc) + " 2>&1";
    if (!target.empty())
        command += std::string(joiner) + sdb_path(tizenloc) + " -s " + target + " capability 2>&1";
    return command;
}

static std::string tizen_install_command(const std::string& tizenloc, const std::string& wgt)
{
    return tizen_cli_path(tizenloc) + " install -n \"" + wgt + "\"";
}

struct CommandResult
{
    std::string output;
    int exit_code = -1;
    double seconds = 0.0;
};

// Runs one command through the shell with stderr folded into stdout.
static CommandResult run_command(const std::string& command)
{
    CommandResult result;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const std::string shell_command = command + " 2>&1";

    FILE* pipe = TIZEN_POPEN(shell_command.c_str(), "r");
    if (pipe == nullptr)
    {
        result.output = "could not start a shell for: " + command + "\n";
        return result;
    }

    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        result.output += buffer;

    const int status = TIZEN_PCLOSE(pipe);
#ifdef _WIN32
    result.exit_code = status;
#else
    result.exit_code = (status == -1) ? -1 : WEXITSTATUS(status);
#endif
    result.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

static void append_log_locked(LogState& state, const std::string& text)
{
    state.text += text;
    const std::size_t limit = 200000;
    if (state.text.size() > limit)
    {
        state.text.erase(0, state.text.size() - (limit * 3 / 4));
        state.text.insert(0, "[... earlier output trimmed ...]\n");
    }
}

static std::string log_snapshot()
{
    std::lock_guard<std::mutex> lock(g_log->mutex);
    return g_log->text;
}

static std::string tail_of(const std::string& text, std::size_t max_chars = 2400)
{
    if (text.size() <= max_chars)
        return text;
    return "[...]\n" + text.substr(text.size() - max_chars);
}

// Turns a raw installer error into something actionable. Only codes that were
// verified against a real TV are spelled out; everything else stays generic.
static std::string install_failure_hint(const std::string& text)
{
    if (text.find("install failed[118019]") != std::string::npos)
        return "118019: the TV rejected the certificate inside this .wgt.\n"
               "A Samsung TV only installs packages signed with a Samsung "
               "distributor\ncertificate that contains this TV's DUID (created "
               "in Tizen Studio >\nCertificate Manager, or via `tizen "
               "certificate`). Unsigned packages fail\nthe same way - the "
               "certificate is the blocker, not the connection.";
    if (text.find("install failed[118") != std::string::npos)
        return "install failed[118xxx] is a certificate/signature rejection: the "
               ".wgt must be\nsigned for this TV (Samsung distributor "
               "certificate with the TV's DUID).";
    if (text.find("device offline") != std::string::npos ||
        text.find("device unauthorized") != std::string::npos)
        return "The TV is listed but offline/unauthorized: re-enable developer "
               "mode,\nre-enter this PC's IP address and reboot the TV, then "
               "connect again.";
    if (text.find("target not found") != std::string::npos ||
        text.find("closed") != std::string::npos)
        return "The TV closed the connection: check that the developer-mode "
               "host IP\nis this PC, that the TV is not in standby, and that no "
               "firewall blocks\nthe sdb server.";
    return "";
}

// Starts `command` on a worker thread. When it fails, `diagnostic_command` is
// run as well (e.g. `sdb devices`) so the log explains the failure.
static void start_command(const std::string& title, const std::string& command,
                          const std::string& diagnostic_command)
{
    {
        std::lock_guard<std::mutex> lock(g_log->mutex);
        if (g_log->busy)
            return;
        g_log->busy = true;
    }

    // Keeps the shared state alive even if the app is closing while we run.
    const std::shared_ptr<LogState> state = g_log;
    std::thread([state, title, command, diagnostic_command]()
    {
        std::string transcript = "\n=== " + title + " ===\n$ " + command + "\n";

        CommandResult result = run_command(command);
        transcript += result.output;
        if (!result.output.empty() && result.output.back() != '\n')
            transcript += "\n";

        const bool ok = (result.exit_code == 0);
        if (!ok && !diagnostic_command.empty())
        {
            CommandResult diagnostic = run_command(diagnostic_command);
            transcript += "\n$ " + diagnostic_command + "\n" + diagnostic.output;
            if (!diagnostic.output.empty() && diagnostic.output.back() != '\n')
                transcript += "\n";
        }

        char summary[160];
        std::snprintf(summary, sizeof(summary), "[%s, exit code %d, %.1f s]\n",
                      ok ? "OK" : "FAILED", result.exit_code, result.seconds);
        transcript += summary;

        std::lock_guard<std::mutex> lock(state->mutex);
        append_log_locked(*state, transcript);
        state->busy = false;
        state->title = title;
        state->transcript = transcript;
        if (ok)
            state->has_unseen_success = true;
        else
            state->has_unseen_failure = true;
    }).detach();
}

// Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Tizen Sideloader", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        setImGuiWindowSizeToOSWindow(window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static char str0[64] = "";
            static std::string filePath;
            static std::string filePathName;
            static int tizendialog = 0;
            static char tizenloc[155] = "";
            static int demowindow = 0;


            ImGui::Begin("Tizen Sideloader");                         
            bool busy = false;
            bool failure_pending = false;
            bool success_pending = false;
            bool last_was_sideload = false;
            std::string result_text;
            std::string last_title;
            {
                std::lock_guard<std::mutex> lock(g_log->mutex);
                busy = g_log->busy;
                failure_pending = g_log->has_unseen_failure;
                success_pending = g_log->has_unseen_success;
                result_text = g_log->transcript;
                last_title = g_log->title;
                last_was_sideload = (g_log->title.rfind("tizen install", 0) == 0);
                g_log->has_unseen_failure = false;
                g_log->has_unseen_success = false;
            }
            if (failure_pending)
                ImGui::OpenPopup("Command failed");
            if (success_pending)
                ImGui::OpenPopup("Command finished");

            ImGui::Text("TV IP address (port %s is added automatically):", kDefaultSdbPort);
            ImGui::PushItemWidth(200);
            ImGui::InputTextWithHint("##IP", "192.168.1.50", str0, IM_ARRAYSIZE(str0));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("or IP:port to override");

            const std::string target = with_default_port(str0);
            if (target.empty())
                ImGui::TextDisabled("target: <IP>:%s", kDefaultSdbPort);
            else
                ImGui::TextDisabled("target: %s", target.c_str());

            if (!std::string(tizenloc).empty() && !sdb_available(tizenloc))
                ImGui::TextColored(ImVec4(1.00f, 0.45f, 0.45f, 1.00f),
                                   "sdb.exe not found - check the tizen-studio folder");

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Connect!"))
            {
                if (std::string(tizenloc).empty())
                {
                    tizendialog++;
                }
                else if (!sdb_available(tizenloc))
                {
                    ImGui::OpenPopup("Tizen Studio CLI not found");
                }
                else if (target.empty())
                {
                    ImGui::OpenPopup("No IP address");
                }
                else
                {
                    start_command("sdb connect " + target,
                                  sdb_connect_command(tizenloc, target),
                                  sdb_devices_command(tizenloc));
                }
            }
            ImGui::SameLine();
            if (busy)
                ImGui::TextColored(ImVec4(1.00f, 0.75f, 0.20f, 1.00f), "running ...");
            else if (!last_title.empty())
                ImGui::TextDisabled("last command: %s", last_title.c_str());
            ImGui::EndDisabled();

            if (ImGui::Button("Choose WGT", ImVec2(250,100))) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".wgt", config);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filePathName.empty() ? "no .wgt selected yet" : filePathName.c_str());

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Sideload!", ImVec2(250,150)))
            {
                if (filePathName.empty())
                {
                    ImGui::OpenPopup("No WGT selected");
                }
                else if (std::string(tizenloc).empty())
                {
                    tizendialog++;
                }
                else if (!file_exists(tizen_executable(tizenloc)))
                {
                    ImGui::OpenPopup("Tizen Studio CLI not found");
                }
                else
                {
                    start_command("tizen install " + filePathName,
                                  tizen_install_command(tizenloc, filePathName),
                                  sdb_diagnostics_command(tizenloc, target));
                }
            }
            ImGui::EndDisabled();

            if (ImGui::Button("Configure tizen-studio", ImVec2(250,50)))
                tizendialog++;
            ImGui::SameLine();
            if (ImGui::Button(g_show_log ? "Hide log" : "Show log", ImVec2(120, 50)))
                g_show_log = !g_show_log;

            if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
                if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                    filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                    filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                    // action
                    std::cout << filePath;

                }
    
            // close
            ImGuiFileDialog::Instance()->Close();
        }

            if (ImGui::Button("Open ImGui demo window", ImVec2(250,20)))
                demowindow++;
  


           if (tizendialog & 1) {
               ImGui::OpenPopup("Configure tizen-studio");
           }

           if (demowindow & 1) {
               ImGui::ShowDemoWindow();
           }
             
           if (ImGui::BeginPopupModal("Tizen Studio CLI not found", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("The Tizen Studio CLI was not found at:");
                ImGui::TextDisabled("%s", tizenloc);
                ImGui::TextUnformatted("Expected these files:");
                ImGui::TextDisabled("  %s", sdb_executable(tizenloc).c_str());
                ImGui::TextDisabled("  %s", tizen_executable(tizenloc).c_str());
                ImGui::Separator();
                ImGui::TextUnformatted("Install \"Tizen Studio with CLI\" (the IDE is not needed), then");
                ImGui::TextUnformatted("press \"Configure tizen-studio\" and point it at that folder.");
                ImGui::TextLinkOpenURL("Download Tizen Studio", "https://developer.tizen.org/development/tizen-studio/download");
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
        }

           if (ImGui::BeginPopupModal("No IP address", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Enter the IP address of the TV, e.g. 192.168.1.50.");
                ImGui::TextUnformatted("Port 26101 is used automatically; append :port to override it.");
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
        }

           if (ImGui::BeginPopupModal("No WGT selected", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Choose a .wgt file first (button \"Choose WGT\").");
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
        }

           if (ImGui::BeginPopupModal("Command failed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                const std::string hint = install_failure_hint(result_text);
                ImGui::TextUnformatted("The Tizen CLI reported an error. Full output:");
                if (!hint.empty())
                {
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.75f, 0.20f, 1.00f));
                    ImGui::TextUnformatted(hint.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
                ImGui::BeginChild("failure_log", ImVec2(760, 260), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(tail_of(result_text).c_str());
                ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                ImGui::TextUnformatted("Hints: TV in developer mode with this PC's IP registered, same network,");
                ImGui::TextUnformatted("tizen-studio CLI path set, TV reachable on port 26101.");
                ImGui::Separator();
                if (ImGui::Button("Copy to clipboard", ImVec2(160, 0))) { ImGui::SetClipboardText(result_text.c_str()); }
                ImGui::SameLine();
                if (ImGui::Button(g_show_log ? "Hide log" : "Show log", ImVec2(120, 0))) { g_show_log = !g_show_log; }
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
        }

           if (ImGui::BeginPopupModal("Command finished", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.40f, 1.00f),
                                   last_was_sideload ? "Sideload completed." : "Command completed.");
                ImGui::TextUnformatted(last_was_sideload
                                       ? "The app should now show up on your TV."
                                       : "See the log for the full output.");
                ImGui::Separator();
                ImGui::BeginChild("success_log", ImVec2(700, 160), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(tail_of(result_text, 1200).c_str());
                ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
        }

           if (ImGui::BeginPopupModal("Configure tizen-studio", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Enter the location of tizen-studio *CLI*");
                ImGui::Text("You can download it");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL("here", "https://developer.tizen.org/development/tizen-studio/download");
                ImGui::InputText("Please do not include a trailing slash", tizenloc, IM_ARRAYSIZE(tizenloc));
                ImGui::SameLine();;

                ImGui::Separator();

                if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                    tizendialog = 0;
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    ImGui::EndPopup();
            }
            







}

ImGui::End();
        

        if (g_show_log)
        {
            ImGui::SetNextWindowSize(ImVec2(900, 420), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Log", &g_show_log))
            {
                const std::string log_text = log_snapshot();
                static std::size_t last_size = 0;

                if (ImGui::Button("Copy"))
                    ImGui::SetClipboardText(log_text.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                {
                    std::lock_guard<std::mutex> lock(g_log->mutex);
                    g_log->text.clear();
                    last_size = 0;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%d characters - every command is captured here", static_cast<int>(log_text.size()));
                ImGui::Separator();

                ImGui::BeginChild("log_view", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(log_text.c_str(), log_text.c_str() + log_text.size());
                if (log_text.size() != last_size)
                {
                    last_size = log_text.size();
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

void setImGuiWindowSizeToOSWindow(GLFWwindow* window) {
    int windowWidth, windowHeight;
    glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
}