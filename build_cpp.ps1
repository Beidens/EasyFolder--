$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $root) { $root = (Get-Location).Path }

# 读一下版本号 编译产物名和 exe 属性都用它
$vh = Get-Content (Join-Path $root 'src_cpp\version.h') -Raw
$ver = '0.0.1'
if ($vh -match 'APP_VER_STR\s+L"([^"]+)"') { $ver = $Matches[1] }

# 工具链放在中文目录下 ld 打不开自己的库 所以整个复制一份到纯英文的临时目录
# 之前用的 winlibs 那份 ld 被 Defender 判成可疑文件 起不来 就退到 w64devkit
$cands = @(
    @{ Src = (Join-Path $root 'tools\w64devkit\w64devkit'); Dst = (Join-Path $env:TEMP 'dfw64') },
    @{ Src = (Join-Path $root 'tools\winlibs\mingw64'); Dst = (Join-Path $env:TEMP 'dfmingw2') }
)
$gpp = $null
$windres = $null
foreach ($c in $cands) {
    if (-not (Test-Path (Join-Path $c.Dst 'bin\g++.exe'))) {
        if (Test-Path $c.Src) { robocopy "$($c.Src)" "$($c.Dst)" /E /NFL /NDL /NJH /NJS /MT:16 | Out-Null }
    }
    $tld = Join-Path $c.Dst 'bin\ld.exe'
    if (-not (Test-Path $tld)) { continue }
    $probe = & $tld --version 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -and $probe -match 'GNU ld') {
        $gpp = Join-Path $c.Dst 'bin\g++.exe'
        $windres = Join-Path $c.Dst 'bin\windres.exe'
        break
    }
}
if (-not $gpp) { throw 'no usable toolchain' }

$dist = Join-Path $root 'dist'
if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }
$out = Join-Path $dist ("DeskFolder-v{0}.exe" -f $ver)
$bak = Join-Path $dist 'DeskFolder.wpf.bak.exe'
$plain = Join-Path $dist 'DeskFolder.exe'
if ((Test-Path $plain) -and -not (Test-Path $bak)) { Copy-Item $plain $bak }

# 版本信息资源 源文件在中文目录下 所以先切进去用相对路径喂给 windres
$res = Join-Path $env:TEMP 'deskfolder.res'
if (Test-Path $res) { Remove-Item $res -Force }
if (Test-Path $windres) {
    Push-Location (Join-Path $root 'src_cpp')
    & $windres --codepage=65001 -O coff -o $res 'app.rc'
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0) { throw "resource failed: $rc" }
}

$tmp = Join-Path $env:TEMP 'deskfolder_build.exe'
if (Test-Path $tmp) { Remove-Item $tmp -Force }

$src = Get-ChildItem (Join-Path $root 'src_cpp') -Filter *.cpp | ForEach-Object { $_.FullName }
if ($src.Count -eq 0) { throw 'no cpp files' }

$gargs = @('-std=c++17', '-O2', '-municode', '-mwindows', '-Wall', '-Wextra', '-o', $tmp)
$gargs += $src
if (Test-Path $res) { $gargs += $res }
$gargs += @('-lgdiplus', '-lole32', '-luuid', '-lshlwapi', '-lcomctl32', '-ladvapi32', '-lcomdlg32', '-luser32', '-lgdi32', '-lshell32', '-static')

& $gpp @gargs
$code = $LASTEXITCODE

if ($code -ne 0) { throw "compile failed: $code" }
if (-not (Test-Path $tmp)) { throw 'output missing' }
Copy-Item $tmp $out -Force
Remove-Item $tmp -Force
if (Test-Path $plain) { Remove-Item $plain -Force -ErrorAction SilentlyContinue }
Write-Host "ok -> $out" -ForegroundColor Green
