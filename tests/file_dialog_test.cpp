#include "platform/platform.h"
#include "package/package_session.h"
#include <nfd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    nfdresult_t dialogResult = NFD_OKAY, initResult = NFD_OKAY;
    const char* dialogError = nullptr;
    bool supplyPath = true;
    int freed = 0;
    std::string chosenPath = "C:/packages/caf\xC3\xA9.pkz";

    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    nfdresult_t Choose(nfdu8char_t** outPath)
    {
        if (dialogResult == NFD_OKAY && supplyPath)
        {
            *outPath = static_cast<nfdu8char_t*>(std::malloc(chosenPath.size() + 1));
            if (!*outPath) throw std::bad_alloc();
            std::memcpy(*outPath, chosenPath.c_str(), chosenPath.size() + 1);
        }
        return dialogResult;
    }

    template<typename Function>
    void ExpectError(Function function, const char* message)
    {
        try { function(); }
        catch (const std::runtime_error& error)
        {
            Check(std::string(error.what()) == message, "Unexpected dialog error");
            return;
        }
        throw std::runtime_error("Expected a dialog error");
    }
}

// Replace the native backend so these tests never display a file picker.
extern "C"
{
    nfdresult_t NFD_Init() { return initResult; }
    void NFD_Quit() {}
    const char* NFD_GetError() { return dialogError; }
    void NFD_FreePathU8(nfdu8char_t* path) { if (path) ++freed; std::free(path); }
    nfdresult_t NFD_OpenDialogU8_With_Impl(nfdversion_t, nfdu8char_t** path, const nfdopendialogu8args_t*)
    { return Choose(path); }
    nfdresult_t NFD_SaveDialogU8_With_Impl(nfdversion_t, nfdu8char_t** path, const nfdsavedialogu8args_t*)
    { return Choose(path); }
}

int main(int argc, char** argv)
{
    try
    {
        for (bool save : {false, true})
        {
            const auto pick = [save] {
                return save ? PickSaveFile(nullptr, "Package", "pkz", "edited.pkz") : PickPackage(nullptr);
            };
            dialogResult = NFD_OKAY;
            Check(pick() == chosenPath, "Selected UTF-8 path was lost");
            dialogResult = NFD_CANCEL;
            Check(pick().empty(), "Cancelled picker should return an empty path");
            dialogResult = NFD_ERROR;
            ExpectError(pick, "Native file dialog failed.");
            dialogError = "Backend failure";
            ExpectError(pick, "Backend failure");
            dialogError = nullptr;
            dialogResult = NFD_OKAY;
            supplyPath = false;
            ExpectError(pick, "Native file dialog returned no path.");
            supplyPath = true;
            initResult = NFD_ERROR;
            ExpectError(pick, "Native file dialog failed.");
            initResult = NFD_OKAY;
        }
        Check(freed == 2, "Selected paths must be freed exactly once");

        Check(argc > 1, "Fixture directory missing");
        const auto directory = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(directory);
        const auto file = directory / std::filesystem::u8path("caf\xC3\xA9.pkz");
        PKPackage fixture;
        fixture.rootChunks.push_back(CMChunk::Container(CMChunkTypes::Root, 1));
        WriteFile(file, fixture.Serialize());
        chosenPath = file.u8string();
        PackageSession session;
        Check(session.Load(PickPackage(nullptr)), "Picked package should load");
        Check(session.package && session.selected, "Loaded package should have a selected chunk");
        Check(freed == 3, "Loaded package path was not freed");
        std::cout << "File dialog tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
