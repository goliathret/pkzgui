#pragma once
#include "Package/PKPackage.h"
#include "imgui.h"
#include <cstdint>
#include <deque>
#include <string>

enum class SplitAxis
{
    Horizontal,
    Vertical,
};

void Splitter(const char* id, const ImVec2& size, SplitAxis axis, float& value, float direction = 1.0f);
bool BeginPanel(const char* id, const ImVec2& size, ImGuiWindowFlags flags = 0);
void PanelTitle(const char* title);

std::string HexString(uint64_t value);
bool BeginFieldTable();
void EndFieldTable();
void FieldRow(const char* name, const char* type, const std::string& value);

void DrawChunkTree(const PKPackage& package, const CMChunk*& selected, bool revealSelected = false);

class DebugLog
{
public:
    void Add(std::string message);
    void Draw();

private:
    static constexpr size_t kCapacity = 200;

    std::deque<std::string> messages;
    bool changed = false;
};
