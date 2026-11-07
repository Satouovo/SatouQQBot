# Satou QQBot

Windows 原生 QQ 群管理工具，使用 C++20 和 Win32 编写，通过 OneBot v11 正向 WebSocket 连接 NapCat。自带桌面控制窗口，连接配置和群管规则均保存在本机。

## 功能

- 禁言、解除禁言、全体禁言、踢人、拉黑、撤回消息和修改群名片。
- 入群欢迎、退群通知、违禁词处理和刷屏检测。
- 自动入群审批，支持黑名单、拒绝词、同意词及未命中时的处理规则。
- 超级用户和群内角色权限检查，避免对权限不低于操作者或机器人的成员执行处罚。
- 断线后每 3 秒尝试重连；没有连接 NapCat 时也能编辑配置。

不依赖 NoneBot、Koishi 或 Python。已有 Koishi 等程序时，可以为本程序单独新增 NapCat WebSocket 服务端连接。

## 编译与测试

开发环境：Windows x64、Visual Studio 2022 或 Build Tools 2022，以及 CMake 3.20 或更高版本。安装 Visual Studio 时勾选“使用 C++ 的桌面开发”，包含 MSVC、Windows SDK 和 C++ CMake 工具。

在项目目录运行：

```bat
build.bat
```

脚本从 `PATH` 或 Visual Studio 安装目录查找 CMake，构建 Release 并运行本地测试。输出文件：

- `build\Release\SatouQQBot.exe`：桌面程序。
- `build\Release\SatouQQBotTests.exe`：本地测试程序。

也可以在已配置 CMake 的终端执行：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

程序使用静态 MSVC 运行库。打包可分发目录：

```bat
cmake --install build --config Release --prefix dist
```

`dist` 包含程序、示例配置、说明和许可证，不包含本机配置或群数据。GitHub Actions 会自动编译、测试并上传相同内容的 Windows x64 构建产物。

## 接入 NapCat

1. 让 NapCat 登录机器人专用 QQ 号，确保机器人在需要管理的群内具有管理员权限。
2. 在 NapCat WebUI 新增 **WebSocket 服务端**。监听地址设为 `127.0.0.1`，端口例如 `3011`，消息格式选择 `array`。
3. 启动 `SatouQQBot.exe`，填写 `ws://127.0.0.1:3011`。如果 NapCat 设置了 Token，在窗口中填写相同的 Token，然后保存并启动。
4. 在“群管”页编辑欢迎语、违禁词、刷屏检测和审批规则。

也可以将 `config.example.json` 复制到 **程序所在目录**，重命名为 `config.json` 后修改。程序默认从自身所在目录读取配置；需要指定其他位置时，将配置路径作为第一个参数：

```bat
SatouQQBot.exe "D:\QQBot\config.json"
```

### 配置项

| 字段 | 含义 | 默认值 |
| --- | --- | --- |
| `napcat_url` | NapCat 正向 WebSocket 地址 | `ws://127.0.0.1:3011` |
| `access_token` | NapCat Token，无 Token 时留空 | `""` |
| `superusers` | 超级用户 QQ 号数组，建议使用字符串 | `[]` |
| `command_prefixes` | 可接受的指令前缀 | `["", "/"]` |
| `default_mute_seconds` | 默认禁言秒数，范围 1～2592000 | `600` |
| `data_file` | 群配置文件路径，相对于配置文件所在目录 | `data/groups.json` |

NapCat 负责扫码登录，本程序不保存 QQ 密码。`config.json` 包含连接信息，群数据包含群号和规则；这些运行时文件已加入 Git 忽略规则。自定义数据路径时，也应将对应文件加入忽略规则。

当前内置 WebSocket 客户端仅支持 `ws://`，推荐同机连接 NapCat。

## 群内指令

句首直接输入，或加 `/`。时长支持秒、分、小时和天；纯数字按分钟计算，未填写时默认禁言 10 分钟。

| 指令 | 作用 |
| --- | --- |
| `禁 @成员 [时长]` | 禁言，例如 `禁 @成员 10分` |
| `解 @成员` | 解除禁言 |
| `全禁` / `解全禁` | 开启 / 关闭全体禁言 |
| `踢 @成员` | 移出群聊 |
| `黑 @成员` | 移出并拒绝再次加群 |
| `撤回` | 引用目标消息后发送，撤回该消息 |
| `改 @成员 名片` | 修改群名片，留空则清除 |
| `欢迎 开` / `欢迎 关` | 开启 / 关闭入群欢迎 |
| `欢迎语 文本` | 设置欢迎语，支持 `{nickname}`、`{user_id}`、`{card}` |
| `退群通知 开` / `退群通知 关` | 开启 / 关闭退群通知 |
| `违禁词 加 词语` / `违禁词 删 词语` / `违禁词 列` | 管理违禁词 |
| `违禁词 禁言 10分` | 设置违禁词处罚时长，命中后撤回并禁言 |
| `刷屏 开` / `刷屏 关` | 开启 / 关闭刷屏检测，默认关闭 |
| `刷屏 条数 5` / `刷屏 窗口 8` / `刷屏 禁言 60秒` | 设置检测条数、窗口秒数和处罚时长 |
| `帮助` | 按当前身份显示可用指令 |

普通成员不能执行管理指令。操作者和机器人都必须有足够权限；管理员、群主和超级用户不受违禁词和刷屏处罚。

自动入群审批在窗口中设置：优先检查黑名单和拒绝词，再检查同意词。没有配置同意词时，同意其余申请；配置同意词但未命中时，默认不自动处理，也可以开启“未命中白词则拒绝”。

## 验证连接

本地测试使用模拟 OneBot API，不需要登录 QQ，覆盖指令、权限、配置持久化、欢迎通知、违禁词、刷屏检测和入群审批。真实连接需要在测试群手动验证：

1. 管理员发送 `帮助`，检查可用指令。
2. 对测试号发送 `禁 @测试号 10秒`，然后发送 `解 @测试号`。
3. 引用测试消息后发送 `撤回`。
4. 分别测试 `欢迎 关` 和 `欢迎 开` 后的入群行为。

## 目录

```text
assets/       窗口图标与 Windows 资源
src/          桌面窗口、OneBot 连接及群管逻辑
tests/        本地测试与模拟 API
third_party/  随仓库提供的第三方源码及许可证
data/         运行时群配置目录，数据不提交到仓库
.github/      自动构建与测试工作流
```

## 许可证与第三方代码

本项目业务代码使用 [MIT 许可证](LICENSE)。第三方代码随仓库提供，编译时无需另行下载：

| 组件 | 用途 | 许可证 |
| --- | --- | --- |
| [easywsclient](https://github.com/dhbaird/easywsclient) | WebSocket 客户端 | [MIT](third_party/easywsclient/LICENSE) |
| [nlohmann/json 3.11.3](https://github.com/nlohmann/json/tree/v3.11.3) | JSON 解析 | [MIT](third_party/nlohmann/LICENSE.MIT) |

easywsclient 为兼容 MSVC 做了局部调整：使用系统提供的 `snprintf`、地址解析失败时返回无效套接字，并补充 `stdint.h`。
