#include "app/app_menu.h"
#include <cstdio>
#include <string>

#ifdef PKZGUI_NATIVE_MENUS
#include "native_menu_bar.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#else
#include "imgui.h"
#endif

#ifdef __APPLE__
extern "C" void SetMenuShortcut(void* item, const char* key);
#endif

namespace
{
    struct MenuItem
    {
        const char* label;
        const char* shortcut = nullptr;
        Command command = Command::None;
    };

    constexpr MenuItem kSeparator = {nullptr};

    constexpr MenuItem kFileItems[] = {
        {"Open package...", "Ctrl+O", Command::OpenPackage},
        {"Save", "Ctrl+S", Command::Save},
        {"Save As...", nullptr, Command::SaveAs},
        {"Load schema..."},
#ifndef __APPLE__
        kSeparator,
        {"Exit", nullptr, Command::Exit},
#endif
    };
    constexpr MenuItem kEditItems[] = {
        {"Find in package...", "Ctrl+F", Command::FindPackage},
        {"Edit selected field...", "Enter"},
    };
    constexpr MenuItem kViewItems[] = {
        {"Expand all"},
        {"Collapse all"},
        kSeparator,
        {"Simple view"},
        kSeparator,
        {"Dark mode", nullptr, Command::ToggleTheme},
    };
    constexpr MenuItem kTextureItems[] = {
        {"Extract texture (PNG)...", nullptr, Command::ExtractTexture},
        {"Replace texture (PNG)...", nullptr, Command::ReplaceTexture},
        {"Extract texture (DDS)...", nullptr, Command::ExtractTextureDds},
        {"Replace texture (DDS)...", nullptr, Command::ReplaceTextureDds},
    };
    constexpr MenuItem kAudioItems[] = {
        {"Extract sample (WAV)...", nullptr, Command::ExtractAudio},
        {"Play sample", nullptr, Command::PlayAudio},
    };

    struct Menu
    {
        const char* label;
        const MenuItem* items;
        size_t count;
    };

    template<size_t N>
    constexpr Menu DefineMenu(const char* label, const MenuItem (&items)[N])
    {
        return {label, items, N};
    }

    constexpr Menu kMenus[] = {
        DefineMenu("File", kFileItems),
        DefineMenu("Edit", kEditItems),
        DefineMenu("View", kViewItems),
        DefineMenu("Texture", kTextureItems),
        DefineMenu("Audio", kAudioItems),
    };
}

bool AppMenu::Init(GLFWwindow* window)
{
    enabled[Index(Command::OpenPackage)] = true;
    enabled[Index(Command::ToggleTheme)] = true;
    enabled[Index(Command::Exit)] = true;
#ifdef PKZGUI_NATIVE_MENUS
#ifdef _WIN32
    nmb_setup(glfwGetWin32Window(window), nmb_SetupFlags_none);
#else
    (void)window;
    nmb_setup(nullptr, nmb_SetupFlags_none);
#endif
    initialized = true;
    int position = 0;
    for (const Menu& definition : kMenus)
    {
        nmb_Handle menu = nmb_insertMenu(nullptr, position++, definition.label);
        if (!menu)
            break;
        for (size_t i = 0; i < definition.count; ++i)
        {
            const MenuItem& item = definition.items[i];
            if (!item.label)
            {
                nmb_appendSeparator(menu);
                continue;
            }
            std::string caption = item.label;
#ifdef _WIN32
            if (item.shortcut)
                caption += std::string("\t") + item.shortcut;
#endif
            nmb_Handle handle = nmb_appendMenuItem(menu, caption.c_str());
            if (!handle)
                break;
            nmb_setMenuItemEnabled(handle, enabled[Index(item.command)]);
            if (item.command != Command::None)
                items[Index(item.command)] = handle;
#ifdef __APPLE__
            if (item.command == Command::OpenPackage)
                SetMenuShortcut(handle, "o");
            if (item.command == Command::Save)
                SetMenuShortcut(handle, "s");
            if (item.command == Command::FindPackage)
                SetMenuShortcut(handle, "f");
#endif
        }
    }
    if (*nmb_getLastError())
    {
        std::fprintf(stderr, "Native menu error: %s\n", nmb_getLastError());
        return false;
    }
#else
    (void)window;
#endif
    return true;
}

void AppMenu::Shutdown()
{
#ifdef PKZGUI_NATIVE_MENUS
    if (initialized)
        nmb_shutdown();
#endif
    initialized = false;
    items.fill(nullptr);
    enabled.fill(false);
}

Command AppMenu::DrawAndPoll()
{
    Command triggered = Command::None;
#ifdef PKZGUI_NATIVE_MENUS
    nmb_Event event;
    while (nmb_pollEvent(&event))
    {
        if (event.event != nmb_EventType_itemTriggered)
            continue;
        for (size_t i = 1; i < items.size(); ++i)
        {
            if (items[i] != event.sender || !enabled[i])
                continue;
            triggered = static_cast<Command>(i);
            if (triggered == Command::Exit)
                return triggered;
        }
    }
#else
    if (!ImGui::BeginMainMenuBar())
        return triggered;
    for (const Menu& menu : kMenus)
    {
        if (!ImGui::BeginMenu(menu.label))
            continue;
        for (size_t i = 0; i < menu.count; ++i)
        {
            const MenuItem& item = menu.items[i];
            const size_t index = Index(item.command);
            if (!item.label)
                ImGui::Separator();
            else if (ImGui::MenuItem(item.label, item.shortcut, checked[index], enabled[index]))
                triggered = item.command;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
#endif
    return triggered;
}

void AppMenu::SetEnabled(Command command, bool value)
{
    const size_t index = Index(command);
    if (enabled[index] == value)
        return;
    enabled[index] = value;
#ifdef PKZGUI_NATIVE_MENUS
    if (items[index])
        nmb_setMenuItemEnabled(items[index], value);
#endif
}

void AppMenu::SetChecked(Command command, bool value)
{
    const size_t index = Index(command);
    if (checked[index] == value)
        return;
    checked[index] = value;
#ifdef PKZGUI_NATIVE_MENUS
    if (items[index])
        nmb_setMenuItemChecked(items[index], value);
#endif
}
