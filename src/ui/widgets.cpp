#include "ui/widgets.h"
#include "package/package_search.h"
#include <cstdio>
#include <utility>

namespace
{
    void DrawChunk(const CMChunk& chunk, const CMChunk*& selected, bool root)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (chunk.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (root)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (&chunk == selected)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx(&chunk, flags, "%s", PackageChunkLabel(chunk).c_str());
        if (ImGui::IsItemClicked())
            selected = &chunk;
        if (!open || chunk.children.empty())
            return;
        for (const CMChunk& child : chunk.children)
            DrawChunk(child, selected, false);
        ImGui::TreePop();
    }
}

void Splitter(const char* id, const ImVec2& size, SplitAxis axis, float& value, float direction)
{
    const bool horizontal = axis == SplitAxis::Horizontal;
    ImGui::InvisibleButton(id, size);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive())
        value += direction * (horizontal ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y);
}

bool BeginPanel(const char* id, const ImVec2& size, ImGuiWindowFlags flags)
{
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, flags);
}

void PanelTitle(const char* title)
{
    ImGui::TextUnformatted(title);
    ImGui::Separator();
}

std::string HexString(uint64_t value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

bool BeginFieldTable()
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("Field table##single-line", 4, flags))
        return false;
    for (const char* column : {"Name", "Offset", "Type", "Value"})
        ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    return true;
}

void EndFieldTable()
{
    ImGui::EndTable();
}

void FieldRow(const char* name, const char* type, const std::string& value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    ImGui::SetItemTooltip("%s", name);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(type);
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SetItemTooltip("%s", value.c_str());
}

void DrawChunkTree(const PKPackage& package, const CMChunk*& selected)
{
    for (const CMChunk& root : package.rootChunks)
        DrawChunk(root, selected, true);
}

void DebugLog::Add(std::string message)
{
    messages.push_back(std::move(message));
    if (messages.size() > kCapacity)
        messages.pop_front();
    changed = true;
}

void DebugLog::Draw()
{
    const bool following = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    PanelTitle("Debug log");
    for (const std::string& message : messages)
        ImGui::TextWrapped("%s", message.c_str());
    if (changed && following)
        ImGui::SetScrollHereY(1);
    changed = false;
}
