# Upstream sources

本仓库将构建所需的上游源码以内嵌源码快照形式保存，便于复现当前版本的定制输入法。上游许可证文件仍保留在对应目录中。

- macOS 前端：`fcitx/fcitx5-macos`
- Fcitx5 Core：`fcitx/fcitx5`
- Fcitx5 WebView：`fcitx-contrib/fcitx5-webview`
- Rime 插件基础：`fcitx/fcitx5-rime`

定制代码主要集中在：

- `source/fcitx5-macos/CMakeLists.txt`
- `source/fcitx5-macos/src/CMakeLists.txt`
- `source/fcitx5-macos/src/translation-helper.swift`
- `source/fcitx5-macos/webpanel/webpanel.h`
- `source/fcitx5-macos/fcitx5-webview/page/`
- `source/fcitx5-rime/src/`

