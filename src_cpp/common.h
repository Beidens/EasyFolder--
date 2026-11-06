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

// 面板里的格子尺寸 都按 dip 走 高分屏缩放时才不会乱
static const int GRID_COLS = 5;
static const int GRID_ROWS = 3;
static const int CELL_DIP = 118;

struct FolderEntry
{
    wstring path;
    int x;          // 在面板内容区里的位置 单位 dip
    int y;
};

struct FolderData
{
    wstring id;
    wstring name;
    int x;          // 图标中心 物理像素
    int y;
    int hue;
    vector<FolderEntry> items;
    // 外观 这堆 -1 表示还没设置过 走原来的默认值
    wstring skinPath;   // 桌面方块上铺的图片 空就是默认那个白文件夹
    int fillColor;      // 面板底色 RGB
    int fillAlpha;      // 面板底色透明度
    int edgeColor;      // 面板描边 RGB
    int edgeAlpha;      // 面板描边透明度
    int edgeWidth;      // 面板描边粗细 dip 乘 10 存的
    int skinEdgeColor;  // 方块上那张图的描边颜色
    int skinEdgeAlpha;
    int skinEdgeWidth;  // 方块描边粗细 dip 乘 10 0 就是不画
    int tileSize;       // 桌面方块多大 单位 dip
    int itemSize;       // 展开之后里面图标多大 单位 dip
    int pinned;         // 钉住了就拖不动 0 不钉 1 钉
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

// 反过来的 鼠标位置要换算回 dip 才好存
inline double Dip(int px)
{
    double k = (double)g_scale * (double)g_fit;
    return k <= 0 ? (double)px : (double)px / k;
}

// 新加进来的条目先摆空格子 后面用户自己拖
inline void GridPos(int index, int& x, int& y)
{
    if (index < 0) index = 0;
    x = (index % GRID_COLS) * CELL_DIP;
    y = (index / GRID_COLS) * CELL_DIP;
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
Gdiplus::Bitmap* ImageGet(const wstring& path, int size);
void IconsClear();
void LaunchPath(const wstring& path);

bool NameInput(HWND owner, const wstring& title, const wstring& initial, wstring& out);

void FolderShow(FolderData* d);
void FolderDestroy(FolderData* d);
void FolderReloadItems(FolderData* d);
void FolderCleanTopmost();
void FolderApplyLook(FolderData* d);
void AppCreateFolder();
void PanelSettingsOpen(FolderData* d);
