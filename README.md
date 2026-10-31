# EasyFolder--

Windows 桌面图标文件夹

桌面上放一个方块 点开就是自己收的几个快捷方式 不用把桌面铺满

## 它做什么

- 桌面上的方块就是一个文件夹 记在程序自己的配置文件里 不会动你真实的目录
- 点一下展开 变成一格一格的快捷方式 双击就能打开
- 快捷方式能拖着换位置 方块本身也能拖到桌面任意一处
- 方块贴着桌面那一层显示 打开的程序窗口照样能把它盖住 回到桌面它还在原位
- 拖文件进面板可以直接添加 从资源管理器里拖也行

## 设置

托盘图标右键 设置 二级菜单里选某个文件夹

- 改名字
- 桌面方块换成自己的图片
- 面板填充色和描边色 色轮拖动 也能直接填 RGB 和透明度 带几个撞色预设
- 面板描边粗细
- 自定义图片那层描边单独调
- 桌面方块大小 展开后的图标大小

托盘右键还有开机自启动 第一次运行默认开着

## 运行

直接跑 Release 里的 exe 就行 单文件 绿色 配置存到 `%APPDATA%\DeskFolder`

## 自己编译

需要 MinGW-w64 工具链 放在 `tools\w64devkit\w64devkit` 或者 `tools\winlibs\mingw64`

```
powershell -ExecutionPolicy Bypass -File build_cpp.ps1
```

产物在 `dist\DeskFolder-v<版本>.exe`

## 更新记录

看 [CHANGELOG.md](CHANGELOG.md)
