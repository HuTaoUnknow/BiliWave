# BiliWave Windows 安装包

完整版安装包包含 Qt 运行库、FFmpeg、yt-dlp、独立 Python、OCR 和已有歌词对齐模型（不含听写模型）。安装过程中不下载依赖。默认仅为当前用户安装到 `%LOCALAPPDATA%\Programs\BiliWave`，提供中文向导、桌面与开始菜单快捷方式和卸载入口。

无歌词配置的 Lite 版使用独立的 `build-lite/`、`release-lite/`，不包含两个模型、Python、OCR、Whisper 和歌词 worker。下载后直接入库，保留搜索试听、URL 收录、乐库、歌单、播放及顶部进度条；默认安装到 `%LOCALAPPDATA%\Programs\BiliWave Lite`。

```powershell
.\scripts\build-lite.ps1
.\scripts\package-installer.ps1 -Lite
.\scripts\verify-lite-installer.ps1
```

打包来源为 `release/`；排除个人 `data/`、开发用静态/导入库、调试符号及 Python 字节码缓存。第三方许可证与运行需要的 DLL、模型保留。应用数据在安装目录的 `data/`，更新不会写入此目录，卸载不会主动删除用户生成的内容。

准备 Inno Setup 6.7.3 后执行：

```powershell
.\scripts\package-installer.ps1 -Compiler 'C:\path\to\ISCC.exe'
.\scripts\verify-installer.ps1
```

安装包输出在 `dist/`，同时生成 SHA-256 文件和编译日志。添加 `-CopyToDesktop` 可复制安装包及校验文件到桌面。

验证参数 `/VERIFYINSTALL=1` 仅用于构建验收：须指定独立安装目录，不写实际卸载注册项、桌面或开始菜单快捷方式。仍会生成卸载器并测试保留用户数据。正常双击安装时提供完整安装与卸载集成。

编译器来自 [官方发布](https://github.com/jrsoftware/issrc/releases/tag/is-6_7_3)，数字签名为 Pyrsys B.V.，下载 SHA-256：`9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732`。

简体中文语言文件通过 [官方翻译页](https://jrsoftware.org/files/istrans/) 获取，来源为仓库 `Files/Languages/ChineseSimplified.isl`；原贡献者信息保留在文件头部。
