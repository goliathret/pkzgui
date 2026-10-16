#pragma once
#include "Package/PKPackage.h"
#include "Resource/ResourceHeader.h"
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct PackageSession
{
    std::unique_ptr<PKPackage> package;
    const CMChunk* selected = nullptr;
    std::string path;
    std::string status = "No package loaded.";
    std::string error;
    bool dirty = false;
    uint64_t revision = 0;

    bool Load(const std::string& packagePath);
    bool BeginLoad(const std::string& packagePath);
    void PollLoad();
    bool IsLoading() const { return pending.valid(); }
    bool Save(const std::string& destination);

    std::optional<CMChunkResourceHeader> SelectedResource() const;
    void MarkEdited(std::string message);

private:
    std::vector<uint8_t> savedBytes;
    std::future<std::unique_ptr<PackageSession>> pending;
};

struct SelectionKey
{
    const PKPackage* package = nullptr;
    const CMChunk* chunk = nullptr;
    uint64_t revision = 0;

    static SelectionKey Of(const PackageSession& session)
    {
        return {session.package.get(), session.selected, session.revision};
    }

    bool SameChunk(const SelectionKey& other) const { return package == other.package && chunk == other.chunk; }
    bool operator==(const SelectionKey& other) const { return SameChunk(other) && revision == other.revision; }
    bool operator!=(const SelectionKey& other) const { return !(*this == other); }
};
