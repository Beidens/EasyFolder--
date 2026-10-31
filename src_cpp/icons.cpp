#include "common.h"
#include <cstring>
#include <shlwapi.h>

static std::map<wstring, Gdiplus::Bitmap*> g_cache;

// 把 shell 给的 HBITMAP 转成 GDI+ 的预乘位图 顺手处理 alpha 顺序问题
static Gdiplus::Bitmap* FromHBitmap(HBITMAP hbm)
{
    if (!hbm) return NULL;
    BITMAP bm;
    if (!GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
    {
        DeleteObject(hbm);
        return NULL;
    }
    int w = bm.bmWidth, h = bm.bmHeight;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> buf((size_t)w * (size_t)h * 4, 0);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, hbm);
    GetDIBits(dc, hbm, 0, h, buf.data(), &bi, DIB_RGB_COLORS);
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(hbm);

    // 先看一眼是直通 alpha 还是已经预乘过 免得整体压暗
    bool straight = false;
    for (size_t i = 0; i + 3 < buf.size(); i += 4)
    {
        unsigned a = buf[i + 3];
        if (buf[i] > a || buf[i + 1] > a || buf[i + 2] > a)
        {
            straight = true;
            break;
        }
    }

    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    if (!bmp) return NULL;
    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, w, h);
    if (bmp->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
    {
        delete bmp;
        return NULL;
    }
    for (int y = 0; y < h; y++)
    {
        unsigned char* s = buf.data() + (size_t)y * w * 4;
        unsigned char* d = (unsigned char*)bd.Scan0 + (size_t)y * bd.Stride;
        if (straight)
        {
            for (int x = 0; x < w; x++)
            {
                unsigned a = s[x * 4 + 3];
                d[x * 4 + 0] = (unsigned char)((s[x * 4 + 0] * a + 127) / 255);
                d[x * 4 + 1] = (unsigned char)((s[x * 4 + 1] * a + 127) / 255);
                d[x * 4 + 2] = (unsigned char)((s[x * 4 + 2] * a + 127) / 255);
                d[x * 4 + 3] = (unsigned char)a;
            }
        }
        else memcpy(d, s, (size_t)w * 4);
    }
    bmp->UnlockBits(&bd);
    return bmp;
}

static Gdiplus::Bitmap* FromIcon(HICON ico, int size)
{
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(size, size, PixelFormat32bppPARGB);
    if (!bmp) return NULL;
    Gdiplus::Graphics g(bmp);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    HDC dc = g.GetHDC();
    DrawIconEx(dc, 0, 0, ico, size, size, 0, NULL, DI_NORMAL);
    g.ReleaseHDC(dc);
    return bmp;
}

static Gdiplus::Bitmap* Load(const wstring& path, int size)
{
    Gdiplus::Bitmap* bmp = NULL;

    IShellItemImageFactory* fac = NULL;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), NULL, IID_IShellItemImageFactory, (void**)&fac)) && fac)
    {
        SIZE sz;
        sz.cx = size;
        sz.cy = size;
        HBITMAP hbm = NULL;
        if (SUCCEEDED(fac->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT, &hbm)))
            bmp = FromHBitmap(hbm);
        fac->Release();
    }

    if (!bmp)
    {
        SHFILEINFOW sfi;
        ZeroMemory(&sfi, sizeof(sfi));
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
        {
            bmp = FromIcon(sfi.hIcon, size);
            DestroyIcon(sfi.hIcon);
        }
    }
    return bmp;
}

Gdiplus::Bitmap* IconGet(const wstring& path, int size)
{
    wchar_t k[32];
    wsprintfW(k, L"|%d", size);
    wstring key = path + k;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;

    Gdiplus::Bitmap* bmp = Load(path, size);
    g_cache[key] = bmp;
    return bmp;
}

void IconsClear()
{
    for (auto& kv : g_cache) delete kv.second;
    g_cache.clear();
}

void LaunchPath(const wstring& path)
{
    if (path.empty()) return;

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return;

    // 第一种走默认谓词 失败就显式 open 一次 不再弹错误框 免得点一下卡半天
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return;

    // 快捷方式还打不开就把目标取出来自己起进程
    if (path.size() > 4)
    {
        wstring ext = path.substr(path.size() - 4);
        if (ext == L".lnk" || ext == L".LNK")
        {
            IShellLinkW* lnk = NULL;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&lnk)))
            {
                IPersistFile* pf = NULL;
                if (SUCCEEDED(lnk->QueryInterface(IID_IPersistFile, (void**)&pf)))
                {
                    if (SUCCEEDED(pf->Load(path.c_str(), STGM_READ)))
                    {
                        wchar_t target[MAX_PATH * 4];
                        wchar_t args[MAX_PATH * 2];
                        target[0] = 0;
                        args[0] = 0;
                        lnk->GetPath(target, MAX_PATH * 4, NULL, SLGP_RAWPATH);
                        lnk->GetArguments(args, MAX_PATH * 2);
                        if (target[0])
                        {
                            wstring cmd = L"\"" + wstring(target) + L"\"";
                            if (args[0]) cmd += L" " + wstring(args);
                            vector<wchar_t> buf(cmd.begin(), cmd.end());
                            buf.push_back(0);
                            STARTUPINFOW si;
                            PROCESS_INFORMATION pi;
                            ZeroMemory(&si, sizeof(si));
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_SHOWNORMAL;
                            ZeroMemory(&pi, sizeof(pi));
                            if (CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                            {
                                CloseHandle(pi.hThread);
                                CloseHandle(pi.hProcess);
                            }
                        }
                    }
                    pf->Release();
                }
                lnk->Release();
            }
        }
    }
}
