#include "common.h"

using namespace Gdiplus;

static const UINT DUR = 260;
static const wchar_t* kCls = L"DeskFolderTile";

static Font* g_fTitle;
static Font* g_fItem;
static Font* g_fLabel;

class DropTarget;

struct FWin
{
    FolderData* d;
    HWND hwnd;
    HBITMAP dib;
    HDC memdc;
    void* bits;
    Bitmap* canvas;
    Graphics* g;
    int W, H;
    int px, py, pw, ph;
    int tx, ty, ts;
    float anim;
    float from;
    bool expanded;
    bool pressed;
    bool dragging;
    bool moved;
    POINT downScreen;
    int winX, winY;
    int homeX, homeY;   // 用户自己摆的位置 展开时临时挪开 收起来要回这
    int grabX, grabY;
    int hoverItem;
    bool hoverClose;
    bool menuOpen;
    int itemIdx;        // 正被按住的那个格子 -1 就是没有
    bool itemDrag;
    POINT itemDown;
    int itemGrabX, itemGrabY;
    DropTarget* drop;
    ULONGLONG t0;
    ULONGLONG lastToggle;
};

static Font* FTitle()
{
    if (!g_fTitle) g_fTitle = new Font(L"Microsoft YaHei UI", (REAL)Px(16), FontStyleBold, UnitPixel);
    return g_fTitle;
}

static Font* FItem()
{
    if (!g_fItem) g_fItem = new Font(L"Microsoft YaHei UI", (REAL)Px(12), FontStyleRegular, UnitPixel);
    return g_fItem;
}

static Font* FLabel()
{
    if (!g_fLabel) g_fLabel = new Font(L"Microsoft YaHei UI", (REAL)Px(13), FontStyleRegular, UnitPixel);
    return g_fLabel;
}

static int Af(float k, int a)
{
    int v = (int)(k * a + 0.5f);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static Color Hsv(float h, float s, float v, int alpha)
{
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.f, 2) - 1));
    float m = v - c;
    float r = 0, gg = 0, b = 0;
    if (h < 60) { r = c; gg = x; }
    else if (h < 120) { r = x; gg = c; }
    else if (h < 180) { gg = c; b = x; }
    else if (h < 240) { gg = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return Color((BYTE)alpha, (BYTE)((r + m) * 255), (BYTE)((gg + m) * 255), (BYTE)((b + m) * 255));
}

// 整体明暗调一下 面板的上下两段就是靠这个拉出层次
static Color Shade(int rgb, float k, int alpha)
{
    int r = (int)(((rgb >> 16) & 255) * k);
    int gg = (int)(((rgb >> 8) & 255) * k);
    int b = (int)((rgb & 255) * k);
    if (r > 255) r = 255;
    if (gg > 255) gg = 255;
    if (b > 255) b = 255;
    return Color((BYTE)alpha, (BYTE)r, (BYTE)gg, (BYTE)b);
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

static wstring ItemLabel(const wstring& path)
{
    int a = (int)path.size() - 1;
    while (a >= 0 && path[a] != L'\\' && path[a] != L'/') a--;
    wstring n = path.substr(a + 1);
    int dot = (int)n.rfind(L'.');
    if (dot > 0 && dot >= (int)n.size() - 8) n = n.substr(0, dot);
    return n;
}

static void DrawTile(FWin* w, float a)
{
    Graphics& g = *w->g;
    int s = w->ts;
    RectF r((REAL)w->tx, (REAL)w->ty, (REAL)s, (REAL)s);
    GraphicsPath p;
    RoundPath(p, r, (REAL)Px(26));

    Bitmap* skin = NULL;
    if (!w->d->skinPath.empty()) skin = ImageGet(w->d->skinPath, s);

    if (skin)
    {
        // 有自己选的图就不铺底色了 图片带透明的话直接透出桌面
        ImageAttributes ia;
        ColorMatrix cm;
        ZeroMemory(&cm, sizeof(cm));
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1;
        cm.m[3][3] = a;
        ia.SetColorMatrix(&cm);
        g.SetClip(&p);
        g.DrawImage(skin, r, 0, 0, (int)skin->GetWidth(), (int)skin->GetHeight(), UnitPixel, &ia, NULL, NULL);
        g.ResetClip();

        if (w->d->skinEdgeWidth > 0)
        {
            int ec = w->d->skinEdgeColor >= 0 ? w->d->skinEdgeColor : 0xFFFFFF;
            float lw = (float)Px((double)w->d->skinEdgeWidth / 10.0);
            if (lw < 1) lw = 1;
            // 往里收半个线宽 描边就压在方块边上 不会画到外面去
            int ins = (int)(lw * 0.5 + 0.5);
            RectF sr((REAL)(w->tx + ins), (REAL)(w->ty + ins), (REAL)(s - ins * 2), (REAL)(s - ins * 2));
            GraphicsPath sp;
            RoundPath(sp, sr, (REAL)Px(26));
            Pen pen(Shade(ec, 1.0f, Af(a, w->d->skinEdgeAlpha)), lw);
            g.DrawPath(&pen, &sp);
        }
    }
    else
    {
        Color c1 = Hsv((float)w->d->hue, 0.5f, 0.72f, Af(a, 255));
        Color c2 = Hsv((float)(w->d->hue + 24), 0.62f, 0.44f, Af(a, 255));
        LinearGradientBrush lb(r, c1, c2, LinearGradientModeVertical);
        g.FillPath(&lb, &p);

        Pen pen(Color((BYTE)Af(a, 70), 255, 255, 255), 1.0f);
        g.DrawPath(&pen, &p);

        SolidBrush wb(Color((BYTE)Af(a, 230), 255, 255, 255));
        float bx = (REAL)(w->tx + s * 0.2f);
        float bw = (REAL)(s * 0.6f);
        float by = (REAL)(w->ty + s * 0.36f);
        float bh = (REAL)(s * 0.34f);
        GraphicsPath tab;
        RoundPath(tab, RectF(bx, (REAL)(by - bh * 0.22f), (REAL)(bw * 0.52f), (REAL)(bh * 0.5f)), (REAL)Px(5));
        g.FillPath(&wb, &tab);
        GraphicsPath body;
        RoundPath(body, RectF(bx, by, bw, bh), (REAL)Px(7));
        g.FillPath(&wb, &body);
    }

    if (!w->d->name.empty())
    {
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentNear);
        fmt.SetTrimming(StringTrimmingEllipsisCharacter);
        fmt.SetFormatFlags(StringFormatFlagsNoWrap);
        RectF lr((REAL)(w->tx - Px(20)), (REAL)(w->ty + s + Px(3)), (REAL)(s + Px(40)), (REAL)Px(24));
        RectF lr2(lr.X, lr.Y + 1, lr.Width, lr.Height);
        SolidBrush sh(Color((BYTE)Af(a, 160), 0, 0, 0));
        g.DrawString(w->d->name.c_str(), -1, FLabel(), lr2, &fmt, &sh);
        SolidBrush tb(Color((BYTE)Af(a, 245), 255, 255, 255));
        g.DrawString(w->d->name.c_str(), -1, FLabel(), lr, &fmt, &tb);
    }
}

// 画面板里的一格 位置从数据里读 所以拖到哪就在哪
static void DrawCell(FWin* w, int i, float a)
{
    Graphics& g = *w->g;
    const FolderEntry& it = w->d->items[i];
    int cw = Px(CELL_DIP);
    int isz = Px(72);
    float ix = (REAL)(w->px + Px(24) + Px(it.x));
    float iy = (REAL)(w->py + Px(50) + Px(24) + Px(it.y));
    float t = (a - 0.22f - i * 0.025f) / 0.3f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (t <= 0) return;
    float off = (1 - t) * Px(10);

    if (w->hoverItem == i && t >= 1 && w->itemIdx < 0)
    {
        SolidBrush hb(Color((BYTE)Af(a, 24), 255, 255, 255));
        GraphicsPath hp;
        RoundPath(hp, RectF(ix + 4, iy + 4 + off, (REAL)(cw - 8), (REAL)(cw - 8)), (REAL)Px(12));
        g.FillPath(&hb, &hp);
    }

    Bitmap* ico = IconGet(it.path, isz);
    if (ico)
    {
        RectF dr(ix + (cw - isz) / 2.0f, iy + Px(8) + off, (REAL)isz, (REAL)isz);
        ImageAttributes ia;
        ColorMatrix cm;
        ZeroMemory(&cm, sizeof(cm));
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1;
        cm.m[3][3] = t * a;
        ia.SetColorMatrix(&cm);
        g.DrawImage(ico, dr, 0, 0, (int)ico->GetWidth(), (int)ico->GetHeight(), UnitPixel, &ia, NULL, NULL);
    }
    else
    {
        SolidBrush fb(Color((BYTE)Af(t * a, 55), 255, 255, 255));
        GraphicsPath fp;
        RoundPath(fp, RectF(ix + (cw - isz) / 2.0f, iy + Px(8) + off, (REAL)isz, (REAL)isz), (REAL)Px(16));
        g.FillPath(&fb, &fp);
    }

    StringFormat f2;
    f2.SetAlignment(StringAlignmentCenter);
    f2.SetLineAlignment(StringAlignmentNear);
    f2.SetTrimming(StringTrimmingEllipsisCharacter);
    f2.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF lr(ix + 2, iy + Px(84) + off, (REAL)(cw - 4), (REAL)Px(28));
    SolidBrush tb(Color((BYTE)Af(t * a, 236), 238, 240, 246));
    g.DrawString(ItemLabel(it.path).c_str(), -1, FItem(), lr, &f2, &tb);
}

static void DrawPanel(FWin* w, float a)
{
    Graphics& g = *w->g;
    RectF pr((REAL)w->px, (REAL)w->py, (REAL)w->pw, (REAL)w->ph);
    float rad = (REAL)Px(24);

    for (int i = 3; i >= 1; i--)
    {
        RectF sr(pr.X - (REAL)(i * 3), pr.Y - (REAL)(i * 3) + (REAL)Px(8),
                 pr.Width + (REAL)(i * 6), pr.Height + (REAL)(i * 6));
        GraphicsPath sp;
        RoundPath(sp, sr, rad + (REAL)(i * 3));
        SolidBrush sb(Color((BYTE)Af(a, 22), 0, 0, 0));
        g.FillPath(&sb, &sp);
    }

    GraphicsPath bp;
    RoundPath(bp, pr, rad);
    Color c1, c2;
    if (w->d->fillColor >= 0)
    {
        int al = Af(a, w->d->fillAlpha);
        c1 = Shade(w->d->fillColor, 1.0f, al);
        c2 = Shade(w->d->fillColor, 0.66f, al);
    }
    else
    {
        c1 = Color((BYTE)Af(a, 242), 31, 33, 41);
        c2 = Color((BYTE)Af(a, 242), 18, 19, 26);
    }
    LinearGradientBrush lb(pr, c1, c2, LinearGradientModeVertical);
    g.FillPath(&lb, &bp);

    if (w->d->edgeWidth > 0)
    {
        int rgb = w->d->edgeColor >= 0 ? w->d->edgeColor : 0xFFFFFF;
        float lw = (float)Px((double)w->d->edgeWidth / 10.0);
        if (lw < 1) lw = 1;
        Pen pen(Shade(rgb, 1.0f, Af(a, w->d->edgeAlpha)), lw);
        g.DrawPath(&pen, &bp);
    }

    StringFormat fmt;
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF tr((REAL)(w->px + Px(26)), (REAL)w->py, (REAL)(w->pw - Px(96)), (REAL)Px(50));
    SolidBrush tw(Color((BYTE)Af(a, 245), 242, 243, 247));
    g.DrawString(w->d->name.c_str(), -1, FTitle(), tr, &fmt, &tw);

    float cs = (REAL)Px(28);
    float cx = (REAL)(w->px + w->pw - Px(22) - Px(28));
    float cy = (REAL)(w->py + Px(11));
    if (w->hoverClose)
    {
        SolidBrush hb(Color((BYTE)Af(a, 30), 255, 255, 255));
        GraphicsPath hp;
        RoundPath(hp, RectF(cx, cy, cs, cs), (REAL)Px(8));
        g.FillPath(&hb, &hp);
    }
    Pen xp(Color((BYTE)Af(a, 200), 214, 216, 224), 1.4f);
    g.DrawLine(&xp, cx + cs * 0.32f, cy + cs * 0.32f, cx + cs * 0.68f, cy + cs * 0.68f);
    g.DrawLine(&xp, cx + cs * 0.68f, cy + cs * 0.32f, cx + cs * 0.32f, cy + cs * 0.68f);

    Pen sep(Color((BYTE)Af(a, 26), 255, 255, 255), 1.0f);
    g.DrawLine(&sep, (REAL)(w->px + Px(24)), (REAL)(w->py + Px(50)), (REAL)(w->px + w->pw - Px(24)), (REAL)(w->py + Px(50)));

    int n = (int)w->d->items.size();
    if (n > GRID_COLS * GRID_ROWS) n = GRID_COLS * GRID_ROWS;
    for (int i = 0; i < n; i++)
        if (i != w->itemIdx) DrawCell(w, i, a);
    // 手上拖着的那个最后画 保证压在最上层
    if (w->itemIdx >= 0 && w->itemIdx < n) DrawCell(w, w->itemIdx, a);
}

static void Render(FWin* w)
{
    if (!w->g) return;
    Graphics& g = *w->g;
    g.ResetTransform();
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    float a = w->anim;
    if (a > 0.002f)
    {
        float s = 0.35f + 0.65f * a;
        float cx = (float)(w->px + w->pw / 2);
        float cy = (float)(w->py + w->ph / 2);
        g.TranslateTransform(cx, cy);
        g.ScaleTransform(s, s);
        g.TranslateTransform(-cx, -cy);
        DrawPanel(w, a);
        g.ResetTransform();
    }
    float ca = 1.0f - a;
    if (ca > 0.002f)
    {
        float s = 1.0f - 0.1f * a;
        float cx = (float)(w->tx + w->ts / 2);
        float cy = (float)(w->ty + w->ts / 2);
        g.TranslateTransform(cx, cy);
        g.ScaleTransform(s, s);
        g.TranslateTransform(-cx, -cy);
        DrawTile(w, ca);
        g.ResetTransform();
    }

    POINT dst;
    dst.x = w->winX;
    dst.y = w->winY;
    SIZE sz;
    sz.cx = w->W;
    sz.cy = w->H;
    POINT src;
    src.x = 0;
    src.y = 0;
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(w->hwnd, NULL, &dst, &sz, w->memdc, &src, 0, &bf, ULW_ALPHA);
}

// 一批文件往文件夹里塞 拖进来和 OLE 那条路都走这里
static int AddDrop(FWin* w, HDROP drop)
{
    int n = (int)DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    int room = GRID_COLS * GRID_ROWS - (int)w->d->items.size();
    for (int i = 0; i < n && room > 0; i++)
    {
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, (UINT)i, path, MAX_PATH * 4))
            if (StoreAddItem(w->d, path)) room--;
    }
    Render(w);
    return n;
}

// 从资源管理器往这边拖 只靠 WM_DROPFILES 有时收不到 再挂个 OLE 落点
class DropTarget : public IDropTarget
{
public:
    FWin* w;
    LONG ref;
    bool ok;

    DropTarget(FWin* win) : w(win), ref(1), ok(false) {}
    virtual ~DropTarget() {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropTarget))
        {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return (ULONG)InterlockedIncrement(&ref);
    }

    // 对象跟着窗口活着 这里只减计数 真正 delete 在 WM_DESTROY
    ULONG STDMETHODCALLTYPE Release() override
    {
        return (ULONG)InterlockedDecrement(&ref);
    }

    static bool HasFiles(IDataObject* obj)
    {
        if (!obj) return false;
        FORMATETC fe;
        ZeroMemory(&fe, sizeof(fe));
        fe.cfFormat = CF_HDROP;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = -1;
        fe.tymed = TYMED_HGLOBAL;
        return obj->QueryGetData(&fe) == S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD, POINTL, DWORD* eff) override
    {
        ok = HasFiles(obj);
        if (eff) *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override
    {
        if (eff) *eff = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        ok = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD, POINTL, DWORD* eff) override
    {
        ok = false;
        if (eff) *eff = DROPEFFECT_NONE;
        if (!HasFiles(obj)) return S_OK;

        FORMATETC fe;
        ZeroMemory(&fe, sizeof(fe));
        fe.cfFormat = CF_HDROP;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = -1;
        fe.tymed = TYMED_HGLOBAL;
        STGMEDIUM sm;
        ZeroMemory(&sm, sizeof(sm));
        if (obj->GetData(&fe, &sm) != S_OK) return S_OK;

        if (sm.tymed == TYMED_HGLOBAL && sm.hGlobal)
        {
            AddDrop(w, (HDROP)sm.hGlobal);
            if (eff) *eff = DROPEFFECT_COPY;
        }
        // 这块内存归 OLE 管 别自己 DragFinish
        ReleaseStgMedium(&sm);
        return S_OK;
    }
};

static void KeepInWorkArea(FWin* w);
static void HomeBack(FWin* w);

static void StartAnim(FWin* w, bool expand)
{
    if (expand) KeepInWorkArea(w);
    else HomeBack(w);

    w->expanded = expand;
    w->from = w->anim;
    w->t0 = GetTickCount64();
    SetTimer(w->hwnd, 1, 15, NULL);
}

static void Tick(FWin* w)
{
    float raw = (float)((double)(GetTickCount64() - w->t0) / (double)DUR);
    if (raw > 1) raw = 1;
    float e = 1 - powf(1 - raw, 3);
    float to = w->expanded ? 1.0f : 0.0f;
    w->anim = w->from + (to - w->from) * e;
    if (raw >= 1)
    {
        w->anim = to;
        KillTimer(w->hwnd, 1);
    }
    Render(w);
}

static int HitItem(FWin* w, POINT p)
{
    if (w->anim < 0.99f) return -1;
    int cw = Px(CELL_DIP);
    int n = (int)w->d->items.size();
    // 从后往前找 后画的在上面 点到叠在一起的地方应该拿上面那个
    for (int i = n - 1; i >= 0; i--)
    {
        int x = w->px + Px(24) + Px(w->d->items[i].x);
        int y = w->py + Px(50) + Px(24) + Px(w->d->items[i].y);
        if (p.x >= x && p.x < x + cw && p.y >= y && p.y < y + cw) return i;
    }
    return -1;
}

static bool HitClose(FWin* w, POINT p)
{
    if (w->anim < 0.99f) return false;
    int x = w->px + w->pw - Px(22) - Px(28);
    int y = w->py + Px(11);
    return p.x >= x && p.x < x + Px(28) && p.y >= y && p.y < y + Px(28);
}

static void FolderMenu(FWin* w);
static void ItemMenu(FWin* w, int index);

static void SavePos(FWin* w)
{
    w->d->x = w->winX + w->W / 2;
    w->d->y = w->winY + w->H / 2;
    w->homeX = w->winX;
    w->homeY = w->winY;
    StoreSave();
}

// 图标贴边的时候点开 面板有一半在屏幕外 整块窗口挪进来 收起来再挪回去
static void KeepInWorkArea(FWin* w)
{
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    POINT pt;
    pt.x = w->winX + w->W / 2;
    pt.y = w->winY + w->H / 2;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT a = mi.rcWork;

    int x = w->winX;
    int y = w->winY;
    int aw = a.right - a.left;
    int ah = a.bottom - a.top;
    if (w->W >= aw) x = a.left;
    else
    {
        if (x < a.left) x = a.left;
        if (x + w->W > a.right) x = a.right - w->W;
    }
    if (w->H >= ah) y = a.top;
    else
    {
        if (y < a.top) y = a.top;
        if (y + w->H > a.bottom) y = a.bottom - w->H;
    }
    if (x == w->winX && y == w->winY) return;

    w->winX = x;
    w->winY = y;
    SetWindowPos(w->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void HomeBack(FWin* w)
{
    if (w->winX == w->homeX && w->winY == w->homeY) return;
    w->winX = w->homeX;
    w->winY = w->homeY;
    SetWindowPos(w->hwnd, NULL, w->winX, w->winY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void FolderMenu(FWin* w)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"打开");
    AppendMenuW(m, MF_STRING, 2, L"重命名");
    AppendMenuW(m, MF_STRING, 4, L"设置");
    AppendMenuW(m, MF_STRING, 3, L"删除文件夹");
    POINT p;
    GetCursorPos(&p);
    w->menuOpen = true;
    SetForegroundWindow(w->hwnd);
    int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, w->hwnd, NULL);
    PostMessageW(w->hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    w->menuOpen = false;

    if (cmd == 1) StartAnim(w, true);
    else if (cmd == 2)
    {
        wstring name = w->d->name;
        if (NameInput(NULL, L"重命名", name, name) && !name.empty())
        {
            w->d->name = name;
            StoreSave();
            Render(w);
        }
    }
    else if (cmd == 4)
    {
        // 设置窗口是模态的 关掉之后把改了的东西重画一遍
        PanelSettingsOpen(w->d);
        Render(w);
    }
    else if (cmd == 3)
    {
        if (MessageBoxW(NULL, L"删除这个图标文件夹 里面的快捷方式不会删", L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        wstring id = w->d->id;
        w->d->hwnd = NULL;
        DestroyWindow(w->hwnd);
        StoreRemove(id);
    }
}

static void ItemMenu(FWin* w, int index)
{
    if (index < 0 || index >= (int)w->d->items.size()) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"打开");
    AppendMenuW(m, MF_STRING, 2, L"从文件夹移除");
    POINT p;
    GetCursorPos(&p);
    w->menuOpen = true;
    SetForegroundWindow(w->hwnd);
    int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, w->hwnd, NULL);
    PostMessageW(w->hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    w->menuOpen = false;

    if (cmd == 1) LaunchPath(w->d->items[index].path);
    else if (cmd == 2)
    {
        StoreRemoveItem(w->d, index);
        w->hoverItem = -1;
        Render(w);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    FWin* w = (FWin*)GetWindowLongPtrW(h, GWLP_USERDATA);

    switch (m)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        w = (FWin*)cs->lpCreateParams;
        w->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);

        w->pw = Px(GRID_COLS * CELL_DIP + 48);
        w->ph = Px(GRID_ROWS * CELL_DIP + 50 + 48);
        w->W = w->pw + Px(72);
        w->H = w->ph + Px(72);
        w->px = Px(36);
        w->py = Px(36);
        w->ts = Px(120);
        w->tx = (w->W - w->ts) / 2;
        w->ty = (w->H - w->ts) / 2 - Px(8);

        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w->W;
        bi.bmiHeader.biHeight = -w->H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        w->dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &w->bits, NULL, 0);
        w->memdc = CreateCompatibleDC(NULL);
        SelectObject(w->memdc, w->dib);
        w->canvas = new Bitmap(w->W, w->H, w->W * 4, PixelFormat32bppPARGB, (BYTE*)w->bits);
        w->g = new Graphics(w->canvas);

        w->drop = new DropTarget(w);
        if (RegisterDragDrop(h, w->drop) != S_OK)
        {
            // 注册不上就退回老的 WM_DROPFILES
            delete w->drop;
            w->drop = NULL;
            DragAcceptFiles(h, TRUE);
        }

        // 老数据里的坐标是按小窗口存的 换成现在这个尺寸后会跑到屏幕外 这里按显示器工作区夹一下
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        POINT pt;
        pt.x = w->d->x;
        pt.y = w->d->y;
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(mon, &mi);
        RECT a = mi.rcWork;
        int x = w->d->x - w->W / 2;
        int y = w->d->y - w->H / 2;
        if (x < a.left) x = a.left;
        if (y < a.top) y = a.top;
        if (x > a.right - w->W) x = a.right - w->W;
        if (y > a.bottom - w->H) y = a.bottom - w->H;
        w->winX = x;
        w->winY = y;
        w->homeX = x;
        w->homeY = y;
        SetWindowPos(h, NULL, x, y, w->W, w->H, SWP_NOZORDER | SWP_NOACTIVATE);

        Render(w);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) Tick(w);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_APP + 1:
        Render(w);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && w->expanded && !w->menuOpen)
            StartAnim(w, false);
        return 0;
    case WM_LBUTTONDOWN:
    {
        // 动画还在跑的时候先不接鼠标 免得拖一半又弹回去
        if (w->anim > 0.01f && w->anim < 0.99f) return 0;
        if (w->anim >= 0.99f)
        {
            // 展开状态下按在图标和关闭钮上是要点它们 空白处才拖整块
            POINT p;
            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            if (HitClose(w, p)) return 0;
            int idx = HitItem(w, p);
            if (idx >= 0)
            {
                // 按住图标先记下来 动起来才算拖 没动就是单击打开
                POINT c;
                GetCursorPos(&c);
                w->itemIdx = idx;
                w->itemDrag = false;
                w->itemDown = c;
                w->itemGrabX = c.x - (w->winX + w->px + Px(24) + Px(w->d->items[idx].x));
                w->itemGrabY = c.y - (w->winY + w->py + Px(50) + Px(24) + Px(w->d->items[idx].y));
                SetCapture(h);
                return 0;
            }
        }
        w->pressed = true;
        w->dragging = false;
        GetCursorPos(&w->downScreen);
        w->grabX = w->downScreen.x - w->winX;
        w->grabY = w->downScreen.y - w->winY;
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (w->itemIdx >= 0)
        {
            POINT c;
            GetCursorPos(&c);
            if (!w->itemDrag)
            {
                int ddx = c.x - w->itemDown.x;
                int ddy = c.y - w->itemDown.y;
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (ddx + ddy > Px(6)) w->itemDrag = true;
            }
            if (w->itemDrag && w->itemIdx < (int)w->d->items.size())
            {
                FolderEntry& it = w->d->items[w->itemIdx];
                int bx = c.x - w->itemGrabX - (w->winX + w->px + Px(24));
                int by = c.y - w->itemGrabY - (w->winY + w->py + Px(50) + Px(24));
                int nx = (int)(Dip(bx) + 0.5);
                int ny = (int)(Dip(by) + 0.5);
                // 别让格子被拖出面板 拖出去就看不见了
                int mx = GRID_COLS * CELL_DIP - CELL_DIP;
                int my = GRID_ROWS * CELL_DIP - CELL_DIP;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > mx) nx = mx;
                if (ny > my) ny = my;
                if (nx != it.x || ny != it.y)
                {
                    it.x = nx;
                    it.y = ny;
                    Render(w);
                }
            }
            return 0;
        }
        if (w->pressed && !w->dragging)
        {
            POINT c;
            GetCursorPos(&c);
            int dx = c.x - w->downScreen.x;
            int dy = c.y - w->downScreen.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy > Px(6)) w->dragging = true;
        }
        if (w->dragging)
        {
            POINT c;
            GetCursorPos(&c);
            int x = c.x - w->grabX;
            int y = c.y - w->grabY;

            // 拖动时至少让那个方块留在当前显示器里 拖到边上不会丢掉
            MONITORINFO mi;
            ZeroMemory(&mi, sizeof(mi));
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
            RECT a = mi.rcWork;
            if (x + w->tx < a.left) x = a.left - w->tx;
            if (y + w->ty < a.top) y = a.top - w->ty;
            if (x + w->tx + w->ts > a.right) x = a.right - w->tx - w->ts;
            if (y + w->ty + w->ts > a.bottom) y = a.bottom - w->ty - w->ts;

            w->winX = x;
            w->winY = y;
            // 分层窗口的内容由 UpdateLayeredWindow 一起搬 光调 SetWindowPos 会只动壳子
            SetWindowPos(h, NULL, w->winX, w->winY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            Render(w);
            return 0;
        }
        if (w->anim >= 0.99f)
        {
            int idx = HitItem(w, p);
            bool hc = HitClose(w, p);
            if (idx != w->hoverItem || hc != w->hoverClose)
            {
                w->hoverItem = idx;
                w->hoverClose = hc;
                Render(w);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (w->itemIdx >= 0)
        {
            int idx = w->itemIdx;
            bool moved = w->itemDrag;
            w->itemIdx = -1;
            w->itemDrag = false;
            if (GetCapture() == h) ReleaseCapture();
            if (moved) StoreSave();
            else if (idx < (int)w->d->items.size()) LaunchPath(w->d->items[idx].path);
            return 0;
        }
        bool wasDrag = w->dragging;
        w->pressed = false;
        w->dragging = false;
        if (GetCapture() == h) ReleaseCapture();
        if (wasDrag)
        {
            SavePos(w);
            return 0;
        }
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (!w->expanded)
        {
            ULONGLONG now = GetTickCount64();
            if (now - w->lastToggle < 300) return 0;
            w->lastToggle = now;
            SetForegroundWindow(h);
            StartAnim(w, true);
            return 0;
        }
        if (HitClose(w, p))
        {
            StartAnim(w, false);
            return 0;
        }
        int idx = HitItem(w, p);
        if (idx >= 0) LaunchPath(w->d->items[idx].path);
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (w->anim >= 0.99f)
        {
            int idx = HitItem(w, p);
            if (idx >= 0) ItemMenu(w, idx);
            return 0;
        }
        if (w->anim > 0.01f) return 0;
        FolderMenu(w);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && w->expanded) StartAnim(w, false);
        return 0;
    case WM_DROPFILES:
    {
        HDROP drop = (HDROP)wp;
        AddDrop(w, drop);
        DragFinish(drop);
        return 0;
    }
    case WM_DESTROY:
    {
        FolderData* d = w->d;
        d->hwnd = NULL;
        d->extra = NULL;
        if (w->drop)
        {
            RevokeDragDrop(h);
            delete w->drop;
            w->drop = NULL;
        }
        if (w->g) delete w->g;
        if (w->canvas) delete w->canvas;
        if (w->memdc) DeleteDC(w->memdc);
        if (w->dib) DeleteObject(w->dib);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete w;
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}

void FolderShow(FolderData* d)
{
    if (d->hwnd && IsWindow(d->hwnd))
    {
        ShowWindow(d->hwnd, SW_SHOWNOACTIVATE);
        return;
    }

    HINSTANCE inst = GetModuleHandleW(NULL);
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    int W = Px(GRID_COLS * CELL_DIP + 48) + Px(72);
    int H = Px(GRID_ROWS * CELL_DIP + 50 + 48) + Px(72);
    FWin* w = new FWin();
    ZeroMemory(w, sizeof(FWin));
    w->d = d;
    w->hoverItem = -1;
    w->itemIdx = -1;

    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kCls, L"", WS_POPUP,
        d->x - W / 2, d->y - H / 2, W, H, NULL, NULL, inst, w);
    if (!h)
    {
        delete w;
        return;
    }
    d->hwnd = h;
    d->extra = w;
    ShowWindow(h, SW_SHOWNOACTIVATE);
}

void FolderDestroy(FolderData* d)
{
    if (d->hwnd && IsWindow(d->hwnd)) DestroyWindow(d->hwnd);
    d->hwnd = NULL;
    d->extra = NULL;
}

void FolderReloadItems(FolderData* d)
{
    if (d->hwnd && IsWindow(d->hwnd)) PostMessageW(d->hwnd, WM_APP + 1, 0, 0);
}
