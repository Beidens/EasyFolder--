#include "common.h"
#include <stdlib.h>

vector<FolderData*> g_folders;
wstring g_dataDir;

static std::string ToU8(const wstring& s)
{
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string r((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n, NULL, NULL);
    return r;
}

static wstring FromU8(const std::string& s)
{
    if (s.empty()) return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    wstring r((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n);
    return r;
}

static wstring B64Enc(const wstring& s)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b = ToU8(s);
    std::string o;
    for (size_t i = 0; i < b.size(); i += 3)
    {
        int n = (int)(b.size() - i);
        unsigned v = (unsigned char)b[i] << 16;
        if (n > 1) v |= (unsigned char)b[i + 1] << 8;
        if (n > 2) v |= (unsigned char)b[i + 2];
        o.push_back(t[(v >> 18) & 63]);
        o.push_back(t[(v >> 12) & 63]);
        o.push_back(n > 1 ? t[(v >> 6) & 63] : '=');
        o.push_back(n > 2 ? t[v & 63] : '=');
    }
    return wstring(o.begin(), o.end());
}

static wstring B64Dec(const wstring& s)
{
    std::string o;
    unsigned v = 0;
    int bits = 0;
    for (wchar_t c : s)
    {
        int d;
        if (c >= L'A' && c <= L'Z') d = c - L'A';
        else if (c >= L'a' && c <= L'z') d = c - L'a' + 26;
        else if (c >= L'0' && c <= L'9') d = c - L'0' + 52;
        else if (c == L'+') d = 62;
        else if (c == L'/') d = 63;
        else continue;
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            o.push_back((char)((v >> bits) & 255));
        }
    }
    return FromU8(o);
}

static vector<std::string> Split(const std::string& s)
{
    vector<std::string> r;
    std::string cur;
    for (char c : s)
    {
        if (c == '|') { r.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    r.push_back(cur);
    return r;
}

static std::string ReadAll(const wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::string r;
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n > 0) r.append(buf, n);
    CloseHandle(h);
    return r;
}

static void WriteAll(const wstring& path, const std::string& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD n = (DWORD)data.size();
    WriteFile(h, data.data(), n, &n, NULL);
    CloseHandle(h);
}

wstring AppDataDir()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, p))) return L".";
    return wstring(p) + L"\\DeskFolder";
}

static wstring DataFile()
{
    return g_dataDir + L"\\folders.dat";
}

static void MakeDir(const wstring& d)
{
    SHCreateDirectoryExW(NULL, d.c_str(), NULL);
}

wstring NewId()
{
    wchar_t b[16];
    unsigned v = (unsigned)GetTickCount64() ^ (unsigned)(rand() * 7919);
    wsprintfW(b, L"%08x", v);
    return wstring(b);
}

bool StoreLoad()
{
    for (FolderData* p : g_folders) delete p;
    g_folders.clear();
    MakeDir(g_dataDir);

    std::string all = ReadAll(DataFile());
    if (all.empty()) return false;

    std::string line;
    for (size_t i = 0; i <= all.size(); i++)
    {
        if (i == all.size() || all[i] == '\n')
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 2)
            {
                vector<std::string> p = Split(line);
                if (line[0] == 'F' && p.size() >= 6)
                {
                    FolderData* f = new FolderData();
                    f->id = FromU8(p[1]);
                    f->name = B64Dec(FromU8(p[2]));
                    f->x = atoi(p[3].c_str());
                    f->y = atoi(p[4].c_str());
                    f->hue = atoi(p[5].c_str());
                    // 外观是从 1.2.0 才开始有的 老文件到 hue 就没了
                    f->skinPath = p.size() >= 7 ? B64Dec(FromU8(p[6])) : wstring();
                    f->fillColor = p.size() >= 8 ? atoi(p[7].c_str()) : -1;
                    f->fillAlpha = p.size() >= 9 ? atoi(p[8].c_str()) : 242;
                    f->edgeColor = p.size() >= 10 ? atoi(p[9].c_str()) : -1;
                    f->edgeAlpha = p.size() >= 11 ? atoi(p[10].c_str()) : 42;
                    f->edgeWidth = p.size() >= 12 ? atoi(p[11].c_str()) : 10;
                    f->skinEdgeColor = p.size() >= 13 ? atoi(p[12].c_str()) : -1;
                    f->skinEdgeAlpha = p.size() >= 14 ? atoi(p[13].c_str()) : 70;
                    f->skinEdgeWidth = p.size() >= 15 ? atoi(p[14].c_str()) : 10;
                    f->tileSize = p.size() >= 16 ? atoi(p[15].c_str()) : -1;
                    f->itemSize = p.size() >= 17 ? atoi(p[16].c_str()) : -1;
                    f->pinned = p.size() >= 18 ? atoi(p[17].c_str()) : 0;
                    f->hwnd = NULL;
                    f->extra = NULL;
                    g_folders.push_back(f);
                }
                else if (line[0] == 'I' && p.size() >= 3)
                {
                    FolderData* f = StoreFind(FromU8(p[1]));
                    if (f)
                    {
                        FolderEntry it;
                        it.path = B64Dec(FromU8(p[2]));
                        // 老数据只有路径 没带位置 按原来的排列给它一个格子
                        if (p.size() >= 5)
                        {
                            it.x = atoi(p[3].c_str());
                            it.y = atoi(p[4].c_str());
                        }
                        else GridPos((int)f->items.size(), it.x, it.y);
                        f->items.push_back(it);
                    }
                }
            }
            line.clear();
        }
        else line.push_back(all[i]);
    }
    return true;
}

void StoreSave()
{
    std::string out = "V1\r\n";
    for (FolderData* f : g_folders)
    {
        out += "F|" + ToU8(f->id) + "|" + ToU8(B64Enc(f->name)) + "|"
             + std::to_string(f->x) + "|" + std::to_string(f->y) + "|"
             + std::to_string(f->hue) + "|" + ToU8(B64Enc(f->skinPath)) + "|"
             + std::to_string(f->fillColor) + "|" + std::to_string(f->fillAlpha) + "|"
             + std::to_string(f->edgeColor) + "|" + std::to_string(f->edgeAlpha) + "|"
             + std::to_string(f->edgeWidth) + "|"
             + std::to_string(f->skinEdgeColor) + "|" + std::to_string(f->skinEdgeAlpha) + "|"
             + std::to_string(f->skinEdgeWidth) + "|"
             + std::to_string(f->tileSize) + "|" + std::to_string(f->itemSize) + "|"
             + std::to_string(f->pinned) + "\r\n";
        for (auto& it : f->items)
            out += "I|" + ToU8(f->id) + "|" + ToU8(B64Enc(it.path)) + "|"
                 + std::to_string(it.x) + "|" + std::to_string(it.y) + "\r\n";
    }
    MakeDir(g_dataDir);
    WriteAll(DataFile(), out);
}

FolderData* StoreFind(const wstring& id)
{
    for (FolderData* f : g_folders)
        if (f->id == id) return f;
    return NULL;
}

FolderData* StoreCreate(const wstring& name, int x, int y)
{
    static const int hues[8] = { 212, 268, 328, 24, 152, 190, 6, 240 };
    FolderData* f = new FolderData();
    f->id = NewId();
    f->name = name;
    f->x = x;
    f->y = y;
    f->hue = hues[g_folders.size() % 8];
    f->fillColor = -1;
    f->fillAlpha = 242;
    f->edgeColor = -1;
    f->edgeAlpha = 42;
    f->edgeWidth = 10;
    f->skinEdgeColor = -1;
    f->skinEdgeAlpha = 70;
    f->skinEdgeWidth = 10;
    f->pinned = 0;
    f->tileSize = -1;
    f->itemSize = -1;
    f->hwnd = NULL;
    f->extra = NULL;
    g_folders.push_back(f);
    StoreSave();
    return f;
}

void StoreRemove(const wstring& id)
{
    for (size_t i = 0; i < g_folders.size(); i++)
    {
        if (g_folders[i]->id == id)
        {
            delete g_folders[i];
            g_folders.erase(g_folders.begin() + i);
            StoreSave();
            return;
        }
    }
}

bool StoreAddItem(FolderData* f, const wstring& path)
{
    for (auto& it : f->items)
        if (it.path == path) return false;
    FolderEntry ni;
    ni.path = path;
    GridPos((int)f->items.size(), ni.x, ni.y);
    f->items.push_back(ni);
    StoreSave();
    return true;
}

void StoreRemoveItem(FolderData* f, int index)
{
    if (index < 0 || index >= (int)f->items.size()) return;
    f->items.erase(f->items.begin() + index);
    StoreSave();
}
