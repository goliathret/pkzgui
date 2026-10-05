// pkzgui — viewer for pkz files (zlib-compressed and decompressed)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Package/CMChunk.h"
#include "Package/PKPackage.h"
#include "pkzgui_schema.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    enum : int
    {
        IDC_TREE = 1001,
        IDC_LIST = 1002,
        IDC_STATUS = 1003,
        IDM_FILE_OPEN = 40001,
        IDM_FILE_SCHEMA = 40002,
        IDM_FILE_EXIT = 40003,
        IDM_VIEW_EXPAND = 40010,
        IDM_VIEW_COLLAPSE = 40011,
    };

    constexpr int kSplitDefault = 340;
    constexpr size_t kMaxFieldRows = 200000;

    HINSTANCE gInst = nullptr;
    HWND gMain = nullptr;
    HWND gTree = nullptr;
    HWND gList = nullptr;
    HWND gStatus = nullptr;
    int gSplitX = kSplitDefault;
    bool gDragging = false;

    pkzgui::Schema gSchema;
    bool gSchemaLoaded = false;
    std::wstring gSchemaPath;
    std::wstring gPackagePath;

    std::unique_ptr<PKPackage> gPackage;
    std::vector<const CMChunk*> gChunkIndex;

    std::wstring Widen(const std::string& s)
    {
        if (s.empty())
            return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
        return out;
    }

    std::string Narrow(const std::wstring& s)
    {
        if (s.empty())
            return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
        return out;
    }

    std::wstring ExeDir()
    {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return L".";
        std::wstring path(buf, n);
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : path.substr(0, slash);
    }

    void SetStatus(const std::wstring& text)
    {
        if (gStatus)
            SendMessageW(gStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void ClearList()
    {
        if (gList)
            ListView_DeleteAllItems(gList);
    }

    std::vector<std::wstring> CollectSchemaPaths()
    {
        std::vector<std::wstring> found;
        const std::wstring dirs[] = {
            ExeDir() + L"\\schemas",
            ExeDir() + L"\\..\\schemas",
            ExeDir() + L"\\..\\..\\schemas",
            L"schemas",
            L"..\\schemas",
        };
        for (const std::wstring& dir : dirs)
        {
            const std::wstring pattern = dir + L"\\*.xml";
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                const std::wstring full = dir + L"\\" + fd.cFileName;
                bool dup = false;
                for (const auto& p : found)
                {
                    const size_t slash = p.find_last_of(L"\\/");
                    const std::wstring base = slash == std::wstring::npos ? p : p.substr(slash + 1);
                    if (_wcsicmp(base.c_str(), fd.cFileName) == 0)
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    found.push_back(full);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        return found;
    }

    enum : int
    {
        IDC_SCHEMA_LIST = 2001,
        IDC_SCHEMA_OK = 2002,
        IDC_SCHEMA_CANCEL = 2003,
        IDC_PROG_BAR = 2101,
        IDC_PROG_LABEL = 2102,
    };

    struct SchemaPickerState
    {
        std::vector<std::wstring> paths;
        std::wstring chosen;
    };

    LRESULT CALLBACK SchemaPickerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        SchemaPickerState* st = reinterpret_cast<SchemaPickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = reinterpret_cast<SchemaPickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                12, 12, 360, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCHEMA_LIST)), gInst, nullptr);
            const std::wstring exe = ExeDir();
            for (size_t i = 0; i < st->paths.size(); ++i)
            {
                const std::wstring& full = st->paths[i];
                std::wstring display = full;
                if (full.size() > exe.size() && _wcsnicmp(full.c_str(), exe.c_str(), exe.size()) == 0)
                {
                    size_t skip = exe.size();
                    while (skip < full.size() && (full[skip] == L'\\' || full[skip] == L'/'))
                        ++skip;
                    display = full.substr(skip);
                }
                else
                {
                    const size_t slash = full.find_last_of(L"\\/");
                    if (slash != std::wstring::npos)
                        display = full.substr(slash + 1);
                }
                const int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str())));
                SendMessageW(list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
            }
            if (!st->paths.empty())
                SendMessageW(list, LB_SETCURSEL, 0, 0);
            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                200, 230, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCHEMA_OK)), gInst, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                290, 230, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCHEMA_CANCEL)), gInst, nullptr);
            return 0;
        }
        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            if (id == IDC_SCHEMA_OK || (id == IDC_SCHEMA_LIST && HIWORD(wParam) == LBN_DBLCLK))
            {
                HWND list = GetDlgItem(hwnd, IDC_SCHEMA_LIST);
                const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0)
                {
                    const size_t pathIdx = static_cast<size_t>(SendMessageW(list, LB_GETITEMDATA, sel, 0));
                    if (pathIdx < st->paths.size())
                        st->chosen = st->paths[pathIdx];
                }
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDC_SCHEMA_CANCEL)
            {
                st->chosen.clear();
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            st->chosen.clear();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool ShowSchemaPicker(std::wstring& outPath)
    {
        auto paths = CollectSchemaPaths();
        if (paths.empty())
            return false;
        if (paths.size() == 1)
        {
            outPath = paths[0];
            return true;
        }
        SchemaPickerState st;
        st.paths = std::move(paths);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SchemaPickerProc;
        wc.hInstance = gInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"pkzgui.SchemaPicker";
        RegisterClassExW(&wc);
        HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Select schema",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
            nullptr, nullptr, gInst, &st);
        if (!hwnd)
            return false;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        UnregisterClassW(L"pkzgui.SchemaPicker", gInst);
        if (st.chosen.empty())
            return false;
        outPath = std::move(st.chosen);
        return true;
    }

    struct ProgressState
    {
        HWND hwnd = nullptr;
        HWND bar = nullptr;
        HWND label = nullptr;
        bool cancelled = false;
    };

    LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ProgressState* st = reinterpret_cast<ProgressState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = reinterpret_cast<ProgressState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            st->hwnd = hwnd;
            st->label = CreateWindowExW(0, L"STATIC", L"Opening package...",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 16, 360, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_LABEL)), gInst, nullptr);
            st->bar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 16, 44, 360, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_BAR)), gInst, nullptr);
            SendMessageW(st->bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(st->bar, PBM_SETPOS, 0, 0);
            return 0;
        }
        case WM_CLOSE:
            st->cancelled = true;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    ProgressState* BeginProgress(const std::wstring& title)
    {
        static ProgressState st;
        st = {};
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ProgressProc;
        wc.hInstance = gInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"pkzgui.Progress";
        RegisterClassExW(&wc);
        HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, title.c_str(),
            WS_OVERLAPPED | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 400, 120,
            gMain, nullptr, gInst, &st);
        if (!hwnd)
            return nullptr;
        EnableWindow(gMain, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return &st;
    }

    void SetProgress(ProgressState* st, int pct, const std::wstring& text)
    {
        if (!st || !st->hwnd)
            return;
        if (st->label && !text.empty())
            SetWindowTextW(st->label, text.c_str());
        if (st->bar)
            SendMessageW(st->bar, PBM_SETPOS, pct, 0);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void EndProgress(ProgressState* st)
    {
        if (!st || !st->hwnd)
            return;
        DestroyWindow(st->hwnd);
        st->hwnd = nullptr;
        EnableWindow(gMain, TRUE);
        if (gMain)
            SetForegroundWindow(gMain);
        UnregisterClassW(L"pkzgui.Progress", gInst);
    }

    void FillList(const std::vector<pkzgui::Row>& rows)
    {
        ClearList();
        if (!gList)
            return;
        ListView_SetItemCount(gList, static_cast<int>(rows.size()));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const pkzgui::Row& r = rows[i];
            std::wstring name;
            for (int d = 0; d < r.depth; ++d)
                name += L"  ";
            name += Widen(r.name);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<wchar_t*>(name.c_str());
            const int idx = ListView_InsertItem(gList, &item);
            if (idx < 0)
                continue;
            const std::wstring off = Widen(r.offset);
            const std::wstring type = Widen(r.type);
            const std::wstring val = Widen(r.value);
            ListView_SetItemText(gList, idx, 1, const_cast<wchar_t*>(off.c_str()));
            ListView_SetItemText(gList, idx, 2, const_cast<wchar_t*>(type.c_str()));
            ListView_SetItemText(gList, idx, 3, const_cast<wchar_t*>(val.c_str()));
        }
    }

    const CMChunk* FindChunkById(const CMChunk& node, uint32_t id)
    {
        if (node.GetMaskedID() == (id & CMChunk::kIdMask) && !node.GetHasChildren())
            return &node;
        for (const CMChunk& c : node.children)
        {
            if (const CMChunk* f = FindChunkById(c, id))
                return f;
        }
        return nullptr;
    }

    const CMChunk* PackageFindChunk(uint32_t id, const CMChunk&)
    {
        if (!gPackage)
            return nullptr;
        for (const CMChunk& root : gPackage->rootChunks)
        {
            if (const CMChunk* f = FindChunkById(root, id))
                return f;
        }
        return nullptr;
    }

    void ShowChunkFields(const CMChunk* chunk)
    {
        if (!chunk)
        {
            ClearList();
            return;
        }
        if (!gSchemaLoaded)
        {
            ClearList();
            SetStatus(L"No schema loaded — showing raw chunk info only is unavailable until a schema is loaded.");
            std::vector<pkzgui::Row> rows;
            rows.push_back({ 0, "Chunk", "", "", "" });
            rows.push_back({ 1, "ID", "", "u24", pkzgui::Hex(chunk->GetMaskedID()) });
            rows.push_back({ 1, "Version", "", "u16", std::to_string(chunk->GetVersion()) });
            rows.push_back({ 1, "Has Children", "", "", chunk->GetHasChildren() ? "1" : "0" });
            rows.push_back({ 1, "Length", "", "", std::to_string(chunk->GetLength()) });
            rows.push_back({ 1, "Offset", "", "", pkzgui::Hex(chunk->offset) });
            if (!chunk->GetHasChildren() && !chunk->data.empty())
            {
                rows.push_back({ 0, "Data", "", "", "" });
                rows.push_back({ 1, "payload", "", "bytes[" + std::to_string(chunk->data.size()) + "]",
                                "(load a schema for structured decode)" });
            }
            FillList(rows);
            return;
        }

        pkzgui::Decoder dec(&gSchema, PackageFindChunk);
        std::vector<pkzgui::Row> rows;
        dec.Describe(*chunk, rows, kMaxFieldRows);
        FillList(rows);

        const std::wstring label = Widen(dec.ChunkLabel(*chunk));
        wchar_t status[512];
        swprintf_s(status, L"%s  |  id 0x%X  v%u  %s  %llu byte(s)",
            label.c_str(),
            chunk->GetMaskedID(),
            chunk->GetVersion(),
            chunk->GetHasChildren() ? L"container" : L"leaf",
            static_cast<unsigned long long>(chunk->GetLength()));
        SetStatus(status);
    }

    HTREEITEM InsertChunkItem(HTREEITEM parent, const CMChunk& chunk, int depth)
    {
        pkzgui::Decoder labeler(gSchemaLoaded ? &gSchema : nullptr, PackageFindChunk);
        std::string name;
        if (gSchemaLoaded)
            name = labeler.ChunkLabel(chunk);
        else
            name = ToString(chunk.GetIDToEnum());
        if (gSchemaLoaded)
        {
            const auto headers = chunk.FindInChildren(CMChunkTypes::GenSub_ResourceHeader);
            if (!headers.empty())
            {
                pkzgui::Decoder dec(&gSchema, PackageFindChunk);
                std::vector<pkzgui::Row> rows;
                dec.Describe(headers.front(), rows, kMaxFieldRows);
                for (const auto& r : rows)
                {
                    if (r.name == "name" && !r.value.empty())
                    {
                        name = r.value;
                        break;
                    }
                }
            }
        }
        char text[512];
        std::snprintf(text, sizeof(text), "%s  [0x%X] v%u  %s  %llu",
            name.c_str(),
            chunk.GetMaskedID(),
            chunk.GetVersion(),
            chunk.GetHasChildren() ? "folder" : "leaf",
            static_cast<unsigned long long>(chunk.GetLength()));
        const std::wstring wtext = Widen(text);
        const size_t idx = gChunkIndex.size();
        gChunkIndex.push_back(&chunk);
        TVINSERTSTRUCTW ins{};
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        ins.item.pszText = const_cast<wchar_t*>(wtext.c_str());
        ins.item.lParam = static_cast<LPARAM>(idx);
        ins.item.cChildren = chunk.GetHasChildren() && !chunk.children.empty() ? 1 : 0;
        HTREEITEM item = TreeView_InsertItem(gTree, &ins);
        if (chunk.GetHasChildren())
        {
            for (const CMChunk& child : chunk.children)
                InsertChunkItem(item, child, depth + 1);
        }
        return item;
    }

    void RebuildTree()
    {
        if (!gTree)
            return;
        TreeView_DeleteAllItems(gTree);
        gChunkIndex.clear();
        ClearList();
        if (!gPackage)
            return;

        for (const CMChunk& root : gPackage->rootChunks)
            InsertChunkItem(TVI_ROOT, root, 0);

        HTREEITEM root = TreeView_GetRoot(gTree);
        while (root)
        {
            TreeView_Expand(gTree, root, TVE_EXPAND);
            root = TreeView_GetNextSibling(gTree, root);
        }

        wchar_t status[256];
        swprintf_s(status, L"Loaded %zu root chunk(s)%s",
            gPackage->rootChunks.size(),
            gPackage->wasCompressed ? L" (was compressed)" : L"");
        SetStatus(status);
    }

    bool LoadSchemaFromPath(const std::wstring& path)
    {
        std::string err;
        pkzgui::Schema schema;
        if (!pkzgui::LoadSchemaFile(Narrow(path), schema, err))
        {
            MessageBoxW(gMain, Widen(err).c_str(), L"Schema load failed", MB_ICONERROR | MB_OK);
            return false;
        }
        gSchema = std::move(schema);
        gSchemaLoaded = true;
        gSchemaPath = path;
        SetStatus(L"Schema: " + Widen(gSchema.name) + L" (" + path + L")");
        if (gPackage)
            RebuildTree();
        return true;
    }

    bool TryDefaultSchema()
    {
        const std::wstring candidates[] = {
            ExeDir() + L"\\schemas\\EOT-360.xml",
            ExeDir() + L"\\..\\schemas\\EOT-360.xml",
            ExeDir() + L"\\..\\..\\schemas\\EOT-360.xml",
            L"schemas\\EOT-360.xml",
            L"..\\schemas\\EOT-360.xml",
        };
        for (const std::wstring& p : candidates)
        {
            const DWORD attr = GetFileAttributesW(p.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                return LoadSchemaFromPath(p);
        }
        return false;
    }

    bool LoadPackageFromPath(const std::wstring& path)
    {
        ProgressState* prog = BeginProgress(L"Opening package");
        SetProgress(prog, 10, L"Reading file...");
        try
        {
            auto pkg = std::make_unique<PKPackage>();
            SetProgress(prog, 40, L"Parsing package...");
            pkg->ReadFromFile(Narrow(path));
            SetProgress(prog, 80, L"Building tree...");
            gPackage = std::move(pkg);
            gPackagePath = path;
            RebuildTree();
            SetProgress(prog, 100, L"Done");
            EndProgress(prog);
            SetWindowTextW(gMain, L"pkzgui");
            return true;
        }
        catch (const std::exception& ex)
        {
            EndProgress(prog);
            MessageBoxW(gMain, Widen(ex.what()).c_str(), L"Failed to open package, probably from a different game?", MB_ICONERROR | MB_OK);
            return false;
        }
    }

    std::wstring OpenFileDialog(const wchar_t* filter, const wchar_t* title)
    {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = gMain;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetOpenFileNameW(&ofn))
            return {};
        return path;
    }

    void LayoutChildren(int cx, int cy)
    {
        if (!gTree || !gList || !gStatus)
            return;

        RECT sb{};
        GetWindowRect(gStatus, &sb);
        const int statusH = sb.bottom - sb.top;

        int split = gSplitX;
        if (split < 120)
            split = 120;
        if (split > cx - 160)
            split = cx - 160;
        gSplitX = split;

        const int gap = 4;
        MoveWindow(gTree, 0, 0, split, cy - statusH, TRUE);
        MoveWindow(gList, split + gap, 0, cx - split - gap, cy - statusH, TRUE);
        MoveWindow(gStatus, 0, cy - statusH, cx, statusH, TRUE);
    }

    void ExpandAll(HTREEITEM item, bool expand)
    {
        if (!item)
            return;
        TreeView_Expand(gTree, item, expand ? TVE_EXPAND : TVE_COLLAPSE);
        HTREEITEM child = TreeView_GetChild(gTree, item);
        while (child)
        {
            ExpandAll(child, expand);
            child = TreeView_GetNextSibling(gTree, child);
        }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
            InitCommonControlsEx(&icc);

            HMENU menu = CreateMenu();
            HMENU file = CreatePopupMenu();
            AppendMenuW(file, MF_STRING, IDM_FILE_OPEN, L"&Open package...\tCtrl+O");
            AppendMenuW(file, MF_STRING, IDM_FILE_SCHEMA, L"Load &schema...");
            AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"E&xit");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
            HMENU view = CreatePopupMenu();
            AppendMenuW(view, MF_STRING, IDM_VIEW_EXPAND, L"&Expand all");
            AppendMenuW(view, MF_STRING, IDM_VIEW_COLLAPSE, L"&Collapse all");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
            SetMenu(hwnd, menu);

            gTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS |
                TVS_SHOWSELALWAYS,
                0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TREE)), gInst,
                nullptr);

            gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), gInst,
                nullptr);
            ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(L"Name");
            col.cx = 260;
            col.iSubItem = 0;
            ListView_InsertColumn(gList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"Offset");
            col.cx = 90;
            col.iSubItem = 1;
            ListView_InsertColumn(gList, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"Type");
            col.cx = 140;
            col.iSubItem = 2;
            ListView_InsertColumn(gList, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"Value");
            col.cx = 480;
            col.iSubItem = 3;
            ListView_InsertColumn(gList, 3, &col);

            gStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), gInst, nullptr);
            SetStatus(L"Open a .pkz / .pak file. Schema defaults to EOT-360 if found.");
            return 0;
        }
        case WM_SIZE:
        {
            LayoutChildren(LOWORD(lParam), HIWORD(lParam));
            return 0;
        }
        case WM_SETCURSOR:
        {
            if (LOWORD(lParam) == HTCLIENT)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (std::abs(pt.x - gSplitX) <= 4)
                {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            const int x = static_cast<short>(LOWORD(lParam));
            if (std::abs(x - gSplitX) <= 4)
            {
                gDragging = true;
                SetCapture(hwnd);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            if (gDragging)
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                gSplitX = static_cast<short>(LOWORD(lParam));
                LayoutChildren(rc.right - rc.left, rc.bottom - rc.top);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            if (gDragging)
            {
                gDragging = false;
                ReleaseCapture();
                return 0;
            }
            break;
        }
        case WM_NOTIFY:
        {
            const NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->idFrom == IDC_TREE && hdr->code == TVN_SELCHANGEDW)
            {
                const NMTREEVIEWW* ntv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                const size_t idx = static_cast<size_t>(ntv->itemNew.lParam);
                if (idx < gChunkIndex.size())
                    ShowChunkFields(gChunkIndex[idx]);
                return 0;
            }
            break;
        }
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDM_FILE_OPEN:
            {
                const std::wstring path = OpenFileDialog(
                    L"Package files (*.pkz;*.pak)\0*.pkz;*.pak\0All files (*.*)\0*.*\0", L"Open PKZ / PAK");
                if (!path.empty())
                    LoadPackageFromPath(path);
                return 0;
            }
            case IDM_FILE_SCHEMA:
            {
                const std::wstring path =
                    OpenFileDialog(L"Schema XML (*.xml)\0*.xml\0All files (*.*)\0*.*\0", L"Load field schema");
                if (!path.empty())
                    LoadSchemaFromPath(path);
                return 0;
            }
            case IDM_FILE_EXIT:
                DestroyWindow(hwnd);
                return 0;
            case IDM_VIEW_EXPAND:
            {
                HTREEITEM root = TreeView_GetRoot(gTree);
                while (root)
                {
                    ExpandAll(root, true);
                    root = TreeView_GetNextSibling(gTree, root);
                }
                return 0;
            }
            case IDM_VIEW_COLLAPSE:
            {
                HTREEITEM root = TreeView_GetRoot(gTree);
                while (root)
                {
                    ExpandAll(root, false);
                    root = TreeView_GetNextSibling(gTree, root);
                }
                return 0;
            }
            }
            break;
        }
        case WM_KEYDOWN:
        {
            if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                PostMessageW(hwnd, WM_COMMAND, IDM_FILE_OPEN, 0);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    gInst = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"pkzgui.Main";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc))
        return 1;

    gMain = CreateWindowExW(0, wc.lpszClassName, L"pkzgui",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr,
        nullptr, hInstance, nullptr);
    if (!gMain)
        return 1;

    ShowWindow(gMain, nCmdShow);
    UpdateWindow(gMain);

    std::wstring schemaArg;
    std::wstring packageArg;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring a = argv[i];
            const size_t dot = a.find_last_of(L'.');
            std::wstring ext = dot == std::wstring::npos ? L"" : a.substr(dot);
            for (auto& c : ext)
                c = static_cast<wchar_t>(towlower(c));
            if (ext == L".xml")
                schemaArg = a;
            else
                packageArg = a;
        }
        LocalFree(argv);
    }

    if (!schemaArg.empty())
    {
        LoadSchemaFromPath(schemaArg);
    }
    else
    {
        std::wstring picked;
        if (ShowSchemaPicker(picked))
            LoadSchemaFromPath(picked);
        else
            TryDefaultSchema();
    }

    if (!packageArg.empty())
        LoadPackageFromPath(packageArg);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

int main()
{
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT);
}