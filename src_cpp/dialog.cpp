#include "common.h"

using namespace Gdiplus;

static HWND g_dlg;
static HWND g_edit;
static WNDPROC g_editOld;
static bool g_ok;
static wstring g_text;
static wstring g_title;
static Font* g_fTitle;
static Font* g_fBtn;
static RECT g_rOk, g_rCancel;
static HBRUSH g_editBrush;
static bool g_wasActive;

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

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN)
    {
        if (wp == VK_RETURN) { PostMessageW(GetParent(h), WM_COMMAND, 1, 0); return 0; }
        if (wp == VK_ESCAPE) { PostMessageW(GetParent(h), WM_COMMAND, 2, 0); return 0; }
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_editOld, h, m, wp, lp);
}

static void Finish(bool ok)
{
    if (ok)
    {
        wchar_t b[512];
        GetWindowTextW(g_edit, b, 512);
        g_text = b;
        g_ok = true;
    }
    DestroyWindow(g_dlg);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
    {
        int w = Px(380), ht = Px(190);
        g_fTitle = new Font(L"Microsoft YaHei UI", (REAL)Px(15), FontStyleBold, UnitPixel);
        g_fBtn = new Font(L"Microsoft YaHei UI", (REAL)Px(13), FontStyleRegular, UnitPixel);

        int pad = Px(22);
        int boxY = pad + Px(34);
        g_rOk.right = w - pad;
        g_rOk.left = w - pad - Px(78);
        g_rOk.top = ht - pad - Px(34);
        g_rOk.bottom = g_rOk.top + Px(34);
        g_rCancel.right = g_rOk.left - Px(10);
        g_rCancel.left = g_rCancel.right - Px(78);
        g_rCancel.top = g_rOk.top;
        g_rCancel.bottom = g_rOk.bottom;

        g_edit = CreateWindowExW(0, L"EDIT", g_text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, pad + Px(12), boxY + Px(11),
            w - pad * 2 - Px(24), Px(22), h, (HMENU)100, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)CreateFontW(-Px(15), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"), TRUE);
        g_editOld = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SendMessageW(g_edit, EM_SETSEL, 0, -1);
        g_editBrush = CreateSolidBrush(RGB(41, 42, 48));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(240, 240, 243));
        SetBkColor(dc, RGB(41, 42, 48));
        return (LRESULT)g_editBrush;
    }
    case WM_ACTIVATE:
        // 有时候刚弹出来拿不到焦点 别一进来就被自己关掉
        if (LOWORD(wp) == WA_ACTIVE) g_wasActive = true;
        else if (LOWORD(wp) == WA_INACTIVE && g_wasActive) Finish(false);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) Finish(true);
        else if (LOWORD(wp) == 2) Finish(false);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT p;
        p.x = GET_X_LPARAM(lp);
        p.y = GET_Y_LPARAM(lp);
        if (PtInRect(&g_rOk, p)) Finish(true);
        else if (PtInRect(&g_rCancel, p)) Finish(false);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        RECT rc;
        GetClientRect(h, &rc);
        RectF full(0, 0, (REAL)rc.right, (REAL)rc.bottom);

        GraphicsPath bg;
        RoundPath(bg, full, (REAL)Px(16));
        SolidBrush brush(Color(245, 26, 27, 32));
        g.FillPath(&brush, &bg);

        int pad = Px(22);
        RectF box((REAL)pad, (REAL)(pad + Px(34)), (REAL)(rc.right - pad * 2), (REAL)Px(44));
        GraphicsPath bp;
        RoundPath(bp, box, (REAL)Px(10));
        SolidBrush b2(Color(255, 41, 42, 48));
        g.FillPath(&b2, &bp);
        Pen pen(Color(60, 255, 255, 255), 1.0f);
        g.DrawPath(&pen, &bp);

        SolidBrush white(Color(240, 240, 243));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentNear);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF tr((REAL)pad, (REAL)pad, (REAL)(rc.right - pad * 2), (REAL)Px(24));
        g.DrawString(g_title.c_str(), -1, g_fTitle, tr, &fmt, &white);

        // 按钮
        auto btn = [&](RECT r, const wchar_t* txt, bool primary)
        {
            RectF bf((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
            GraphicsPath p;
            RoundPath(p, bf, (REAL)Px(9));
            SolidBrush f(primary ? Color(255, 76, 110, 245) : Color(255, 52, 53, 60));
            g.FillPath(&f, &p);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush tx(primary ? Color(255, 255, 255, 255) : Color(235, 230, 232, 240));
            g.DrawString(txt, -1, g_fBtn, bf, &sf, &tx);
        };
        btn(g_rCancel, L"取消", false);
        btn(g_rOk, L"确定", true);

        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        delete g_fTitle; g_fTitle = NULL;
        delete g_fBtn; g_fBtn = NULL;
        if (g_editBrush) { DeleteObject(g_editBrush); g_editBrush = NULL; }
        g_dlg = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool NameInput(HWND owner, const wstring& title, const wstring& initial, wstring& out)
{
    static bool reg = false;
    HINSTANCE inst = GetModuleHandleW(NULL);
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
        wc.lpszClassName = L"DeskFolderNameBox";
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = Px(380), h = Px(190);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top) / 3 - h / 2;

    g_text = initial;
    g_title = title;
    g_ok = false;
    g_wasActive = false;
    g_dlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"DeskFolderNameBox", title.c_str(),
        WS_POPUP, x, y, w, h, owner, NULL, inst, NULL);
    if (!g_dlg) return false;

    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, Px(30), Px(30));
    SetWindowRgn(g_dlg, rgn, TRUE);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(g_dlg, SW_SHOW);
    SetForegroundWindow(g_dlg);
    SetFocus(g_edit);

    MSG msg;
    while (IsWindow(g_dlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (IsDialogMessageW(g_dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) EnableWindow(owner, TRUE);
    out = g_text;
    return g_ok;
}
