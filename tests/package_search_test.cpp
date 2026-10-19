#include "package/package_search.h"
#include "Resource/ResourceHeader.h"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        PKPackage package;
        auto root = CMChunk::Container(Root, 1);
        auto library = CMChunk::Container(Gen_GeometryLibrary, 1);
        auto model = CMChunk::Container(GenSub_Resource, 1);
        model.AddChild(CMChunkResourceHeader::Build(0xBEEF, kCMResourceType_Geometry, 31, 0, 0, 0,
                                                    "SpiderManScarlet[Hi]"));
        model.AddChild(CMChunk::Leaf(HAnim_Info, 21, {}));
        library.AddChild(model);
        root.AddChild(library);
        package.rootChunks.push_back(root);
        const auto* resource = &package.rootChunks[0].children[0].children[0];
        PackageSearch search;
        search.Build(&package);
        search.Filter("SPIDER scarlet geometry");
        Check(search.Count() == 1 && search.Result(0).chunk == resource,
              "Case insensitive multi-term resource search failed");
        search.Filter("0xBeEf");
        Check(search.Count() == 1 && search.Result(0).chunk == resource, "Resource CRC search failed");
        search.Filter("0x4b2");
        Check(search.Count() == 1 && search.Result(0).chunk == &resource->children[1] &&
                  search.Result(0).path.find("SpiderManScarlet") != std::string::npos,
              "Chunk ID search or ancestor path failed");
        search.Filter("hanim_info");
        Check(search.Count() == 1, "Chunk type search failed");
        search.Filter("no such resource");
        Check(search.Count() == 0, "Missing query retained old results");
        search.Filter("   ");
        Check(search.Count() == 0, "Whitespace search returned every chunk");
        search.Build(nullptr);
        search.Filter("spider");
        Check(search.Count() == 0, "Index retained pointers after unloading");
        std::puts("PASS: package search names, types, IDs, CRCs, paths and cache reset");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
