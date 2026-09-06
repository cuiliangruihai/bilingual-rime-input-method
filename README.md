# Bilingual Rime Input Method for macOS

这是一个面向 macOS 的双语 Rime 输入法源码快照。输入拼音后，候选词可以同时显示中文和英文释义；中文输出、英文输出、候选框布局和英文显示均由输入法快捷键及配置控制。

## 当前包含

- `source/fcitx5-macos/`：macOS Fcitx5 前端，以及本次定制使用的 Fcitx5 Core、WebView 源码。
- `source/fcitx5-rime/`：Rime 插件定制，包括双语候选、英文选择、翻译缓存和 Apple Translation Framework 辅助进程接入。

这是一个可离线保存和继续开发的源码快照。上游依赖的许可证和原始版权信息保留在各源码目录中。

## 构建

需要 macOS、Xcode Command Line Tools、CMake、Ninja、Swift，以及 Fcitx5/Rime 构建所需的系统依赖。

在仓库根目录执行：

```sh
cmake -S source/fcitx5-macos -B build/arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_CUSTOM_RIME=ON \
  -DCUSTOM_RIME_SOURCE_DIR="$PWD/source/fcitx5-rime"

cmake --build build/arm64
```

如果系统尚未提供 Rime、cURL 或 `nlohmann_json` 的开发文件，请先按 Fcitx5 macOS 项目的依赖说明安装它们。

## 设计要点

- 候选项支持中文主文本和英文第二行。
- `Space` 选择中文；按住 `Option` 时可选择对应英文。
- `Ctrl+Shift+E` 切换英文释义的永久显示开关。
- 短词优先使用本地 CC-CEDICT；句子可通过 Apple Translation Framework 辅助进程翻译。
- 翻译结果使用缓存，离线或无结果时不阻塞中文输入。

## 注意事项

仓库只包含源码和构建所需的静态资源，不包含本机输入法配置、用户词库、访问令牌、安装后的 `Fcitx5.app`、构建目录或缓存。在线翻译接口如果启用，应通过本机环境变量或独立配置提供凭据，不要把凭据提交到 Git。

