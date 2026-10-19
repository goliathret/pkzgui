#include "ui/workspace.h"
#include "ui/widgets.h"
#include <algorithm>
#include <utility>

namespace
{
    constexpr float kGap = 4.0f;
    const ImVec2 kPanelPadding(8, 8);
    const ImVec2 kInspectorPadding(10, 8);
    constexpr const char* kReplaceTexturePopup = "Replace texture";
    constexpr const char* kDiscardChangesPopup = "Open package";
    constexpr const char* kErrorPopup = "Operation failed";

    void DrawChunkFields(const CMChunk& chunk)
    {
        FieldRow("Chunk", "", ToString(chunk.GetIDToEnum()));
        FieldRow("ID", "u32", HexString(chunk.GetMaskedID()));
        FieldRow("Version", "u16", std::to_string(chunk.version));
        FieldRow("Length", "", std::to_string(chunk.length));
        FieldRow("Offset", "", HexString(chunk.offset));
        FieldRow("Header Size", "", std::to_string(chunk.HeaderSize()));
        FieldRow("Children", "", std::to_string(chunk.children.size()));
    }

    void DrawResourceFields(const CMChunkResourceHeader& header)
    {
        FieldRow("name", "string", header.GetName());
        FieldRow("type", "", header.GetResourceTypeName());
        FieldRow("crc", "u32", HexString(header.GetCRC()));
        FieldRow("languageMask", "u32", HexString(header.GetLanguageMask()));
        FieldRow("qualityLevel", "u32", std::to_string(header.GetQualityLevel()));
        FieldRow("dataOffset", "u64", HexString(header.GetDataOffset()));
        FieldRow("postLoadDataCRC", "u32", HexString(header.GetPostLoadDataCRC()));
    }
}

Workspace::Workspace(GLFWwindow* window) : window(window)
{
}

void Workspace::Open(const std::string& path)
{
    session.BeginLoad(path);
}

void Workspace::Draw()
{
    PollBackgroundWork();
    if (deferred != Command::None)
        Execute(std::exchange(deferred, Command::None));

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Workspace", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    const float statusHeight = ImGui::GetFrameHeight();
    const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - statusHeight);
    const float width = ImGui::GetContentRegionAvail().x;
    panes.inspectorWidth = std::clamp(panes.inspectorWidth, 180.0f, std::max(180.0f, width * 0.45f));

    DrawMainColumn(std::max(1.0f, width - panes.inspectorWidth - kGap), bodyHeight);
    ImGui::SameLine(0, 0);
    Splitter("Fields splitter", ImVec2(kGap, bodyHeight), SplitAxis::Horizontal, panes.inspectorWidth, -1.0f);
    ImGui::SameLine(0, 0);
    DrawInspectorColumn(bodyHeight);
    DrawStatusBar(statusHeight);

    ImGui::End();
    ImGui::PopStyleVar();

    DrawBusyOverlay();
    DrawReplaceTextureDialog();
    DrawDiscardChangesDialog();
    DrawErrorDialog();
}

void Workspace::DrawMainColumn(float width, float height)
{
    panes.chunksWidth = std::clamp(panes.chunksWidth, 100.0f, std::max(100.0f, width - 120.0f));
    panes.logHeight = std::clamp(panes.logHeight, 60.0f, std::max(60.0f, height - 160.0f));

    ImGui::BeginChild("Main panels", ImVec2(width, height));
    ImGui::BeginChild("Upper panels", ImVec2(0, height - panes.logHeight - kGap));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kPanelPadding);
    ImGui::BeginDisabled(IsBusy());
    DrawChunkPanel();
    ImGui::SameLine(0, 0);
    Splitter("Chunk splitter", ImVec2(kGap, ImGui::GetContentRegionAvail().y), SplitAxis::Horizontal, panes.chunksWidth);
    ImGui::SameLine(0, 0);
    RefreshSelection();
    DrawResourcePanel();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    Splitter("Log splitter", ImVec2(width, kGap), SplitAxis::Vertical, panes.logHeight, -1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kPanelPadding);
    DrawLogPanel();
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void Workspace::DrawChunkPanel()
{
    BeginPanel("Chunks", ImVec2(panes.chunksWidth, 0), ImGuiWindowFlags_HorizontalScrollbar);
    PanelTitle("Chunks");
    const SelectionKey key{session.package.get(), nullptr, session.revision};
    const bool rebuilt = key != searchKey;
    if (rebuilt)
    {
        packageSearch.Build(session.package.get());
        searchKey = key;
    }
    if (focusSearch)
    {
        ImGui::SetKeyboardFocusHere();
        focusSearch = false;
    }
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::InputTextWithHint("##Package search", "Search names, types, IDs...", searchText,
        sizeof(searchText), ImGuiInputTextFlags_EscapeClearsAll);
    if (changed || rebuilt)
        packageSearch.Filter(searchText);
    if (searchText[0])
    {
        ImGui::TextDisabled("%zu matches", packageSearch.Count());
        ImGuiListClipper clipper;
        clipper.Begin(int(packageSearch.Count()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto& result = packageSearch.Result(size_t(row));
                ImGui::PushID(result.chunk);
                if (ImGui::Selectable(result.label.c_str(), result.chunk == session.selected))
                    session.selected = result.chunk;
                ImGui::SetItemTooltip("%s", result.path.c_str());
                ImGui::PopID();
            }
    }
    else if (session.package)
        DrawChunkTree(*session.package, session.selected);
    ImGui::EndChild();
}

void Workspace::DrawResourcePanel()
{
    BeginPanel("Resource view", ImVec2(0, 0));
    PanelTitle("Resource view");
    if (texturePreview.IsActive())
        texturePreview.Draw();
    else if (modelPreview.IsActive())
        modelPreview.Draw();
    else if (hudPreview.IsActive())
        hudPreview.Draw();
    else if (audio)
        Execute(DrawAudioPreview(*audio, player));
    else
        ImGui::TextDisabled("Select a texture, geometry model, hierarchy, HUD, or audio sample.");
    ImGui::EndChild();
}

void Workspace::DrawLogPanel()
{
    for (std::string& message : modelPreview.TakeLogMessages())
        log.Add(std::move(message));
    BeginPanel("Debug log", ImVec2(0, 0));
    log.Draw();
    ImGui::EndChild();
}

void Workspace::DrawInspectorColumn(float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kInspectorPadding);
    ImGui::BeginChild("Inspector column", ImVec2(0, height));
    const float top = ImGui::GetCursorPosY();
    const bool showModelActions = modelPreview.IsActive();
    panes.modelActionsHeight = std::clamp(panes.modelActionsHeight, 100.0f, std::max(100.0f, height - 120.0f));
    const float fieldsHeight = showModelActions ? std::max(1.0f, height - panes.modelActionsHeight - kGap) : height;

    BeginPanel("Fields", ImVec2(0, fieldsHeight));
    ImGui::BeginDisabled(IsBusy());
    DrawFields();
    ImGui::EndDisabled();
    ImGui::EndChild();

    if (showModelActions)
    {
        ImGui::SetCursorPosY(top + fieldsHeight);
        Splitter("Model exports splitter", ImVec2(ImGui::GetContentRegionAvail().x, kGap), SplitAxis::Vertical,
            panes.modelActionsHeight, -1.0f);
        ImGui::SetCursorPosY(top + fieldsHeight + kGap);
        DrawModelActionsPanel();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void Workspace::DrawFields()
{
    ImGui::TextUnformatted("Fields");
    if (resource)
    {
        const std::string name = resource->GetName();
        ImGui::TextUnformatted(name.c_str());
        ImGui::SetItemTooltip("%s", name.c_str());
    }
    else if (session.selected)
        ImGui::TextUnformatted(ToString(session.selected->GetIDToEnum()).c_str());
    ImGui::Separator();

    if (!BeginFieldTable())
        return;
    if (session.selected)
    {
        DrawChunkFields(*session.selected);
        if (resource)
            DrawResourceFields(*resource);
        texturePreview.DrawFields();
        modelPreview.DrawFields();
        hudPreview.DrawFields();
        if (audio)
            DrawAudioFields(*audio);
        if (!audioError.empty())
            FieldRow("error", "", audioError);
    }
    EndFieldTable();
}

void Workspace::DrawModelActionsPanel()
{
    BeginPanel("Model exports", ImVec2(0, panes.modelActionsHeight));
    ImGui::BeginDisabled(IsBusy());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    modelPreview.DrawActions(session, window);
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void Workspace::DrawStatusBar(float height)
{
    ImGui::BeginChild("Status", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(session.IsLoading() ? "Loading package..." : session.status.c_str());
    ImGui::EndChild();
}

void Workspace::DrawBusyOverlay()
{
    if (!IsBusy())
        return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Loading", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(session.IsLoading() ? "Loading package..." : "Decoding XMA2 with ffmpeg...");
    ImGui::End();
}

void Workspace::DrawReplaceTextureDialog()
{
    if (!replacement)
        return;
    ImGui::OpenPopup(kReplaceTexturePopup);
    if (!ImGui::BeginPopupModal(kReplaceTexturePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::InputInt("Width", &replacement->width);
    ImGui::InputInt("Height", &replacement->height);
    ImGui::TextUnformatted("Encoded in the original GPU format only.");
    const bool confirmed = ImGui::Button("OK");
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");
    if (confirmed)
        ApplyTextureReplacement();
    if (confirmed || cancelled)
    {
        replacement.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Workspace::DrawDiscardChangesDialog()
{
    if (confirmDiscard)
        ImGui::OpenPopup(kDiscardChangesPopup);
    if (!ImGui::BeginPopupModal(kDiscardChangesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("Discard unsaved changes?");
    if (ImGui::Button("Yes"))
    {
        discardApproved = true;
        deferred = Command::OpenPackage;
        confirmDiscard = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("No"))
    {
        confirmDiscard = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Workspace::DrawErrorDialog()
{
    if (!session.error.empty())
        ImGui::OpenPopup(kErrorPopup);
    if (!ImGui::BeginPopupModal(kErrorPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40);
    ImGui::TextUnformatted(session.error.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Close"))
    {
        session.error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}