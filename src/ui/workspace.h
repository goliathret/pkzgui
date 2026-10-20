#pragma once
#include "app/app_menu.h"
#include "audio/audio.h"
#include "hud/hud_preview.h"
#include "model/model_preview.h"
#include "package/package_session.h"
#include "package/package_search.h"
#include "texture/texture.h"
#include "Resource/TextureUsage.h"
#include "ui/widgets.h"
#include <optional>
#include <string>

struct GLFWwindow;

class Workspace
{
public:
    explicit Workspace(GLFWwindow* window);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    void Open(const std::string& path);
    void Execute(Command command);
    void Draw();
    void UpdateMenus(AppMenu& menu) const;

private:
    struct PaneSizes
    {
        float chunksWidth = 280.0f;
        float inspectorWidth = 340.0f;
        float logHeight = 200.0f;
        float modelActionsHeight = 280.0f;
    };

    struct PendingReplacement
    {
        std::string path;
        int width = 0;
        int height = 0;
        bool dds = false;
    };

    bool IsBusy() const;
    bool IsReady() const;

    void PollBackgroundWork();
    void LogSessionChanges();
    void LogStatusNow();
    void RefreshSelection();

    void OpenPackage();
    void SavePackage(bool chooseDestination);
    void ExtractSelectedTexture(bool dds = false);
    void BeginTextureReplacement(bool dds = false);
    void ApplyTextureReplacement();
    void ExtractSelectedAudio();
    void PlaySelectedAudio();

    void DrawMainColumn(float width, float height);
    void DrawChunkPanel();
    void DrawResourcePanel();
    void DrawLogPanel();
    void DrawInspectorColumn(float height);
    void DrawFields();
    void DrawModelActionsPanel();
    void DrawTextureReferencesPanel();
    void NavigateTextureReference();
    void DrawStatusBar(float height);

    void DrawBusyOverlay();
    void DrawReplaceTextureDialog();
    void DrawDiscardChangesDialog();
    void DrawErrorDialog();

    GLFWwindow* window;
    PackageSession session;
    TexturePreview texturePreview;
    ModelPreview modelPreview;
    HudPreview hudPreview;
    AudioPlayer player;
    DebugLog log;
    PaneSizes panes;

    PackageSearch packageSearch;
    SelectionKey searchKey;
    char searchText[256] = {};
    bool focusSearch = false;
    bool revealSelection = false;
    TextureUsageIndex textureUsageIndex;
    SelectionKey usageIndexKey;
    SelectionKey usageSelection;
    std::vector<TextureUsageIndex::Usage> textureUsages;
    std::optional<TextureUsageIndex::Usage> pendingTextureReference;
    SelectionKey refreshed;
    std::optional<CMChunkResourceHeader> resource;
    std::optional<RZAudioSample> audio;
    std::string audioError;

    std::optional<PendingReplacement> replacement;
    bool confirmDiscard = false;
    bool discardApproved = false;
    Command deferred = Command::None;

    std::string loggedStatus;
    std::string loggedError;
    bool loggedLoading = false;
};
