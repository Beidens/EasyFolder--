#pragma once
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cmath>
#include <math.h>
#include <algorithm>
#include <map>

using std::wstring;
using std::vector;

struct FolderData
{
    wstring id;
    wstring name;
    int x;          // 图标中心 物理像素
    int y;
    int hue;
    vector<wstring> items;
    HWND hwnd;
    void* extra;    // 窗口私有数据
};

extern vector<FolderData*> g_folders;
extern wstring g_dataDir;
extern float g_scale;
extern float g_fit;    // 整体缩放 屏幕装不下时按比例缩

// dip 转像素 所有界面尺寸都走这个 不然高分屏上会小得离谱
inline int Px(double dip)
{
    int v = (int)(dip * g_scale * g_fit + 0.5);
    return v < 1 ? 1 : v;
}

wstring AppDataDir();
wstring NewId();
bool StoreLoad();
void StoreSave();
FolderData* StoreCreate(const wstring& name, int x, int y);
void StoreRemove(const wstring& id);
FolderData* StoreFind(const wstring& id);
bool StoreAddItem(FolderData* f, const wstring& path);
void StoreRemoveItem(FolderData* f, int index);

Gdiplus::Bitmap* IconGet(const wstring& path, int size);
void IconsClear();
void LaunchPath(const wstring& path);

bool NameInput(HWND owner, const wstring& title, const wstring& initial, wstring& out);

void FolderShow(FolderData* d);
void FolderDestroy(FolderData* d);
void FolderReloadItems(FolderData* d);
void AppCreateFolder();
