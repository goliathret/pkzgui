#pragma once
#include <array>
#include <cstddef>

struct GLFWwindow;

enum class Command
{
    None,
    OpenPackage,
    Save,
    SaveAs,
    ExtractTexture,
    ReplaceTexture,
    ExtractAudio,
    PlayAudio,
    ToggleTheme,
    Exit,
    Count,
};

constexpr std::size_t kCommandCount = static_cast<std::size_t>(Command::Count);

constexpr std::size_t Index(Command command)
{
    return static_cast<std::size_t>(command);
}

class AppMenu
{
public:
    bool Init(GLFWwindow* window);
    void Shutdown();
    Command DrawAndPoll();
    void SetEnabled(Command command, bool enabled);
    void SetChecked(Command command, bool checked);

private:
    std::array<void*, kCommandCount> items{};
    std::array<bool, kCommandCount> enabled{};
    std::array<bool, kCommandCount> checked{};
    bool initialized = false;
};
