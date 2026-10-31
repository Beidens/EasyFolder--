#include "common.h"
#include "version.h"

float g_scale = 1.0f;
float g_fit = 1.0f;

static HINSTANCE g_inst;
static HWND g_msgWnd;
static NOTIFYICONDATAW g_nid;
static ULONG_PTR g_gdip;

static const UINT WM_TRAY = WM_APP + 2;
static const wchar_t* kMsgCls = L"DeskFolderMsg";

static void DoCreate(const wstring& name);

// 开机自启动 写 HKCU 的 Run 表 状态另存一个小文件 免得反复读注册表
static bool g_autoStart;

static void AutoStartApply(bool on)
{
    HKEY k = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS)
    {
        if (on)
        {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(NULL, exe, MAX_PATH);
            wstring cmd = L"\"" + wstring(exe) + L"\"";
            RegSetValueExW(k, L"DeskFolder", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        }
        else RegDeleteValueW(k, L"DeskFolder");
        RegCloseKey(k);
    }
    g_autoStart = on;

    wstring f = g_dataDir + L"\\autostart.txt";
    HANDLE hf = CreateFileW(f.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE)
    {
        const char* s = on ? "1" : "0";
        DWORD n2 = 0;
        WriteFile(hf, s, 1, &n2, NULL);
        CloseHandle(hf);
    }
}

static void AutoStartInit()
{
    wstring f = g_dataDir + L"\\autostart.txt";
    HANDLE hf = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE)
    {
        // 头一次跑 默认就给它打开
        AutoStartApply(true);
        return;
    }
    char c = 0;
    DWORD n2 = 0;
    ReadFile(hf, &c, 1, &n2, NULL);
    CloseHandle(hf);
    g_autoStart = (c == '1');
    // 注册表里的值有可能被别的东西清掉 每次起来顺手对齐
    AutoStartApply(g_autoStart);
}

static void InitDpi()
{
    typedef BOOL(WINAPI * PFN_CTX)(void*);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN_CTX f = (PFN_CTX)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (f) f((void*)-4);   // 每显示器 v2
    else SetProcessDPIAware();

    HDC dc = GetDC(NULL);
    int px = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    g_scale = px / 96.0f;
    if (g_scale < 1.0f) g_scale = 1.0f;

    // 屏幕装不下整个面板就整体缩一点 小屏本子也不至于溢出
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    float wDip = (wa.right - wa.left) / g_scale;
    float hDip = (wa.bottom - wa.top) / g_scale;
    float fw = wDip * 0.86f / 710.0f;
    float fh = hDip * 0.86f / 524.0f;
    g_fit = fw < fh ? fw : fh;
    if (g_fit > 1.0f) g_fit = 1.0f;
    if (g_fit < 0.6f) g_fit = 0.6f;
}

static void ShowAll()
{
    for (FolderData* f : g_folders) FolderShow(f);
}

static void BuildTray()
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_msgWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(101));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"桌面图标文件夹 v" APP_VER_STR, 64);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayMenu()
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"新建图标文件夹");
    AppendMenuW(m, MF_STRING, 2, L"显示全部");

    // 设置挂二级菜单 有几个文件夹就列几个
    HMENU sub = CreatePopupMenu();
    int n = (int)g_folders.size();
    if (n > 0)
    {
        for (int i = 0; i < n; i++)
        {
            wstring t = g_folders[i]->name;
            if (t.empty()) t = L"未命名";
            AppendMenuW(sub, MF_STRING, 1000 + i, t.c_str());
        }
    }
    else AppendMenuW(sub, MF_GRAYED | MF_STRING, 0, L"还没有文件夹");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"设置");
    AppendMenuW(m, MF_STRING | (g_autoStart ? MF_CHECKED : MF_UNCHECKED), 5, L"开机自启动");

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 3, L"退出");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g_msgWnd);
    int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, g_msgWnd, NULL);
    DestroyMenu(m);
    if (cmd == 1) DoCreate(L"");
    else if (cmd == 2) ShowAll();
    else if (cmd == 3) DestroyWindow(g_msgWnd);
    else if (cmd == 5) AutoStartApply(!g_autoStart);
    else if (cmd >= 1000 && cmd - 1000 < (int)g_folders.size())
    {
        FolderData* d = g_folders[cmd - 1000];
        FolderShow(d);
        PanelSettingsOpen(d);
    }
}

static void RegisterShell()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);

    HKEY k = NULL;
    RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\DesktopBackground\\Shell\\DeskFolderNew",
        0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
    if (k)
    {
        const wchar_t* title = L"新建图标文件夹";
        RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE*)title, (DWORD)((wcslen(title) + 1) * sizeof(wchar_t)));
        const wchar_t* pos = L"Top";
        RegSetValueExW(k, L"Position", 0, REG_SZ, (const BYTE*)pos, (DWORD)((wcslen(pos) + 1) * sizeof(wchar_t)));
        wstring icon = L"\"" + wstring(exe) + L"\",0";
        RegSetValueExW(k, L"Icon", 0, REG_SZ, (const BYTE*)icon.c_str(), (DWORD)((icon.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }

    HKEY c = NULL;
    RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\DesktopBackground\\Shell\\DeskFolderNew\\command",
        0, NULL, 0, KEY_WRITE, NULL, &c, NULL);
    if (c)
    {
        wstring cmd = L"\"" + wstring(exe) + L"\" --create";
        RegSetValueExW(c, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(c);
    }
}

static void DoCreate(const wstring& name)
{
    wstring n = name;
    if (n.empty())
    {
        if (!NameInput(NULL, L"新建文件夹", L"新建文件夹", n)) return;
    }
    if (n.empty()) return;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int idx = (int)g_folders.size();
    int x = wa.right - Px(170) - (idx % 3) * Px(150);
    int y = wa.top + Px(200) + ((idx / 3) % 4) * Px(160);

    FolderData* d = StoreCreate(n, x, y);
    FolderShow(d);
}

static LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_TRAY:
        if (wp == 1)
        {
            if (LOWORD(lp) == WM_RBUTTONUP) TrayMenu();
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) ShowAll();
        }
        return 0;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lp;
        if (!cds || !cds->lpData) return 0;
        size_t n = cds->cbData / sizeof(wchar_t);
        if (n) n--;
        wstring s((const wchar_t*)cds->lpData, n);
        if (s.compare(0, 7, L"create|") == 0) DoCreate(s.substr(7));
        else ShowAll();
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR args, int show)
{
    (void)prev;
    (void)args;
    (void)show;
    g_inst = inst;
    srand((unsigned)GetTickCount64());

    InitDpi();
    // 拖放要走 OLE 所以用 OleInitialize 取代 CoInitializeEx
    OleInitialize(NULL);

    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&g_gdip, &si, NULL);

    g_dataDir = AppDataDir();
    StoreLoad();
    AutoStartInit();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool create = false;
    wstring name;
    for (int i = 1; i < argc; i++)
    {
        if (!lstrcmpiW(argv[i], L"--create"))
        {
            create = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') name = argv[++i];
        }
    }
    LocalFree(argv);

    HWND other = FindWindowW(kMsgCls, NULL);
    if (other)
    {
        wstring s = L"show";
        if (create) s = L"create|" + name;
        COPYDATASTRUCT cds;
        cds.dwData = 0;
        cds.cbData = (DWORD)((s.size() + 1) * sizeof(wchar_t));
        cds.lpData = (void*)s.c_str();
        SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
        Gdiplus::GdiplusShutdown(g_gdip);
        OleUninitialize();
        return 0;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMsgCls;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);

    // 这个窗只是用来接单实例消息 不能做成 message only 不然别的进程 FindWindow 找不到
    g_msgWnd = CreateWindowExW(0, kMsgCls, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);

    RegisterShell();
    BuildTray();
    ShowAll();

    if (create) DoCreate(name);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) TranslateMessage(&msg), DispatchMessageW(&msg);

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    IconsClear();
    for (FolderData* f : g_folders) delete f;
    g_folders.clear();
    OleUninitialize();
    Gdiplus::GdiplusShutdown(g_gdip);
    return 0;
}
