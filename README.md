# Bilingual Rime Input Method for macOS

一个面向 macOS 的双语 Rime 输入法实验项目：输入拼音，候选框同时展示中文候选和对应的英文释义；确认中文或英文由主操作与 `Option` 修饰键明确区分。

> 当前仓库是可继续开发的源码快照，项目仍处于实验阶段。它适合希望在 macOS 上研究 Fcitx5、Rime、候选框 UI 和本地翻译体验的开发者。

## 体验目标

```text
输入 nihao

┌─────────────────────────────────────────────┐
│ 1 你好          2 您好          3 你好吗      │
│   hello           hi              how are you │
└─────────────────────────────────────────────┘
```

中文和英文是同一候选项的两行信息，而不是互相挤压的附加文本。横向候选框会根据内容自适应宽度；英文暂时没有结果时保留空白行，避免候选项跳动。

## 主要功能

- 拼音输入，使用 Rime 生成中文候选。
- 候选项支持“中文主文本 + 英文第二行”。
- 支持横向和纵向候选框布局。
- 支持只显示中文、只显示英文或中英双语。
- 支持 `Space` 选择中文，按住 `Option` 选择同一候选项的英文。
- 支持 `Option+1` 到 `Option+5` 直接提交对应英文候选。
- 支持 `Ctrl+Shift+E` 永久切换英文释义显示。
- 翻译在后台线程执行，并使用缓存，避免阻塞正常输入。
- 可按天记录已提交的中文输入，作为后续英语学习的原始语料。
- 优先复用 Rime 候选注释中的本地词典释义；短语和句子可使用 macOS Translation Framework。
- 保留可选的在线 HTTP 翻译接口，但默认不联网、不要求账号或 API Key。

## 快捷键

| 操作 | 默认按键 | 作用 |
| --- | --- | --- |
| 选择中文 | `Space` | 提交当前高亮候选的中文 |
| 选择英文 | `Option+Space` | 提交当前高亮候选的英文 |
| 选择第 1–5 个中文候选 | `1`–`5` | 提交对应中文候选 |
| 选择第 1–5 个英文候选 | `Option+1`–`Option+5` | 提交对应英文释义 |
| 切换英文显示 | `Ctrl+Shift+E` | 永久切换英文第二行是否显示 |
| 移动候选 | 方向键 | 在候选列表中移动高亮项 |

具体按键仍会受到当前 Rime schema 和 macOS 应用快捷键的影响；如果某个应用拦截了 `Option` 或方向键，可以在 Rime/Fcitx5 配置中调整。

## 翻译方案

翻译链路按以下优先级设计：

1. Rime schema 提供的候选注释，例如接入本地 CC-CEDICT 后的词语释义。
2. macOS Translation Framework，通过随输入法安装的 `Fcitx5TranslationHelper` 处理短语和句子。
3. 可选在线 HTTP provider，仅当显式设置 `RIME_TRANSLATOR_PROVIDER=online` 时启用。

在线 provider 使用以下环境变量：

```sh
export RIME_TRANSLATOR_PROVIDER=online
export RIME_ONLINE_TRANSLATION_URL="https://example.com/translate"
export RIME_ONLINE_TRANSLATION_TOKEN="your-token"
```

服务端返回 JSON 时支持 `translation`、`translatedText`，或 `data.translation` 字段。不要把 token 写进源码、配置文件或 Git 提交。

## 每日中文学习记录

输入法不负责猜测停顿、识别句子边界或调用大模型。它只在中文内容真正提交后，异步追加到本地每天一个的 JSON 文件中：

```text
<Fcitx5 用户数据目录>/rime/learning/2026-09-07.json
```

文件格式：

```json
{
  "date": "2026-09-07",
  "text": "你好\n我今天想吃苹果\n这个方案还需要继续优化"
}
```

`text` 保存当天所有已提交中文；每次提交之间用换行区分，但这个换行不是句子识别结果。后续 Web 学习项目可以自行组合语料，再使用大模型完成分句、整句翻译以及单词和短语抽取。

记录默认开启，可在 Fcitx5 的 Rime 配置中关闭：

```ini
LearningLogEnabled=False
```

记录器运行在后台线程，文件采用临时文件写入后原子替换，并设置为仅当前用户可读写，避免记录过程影响输入或留下半个 JSON 文件。

## 构建

### 环境要求

- macOS 13.3 或更新版本
- Xcode Command Line Tools
- CMake、Ninja 和 Swift
- Fcitx5/Rime 构建所需的 cURL、`nlohmann_json`、Rime 开发文件

先阅读 [Fcitx5 macOS 构建说明](source/fcitx5-macos/README.zh-CN.md)，准备好系统依赖后，在仓库根目录执行：

```sh
cmake -S source/fcitx5-macos -B build/arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_CUSTOM_RIME=ON \
  -DCUSTOM_RIME_SOURCE_DIR="$PWD/source/fcitx5-rime"

cmake --build build/arm64
```

生成的安装目标由 Fcitx5 macOS 的标准安装流程处理。系统安装输入法需要管理员权限，建议先在独立测试用户或虚拟机中验证。

## 项目结构

```text
.
├── README.md
├── docs/
│   └── upstream-sources.md
└── source/
    ├── fcitx5-macos/       # macOS 前端、Fcitx5 Core、WebView
    └── fcitx5-rime/        # Rime 插件与双语翻译逻辑
```

关键定制点：

- `source/fcitx5-macos/src/translation-helper.swift`：macOS 本地翻译辅助进程。
- `source/fcitx5-macos/webpanel/`：候选框配置和候选显示模式。
- `source/fcitx5-macos/fcitx5-webview/page/`：候选框布局、字体、颜色和双语第二行。
- `source/fcitx5-rime/src/rimecandidate.cpp`：候选中文与英文释义绑定。
- `source/fcitx5-rime/src/rimestate.cpp`：中文/英文确认逻辑和快捷键处理。
- `source/fcitx5-rime/src/translation.cpp`：本地翻译辅助、缓存和可选在线 provider。

## 隐私与数据

- 默认翻译 provider 是 macOS 本地 Translation Framework。
- 默认不会调用在线翻译服务。
- 翻译结果只用于当前输入法的候选显示和本地缓存。
- 每日中文学习记录只保存在本机，默认不上传到任何服务。
- 本仓库不包含个人 Rime 用户词库、系统输入法配置、访问令牌、安装后的 App、构建目录或依赖缓存。

## 项目状态

当前版本重点验证：

- 中英双行候选框的布局一致性。
- 中文和英文的独立确认路径。
- Apple Translation Framework 的后台翻译和缓存。
- 横向/纵向候选框及中英显示模式配置。

仍需持续完善：

- 更多 Rime schema 和词典的兼容性。
- 不同 macOS 版本、显示缩放和应用输入框的兼容性。
- 更完整的自动化测试、打包和签名流程。

## 上游项目与许可证

本项目基于 Fcitx5、Rime 及其 macOS/WebView 组件。构建所需的上游源码以源码快照形式保存在仓库中，原始许可证和版权信息保留在对应目录。详细来源见 [docs/upstream-sources.md](docs/upstream-sources.md)。

请在分发二进制版本前逐项检查上游许可证、系统框架要求和代码签名要求。
