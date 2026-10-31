# 使用 .NET Framework 自带的 csc 编译器编译（无需安装 SDK / Visual Studio）
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "src"
$outDir = Join-Path $root "dist"
$fw = Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319"
$gac = Join-Path $env:WINDIR "Microsoft.NET\assembly"

if (-not (Test-Path (Join-Path $fw "csc.exe"))) {
    throw "找不到 .NET Framework 4 编译器: $fw\csc.exe"
}

function Resolve-Assembly([string]$name) {
    $patterns = @(
        (Join-Path $gac "GAC_MSIL\$name\*\*.dll"),
        (Join-Path $gac "GAC_64\$name\*\*.dll"),
        (Join-Path $fw "$name.dll")
    )
    foreach ($p in $patterns) {
        $f = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($f) { return $f.FullName }
    }
    throw "找不到程序集 $name"
}

$names = @("WindowsBase", "PresentationCore", "PresentationFramework", "System.Xaml")
$refs = $names | ForEach-Object { "/r:`"$(Resolve-Assembly $_)`"" }
$sources = Get-ChildItem -Path $src -Filter *.cs | ForEach-Object { $_.FullName }

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$exe = Join-Path $outDir "DeskFolder.exe"
Write-Host "正在编译 -> $exe"

& "$fw\csc.exe" /nologo /target:winexe /platform:anycpu /optimize+ /out:"$exe" $refs $sources
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

Write-Host "编译完成: $exe"
