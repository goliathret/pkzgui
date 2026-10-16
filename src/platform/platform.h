#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

#ifdef _WIN32
constexpr const char* kExecutableSuffix = ".exe";
#else
constexpr const char* kExecutableSuffix = "";
#endif

std::vector<uint8_t> ReadFile(const std::filesystem::path& path);
void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);
std::string SanitizeFileName(std::string name);
std::string FileName(const std::string& path);

class TemporaryDirectory
{
public:
    TemporaryDirectory();
    ~TemporaryDirectory();
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& Path() const { return path; }

private:
    std::filesystem::path path;
};

std::string PickOpenFile(GLFWwindow* parent, const char* filterName, const char* extensions);
std::string PickSaveFile(GLFWwindow* parent, const char* filterName, const char* extensions, const std::string& defaultName);
std::string PickPackage(GLFWwindow* parent);
std::string PickPackageDestination(GLFWwindow* parent, const std::string& defaultName);

std::filesystem::path ExecutableDirectory();
int RunProcess(const std::vector<std::filesystem::path>& arguments, std::chrono::seconds timeout);
