#pragma once
#include "app/app_menu.h"
#include "package/package_session.h"
#include "Resource/RZAudioSample.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

bool IsAudioSelection(const PackageSession& session, const std::optional<CMChunkResourceHeader>& resource);
RZAudioSample SelectedAudioSample(const PackageSession& session);

class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool IsBusy() const;
    bool IsLoaded() const;
    bool IsPlaying() const;
    float Position() const;
    float Duration() const;
    const std::vector<float>& Waveform() const;

    void LoadAndPlay(RZAudioSample sample);
    void Export(RZAudioSample sample, std::string path);
    void Poll(std::string& status, std::string& error);

    void Play();
    void Pause();
    void Rewind();
    void Seek(float seconds);
    void Stop();

private:
    struct State;
    void Start(RZAudioSample sample, std::string exportPath);

    std::unique_ptr<State> state;
};

Command DrawAudioPreview(const RZAudioSample& sample, AudioPlayer& player);
void DrawAudioFields(const RZAudioSample& sample);
