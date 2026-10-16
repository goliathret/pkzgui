#include "platform/platform.h"
#include <nfd.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#include <cerrno>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#if defined(__APPLE__) || defined(_WIN32)
#include <nfd_glfw3.h>
#endif

namespace
{
    constexpr const char* kPackageFilter = "Package files";
    constexpr const char* kPackageExtensions = "pkz,pak";

    const char* DialogError()
    {
        const char* error = NFD_GetError();
        return error ? error : "Native file dialog failed.";
    }

    class DialogSession
    {
    public:
        DialogSession()
        {
            if (NFD_Init() != NFD_OKAY)
                throw std::runtime_error(DialogError());
        }
        ~DialogSession() { NFD_Quit(); }
        DialogSession(const DialogSession&) = delete;
        DialogSession& operator=(const DialogSession&) = delete;
    };

    nfdwindowhandle_t NativeParent(GLFWwindow* window)
    {
        nfdwindowhandle_t parent{};
        if (!window)
            return parent;
#if defined(__APPLE__) || defined(_WIN32)
        NFD_GetNativeWindowFromGLFWWindow(window, &parent);
#else
        (void)window;
#endif
        return parent;
    }

    std::string TakeDialogResult(nfdresult_t result, nfdu8char_t* path)
    {
        if (result == NFD_ERROR)
            throw std::runtime_error(DialogError());
        if (result == NFD_CANCEL)
            return {};
        const std::unique_ptr<nfdu8char_t, decltype(&NFD_FreePathU8)> owned(path, NFD_FreePathU8);
        if (result != NFD_OKAY || !path)
            throw std::runtime_error("Native file dialog returned no path.");
        return std::string(path);
    }

    std::string ProgramName(const std::vector<std::filesystem::path>& arguments)
    {
        return arguments.front().filename().u8string();
    }

#ifdef _WIN32
    std::wstring QuoteArgument(const std::wstring& value)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (wchar_t c : value)
        {
            if (c == L'\\')
            {
                ++slashes;
                continue;
            }
            result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += c;
        }
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }
#endif
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open file: " + path.u8string());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (input.bad())
        throw std::runtime_error("Cannot read file: " + path.u8string());
    return bytes;
}

void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Cannot open file for writing: " + path.u8string());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Failed writing file: " + path.u8string());
}

std::string SanitizeFileName(std::string name)
{
    static const std::string reserved = "/\\:*?\"<>|";
    if (name.empty())
        name = "sample";
    for (char& c : name)
        if (reserved.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32)
            c = '_';
    return name;
}

std::string FileName(const std::string& path)
{
    return std::filesystem::u8path(path).filename().u8string();
}

TemporaryDirectory::TemporaryDirectory()
{
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        path = std::filesystem::temp_directory_path() /
            ("pkzgui-" + std::to_string(random()) + "-" + std::to_string(random()));
        if (std::filesystem::create_directory(path))
            return;
    }
    throw std::runtime_error("Cannot create a temporary directory.");
}

TemporaryDirectory::~TemporaryDirectory()
{
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

std::string PickOpenFile(GLFWwindow* parent, const char* filterName, const char* extensions)
{
    const DialogSession session;
    const nfdu8filteritem_t filter = {filterName, extensions};
    nfdopendialogu8args_t args{};
    args.filterList = &filter;
    args.filterCount = 1;
    args.parentWindow = NativeParent(parent);
    nfdu8char_t* path = nullptr;
    const nfdresult_t result = NFD_OpenDialogU8_With(&path, &args);
    return TakeDialogResult(result, path);
}

std::string PickSaveFile(GLFWwindow* parent, const char* filterName, const char* extensions, const std::string& defaultName)
{
    const DialogSession session;
    const nfdu8filteritem_t filter = {filterName, extensions};
    nfdsavedialogu8args_t args{};
    args.filterList = &filter;
    args.filterCount = 1;
    args.parentWindow = NativeParent(parent);
    args.defaultName = defaultName.c_str();
    nfdu8char_t* path = nullptr;
    const nfdresult_t result = NFD_SaveDialogU8_With(&path, &args);
    return TakeDialogResult(result, path);
}

std::string PickPackage(GLFWwindow* parent)
{
    return PickOpenFile(parent, kPackageFilter, kPackageExtensions);
}

std::string PickPackageDestination(GLFWwindow* parent, const std::string& defaultName)
{
    return PickSaveFile(parent, kPackageFilter, kPackageExtensions, defaultName);
}

std::filesystem::path ExecutableDirectory()
{
    std::filesystem::path executable;
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size && size < buffer.size())
        executable = std::wstring(buffer.data(), size);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        executable = buffer.data();
#else
    std::vector<char> buffer(32768);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size > 0 && static_cast<size_t>(size) < buffer.size())
        executable = std::string(buffer.data(), size_t(size));
#endif
    return executable.parent_path();
}

#ifdef _WIN32
int RunProcess(const std::vector<std::filesystem::path>& arguments, std::chrono::seconds timeout)
{
    std::wstring command;
    for (const auto& argument : arguments)
        command += (command.empty() ? L"" : L" ") + QuoteArgument(argument.native());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(arguments.front().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
            nullptr, &startup, &process))
        throw std::runtime_error("Failed to launch " + ProgramName(arguments) + ".");

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    const bool finished = WaitForSingleObject(process.hProcess, static_cast<DWORD>(milliseconds)) == WAIT_OBJECT_0;
    if (!finished)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!finished)
        throw std::runtime_error(ProgramName(arguments) + " timed out.");
    return static_cast<int>(exitCode);
}
#else
int RunProcess(const std::vector<std::filesystem::path>& arguments, std::chrono::seconds timeout)
{
    std::vector<std::string> strings;
    for (const auto& argument : arguments)
        strings.push_back(argument.native());
    std::vector<char*> argv;
    for (auto& argument : strings)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t process = 0;
    if (posix_spawn(&process, argv.front(), nullptr, nullptr, argv.data(), environ))
        throw std::runtime_error("Failed to launch " + ProgramName(arguments) + ".");

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    for (;;)
    {
        const pid_t result = waitpid(process, &status, WNOHANG);
        if (result == process)
            break;
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("Failed waiting for " + ProgramName(arguments) + ".");
        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(process, SIGKILL);
            while (waitpid(process, &status, 0) < 0 && errno == EINTR)
            {
            }
            throw std::runtime_error(ProgramName(arguments) + " timed out.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif
