#pragma once
#include "app/app_menu.h"
#include <memory>
#include <string>

struct GLFWwindow;
class Workspace;

class Application
{
public:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize();
    void Open(const std::string& path);
    void Run();

private:
    bool CreateMainWindow();
    bool InitializeImGui();
    Command PollShortcuts() const;
    void Execute(Command command);
    void RenderFrame();

    GLFWwindow* window = nullptr;
    bool glfwReady = false;
    bool imguiContextReady = false;
    bool imguiPlatformReady = false;
    bool imguiRendererReady = false;
    bool darkMode = false;
    AppMenu menu;
    std::unique_ptr<Workspace> workspace;
};
