#include "common.h"
#include <commdlg.h>
#include <stdlib.h>

using namespace Gdiplus;

// 设置窗口 全部自绘 跟主程序一个调子
static const int SET_W = 640;
static const int SET_H = 800;

static Font* g_fTitle;
static Font* g_fLab;
static Font* g_fSm;
static Font* g_fBtn;
static HBRUSH g_eb;
static HWND g_dlg;
static FolderData* g_d;
static HWND g_hName;
static HWND g_hR, g_hG, g_hB, g_hA;
static WNDPROC g_editOld;
static bool g_wasActive;
static Bitmap* g_buf;

static float g_hue, g_sat, g_val;
static int g_alpha;
static int g_target;    // 0 面板填充 1 面板描边 2 桌面方块描边
static int g_edgeW;

static RECT g_rClose, g_rDone, g_rPick, g_rReset;
static RECT g_rTab0, g_rTab1, g_rTab2, g_rTab3;
static RECT g_rTileTrack, g_rItemTrack;
static bool g_sizeTab;      // 大小这页签跟颜色那几个不是一回事 单独记
static int g_tileDip;       // 桌面方块大小
static int g_itemDip;       // 展开之后图标大小
static POINT g_wc;
static int g_wr;
static RECT g_rBar;
static RECT g_rSwatch;
static RECT g_rTrack;
static RECT g_rSw[8];
static Bitmap* g_wheel;
static float g_wheelVal = -1;
static int g_drag;      // 1 色轮 2 明度条 3 粗细条
static bool g_modal;    // 开着文件框的时候别被 inactive 关掉

struct Preset
{
    int fill;
    int edge;
    const wchar_t* tip;
};

// 挑的都是对比拉满的组合 压在深色桌面上也显眼
static const Preset kPresets[] = {
    { 0x141414, 0xE8B646, L"黑金" },
    { 0x1B1035, 0x00E5FF, L"紫青" },
    { 0x2B0A22, 0xFF2E88, L"骚粉" },
    { 0x06160F, 0x3DFF9E, L"荧光绿" },
    { 0x1A0B05, 0xFF6A00, L"熔岩" },
    { 0x0E1420, 0x5B8CFF, L"深海" },
    { 0x0A0A0A, 0xFF3B30, L"纯黑红" },
    { 0x110B2E, 0x9B5BFF, L"午夜紫" },
};

static Color Col(int rgb, int a)
{
    return Color((BYTE)a, (BYTE)((rgb >> 16) & 255), (BYTE)((rgb >> 8) & 255), (BYTE)(rgb & 255));
}

static int HsvRgb(float h, float s, float v)
{
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.f, 2) - 1));
    float m = v - c;
    float r = 0, g2 = 0, b = 0;
    if (h < 60) { r = c; g2 = x; }
    else if (h < 120) { r = x; g2 = c; }
    else if (h < 180) { g2 = c; b = x; }
    else if (h < 240) { g2 = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    int ri = (int)((r + m) * 255 + 0.5f);
    int gi = (int)((g2 + m) * 255 + 0.5f);
    int bi = (int)((b + m) * 255 + 0.5f);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    if (ri < 0) ri = 0;
    if (gi < 0) gi = 0;
    if (bi < 0) bi = 0;
    return (ri << 16) | (gi << 8) | bi;
}

// 存的是 rgb 切回来要换算成色轮上的位置 不然一进设置颜色就跳
static void RgbHsv(int rgb, float& h, float& s, float& v)
{
    float r = (float)((rgb >> 16) & 255) / 255.f;
    float g2 = (float)((rgb >> 8) & 255) / 255.f;
    float b = (float)(rgb & 255) / 255.f;
    float mx = r > g2 ? r : g2;
    if (b > mx) mx = b;
    float mn = r < g2 ? r : g2;
    if (b < mn) mn = b;
    v = mx;
    float d = mx - mn;
    if (mx <= 0 || d <= 0)
    {
        s = 0;
        h = 0;
        return;
    }
    s = d / mx;
    if (mx == r) h = 60.f * fmodf((g2 - b) / d, 6.f);
    else if (mx == g2) h = 60.f * ((b - r) / d + 2.f);
    else h = 60.f * ((r - g2) / d + 4.f);
    if (h < 0) h += 360;
}

static void RoundPath(GraphicsPath& p, RectF r, float rad)
{
    if (rad < 1) rad = 1;
    if (rad > r.Width / 2) rad = r.Width / 2;
    if (rad > r.Height / 2) rad = r.Height / 2;
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static RECT Rc(int x, int y, int w, int h)
{
    RECT r;
    r.left = Px(x);
    r.top = Px(y);
    r.right = r.left + Px(w);
    r.bottom = r.top + Px(h);
    return r;
}

static void Refresh()
{
    if (g_dlg) InvalidateRect(g_dlg, NULL, FALSE);
}

static void MakeWheel()
{
    if (g_wheel) { delete g_wheel; g_wheel = NULL; }
    int s = g_wr * 2;
    if (s < 8) return;
    g_wheel = new Bitmap(s, s, PixelFormat32bppPARGB);
    BitmapData bd;
    Rect rc(0, 0, s, s);
    if (g_wheel->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppPARGB, &bd) != Ok) return;
    float lim = (float)g_wr;
    for (int y = 0; y < s; y++)
    {
        BYTE* row = (BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
        float dy = (float)y - lim + 0.5f;
        for (int x = 0; x < s; x++)
        {
            int k = x * 4;
            float dx = (float)x - lim + 0.5f;
            float dd = sqrtf(dx * dx + dy * dy);
            if (dd > lim) { row[k + 3] = 0; continue; }
            int a = 255;
            if (dd > lim - 1.2f)
            {
                a = (int)(255.f * (lim - dd) / 1.2f);
                if (a < 0) a = 0;
            }
            float hh = atan2f(dy, dx) * 57.29578f;
            if (hh < 0) hh += 360;
            float ss = dd / lim;
            if (ss > 1) ss = 1;
            int c = HsvRgb(hh, ss, g_val);
            row[k + 0] = (BYTE)(c & 255);
            row[k + 1] = (BYTE)((c >> 8) & 255);
            row[k + 2] = (BYTE)((c >> 16) & 255);
            row[k + 3] = (BYTE)a;
        }
    }
    g_wheel->UnlockBits(&bd);
    g_wheelVal = g_val;
}

static void PullTarget()
{
    int rgb;
    if (g_target == 0)
    {
        rgb = g_d->fillColor >= 0 ? g_d->fillColor : 0x1F2129;
        g_alpha = g_d->fillAlpha;
    }
    else if (g_target == 1)
    {
        rgb = g_d->edgeColor >= 0 ? g_d->edgeColor : 0xFFFFFF;
        g_alpha = g_d->edgeAlpha;
    }
    else
    {
        rgb = g_d->skinEdgeColor >= 0 ? g_d->skinEdgeColor : 0xFFFFFF;
        g_alpha = g_d->skinEdgeAlpha;
    }
    RgbHsv(rgb, g_hue, g_sat, g_val);

    int wv = g_target == 2 ? g_d->skinEdgeWidth : g_d->edgeWidth;
    g_edgeW = (wv + 5) / 10;
    if (g_edgeW < 0) g_edgeW = 0;
    if (g_edgeW > 10) g_edgeW = 10;
}

static void SyncEdits()
{
    int rgb = HsvRgb(g_hue, g_sat, g_val);
    wchar_t b[16];
    wsprintfW(b, L"%d", (rgb >> 16) & 255);
    SetWindowTextW(g_hR, b);
    wsprintfW(b, L"%d", (rgb >> 8) & 255);
    SetWindowTextW(g_hG, b);
    wsprintfW(b, L"%d", rgb & 255);
    SetWindowTextW(g_hB, b);
    wsprintfW(b, L"%d", g_alpha);
    SetWindowTextW(g_hA, b);
}

static void PushAll()
{
    int rgb = HsvRgb(g_hue, g_sat, g_val);
    if (g_target == 0)
    {
        g_d->fillColor = rgb;
        g_d->fillAlpha = g_alpha;
        g_d->edgeWidth = g_edgeW * 10;
    }
    else if (g_target == 1)
    {
        g_d->edgeColor = rgb;
        g_d->edgeAlpha = g_alpha;
        g_d->edgeWidth = g_edgeW * 10;
    }
    else
    {
        g_d->skinEdgeColor = rgb;
        g_d->skinEdgeAlpha = g_alpha;
        g_d->skinEdgeWidth = g_edgeW * 10;
    }
    StoreSave();
    FolderReloadItems(g_d);
}

static int GetInt(HWND h, int def)
{
    wchar_t b[32];
    if (!GetWindowTextW(h, b, 32)) return def;
    if (!b[0]) return def;
    int v = _wtoi(b);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
}

static void CommitEdit(HWND h)
{
    if (!g_d) return;
    if (h == g_hName)
    {
        wchar_t b[512];
        GetWindowTextW(h, b, 512);
        wstring n(b);
        if (n.empty()) return;
        if (n != g_d->name)
        {
            g_d->name = n;
            StoreSave();
            FolderReloadItems(g_d);
        }
        return;
    }
    if (h != g_hR && h != g_hG && h != g_hB && h != g_hA) return;

    int r = GetInt(g_hR, 0);
    int g2 = GetInt(g_hG, 0);
    int b2 = GetInt(g_hB, 0);
    g_alpha = GetInt(g_hA, 128);

    int rgb = (r << 16) | (g2 << 8) | b2;
    RgbHsv(rgb, g_hue, g_sat, g_val);
    if (g_wheelVal != g_val) MakeWheel();
    SyncEdits();
    Refresh();
    PushAll();
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN)
    {
        if (wp == VK_RETURN || wp == VK_ESCAPE)
        {
            SetFocus(g_dlg);
            return 0;
        }
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    if (m == WM_KILLFOCUS) PostMessageW(g_dlg, WM_APP + 5, (WPARAM)h, 0);
    return CallWindowProcW(g_editOld, h, m, wp, lp);
}

static void CloseDlg()
{
    if (g_dlg) DestroyWindow(g_dlg);
}

static void PickImage()
{
    wchar_t file[MAX_PATH * 2];
    file[0] = 0;
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.hwndOwner = g_dlg;
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"图片\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ico\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    g_modal = true;
    BOOL ok = GetOpenFileNameW(&ofn);
    g_modal = false;
    if (!ok) return;
    g_d->skinPath = file;
    StoreSave();
    FolderReloadItems(g_d);
    Refresh();
}

static void ResetSkin()
{
    g_d->skinPath.clear();
    StoreSave();
    FolderReloadItems(g_d);
    Refresh();
}

// 大小这页跟颜色那几个输入框互斥 切过来的时候把它们收起来
static void ShowColorCtl(bool on)
{
    HWND arr[4] = { g_hR, g_hG, g_hB, g_hA };
    for (int i = 0; i < 4; i++)
        if (arr[i]) ShowWindow(arr[i], on ? SW_SHOW : SW_HIDE);
    if (!on)
    {
        HWND f = GetFocus();
        for (int i = 0; i < 4; i++)
            if (arr[i] && f == arr[i]) SetFocus(g_dlg);
    }
}

static void LoadSizes()
{
    g_tileDip = g_d->tileSize > 0 ? g_d->tileSize : 120;
    g_itemDip = g_d->itemSize > 0 ? g_d->itemSize : 72;
}

static void ApplySizes()
{
    g_d->tileSize = g_tileDip;
    g_d->itemSize = g_itemDip;
    StoreSave();
}

static void SetTarget(int t)
{
    bool same = (t == g_target);
    g_sizeTab = false;
    ShowColorCtl(true);
    if (same)
    {
        Refresh();
        return;
    }
    g_target = t;
    PullTarget();
    if (g_wheelVal != g_val) MakeWheel();
    SyncEdits();
    Refresh();
}

static void SetSizeTab()
{
    if (g_sizeTab) return;
    g_sizeTab = true;
    LoadSizes();
    ShowColorCtl(false);
    Refresh();
}

static void HitSize(POINT p)
{
    RECT* r = g_drag == 4 ? &g_rTileTrack : &g_rItemTrack;
    int lo = g_drag == 4 ? 72 : 48;
    int hi = g_drag == 4 ? 220 : 120;
    int w = r->right - r->left;
    if (w < 1) return;
    float k = (float)(p.x - r->left) / (float)w;
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    int v = lo + (int)(k * (hi - lo) + 0.5f);
    if (g_drag == 4) g_tileDip = v;
    else g_itemDip = v;
    Refresh();
}

static void ApplyPreset(int i)
{
    g_d->fillColor = kPresets[i].fill;
    g_d->fillAlpha = 242;
    g_d->edgeColor = kPresets[i].edge;
    if (g_d->edgeAlpha < 90) g_d->edgeAlpha = 190;
    // 方块上那张图的描边也顺手染成同一个色 整套看着才是一套
    g_d->skinEdgeColor = kPresets[i].edge;
    if (g_d->skinEdgeAlpha < 120) g_d->skinEdgeAlpha = 200;
    if (g_target == 2) g_d->skinEdgeWidth = g_edgeW * 10;
    else g_d->edgeWidth = g_edgeW * 10;
    StoreSave();
    FolderReloadItems(g_d);
    PullTarget();
    if (g_wheelVal != g_val) MakeWheel();
    SyncEdits();
    Refresh();
}

static void HitWheel(POINT p)
{
    float dx = (float)(p.x - g_wc.x);
    float dy = (float)(p.y - g_wc.y);
    float ang = atan2f(dy, dx) * 57.29578f;
    if (ang < 0) ang += 360;
    float dd = sqrtf(dx * dx + dy * dy);
    float ss = dd / (float)g_wr;
    if (ss > 1) ss = 1;
    g_hue = ang;
    g_sat = ss;
    SyncEdits();
    Refresh();
}

static void HitBar(POINT p)
{
    int h = g_rBar.bottom - g_rBar.top;
    if (h < 1) return;
    float v = 1.f - (float)(p.y - g_rBar.top) / (float)h;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    g_val = v;
    MakeWheel();
    SyncEdits();
    Refresh();
}

static void HitTrack(POINT p)
{
    int w = g_rTrack.right - g_rTrack.left;
    if (w < 1) return;
    int v = (int)((float)(p.x - g_rTrack.left) / (float)w * 10.f + 0.5f);
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    if (v == g_edgeW) return;
    g_edgeW = v;
    Refresh();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
    {
        g_fTitle = new Font(L"Microsoft YaHei UI", (REAL)Px(16), FontStyleBold, UnitPixel);
        g_fLab = new Font(L"Microsoft YaHei UI", (REAL)Px(13), FontStyleRegular, UnitPixel);
        g_fSm = new Font(L"Microsoft YaHei UI", (REAL)Px(12), FontStyleRegular, UnitPixel);
        g_fBtn = new Font(L"Microsoft YaHei UI", (REAL)Px(13), FontStyleRegular, UnitPixel);

        g_rClose = Rc(SET_W - 26 - 28, 12, 28, 28);
        g_rDone = Rc(SET_W - 26 - 110, SET_H - 16 - 40, 110, 40);
        g_rPick = Rc(140, 186, 130, 38);
        g_rReset = Rc(140, 234, 130, 38);
        g_rTab0 = Rc(26, 310, 90, 34);
        g_rTab1 = Rc(126, 310, 90, 34);
        g_rTab2 = Rc(226, 310, 90, 34);
        g_rTab3 = Rc(326, 310, 90, 34);
        g_rTileTrack = Rc(26, 400, 300, 8);
        g_rItemTrack = Rc(26, 514, 300, 8);

        g_wc.x = Px(130);
        g_wc.y = Px(442);
        g_wr = Px(84);

        g_rBar = Rc(250, 358, 24, 168);
        g_rSwatch = Rc(446, 346, 160, 180);
        g_rTrack = Rc(26, 604, 300, 8);
        for (int i = 0; i < 8; i++) g_rSw[i] = Rc(26 + i * 70, 674, 60, 60);

        int fx = Px(320);
        int fw = Px(100);
        int fh = Px(36);
        g_hName = CreateWindowExW(0, L"EDIT", g_d->name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            Px(26), Px(88), Px(588), Px(44), h, (HMENU)100, GetModuleHandleW(NULL), NULL);
        g_hR = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            fx, Px(346), fw, fh, h, (HMENU)201, GetModuleHandleW(NULL), NULL);
        g_hG = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            fx, Px(394), fw, fh, h, (HMENU)202, GetModuleHandleW(NULL), NULL);
        g_hB = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            fx, Px(442), fw, fh, h, (HMENU)203, GetModuleHandleW(NULL), NULL);
        g_hA = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            fx, Px(490), fw, fh, h, (HMENU)204, GetModuleHandleW(NULL), NULL);

        HFONT fnt = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND arr[5] = { g_hName, g_hR, g_hG, g_hB, g_hA };
        for (int i = 0; i < 5; i++)
        {
            SendMessageW(arr[i], WM_SETFONT, (WPARAM)fnt, TRUE);
            g_editOld = (WNDPROC)SetWindowLongPtrW(arr[i], GWLP_WNDPROC, (LONG_PTR)EditProc);
        }
        // 数值框里的字往左靠一点 不然贴着边框
        for (int i = 1; i < 5; i++)
            SendMessageW(arr[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(Px(8), Px(8)));

        g_eb = CreateSolidBrush(RGB(41, 42, 48));
        RECT crc;
        GetClientRect(h, &crc);
        if (crc.right > 0 && crc.bottom > 0)
            g_buf = new Bitmap(crc.right, crc.bottom, PixelFormat32bppPARGB);

        PullTarget();
        MakeWheel();
        SyncEdits();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(240, 240, 243));
        SetBkColor(dc, RGB(41, 42, 48));
        return (LRESULT)g_eb;
    }
    case WM_ACTIVATE:
        // 刚弹出来会先收到一次 inactive 用这个标记挡一下
        if (LOWORD(wp) == WA_ACTIVE) g_wasActive = true;
        else if (LOWORD(wp) == WA_INACTIVE && g_wasActive && !g_modal) CloseDlg();
        return 0;
    case WM_APP + 5:
        CommitEdit((HWND)wp);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (PtInRect(&g_rClose, p)) { CloseDlg(); return 0; }
        if (PtInRect(&g_rDone, p)) { CloseDlg(); return 0; }
        if (PtInRect(&g_rPick, p)) { PickImage(); return 0; }
        if (PtInRect(&g_rReset, p)) { ResetSkin(); return 0; }
        if (PtInRect(&g_rTab0, p)) { SetTarget(0); return 0; }
        if (PtInRect(&g_rTab1, p)) { SetTarget(1); return 0; }
        if (PtInRect(&g_rTab2, p)) { SetTarget(2); return 0; }
        if (PtInRect(&g_rTab3, p)) { SetSizeTab(); return 0; }

        if (g_sizeTab)
        {
            RECT tt = g_rTileTrack;
            tt.top -= Px(14);
            tt.bottom += Px(14);
            if (PtInRect(&tt, p))
            {
                g_drag = 4;
                SetCapture(h);
                HitSize(p);
                return 0;
            }
            RECT ti = g_rItemTrack;
            ti.top -= Px(14);
            ti.bottom += Px(14);
            if (PtInRect(&ti, p))
            {
                g_drag = 5;
                SetCapture(h);
                HitSize(p);
                return 0;
            }
            return 0;
        }

        for (int i = 0; i < 8; i++)
        {
            if (PtInRect(&g_rSw[i], p))
            {
                ApplyPreset(i);
                return 0;
            }
        }

        int dx = p.x - g_wc.x;
        int dy = p.y - g_wc.y;
        if (dx * dx + dy * dy <= g_wr * g_wr)
        {
            g_drag = 1;
            SetCapture(h);
            HitWheel(p);
            return 0;
        }
        RECT bar = g_rBar;
        bar.left -= Px(10);
        bar.right += Px(10);
        if (PtInRect(&bar, p))
        {
            g_drag = 2;
            SetCapture(h);
            HitBar(p);
            return 0;
        }
        RECT tr = g_rTrack;
        tr.top -= Px(12);
        tr.bottom += Px(12);
        if (PtInRect(&tr, p))
        {
            g_drag = 3;
            SetCapture(h);
            HitTrack(p);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!g_drag) return 0;
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (g_drag == 1) HitWheel(p);
        else if (g_drag == 2) HitBar(p);
        else if (g_drag == 3) HitTrack(p);
        else HitSize(p);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!g_drag) return 0;
        int was = g_drag;
        g_drag = 0;
        if (GetCapture() == h) ReleaseCapture();
        if (was == 4 || was == 5) ApplySizes();
        else PushAll();
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_buf)
        {
            Graphics g(g_buf);
            g.ResetTransform();
            g.Clear(Color(0, 0, 0, 0));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);

            RECT rc;
            GetClientRect(h, &rc);
            RectF full(0, 0, (REAL)rc.right, (REAL)rc.bottom);

            GraphicsPath bg;
            RoundPath(bg, full, (REAL)Px(18));
            SolidBrush sb(Color(252, 20, 21, 26));
            g.FillPath(&sb, &bg);

            StringFormat sf;
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(StringFormatFlagsNoWrap);
            sf.SetTrimming(StringTrimmingEllipsisCharacter);

            // 标题
            SolidBrush t1(Col(0xF2F3F7, 250));
            RectF tr((REAL)Px(26), 0, (REAL)Px(400), (REAL)Px(52));
            g.DrawString(L"文件夹设置", -1, g_fTitle, tr, &sf, &t1);

            // 关闭
            Pen xp(Col(0xD0D2D8, 210), 1.5f);
            REAL cs = (REAL)Px(28);
            REAL cx = (REAL)g_rClose.left;
            REAL cy = (REAL)g_rClose.top;
            g.DrawLine(&xp, cx + cs * 0.32f, cy + cs * 0.32f, cx + cs * 0.68f, cy + cs * 0.68f);
            g.DrawLine(&xp, cx + cs * 0.68f, cy + cs * 0.32f, cx + cs * 0.32f, cy + cs * 0.68f);

            auto label = [&](const wchar_t* s, int x, int y, int rgb)
            {
                SolidBrush b(Col(rgb, 200));
                RectF r((REAL)Px(x), (REAL)Px(y), (REAL)Px(400), (REAL)Px(22));
                g.DrawString(s, -1, g_fLab, r, &sf, &b);
            };

            label(L"名字", 26, 60, 0x8A8D96);
            label(L"桌面图标图片", 26, 146, 0x8A8D96);
            label(L"外观", 26, 284, 0x8A8D96);

            // 输入框外框
            auto frame = [&](int x, int y, int w2, int ht)
            {
                RectF r((REAL)Px(x - 6), (REAL)Px(y - 2), (REAL)Px(w2 + 12), (REAL)Px(ht + 4));
                GraphicsPath p;
                RoundPath(p, r, (REAL)Px(10));
                SolidBrush b(Col(0x292A30, 255));
                g.FillPath(&b, &p);
                Pen pen(Col(0xFFFFFF, 26), 1.f);
                g.DrawPath(&pen, &p);
            };
            frame(26, 88, 588, 44);
            int rows[4] = { 346, 394, 442, 490 };
            if (!g_sizeTab)
                for (int i = 0; i < 4; i++) frame(320, rows[i], 100, 36);

            // 图片预览
            RectF pv((REAL)Px(26), (REAL)Px(176), (REAL)Px(96), (REAL)Px(96));
            GraphicsPath pvp;
            RoundPath(pvp, pv, (REAL)Px(16));
            SolidBrush pvb(Col(0x292A30, 255));
            g.FillPath(&pvb, &pvp);
            Bitmap* sk = g_d->skinPath.empty() ? NULL : ImageGet(g_d->skinPath, Px(88));
            if (sk)
            {
                g.SetClip(&pvp);
                g.DrawImage(sk, RectF(pv.X + Px(4), pv.Y + Px(4), (REAL)Px(88), (REAL)Px(88)));
                g.ResetClip();
            }
            else
            {
                SolidBrush fb(Col(0xFFFFFF, 220));
                RectF b1(pv.X + pv.Width * 0.22f, pv.Y + pv.Height * 0.38f, pv.Width * 0.56f, pv.Height * 0.3f);
                GraphicsPath bp2;
                RoundPath(bp2, b1, (REAL)Px(8));
                g.FillPath(&fb, &bp2);
            }
            Pen pvpen(Col(0xFFFFFF, 46), 1.f);
            g.DrawPath(&pvpen, &pvp);

            auto button = [&](RECT r, const wchar_t* txt, bool strong, bool on)
            {
                RectF bf((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
                GraphicsPath p;
                RoundPath(p, bf, (REAL)Px(9));
                SolidBrush f(strong ? Col(0x4C6EF5, 255) : Col(0x34353C, 255));
                if (on) f.SetColor(Col(0x5A7CFF, 255));
                g.FillPath(&f, &p);
                StringFormat sf2;
                sf2.SetAlignment(StringAlignmentCenter);
                sf2.SetLineAlignment(StringAlignmentCenter);
                SolidBrush tx(strong ? Col(0xFFFFFF, 255) : Col(0xE6E8F0, 235));
                g.DrawString(txt, -1, g_fBtn, bf, &sf2, &tx);
            };
            button(g_rPick, L"选择图片", false, false);
            button(g_rReset, L"恢复默认", false, false);

            // 填充 描边 图标 大小 四个页签
            button(g_rTab0, L"填充", false, !g_sizeTab && g_target == 0);
            button(g_rTab1, L"描边", false, !g_sizeTab && g_target == 1);
            button(g_rTab2, L"图标", false, !g_sizeTab && g_target == 2);
            button(g_rTab3, L"大小", false, g_sizeTab);

            if (!g_sizeTab)
            {
            // 色轮
            if (g_wheel) g.DrawImage(g_wheel, RectF((REAL)(g_wc.x - g_wr), (REAL)(g_wc.y - g_wr), (REAL)(g_wr * 2), (REAL)(g_wr * 2)));
            float ang = g_hue * 3.14159f / 180.f;
            float mx = g_wc.x + cosf(ang) * g_sat * g_wr;
            float my = g_wc.y + sinf(ang) * g_sat * g_wr;
            SolidBrush mb(Col(0xFFFFFF, 255));
            g.FillEllipse(&mb, mx - Px(4), my - Px(4), (REAL)Px(8), (REAL)Px(8));
            Pen mo(Col(0x1E1E24, 255), 1.6f);
            g.DrawEllipse(&mo, mx - Px(6), my - Px(6), (REAL)Px(12), (REAL)Px(12));

            // 明度条
            RectF br((REAL)g_rBar.left, (REAL)g_rBar.top, (REAL)(g_rBar.right - g_rBar.left), (REAL)(g_rBar.bottom - g_rBar.top));
            GraphicsPath brp;
            RoundPath(brp, br, (REAL)Px(10));
            Color ct = Col(HsvRgb(g_hue, g_sat, 1.f), 255);
            Color cb(255, 0, 0, 0);
            LinearGradientBrush lgb(br, ct, cb, LinearGradientModeVertical);
            g.FillPath(&lgb, &brp);
            Pen brpen(Col(0xFFFFFF, 40), 1.f);
            g.DrawPath(&brpen, &brp);
            float vy = br.Y + (1.f - g_val) * br.Height;
            Pen vp(Col(0xFFFFFF, 255), 2.f);
            g.DrawLine(&vp, br.X - Px(3), vy, br.X + br.Width + Px(3), vy);

            // rgb 标签
            const wchar_t* tags[4] = { L"R", L"G", L"B", L"A" };
            for (int i = 0; i < 4; i++)
            {
                SolidBrush b(Col(0x8A8D96, 210));
                RectF r((REAL)Px(292), (REAL)Px(rows[i]), (REAL)Px(20), (REAL)Px(36));
                StringFormat s2;
                s2.SetAlignment(StringAlignmentFar);
                s2.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(tags[i], -1, g_fLab, r, &s2, &b);
            }
            // 数值框里右边的小字提示不弄了 免得跟输入框里的字打架

            // 颜色预览 压在棋盘格上才看得出透明度
            RectF swv((REAL)g_rSwatch.left, (REAL)g_rSwatch.top, (REAL)(g_rSwatch.right - g_rSwatch.left), (REAL)(g_rSwatch.bottom - g_rSwatch.top));
            GraphicsPath swp;
            RoundPath(swp, swv, (REAL)Px(14));
            g.SetClip(&swp);
            for (int y = 0; y < (int)swv.Height; y += Px(16))
            {
                for (int x = 0; x < (int)swv.Width; x += Px(16))
                {
                    int t2 = ((x / Px(16)) + (y / Px(16))) % 2;
                    SolidBrush cb2(t2 ? Col(0x3A3B42, 255) : Col(0x2A2B31, 255));
                    g.FillRectangle(&cb2, swv.X + x, swv.Y + y, (REAL)Px(16), (REAL)Px(16));
                }
            }
            int crgb = HsvRgb(g_hue, g_sat, g_val);
            SolidBrush fillb(Col(crgb, g_alpha));
            g.FillRectangle(&fillb, swv);
            g.ResetClip();
            Pen swpen(Col(0xFFFFFF, 50), 1.f);
            g.DrawPath(&swpen, &swp);

            SolidBrush tip(Col(0x8A8D96, 200));
            RectF tr2((REAL)g_rSwatch.left, (REAL)(g_rSwatch.bottom + 4), (REAL)(g_rSwatch.right - g_rSwatch.left), (REAL)Px(20));
            StringFormat s3;
            s3.SetAlignment(StringAlignmentCenter);
            s3.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(g_target == 0 ? L"面板填充" : (g_target == 1 ? L"面板描边" : L"图标描边"), -1, g_fSm, tr2, &s3, &tip);

            // 粗细滑条
            label(L"描边粗细", 26, 566, 0x8A8D96);
            RectF tk((REAL)g_rTrack.left, (REAL)g_rTrack.top, (REAL)(g_rTrack.right - g_rTrack.left), (REAL)(g_rTrack.bottom - g_rTrack.top));
            GraphicsPath tkb;
            RoundPath(tkb, tk, (REAL)(tk.Height / 2));
            SolidBrush tks(Col(0x3A3B42, 255));
            g.FillPath(&tks, &tkb);
            float kx = tk.X + (float)g_edgeW / 10.f * tk.Width;
            SolidBrush kf(Col(0x4C6EF5, 255));
            g.FillEllipse(&kf, kx - Px(9), tk.Y + tk.Height / 2 - Px(9), (REAL)Px(18), (REAL)Px(18));
            Pen kp(Col(0xFFFFFF, 60), 1.f);
            g.DrawEllipse(&kp, kx - Px(9), tk.Y + tk.Height / 2 - Px(9), (REAL)Px(18), (REAL)Px(18));
            wchar_t wbuf[16];
            if (g_edgeW == 0) lstrcpynW(wbuf, L"关", 16);
            else wsprintfW(wbuf, L"%d", g_edgeW);
            SolidBrush kv(Col(0xC8CAD2, 220));
            RectF kr((REAL)(g_rTrack.right + Px(16)), (REAL)(g_rTrack.top - Px(10)), (REAL)Px(60), (REAL)Px(28));
            g.DrawString(wbuf, -1, g_fLab, kr, &sf, &kv);

            // 预设
            label(L"撞色预设", 26, 644, 0x8A8D96);
            for (int i = 0; i < 8; i++)
            {
                RECT r = g_rSw[i];
                RectF f((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
                GraphicsPath p;
                RoundPath(p, f, (REAL)Px(12));
                SolidBrush fb2(Col(kPresets[i].fill, 255));
                g.FillPath(&fb2, &p);
                Pen ep(Col(kPresets[i].edge, 255), (REAL)Px(3));
                g.DrawPath(&ep, &p);
                bool on = g_d->fillColor == kPresets[i].fill && g_d->edgeColor == kPresets[i].edge;
                if (on)
                {
                    Pen sel(Col(0xFFFFFF, 220), 2.f);
                    g.DrawPath(&sel, &p);
                }
            }

            }
            else
            {
                // 大小这页 两条滑条 拖完关掉窗口才真正重排
                auto slider = [&](RECT tr, int v, int lo, int hi)
                {
                    RectF tkr((REAL)tr.left, (REAL)tr.top, (REAL)(tr.right - tr.left), (REAL)(tr.bottom - tr.top));
                    GraphicsPath tkp;
                    RoundPath(tkp, tkr, (REAL)(tkr.Height / 2));
                    SolidBrush tks(Col(0x3A3B42, 255));
                    g.FillPath(&tks, &tkp);
                    float k = (float)(v - lo) / (float)(hi - lo);
                    float kx = tkr.X + k * tkr.Width;
                    SolidBrush kf(Col(0x4C6EF5, 255));
                    g.FillEllipse(&kf, kx - Px(9), tkr.Y + tkr.Height / 2 - Px(9), (REAL)Px(18), (REAL)Px(18));
                    Pen kp(Col(0xFFFFFF, 60), 1.f);
                    g.DrawEllipse(&kp, kx - Px(9), tkr.Y + tkr.Height / 2 - Px(9), (REAL)Px(18), (REAL)Px(18));
                    wchar_t vb[16];
                    wsprintfW(vb, L"%d", v);
                    SolidBrush vv(Col(0xC8CAD2, 220));
                    RectF vr((REAL)(tr.right + Px(18)), (REAL)(tr.top - Px(10)), (REAL)Px(70), (REAL)Px(28));
                    g.DrawString(vb, -1, g_fLab, vr, &sf, &vv);
                };

                label(L"桌面图标大小", 26, 360, 0x8A8D96);
                slider(g_rTileTrack, g_tileDip, 72, 220);

                label(L"展开后图标大小", 26, 474, 0x8A8D96);
                slider(g_rItemTrack, g_itemDip, 48, 120);

                SolidBrush hint(Col(0x8A8D96, 170));
                RectF hr((REAL)Px(26), (REAL)Px(566), (REAL)Px(520), (REAL)Px(24));
                g.DrawString(L"拖完关掉这个窗口 图标就按新的尺寸重排", -1, g_fSm, hr, &sf, &hint);
            }

            button(g_rDone, L"完成", true, false);

            Graphics gdc(dc);
            gdc.DrawImage(g_buf, 0, 0);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        delete g_fTitle; g_fTitle = NULL;
        delete g_fLab; g_fLab = NULL;
        delete g_fSm; g_fSm = NULL;
        delete g_fBtn; g_fBtn = NULL;
        if (g_eb) { DeleteObject(g_eb); g_eb = NULL; }
        if (g_buf) { delete g_buf; g_buf = NULL; }
        if (g_wheel) { delete g_wheel; g_wheel = NULL; }
        g_hName = NULL;
        g_hR = g_hG = g_hB = g_hA = NULL;
        g_dlg = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void PanelSettingsOpen(FolderData* d)
{
    if (!d) return;
    if (g_dlg && IsWindow(g_dlg))
    {
        SetForegroundWindow(g_dlg);
        return;
    }

    HINSTANCE inst = GetModuleHandleW(NULL);
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"DeskFolderSettings";
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(101));
        RegisterClassExW(&wc);
        reg = true;
    }

    g_d = d;
    g_target = 0;
    g_sizeTab = false;
    g_drag = 0;
    g_wasActive = false;

    int w = Px(SET_W);
    int h = Px(SET_H);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    POINT pt;
    pt.x = d->x;
    pt.y = d->y;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int avw = wa.right - wa.left;
    int avh = wa.bottom - wa.top;
    if (w > avw) w = avw;
    if (h > avh) h = avh;
    int x = wa.left + (avw - w) / 2;
    int y = wa.top + (avh - h) / 2;

    HWND owner = d->hwnd;
    // 这里不能带 WS_EX_TOPMOST 有个坑 顶层窗口挂了所有者之后
    // 系统会顺手把所有者也就是桌面图标也标成置顶 面板就永远压在别的程序上面
    g_dlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"DeskFolderSettings", L"文件夹设置",
        WS_POPUP, x, y, w, h, owner, NULL, inst, NULL);
    if (!g_dlg) return;

    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, Px(36), Px(36));
    SetWindowRgn(g_dlg, rgn, TRUE);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(g_dlg, SW_SHOW);
    SetForegroundWindow(g_dlg);
    SetFocus(g_hName);

    MSG msg;
    while (IsWindow(g_dlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (IsDialogMessageW(g_dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) EnableWindow(owner, TRUE);
}
