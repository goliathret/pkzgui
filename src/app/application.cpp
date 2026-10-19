#include "app/application.h"
#include "render/gl.h"
#include "ui/workspace.h"
#include "pkzgui_font.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    constexpr int kInitialWidth = 1280;
    constexpr int kInitialHeight = 800;
    constexpr int kMinimumWidth = 480;
    constexpr int kMinimumHeight = 320;
    constexpr float kFontSize = 13.0f;
    constexpr float kBackground[4] = {0.08f, 0.09f, 0.11f, 1.0f};

    void ReportGlfwError(int error, const char* description)
    {
        std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
    }

    std::filesystem::path PreferenceFile()
    {
#ifdef _WIN32
        const char* appData = std::getenv("APPDATA");
        if (!appData)
            return {};
        return std::filesystem::u8path(appData) / "pkzgui" / "theme";
#else
        const char* home = std::getenv("HOME");
        if (!home)
            return {};
#ifdef __APPLE__
        return std::filesystem::u8path(home) / "Library" / "Application Support" / "pkzgui" / "theme";
#else
        const char* config = std::getenv("XDG_CONFIG_HOME");
        const auto root = config ? std::filesystem::u8path(config) : std::filesystem::u8path(home) / ".config";
        return root / "pkzgui" / "theme";
#endif
#endif
    }

    bool LoadDarkModePreference()
    {
        std::ifstream input(PreferenceFile());
        std::string value;
        input >> value;
        return value == "dark";
    }

    void SaveDarkModePreference(bool dark)
    {
        const auto file = PreferenceFile();
        if (file.empty())
            return;
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        std::ofstream(file) << (dark ? "dark" : "light") << '\n';
    }

    void ApplyTheme(bool dark)
    {
        if (dark)
            ImGui::StyleColorsDark();
        else
            ImGui::StyleColorsLight();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = style.ChildRounding = style.FrameRounding = 0;
        style.ItemSpacing = ImVec2(8, 0);
        if (dark)
        {
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1);
            return;
        }
        style.Colors[ImGuiCol_WindowBg] = ImVec4(1, 1, 1, 1);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.94f, 0.94f, 0.94f, 1);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.96f, 0.96f, 0.96f, 1);
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(1, 1, 1, 1);
    }
}

Application::Application() = default;

Application::~Application()
{
    workspace.reset();
    menu.Shutdown();

    // shut down while window STILL EXISTS.
    if (imguiRendererReady)
        ImGui_ImplOpenGL3_Shutdown();
    if (imguiPlatformReady)
        ImGui_ImplGlfw_Shutdown();
    if (imguiContextReady)
        ImGui::DestroyContext();

    imguiRendererReady = false;
    imguiPlatformReady = false;
    imguiContextReady = false;

    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    if (glfwReady)
        glfwTerminate();
}

bool Application::Initialize()
{
    glfwSetErrorCallback(ReportGlfwError);
    glfwReady = glfwInit() == GLFW_TRUE;
    if (!glfwReady || !CreateMainWindow() || !InitializeImGui() || !menu.Init(window))
        return false;
    workspace = std::make_unique<Workspace>(window);
    return true;
}

bool Application::CreateMainWindow()
{
    ApplyOpenGLContextHints();
    window = glfwCreateWindow(kInitialWidth, kInitialHeight, "Goliath Studio X", nullptr, nullptr);
    if (!window)
        return false;
    glfwSetWindowSizeLimits(window, kMinimumWidth, kMinimumHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    return true;
}

bool Application::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextReady = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(FontData), sizeof(FontData), kFontSize, &fontConfig);

    darkMode = LoadDarkModePreference();
    ApplyTheme(darkMode);

    imguiPlatformReady = ImGui_ImplGlfw_InitForOpenGL(window, true);
    imguiRendererReady = imguiPlatformReady && ImGui_ImplOpenGL3_Init(kGlslVersion);
    return imguiRendererReady;
}

void Application::Open(const std::string& path)
{
    workspace->Open(path);
}

void Application::Run()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        Command command = menu.DrawAndPoll();
        if (const Command shortcut = PollShortcuts(); shortcut != Command::None)
            command = shortcut;
        Execute(command);
        workspace->Draw();
        workspace->UpdateMenus(menu);
        menu.SetChecked(Command::ToggleTheme, darkMode);

        RenderFrame();
    }
}

Command Application::PollShortcuts() const
{
    const ImGuiIO& io = ImGui::GetIO();
#ifdef __APPLE__
    const bool modifier = io.KeySuper;
#else
    const bool modifier = io.KeyCtrl;
#endif
    if (!modifier)
        return Command::None;
    if (ImGui::IsKeyPressed(ImGuiKey_O, false))
        return Command::OpenPackage;
    if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        return Command::Save;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        return Command::FindPackage;
    return Command::None;
}

void Application::Execute(Command command)
{
    switch (command)
    {
    case Command::ToggleTheme:
        darkMode = !darkMode;
        ApplyTheme(darkMode);
        SaveDarkModePreference(darkMode);
        break;
    case Command::Exit:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    default:
        workspace->Execute(command);
        break;
    }
}

void Application::RenderFrame()
{
    ImGui::Render();
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(kBackground[0], kBackground[1], kBackground[2], kBackground[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}
