#include "audio/audio.h"
#include "platform/platform.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value)
    {
        bytes[at] = uint8_t(value >> 8);
        bytes[at + 1] = uint8_t(value);
    }

    void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            bytes[at + i] = uint8_t(value >> (24 - 8 * i));
    }

    std::vector<uint8_t> SampleData()
    {
        std::vector<uint8_t> payload(87, 0);
        Put32(payload, 4, 4);
        Put32(payload, 24, 3);
        Put16(payload, 28, 0x166);
        Put16(payload, 30, 2);
        Put32(payload, 32, 48000);
        Put16(payload, 42, 16);
        Put16(payload, 46, 1);
        payload[84] = 1;
        payload[85] = 2;
        payload[86] = 3;
        return payload;
    }

    void SampleDataTests()
    {
        PackageSession session;
        session.package = std::make_unique<PKPackage>();
        session.package->rootChunks.push_back(CMChunk::Leaf(Audio_SampleData, 1, SampleData()));
        session.selected = &session.package->rootChunks[0];

        const auto sample = SelectedAudioSample(session);
        Check(sample.format.channels == 2 && sample.format.samplesPerSec == 48000 &&
                sample.data == std::vector<uint8_t>({1, 2, 3}),
            "Wrong XMA payload/format");

        const auto wave = sample.BuildWave();
        Check(wave.size() == 83 && std::string(wave.begin(), wave.begin() + 4) == "RIFF", "RIFF size/header wrong");
        Check(wave[4] == 75 && wave[20] == 0x66 && wave[21] == 1 && wave[22] == 2, "XMA format endianness changed");
        Check(wave[36] == 34 && wave[37] == 0 && wave[76] == 3 && wave[80] == 1 && wave[82] == 3, "WAV extension/payload wrong");

        session.package->rootChunks[0].data.pop_back();
        bool rejected = false;
        try
        {
            SelectedAudioSample(session);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Check(rejected, "Truncated audio accepted");
    }

    void CutsceneTests()
    {
        const auto payload = SampleData();
        std::vector<uint8_t> header(76), data(23);
        std::copy(payload.begin() + 28, payload.begin() + 80, header.begin() + 24);
        Put32(data, 16, 3);
        data[20] = 1;
        data[21] = 2;
        data[22] = 3;

        PackageSession session;
        session.package = std::make_unique<PKPackage>();
        session.package->rootChunks.push_back(CMChunk::Leaf(CutScene_Audio_SampleHeader, 1, header));
        session.package->rootChunks.push_back(CMChunk::Leaf(CutScene_Audio_SampleData, 1, data));
        session.selected = &session.package->rootChunks[1];
        Check(SelectedAudioSample(session).data == std::vector<uint8_t>({1, 2, 3}), "Cutscene audio resolution differs");
    }

    void TemporaryDirectoryTests()
    {
        std::filesystem::path location;
        {
            TemporaryDirectory files;
            location = files.Path();
            WriteFile(location / "test", {1});
            Check(std::filesystem::exists(location), "Temporary directory missing");
        }
        Check(!std::filesystem::exists(location), "Temporary files leaked");
    }
}

int main()
{
    try
    {
        SampleDataTests();
        CutsceneTests();
        TemporaryDirectoryTests();
        std::cout << "XMA2 format, RIFF wrapper, cutscene and cleanup tests passed\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
