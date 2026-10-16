#include "texture/texture.h"
#include "platform/platform.h"
#include "BaseUtils/BUPng.h"
#include <iostream>

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    template<typename Function>
    bool Throws(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    PKPackage BuildFixture()
    {
        const std::vector<uint8_t> pixels(16 * 16 * 4, 255);
        const RZTexture texture = RZTexture::FromRGBA8("test texture", pixels.data(), 16, 16);
        PKPackage fixture;
        auto root = CMChunk::Container(Root, 1);
        auto library = CMChunk::Container(Gen_TextureLibrary, 1);
        library.AddChild(texture.BuildLibraryResource(0));
        const uint64_t postOffset = root.HeaderSize() + library.SerializedSize();
        library.children[0] = texture.BuildLibraryResource(postOffset);
        root.AddChild(std::move(library));
        root.AddChild(texture.BuildPostLoadData(postOffset));
        fixture.rootChunks.push_back(std::move(root));
        return fixture;
    }

    const CMChunk* TextureChunk(const PackageSession& session)
    {
        return session.package->rootChunks[0].children[0].children[0].FindChild(Texture);
    }

    void RunRoundTrip(const PKPackage& fixture, const std::filesystem::path& dir, bool compressed)
    {
        const auto source = dir / (compressed ? "source.pkz" : "source.pak");
        auto bytes = fixture.Serialize();
        if (compressed)
            bytes = BUCompress::Compress(bytes);
        WriteFile(source, bytes);

        PackageSession session;
        Check(session.Load(source.u8string()), "Fixture did not load");
        session.selected = TextureChunk(session);
        const auto original = SelectedTexture(session);
        const auto header = SelectedTextureHeader(session);

        ExtractTexture(*session.package, header, (dir / "export.png").u8string());
        const auto png = BUPng::Decode(ReadFile(dir / "export.png"));
        Check(png.width == 16 && png.height == 16 && png.rgba == original.DecodeLevel0(), "Export pixels differ");

        std::vector<uint8_t> replacement(8 * 8 * 4, 0);
        for (size_t i = 0; i < replacement.size(); i += 4)
        {
            replacement[i] = 255;
            replacement[i + 3] = 255;
        }
        WriteFile(dir / "replacement.png", BUPng::EncodeRGBA8(replacement.data(), 8, 8));

        const auto textureSelection = session.selected;
        session.selected = &session.package->rootChunks[0];
        ReplaceTexture(session, header, (dir / "replacement.png").u8string(), 16, 16);
        Check(session.selected == &session.package->rootChunks[0], "Explicit texture replacement changed the model selection");

        session.selected = textureSelection;
        const auto changed = SelectedTexture(session);
        Check(session.dirty && changed.desc.d3dFormat == original.desc.d3dFormat && changed.gpuData.size() == original.gpuData.size(),
            "Layout/format changed");
        const auto rgba = changed.DecodeLevel0();
        Check(rgba[0] == 255 && rgba[1] == 0 && rgba[3] == 255, "Replacement pixels incorrect");

        const auto updated = SelectedTextureHeader(session);
        const auto post = session.package->FindRootAtOffset(updated.GetDataOffset());
        const auto crc = RZTexture::PostLoadCRC(post->FindChild(Texture_Data)->data, false);
        Check(updated.GetPostLoadDataCRC() == crc &&
                CMChunkResourceHeader(*post->FindChild(GenSub_ResourceHeader)).GetPostLoadDataCRC() == crc,
            "CRCs differ");

        Check(session.Save(source.u8string()) && !session.dirty, "Save failed");
        PackageSession reloaded;
        Check(reloaded.Load(source.u8string()) && reloaded.package->wasCompressed == compressed, "Saved package did not reload");
        reloaded.selected = TextureChunk(reloaded);
        Check(SelectedTexture(reloaded).DecodeLevel0() == rgba, "Saved pixels differ");

        const auto saveAs = dir / "save-as.pak";
        WriteFile(saveAs, std::vector<uint8_t>(bytes.size(), 9));
        Check(session.Save(saveAs.u8string()), "Save As failed");
        Check(reloaded.Load(saveAs.u8string()), "Save As corrupted an existing target");

        const auto before = session.package->Serialize();
        const bool rejected = Throws([&] {
            ReplaceTexture(session, SelectedTextureHeader(session), (dir / "replacement.png").u8string(), 32, 16);
        });
        Check(rejected && before == session.package->Serialize(), "Invalid dimensions modified package");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Expected fixture directory");
        const auto dir = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(dir);
        const PKPackage fixture = BuildFixture();
        for (bool compressed : {false, true})
            RunRoundTrip(fixture, dir, compressed);
        std::cout << "Texture export, replacement, CRC and save tests passed\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
