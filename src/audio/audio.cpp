#include "audio/audio.h"
#include "platform/platform.h"
#include "ui/widgets.h"
#include "imgui.h"
#include "miniaudio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <stdexcept>

namespace
{
    constexpr auto kDecodeTimeout = std::chrono::seconds(60);
    constexpr size_t kWaveformBins = 512;
    constexpr ma_uint64 kDecodeBlockFrames = 4096;
    constexpr float kMaxWaveformHeight = 180.0f;
    constexpr float kWaveformInset = 8.0f;
    constexpr float kTransportButtonWidth = 64.0f;

    struct DecodedAudio
    {
        std::shared_ptr<TemporaryDirectory> files;
        std::filesystem::path wav;
        std::vector<float> waveform;
        std::string message;
        bool play = false;
    };

    bool IsAudioResource(const std::optional<CMChunkResourceHeader>& resource)
    {
        return resource && resource->GetResourceType() == kCMResourceType_AudioSample;
    }

    std::filesystem::path FindFfmpeg()
    {
        const std::string name = std::string("ffmpeg") + kExecutableSuffix;
        const auto beside = ExecutableDirectory() / name;
        if (std::filesystem::is_regular_file(beside))
            return beside;
        if (std::filesystem::is_regular_file(name))
            return std::filesystem::absolute(name);
#ifdef PKZGUI_FFMPEG_PATH
        if (std::filesystem::is_regular_file(PKZGUI_FFMPEG_PATH))
            return PKZGUI_FFMPEG_PATH;
#endif
        throw std::runtime_error("FFmpeg not found beside pkzgui. Build external/FFmpeg and rebuild pkzgui; see README.md.");
    }

    void DecodeXma2ToWav(const std::filesystem::path& input, const std::filesystem::path& output)
    {
        const int exitCode = RunProcess(
            {FindFfmpeg(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", input, "-c:a", "pcm_s16le", output},
            kDecodeTimeout);
        if (exitCode != 0)
            throw std::runtime_error("FFmpeg failed to decode XMA2.");
    }

    std::string DescribeFormat(const RZAudioSample& sample)
    {
        return std::to_string(sample.format.samplesPerSec) + " Hz, " + std::to_string(sample.format.channels) + " ch";
    }

    std::vector<float> ComputeWaveform(const std::filesystem::path& wav)
    {
        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file(wav.u8string().c_str(), &config, &decoder) != MA_SUCCESS)
            throw std::runtime_error("Cannot read decoded audio waveform.");
        const std::unique_ptr<ma_decoder, decltype(&ma_decoder_uninit)> cleanup(&decoder, ma_decoder_uninit);

        std::vector<float> peaks(kWaveformBins, 0.0f);
        ma_uint64 length = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &length);
        if (!length)
            return peaks;

        const ma_uint32 channels = decoder.outputChannels;
        std::vector<float> samples(kDecodeBlockFrames * channels);
        for (ma_uint64 offset = 0;;)
        {
            ma_uint64 count = 0;
            const ma_result result = ma_decoder_read_pcm_frames(&decoder, samples.data(), kDecodeBlockFrames, &count);
            for (ma_uint64 frame = 0; frame < count; ++frame)
            {
                float& peak = peaks[std::min<size_t>(kWaveformBins - 1, (offset + frame) * kWaveformBins / length)];
                for (ma_uint32 channel = 0; channel < channels; ++channel)
                    peak = std::max(peak, std::abs(samples[frame * channels + channel]));
            }
            offset += count;
            if (result != MA_SUCCESS || !count)
                break;
        }
        return peaks;
    }

    DecodedAudio Decode(const RZAudioSample& sample, const std::string& exportPath)
    {
        DecodedAudio decoded;
        decoded.files = std::make_shared<TemporaryDirectory>();
        const auto input = decoded.files->Path() / "xma.wav";
        decoded.wav = decoded.files->Path() / "decoded.wav";
        WriteFile(input, sample.BuildWave());
        DecodeXma2ToWav(input, decoded.wav);

        decoded.play = exportPath.empty();
        if (decoded.play)
        {
            decoded.waveform = ComputeWaveform(decoded.wav);
            decoded.message = "Playing " + sample.name + " (" + DescribeFormat(sample) + ")";
        }
        else
        {
            std::filesystem::copy_file(decoded.wav, std::filesystem::u8path(exportPath),
                std::filesystem::copy_options::overwrite_existing);
            decoded.message = "Extracted " + exportPath + " (" + DescribeFormat(sample) + ")";
        }
        return decoded;
    }

    std::string Timestamp(float seconds)
    {
        const int tenths = static_cast<int>(std::max(0.0f, seconds) * 10);
        const int total = tenths / 10;
        char text[32];
        std::snprintf(text, sizeof(text), "%d:%02d.%d", total / 60, total % 60, tenths % 10);
        return text;
    }

    void DrawWaveform(const AudioPlayer& player, float height)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), height);
        ImGui::InvisibleButton("Audio waveform", ImVec2(size.x, std::max(1.0f, size.y)));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);

        const auto& waveform = player.Waveform();
        const float center = origin.y + height / 2;
        if (waveform.empty())
        {
            const char* hint = "Press Play to load the waveform";
            const ImVec2 text = ImGui::CalcTextSize(hint);
            if (size.x > text.x && height > text.y)
                drawList->AddText(ImVec2(origin.x + (size.x - text.x) / 2, center - text.y / 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
            return;
        }

        const float progress = player.Duration() > 0 ? std::clamp(player.Position() / player.Duration(), 0.0f, 1.0f) : 0.0f;
        const float maxAmplitude = std::max(0.0f, height / 2 - kWaveformInset);
        for (size_t i = 0; i < waveform.size(); ++i)
        {
            const float x = origin.x + (i + 0.5f) * size.x / waveform.size();
            const float amplitude = std::clamp(waveform[i], 0.0f, 1.0f) * maxAmplitude;
            const bool played = float(i) / waveform.size() < progress;
            drawList->AddLine(ImVec2(x, center - amplitude), ImVec2(x, center + amplitude),
                ImGui::GetColorU32(played ? ImGuiCol_SliderGrabActive : ImGuiCol_PlotHistogram));
        }
        const float playhead = origin.x + progress * size.x;
        drawList->AddLine(ImVec2(playhead, origin.y), ImVec2(playhead, origin.y + height), ImGui::GetColorU32(ImGuiCol_Text));
    }

    void DrawSeekBar(AudioPlayer& player)
    {
        const std::string time = Timestamp(player.Position()) + " / " + Timestamp(player.Duration());
        const float timeWidth = ImGui::CalcTextSize(time.c_str()).x;
        ImGui::BeginDisabled(!player.IsLoaded());
        float position = player.Position();
        ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - timeWidth - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::SliderFloat("##Audio position", &position, 0.0f, player.Duration(), ""))
            player.Seek(position);
        ImGui::SameLine();
        ImGui::TextUnformatted(time.c_str());
        ImGui::EndDisabled();
    }

    Command DrawTransport(AudioPlayer& player)
    {
        Command requested = Command::None;
        if (ImGui::Button(player.IsPlaying() ? "Pause" : "Play", ImVec2(kTransportButtonWidth, 0)))
        {
            if (player.IsPlaying())
                player.Pause();
            else
                requested = Command::PlayAudio;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!player.IsLoaded());
        if (ImGui::Button("Stop"))
            player.Rewind();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Extract WAV..."))
            requested = Command::ExtractAudio;
        return requested;
    }
}

bool IsAudioSelection(const PackageSession& session, const std::optional<CMChunkResourceHeader>& resource)
{
    return session.selected && (IsAudioResource(resource) || RZAudioSample::IsSampleChunk(*session.selected));
}

RZAudioSample SelectedAudioSample(const PackageSession& session)
{
    if (!session.package || !session.selected)
        throw std::runtime_error("Select an audio sample resource first.");
    const auto resource = session.SelectedResource();
    RZAudioSample sample = IsAudioResource(resource) ? RZAudioSample::Load(*session.package, *resource)
                                                     : RZAudioSample::FromChunk(*session.package, *session.selected);
    if (sample.name.empty())
        sample.name = "sample";
    return sample;
}

struct AudioPlayer::State
{
    std::future<DecodedAudio> pending;
    std::shared_ptr<TemporaryDirectory> files;
    std::vector<float> waveform;
    ma_engine engine{};
    ma_sound sound{};
    bool engineReady = false;
    bool soundLoaded = false;
    bool finishReported = false;

    void OpenSound(DecodedAudio& decoded)
    {
        if (!engineReady)
        {
            if (ma_engine_init(nullptr, &engine) != MA_SUCCESS)
                throw std::runtime_error("Cannot open audio playback device.");
            engineReady = true;
        }
        CloseSound();
        if (ma_sound_init_from_file(&engine, decoded.wav.u8string().c_str(), 0, nullptr, nullptr, &sound) != MA_SUCCESS)
            throw std::runtime_error("Cannot load decoded WAV for playback.");
        soundLoaded = true;
        files = std::move(decoded.files);
        waveform = std::move(decoded.waveform);
    }

    void CloseSound()
    {
        if (soundLoaded)
            ma_sound_uninit(&sound);
        soundLoaded = false;
        files.reset();
        waveform.clear();
    }
};

AudioPlayer::AudioPlayer() : state(std::make_unique<State>())
{
}

AudioPlayer::~AudioPlayer()
{
    Stop();
    if (state->engineReady)
        ma_engine_uninit(&state->engine);
}

bool AudioPlayer::IsBusy() const
{
    return state->pending.valid();
}

bool AudioPlayer::IsLoaded() const
{
    return state->soundLoaded;
}

bool AudioPlayer::IsPlaying() const
{
    return IsLoaded() && ma_sound_is_playing(&state->sound);
}

float AudioPlayer::Position() const
{
    float seconds = 0;
    if (IsLoaded())
        ma_sound_get_cursor_in_seconds(&state->sound, &seconds);
    return seconds;
}

float AudioPlayer::Duration() const
{
    float seconds = 0;
    if (IsLoaded())
        ma_sound_get_length_in_seconds(&state->sound, &seconds);
    return seconds;
}

const std::vector<float>& AudioPlayer::Waveform() const
{
    return state->waveform;
}

void AudioPlayer::LoadAndPlay(RZAudioSample sample)
{
    Start(std::move(sample), {});
}

void AudioPlayer::Export(RZAudioSample sample, std::string path)
{
    Start(std::move(sample), std::move(path));
}

void AudioPlayer::Start(RZAudioSample sample, std::string exportPath)
{
    if (IsBusy())
        return;
    if (exportPath.empty())
        Stop();
    state->pending = std::async(std::launch::async, [sample = std::move(sample), exportPath = std::move(exportPath)] {
        return Decode(sample, exportPath);
    });
}

void AudioPlayer::Poll(std::string& status, std::string& error)
{
    if (IsLoaded() && ma_sound_at_end(&state->sound) && !state->finishReported)
    {
        state->finishReported = true;
        status = "Playback finished.";
    }
    if (!IsBusy() || state->pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        DecodedAudio decoded = state->pending.get();
        if (decoded.play)
        {
            state->OpenSound(decoded);
            Play();
        }
        status = std::move(decoded.message);
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        status = "Audio operation failed.";
    }
}

void AudioPlayer::Play()
{
    if (!IsLoaded())
        return;
    if (ma_sound_at_end(&state->sound))
        Seek(0);
    state->finishReported = false;
    if (ma_sound_start(&state->sound) != MA_SUCCESS)
        throw std::runtime_error("Cannot start audio playback.");
}

void AudioPlayer::Pause()
{
    if (IsLoaded())
        ma_sound_stop(&state->sound);
}

void AudioPlayer::Rewind()
{
    Pause();
    Seek(0);
}

void AudioPlayer::Seek(float seconds)
{
    if (!IsLoaded())
        return;
    ma_uint32 sampleRate = 0;
    ma_uint64 length = 0;
    ma_sound_get_data_format(&state->sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    ma_sound_get_length_in_pcm_frames(&state->sound, &length);
    if (length && sampleRate)
    {
        const auto frame = static_cast<ma_uint64>(std::clamp(seconds, 0.0f, Duration()) * sampleRate);
        ma_sound_seek_to_pcm_frame(&state->sound, std::min(length - 1, frame));
    }
    state->finishReported = false;
}

void AudioPlayer::Stop()
{
    state->CloseSound();
}

Command DrawAudioPreview(const RZAudioSample& sample, AudioPlayer& player)
{
    ImGui::TextWrapped("%s", sample.name.c_str());
    ImGui::TextDisabled("%u Hz, %u channels", sample.format.samplesPerSec, sample.format.channels);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float controlsHeight = ImGui::GetFrameHeight() * 2 + style.ItemSpacing.y;
    const float space = std::max(0.0f, ImGui::GetContentRegionAvail().y - controlsHeight - style.ItemSpacing.y);
    const float height = std::min(kMaxWaveformHeight, space);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (space - height) / 2);
    DrawWaveform(player, height);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, ImGui::GetContentRegionAvail().y - controlsHeight));
    DrawSeekBar(player);
    return DrawTransport(player);
}

void DrawAudioFields(const RZAudioSample& sample)
{
    const auto& format = sample.format;
    FieldRow("formatTag", "u16", HexString(format.formatTag) + " (XMA2)");
    FieldRow("channels", "u16", std::to_string(format.channels));
    FieldRow("samplesPerSec", "u32", std::to_string(format.samplesPerSec));
    FieldRow("avgBytesPerSec", "u32", std::to_string(format.avgBytesPerSec));
    FieldRow("blockAlign", "u16", std::to_string(format.blockAlign));
    FieldRow("bitsPerSample", "u16", std::to_string(format.bitsPerSample));
    FieldRow("numStreams", "u16", std::to_string(format.numStreams));
    FieldRow("samplesEncoded", "u32", std::to_string(format.samplesEncoded));
    FieldRow("bytesPerBlock", "u32", std::to_string(format.bytesPerBlock));
    FieldRow("blockCount", "u16", std::to_string(format.blockCount));
    FieldRow("payload", "bytes", std::to_string(sample.data.size()));
}
