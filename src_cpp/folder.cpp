#include "common.h"

using namespace Gdiplus;

static const UINT DUR = 260;
static const wchar_t* kCls = L"DeskFolderTile";

// 方块和格子的尺寸 老数据这两个字段是 -1 就走默认
static int TileDip(FolderData* d)
{
    int v = d->tileSize > 0 ? d->tileSize : 120;
    if (v < 72) v = 72;
    if (v > 220) v = 220;
    return v;
}

static int ItemDip(FolderData* d)
{
    int v = d->itemSize > 0 ? d->itemSize : 72;
    if (v < 48) v = 48;
    if (v > 120) v = 120;
    return v;
}

// 格子跟着图标一起长 图标调大以后里面的字和间距才不会挤成一团
static int CellDip(FolderData* d)
{
    return ItemDip(d) + 46;
}

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
    bool hoverPin;
    int scroll;         // 面板内容往上卷了多少 单位像素
    bool menuOpen;
    int itemIdx;        // 正被按住的那个格子 -1 就是没有
    bool itemDrag;
    POINT itemDown;
    int itemGrabX, itemGrabY;
    DropTarget* drop;
    ULONGLONG t0;
    ULONGLONG lastToggle;
};

// 面板里放格子那一块有多高 单位像素
static int ViewH(FWin* w)
{
    return Px(GRID_ROWS * CellDip(w->d));
}

// 内容一直伸到多深 有格子摆到很下面就算到那
// 没超出可视区就不用卷 滚动条也不画
static int ContentH(FWin* w)
{
    int cell = Px(CellDip(w->d));
    int b = ViewH(w);
    for (int i = 0; i < (int)w->d->items.size(); i++)
    {
        int y = Px(w->d->items[i].y) + cell;
        if (y > b) b = y;
    }
    return b;
}

// 往下最多摆到哪 留够余量就行 免得手一直按在底边内容没完没了地长
static int MaxItemY(FWin* w)
{
    int cell = CellDip(w->d);
    return GRID_ROWS * cell * 8 - cell;
}

static void ClampScroll(FWin* w)
{
    int mx = ContentH(w) - ViewH(w);
    if (mx < 0) mx = 0;
    if (w->scroll > mx) w->scroll = mx;
    if (w->scroll < 0) w->scroll = 0;
}

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
    int cw = Px(CellDip(w->d));
    int isz = Px(ItemDip(w->d));
    float ix = (REAL)(w->px + Px(24) + Px(it.x));
    float iy = (REAL)(w->py + Px(50) + Px(24) + Px(it.y) - w->scroll);
    // 格子多了之后出场那个错开不能一直往后排 不然靠后的永远等不到出场
    float t = (a - 0.22f - (i < 12 ? i : 12) * 0.025f) / 0.3f;
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
    RectF lr(ix + 2, iy + Px(8) + isz + Px(6) + off, (REAL)(cw - 4), (REAL)Px(28));
    SolidBrush tb(Color((BYTE)Af(t * a, 236), 238, 240, 246));
    g.DrawString(ItemLabel(it.path).c_str(), -1, FItem(), lr, &f2, &tb);
}

// 内容多过一屏就在右边画根细条 一眼能看出下面还压着东西
static void DrawBar(FWin* w, float a)
{
    int maxs = ContentH(w) - ViewH(w);
    if (maxs <= 0) return;
    Graphics& g = *w->g;
    float vh = (float)ViewH(w);
    float th = vh * vh / (float)(vh + maxs);
    if (th < (float)Px(34)) th = (float)Px(34);
    float bx = (float)(w->px + w->pw - Px(15));
    float top = (float)(w->py + Px(50) + Px(24));
    float by = top + (vh - th) * ((float)w->scroll / (float)maxs);

    SolidBrush tb(Color((BYTE)Af(a, 36), 255, 255, 255));
    GraphicsPath tp;
    RoundPath(tp, RectF(bx, top, (REAL)Px(4), (REAL)vh), (REAL)Px(2));
    g.FillPath(&tb, &tp);

    SolidBrush bb(Color((BYTE)Af(a, 130), 255, 255, 255));
    GraphicsPath bp;
    RoundPath(bp, RectF(bx, by, (REAL)Px(4), (REAL)th), (REAL)Px(2));
    g.FillPath(&bb, &bp);
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

    // 叉号左边那颗钉子 钉上就实心 没钉就是空心的
    float pinX = (REAL)(w->px + w->pw - Px(22) - Px(28) - Px(32));
    float pinY = (REAL)(w->py + Px(11));
    if (w->hoverPin)
    {
        SolidBrush hb(Color((BYTE)Af(a, 30), 255, 255, 255));
        GraphicsPath hp;
        RoundPath(hp, RectF(pinX, pinY, cs, cs), (REAL)Px(8));
        g.FillPath(&hb, &hp);
    }
    bool pinOn = w->d->pinned != 0;
    Pen pp(Color((BYTE)Af(a, pinOn ? 235 : 120), 214, 216, 224), 1.4f);
    g.DrawEllipse(&pp, pinX + cs * 0.27f, pinY + cs * 0.17f, cs * 0.46f, cs * 0.46f);
    if (pinOn)
    {
        SolidBrush pb(Color((BYTE)Af(a, 235), 214, 216, 224));
        g.FillEllipse(&pb, pinX + cs * 0.27f, pinY + cs * 0.17f, cs * 0.46f, cs * 0.46f);
    }
    g.DrawLine(&pp, pinX + cs * 0.36f, pinY + cs * 0.56f, pinX + cs * 0.64f, pinY + cs * 0.56f);
    g.DrawLine(&pp, pinX + cs * 0.5f, pinY + cs * 0.56f, pinX + cs * 0.5f, pinY + cs * 0.84f);

    Pen sep(Color((BYTE)Af(a, 26), 255, 255, 255), 1.0f);
    g.DrawLine(&sep, (REAL)(w->px + Px(24)), (REAL)(w->py + Px(50)), (REAL)(w->px + w->pw - Px(24)), (REAL)(w->py + Px(50)));

    int n = (int)w->d->items.size();
    // 格子在面板框里画 卷到框外的那截让裁剪挡掉 不然会爬到标题栏上
    RectF clip((REAL)(w->px + Px(24)), (REAL)(w->py + Px(50) + Px(24)),
               (REAL)Px(GRID_COLS * CellDip(w->d)), (REAL)ViewH(w));
    g.SetClip(clip);
    for (int i = 0; i < n; i++)
        if (i != w->itemIdx) DrawCell(w, i, a);
    // 手上拖着的那个最后画 保证压在最上层
    if (w->itemIdx >= 0 && w->itemIdx < n) DrawCell(w, w->itemIdx, a);
    g.ResetClip();

    DrawBar(w, a);
}

static void Render(FWin* w)
{
    if (!w->g) return;
    ClampScroll(w);
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
    // 位置统一由 SetWindowPos 管 这里只把像素推上去 传坐标的话窗口会被拽回屏幕坐标去
    UpdateLayeredWindow(w->hwnd, NULL, NULL, &sz, w->memdc, &src, 0, &bf, ULW_ALPHA);
}

// 一批文件往文件夹里塞 拖进来和 OLE 那条路都走这里
static int AddDrop(FWin* w, HDROP drop)
{
    int n = (int)DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    // 装多少都行 一屏放不下就往下卷
    for (int i = 0; i < n; i++)
    {
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, (UINT)i, path, MAX_PATH * 4))
            StoreAddItem(w->d, path);
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
static void MoveWin(FWin* w, int x, int y);

static void StartAnim(FWin* w, bool expand)
{
    // 打开的时候从头看 上次卷到哪了不管
    if (expand)
    {
        KeepInWorkArea(w);
        w->scroll = 0;
    }
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
    // 先看这个点是不是落在框里 卷到框外的格子点不到
    int left = w->px + Px(24);
    int top = w->py + Px(50) + Px(24);
    int cw = Px(CellDip(w->d));
    if (p.x < left || p.x >= left + Px(GRID_COLS * CellDip(w->d))) return -1;
    if (p.y < top || p.y >= top + ViewH(w)) return -1;
    int n = (int)w->d->items.size();
    // 从后往前找 后画的在上面 点到叠在一起的地方应该拿上面那个
    for (int i = n - 1; i >= 0; i--)
    {
        int x = left + Px(w->d->items[i].x);
        int y = top + Px(w->d->items[i].y) - w->scroll;
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

// 钉子就在叉号左边那一格
static bool HitPin(FWin* w, POINT p)
{
    if (w->anim < 0.99f) return false;
    int x = w->px + w->pw - Px(22) - Px(28) - Px(32);
    int y = w->py + Px(11);
    return p.x >= x && p.x < x + Px(28) && p.y >= y && p.y < y + Px(28);
}

// 手上的格子不能被拖出框 卷一下让它始终留在眼前
static void DragFollow(FWin* w)
{
    if (w->itemIdx < 0 || w->itemIdx >= (int)w->d->items.size()) return;
    int top = Px(w->d->items[w->itemIdx].y);
    int cell = Px(CellDip(w->d));
    int vh = ViewH(w);
    if (top - w->scroll < 0) w->scroll = top;
    else if (top + cell - w->scroll > vh) w->scroll = top + cell - vh;
    ClampScroll(w);
}

// 拖到上下两头 手停着不动也要一直翻
// 定时器三十毫秒挪一点 顺着方向把格子和卷动一起走 看着就像在拖
static void DragScroll(FWin* w)
{
    if (!w->itemDrag || w->itemIdx < 0 || w->itemIdx >= (int)w->d->items.size())
    {
        KillTimer(w->hwnd, 3);
        return;
    }
    POINT c;
    GetCursorPos(&c);
    ScreenToClient(w->hwnd, &c);
    int top = w->py + Px(50) + Px(24);
    int vh = ViewH(w);
    int edge = Px(46);
    int step = Px(16);
    int dir = 0;
    if (c.y < top + edge) dir = -1;
    else if (c.y > top + vh - edge) dir = 1;
    if (dir == 0) return;

    int dipStep = (int)(Dip(step) + 0.5);
    if (dipStep < 1) dipStep = 1;
    FolderEntry& it = w->d->items[w->itemIdx];
    if (dir > 0) it.y += dipStep;
    else if (it.y > 0) it.y -= dipStep;
    else return;
    if (it.y < 0) it.y = 0;
    if (it.y > MaxItemY(w)) it.y = MaxItemY(w);
    DragFollow(w);
    Render(w);
}

static void FolderMenu(FWin* w);
static void ItemMenu(FWin* w, int index);

static void SavePos(FWin* w)
{
    // 存自己摆的那个位置 别存展开时临时挪过去那一下
    // 拿展开时的位置写盘 下次开机会跑到面板挪过去的那里 看着就是每次重启都偏一点
    w->d->x = w->homeX + w->W / 2;
    w->d->y = w->homeY + w->H / 2;
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
    MoveWin(w, x, y);
}

static void HomeBack(FWin* w)
{
    if (w->winX == w->homeX && w->winY == w->homeY) return;
    w->winX = w->homeX;
    w->winY = w->homeY;
    MoveWin(w, w->winX, w->winY);
}

// 方块是普通窗口 位置统一按屏幕坐标算 这里只是挪个位置
static void MoveWin(FWin* w, int x, int y)
{
    SetWindowPos(w->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void FolderMenu(FWin* w);

// 被系统最小化或者藏起来了 就把自己弄回来
// 关键是别抢前台 显示桌面那一下最烦人来劲的窗口 用 SW_RESTORE 会把它激活 结果被系统反手再压回去 方块就一直闪到看不见
// 换成 SW_SHOWNOACTIVATE 站回来但不吱声 桌面层该在哪还在哪
static void WakeUp(HWND h)
{
    if (!h || !IsWindow(h)) return;
    if (IsIconic(h))
    {
        WINDOWPLACEMENT pl;
        ZeroMemory(&pl, sizeof(pl));
        pl.length = sizeof(pl);
        if (GetWindowPlacement(h, &pl))
        {
            pl.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(h, &pl);
        }
    }
    if (!IsWindowVisible(h)) ShowWindow(h, SW_SHOWNOACTIVATE);
}

// 桌面不是一层窗口 壁纸和图标是 WorkerW 盖在 Progman 上面的 得站到这串顶上才看得见
// 顶上再往上就是任务栏和程序了 那些不归我们管
// 这一串都是 explorer 自己的窗口 跨进程挪层级系统不会拦 提权程序占着上面也没关系
static HWND DeskAnchor()
{
    HWND h = FindWindowW(L"Progman", NULL);
    if (!h || !IsWindow(h)) return NULL;
    wchar_t cn[64];
    for (;;)
    {
        HWND nxt = GetWindow(h, GW_HWNDPREV);
        if (!nxt) break;
        if (!GetClassNameW(nxt, cn, 64)) break;
        if (lstrcmpiW(cn, L"WorkerW") != 0 && lstrcmpiW(cn, L"Progman") != 0) break;
        h = nxt;
    }
    return h;
}

// 方块只待在桌面正上方那一格 程序窗口永远压在它上面
// 锚点只拿桌面层自己和自己的方块 这俩都是自己人 系统不会拦
// 之前拿桌面上面第一个窗口当锚点 碰上提权程序的窗口整条命令被拒 方块就赖在最上层下不来
static void DeskLayer(FWin* w)
{
    if (!w || !w->hwnd || !IsWindow(w->hwnd)) return;
    // 显示桌面会把方块藏起来 而且不给你留恢复的机会 前头那句判断一挡就再没人管了
    // 所以头一步先叫醒 叫醒了再谈站哪
    if (IsIconic(w->hwnd) || !IsWindowVisible(w->hwnd))
    {
        WakeUp(w->hwnd);
        if (!IsWindowVisible(w->hwnd)) return;
    }
    HWND desk = DeskAnchor();
    if (!desk || !IsWindow(desk) || !IsWindowVisible(desk)) return;

    // 从桌面层往上数 一路全是方块的那一串就是桌面层 在串里就一下都不动
    // 这样两个方块挨着放也不会互相判错来回踢
    bool in = false;
    HWND h = GetWindow(desk, GW_HWNDPREV);
    wchar_t cn[64];
    while (h)
    {
        if (!GetClassNameW(h, cn, 64) || lstrcmpiW(cn, kCls) != 0) break;
        if (h == w->hwnd) in = true;
        h = GetWindow(h, GW_HWNDPREV);
    }
    if (in) return;

    // 本来就贴着桌面层 不用动
    if (GetWindow(w->hwnd, GW_HWNDNEXT) == desk) return;

    // 要站到桌面层的上面 可 SetWindowPos 只能指定"插到谁下边" 所以得拿桌面层顶上那个窗口当参照
    // 之前拿桌面层自己或者方块当参照 等于把自己塞进桌子里 平时看着还行 一按 Win+D 系统一重排就掉到桌面底下
    HWND above = GetWindow(desk, GW_HWNDPREV);
    int r = SetWindowPos(w->hwnd, above ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // 桌面层上面紧挨着的是提权程序的窗口时这句会被系统拒掉 那也总比被桌面盖住强 先顶到最上面
    if (!r) SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    // 弹菜单之前把窗口提到前台了 收完菜单马上按回桌面那一层
    DeskLayer(w);

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
        // 设置窗口是模态的 关掉之后尺寸和配色都可能变了 按新的重开一遍
        PanelSettingsOpen(w->d);
        FolderCleanTopmost();
        FolderApplyLook(w->d);
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
    DeskLayer(w);

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
    case WM_WINDOWPOSCHANGING:
    {
        // 方块是普通窗口 谁也别想把它提到置顶层去
        // 有东西想用 HWND_TOPMOST 塞它 这里改回普通层
        WINDOWPOS* p = (WINDOWPOS*)lp;
        if (!(p->flags & SWP_NOZORDER) && p->hwndInsertAfter == HWND_TOPMOST)
            p->hwndInsertAfter = HWND_NOTOPMOST;

        // 显示桌面压根不给最小化那一套 它直接把窗口藏了 藏之前会先走这一趟
        // 把隐藏标记摘掉 它要藏也藏不动 方块就一直待在桌面上 后面那次叫醒只是补个保险
        if (p->flags & SWP_HIDEWINDOW)
        {
            p->flags &= ~SWP_HIDEWINDOW;
            if (w) PostMessageW(h, WM_APP + 2, 0, 0);
        }
        break;
    }
    case WM_WINDOWPOSCHANGED:
    {
        // 点一下方块系统就会把它抬到前面 不等半秒 下一圈消息就按回桌面那一层
        // 菜单开着的时候先别折腾 不然右键菜单可能会被自己弄没了
        WINDOWPOS* p = (WINDOWPOS*)lp;
        if (!(p->flags & SWP_NOZORDER) && w && !w->menuOpen) PostMessageW(h, WM_APP + 2, 0, 0);
        break;
    }
    case WM_SYSCOMMAND:
        // 显示桌面那一下会把窗口最小化 这里直接拒掉 不然回到桌面图标就不见了
        if ((wp & 0xFFF0) == SC_MINIMIZE) return 0;
        break;
    case WM_SIZE:
        // 还是被最小化了就赶紧站起来
        if (wp == SIZE_MINIMIZED)
        {
            PostMessageW(h, WM_APP + 2, 0, 0);
            return 0;
        }
        break;
    case WM_SHOWWINDOW:
        // 被外部藏起来了 桌面图标理应一直挂着 拉回来
        if (!wp && w) PostMessageW(h, WM_APP + 2, 0, 0);
        else if (wp && w) DeskLayer(w);
        return 0;
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        w = (FWin*)cs->lpCreateParams;
        w->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);

        int cell = CellDip(w->d);
        w->pw = Px(GRID_COLS * cell + 48);
        w->ph = Px(GRID_ROWS * cell + 50 + 48);
        int ts = Px(TileDip(w->d));
        w->W = (w->pw > ts ? w->pw : ts) + Px(72);
        w->H = (w->ph > ts ? w->ph : ts) + Px(72);
        w->px = Px(36);
        w->py = Px(36);
        w->ts = ts;
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
        // 放好位置就行 层级交给 DeskLayer
        SetWindowPos(h, NULL, x, y, w->W, w->H, SWP_NOZORDER | SWP_NOACTIVATE);
        // 两百毫秒看一眼 显示桌面把它藏了也能很快回来 平时在链上就是几句判断 不怎么耗
        SetTimer(h, 2, 200, NULL);

        Render(w);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) Tick(w);
        else if (wp == 2) DeskLayer(w);
        else if (wp == 3) DragScroll(w);
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
    case WM_APP + 2:
        // 从被藏起来的状态拉回来 位置和画的内容都还在
        if (w && IsWindow(h))
        {
            WakeUp(h);
            Render(w);
            DeskLayer(w);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && w->expanded && !w->menuOpen)
            StartAnim(w, false);
        return 0;
    case WM_MOUSEACTIVATE:
        // 点进来就把焦点拿过去 不然滚轮全被上一个窗口吃掉了 面板翻不动
        return MA_ACTIVATE;
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
            if (HitClose(w, p) || HitPin(w, p)) return 0;
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
                w->itemGrabY = c.y - (w->winY + w->py + Px(50) + Px(24) + Px(w->d->items[idx].y) - w->scroll);
                SetCapture(h);
                return 0;
            }
        }
        // 钉住了就别拖了 收起来展开都一样
        if (w->d->pinned) return 0;
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
            // 手动了也没用 钉住的时候格子不动
            // 标记一下算拖过 松手就不会顺手把图标打开
            if (!w->itemDrag)
            {
                int ddx = c.x - w->itemDown.x;
                int ddy = c.y - w->itemDown.y;
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (ddx + ddy > Px(6))
                {
                    w->itemDrag = true;
                    if (!w->d->pinned) SetTimer(w->hwnd, 3, 30, NULL);
                }
            }
            if (w->itemDrag && !w->d->pinned && w->itemIdx < (int)w->d->items.size())
            {
                FolderEntry& it = w->d->items[w->itemIdx];
                int bx = c.x - w->itemGrabX - (w->winX + w->px + Px(24));
                int by = c.y - w->itemGrabY - (w->winY + w->py + Px(50) + Px(24)) + w->scroll;
                int nx = (int)(Dip(bx) + 0.5);
                int ny = (int)(Dip(by) + 0.5);
                // 左右还是只能在面板里 上下能一直往下摆 卷起来的那截也算地方
                int cell = CellDip(w->d);
                int mx = GRID_COLS * cell - cell;
                int my = MaxItemY(w);
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (ny > my) ny = my;
                if (nx > mx) nx = mx;
                if (nx != it.x || ny != it.y)
                {
                    it.x = nx;
                    it.y = ny;
                    DragFollow(w);
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
            // 拖到哪就定在哪 以后收起来回的也是这 不然后面一收一开又跳回去
            w->homeX = x;
            w->homeY = y;
            // 分层窗口的内容由 UpdateLayeredWindow 一起搬 光调 SetWindowPos 会只动壳子
            MoveWin(w, w->winX, w->winY);
            Render(w);
            return 0;
        }
        if (w->anim >= 0.99f)
        {
            int idx = HitItem(w, p);
            bool hc = HitClose(w, p);
            bool hp = HitPin(w, p);
            if (idx != w->hoverItem || hc != w->hoverClose || hp != w->hoverPin)
            {
                w->hoverItem = idx;
                w->hoverClose = hc;
                w->hoverPin = hp;
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
            KillTimer(h, 3);
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
        if (HitPin(w, p))
        {
            w->d->pinned = w->d->pinned ? 0 : 1;
            StoreSave();
            Render(w);
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
    case WM_MOUSEWHEEL:
    {
        // 面板打开着才管滚轮 收起来的时候留给桌面
        if (w->anim < 0.99f) return 0;
        int z = GET_WHEEL_DELTA_WPARAM(wp);
        int step = Px(CellDip(w->d)) / 2;
        if (step < Px(24)) step = Px(24);
        w->scroll -= (z / 120) * step;
        ClampScroll(w);
        Render(w);
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

    int cell = CellDip(d);
    int pw = Px(GRID_COLS * cell + 48);
    int ph = Px(GRID_ROWS * cell + 50 + 48);
    int ts = Px(TileDip(d));
    int W = (pw > ts ? pw : ts) + Px(72);
    int H = (ph > ts ? ph : ts) + Px(72);
    FWin* w = new FWin();
    ZeroMemory(w, sizeof(FWin));
    w->d = d;
    w->hoverItem = -1;
    w->itemIdx = -1;

    // 挂桌面子窗口那条路走不通 分层内容推不上去 还是普通窗口
    // 层级靠 DeskLayer 钉在桌面正上方 锚点是桌面自己 所以提权程序也拦不住
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kCls, L"", WS_POPUP,
        d->x - W / 2, d->y - H / 2, W, H, NULL, NULL, inst, w);
    if (!h) return;
    d->hwnd = h;
    d->extra = w;
    ShowWindow(h, SW_SHOWNOACTIVATE);
    // 刚显示出来是在普通窗口最上面 马上按回桌面正上方
    DeskLayer(w);
}

void FolderDestroy(FolderData* d)
{
    if (d->hwnd && IsWindow(d->hwnd)) DestroyWindow(d->hwnd);
    d->hwnd = NULL;
    d->extra = NULL;
}

void FolderApplyLook(FolderData* d)
{
    if (!d || !d->hwnd || !IsWindow(d->hwnd)) return;
    FWin* w = (FWin*)d->extra;
    // 尺寸改过之后窗口本身也得换大小 直接按原来的中心点重建最省事
    if (w)
    {
        d->x = w->homeX + w->W / 2;
        d->y = w->homeY + w->H / 2;
    }
    FolderDestroy(d);
    FolderShow(d);
}

void FolderReloadItems(FolderData* d)
{
    if (d->hwnd && IsWindow(d->hwnd)) PostMessageW(d->hwnd, WM_APP + 1, 0, 0);
}

void FolderCleanTopmost()
{
    // 保险起见 把所有方块的置顶标记都撸掉
    // 之前设置窗口挂成顶层窗口 系统把所有者跟着标了 桌面图标就压在所有程序上面了
    // 光清扩展样式不够 得走 SetWindowPos 才真把窗口从置顶层挪出来
    for (FolderData* f : g_folders)
    {
        if (!f->hwnd || !IsWindow(f->hwnd)) continue;
        LONG_PTR ex = GetWindowLongPtrW(f->hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TOPMOST)
            SetWindowLongPtrW(f->hwnd, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
        // 这一下只清标记 不动位置和层级
        SetWindowPos(f->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
