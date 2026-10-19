#include "ui/workspace.h"
#include "platform/platform.h"
#include <utility>

namespace
{
    constexpr const char* kDecodingStatus = "Decoding XMA2 with ffmpeg...";
}

bool Workspace::IsBusy() const
{
    return session.IsLoading() || player.IsBusy();
}

bool Workspace::IsReady() const
{
    return !IsBusy() && !replacement && !confirmDiscard && session.error.empty();
}

void Workspace::Execute(Command command)
{
    if (command == Command::None || !IsReady())
        return;
    try
    {
        switch (command)
        {
        case Command::OpenPackage:
            OpenPackage();
            break;
        case Command::Save:
            SavePackage(false);
            break;
        case Command::SaveAs:
            SavePackage(true);
            break;
        case Command::FindPackage:
            focusSearch = true;
            break;
        case Command::ExtractTexture:
            ExtractSelectedTexture();
            break;
        case Command::ReplaceTexture:
            BeginTextureReplacement();
            break;
        case Command::ExtractAudio:
            ExtractSelectedAudio();
            break;
        case Command::PlayAudio:
            PlaySelectedAudio();
            break;
        default:
            break;
        }
    }
    catch (const std::exception& ex)
    {
        session.error = ex.what();
    }
}

void Workspace::UpdateMenus(AppMenu& menu) const
{
    const bool ready = IsReady();
    const bool hasPackage = session.package != nullptr;
    const bool textureSelected = ready && texturePreview.IsActive() && resource &&
        resource->GetResourceType() == kCMResourceType_Texture;
    const bool audioSelected = ready && audio.has_value();
    menu.SetEnabled(Command::OpenPackage, ready);
    menu.SetEnabled(Command::Save, ready && hasPackage);
    menu.SetEnabled(Command::SaveAs, ready && hasPackage);
    menu.SetEnabled(Command::FindPackage, ready && hasPackage);
    menu.SetEnabled(Command::ExtractTexture, textureSelected);
    menu.SetEnabled(Command::ReplaceTexture, textureSelected);
    menu.SetEnabled(Command::ExtractAudio, audioSelected);
    menu.SetEnabled(Command::PlayAudio, audioSelected);
}

void Workspace::PollBackgroundWork()
{
    session.PollLoad();
    player.Poll(session.status, session.error);
    LogSessionChanges();
}

void Workspace::LogSessionChanges()
{
    if (session.status != loggedStatus)
        LogStatusNow();
    if (session.IsLoading() && !loggedLoading)
        log.Add("Loading package...");
    loggedLoading = session.IsLoading();
    if (session.error != loggedError)
    {
        if (!session.error.empty())
            log.Add("Error: " + session.error);
        loggedError = session.error;
    }
}

void Workspace::LogStatusNow()
{
    log.Add(session.status);
    loggedStatus = session.status;
}

void Workspace::RefreshSelection()
{
    const SelectionKey key = SelectionKey::Of(session);
    if (key != refreshed)
    {
        refreshed = key;
        resource.reset();
        audio.reset();
        audioError.clear();
        player.Stop();
        if (key.package && key.chunk)
        {
            try
            {
                resource = session.SelectedResource();
                if (IsAudioSelection(session, resource))
                    audio = SelectedAudioSample(session);
            }
            catch (const std::exception& ex)
            {
                audioError = ex.what();
            }
        }
    }
    texturePreview.Update(session);
    modelPreview.Update(session);
    hudPreview.Update(session);
}

void Workspace::OpenPackage()
{
    if (session.dirty && !discardApproved)
    {
        confirmDiscard = true;
        return;
    }
    discardApproved = false;
    const auto path = PickPackage(window);
    if (path.empty())
        return;
    player.Stop();
    session.BeginLoad(path);
}

void Workspace::SavePackage(bool chooseDestination)
{
    if (!session.package)
        return;
    std::string destination = session.path;
    if (chooseDestination || destination.empty())
        destination = PickPackageDestination(window, FileName(session.path));
    if (!destination.empty())
        session.Save(destination);
}

void Workspace::ExtractSelectedTexture()
{
    const auto header = SelectedTextureHeader(session);
    const auto path = PickSaveFile(window, "PNG", "png", SanitizeFileName(header.GetName()) + ".png");
    if (path.empty())
        return;
    ExtractTexture(*session.package, header, path);
    session.status = "Extracted " + path;
}

void Workspace::BeginTextureReplacement()
{
    const RZTexture texture = SelectedTexture(session);
    const auto path = PickOpenFile(window, "PNG", "png");
    if (!path.empty())
        replacement = PendingReplacement{path, int(texture.desc.width), int(texture.desc.height)};
}

void Workspace::ApplyTextureReplacement()
{
    try
    {
        ReplaceTexture(session, SelectedTextureHeader(session), replacement->path,
            static_cast<uint32_t>(replacement->width), static_cast<uint32_t>(replacement->height));
        LogStatusNow();
    }
    catch (const std::exception& ex)
    {
        session.error = ex.what();
    }
}

void Workspace::ExtractSelectedAudio()
{
    RZAudioSample sample = SelectedAudioSample(session);
    const auto path = PickSaveFile(window, "WAV", "wav", SanitizeFileName(sample.name) + ".wav");
    if (path.empty())
        return;
    player.Export(std::move(sample), path);
    session.status = kDecodingStatus;
}

void Workspace::PlaySelectedAudio()
{
    if (player.IsLoaded())
    {
        player.Play();
        return;
    }
    player.LoadAndPlay(SelectedAudioSample(session));
    session.status = kDecodingStatus;
}
