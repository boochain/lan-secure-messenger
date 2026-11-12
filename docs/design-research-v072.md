# v0.7.2 设计调研：导航、溢出菜单、恢复密钥页与应用图标

调研日期：2026-11-13（Asia/Shanghai）。对照对象：Telegram、Signal、WhatsApp 的 Android / 桌面界面，以及 Material 自适应图标、Windows Fluent 图标规范。本轮改版按这些现成应用的结构来，而不是再发明一套。

现有产品的不协调之处（改之前的事实）：

- 应用图标不统一：Android / Linux 是亮蓝色方块加三条白杠（像菜单或列表），WinUI 是另一种亮蓝色气泡加三个圆点。两套隐喻、两种蓝，都和界面主色 `#3F5F90`（M3 seed `#005FB8` 的固定调色）对不上。
- Android 底栏有 **6** 个目的地（聊天、联系人、群组、传输、文件站、设置），顶栏同时放软件名、重连文字按钮、搜索、资料、⋯，挤在一起。
- Android ⋯ 菜单有 **14** 项；WinUI 会话 ⋯ 菜单超过 **20** 项。
- 恢复密钥在各端都是 Toast / 状态栏 / 消息框，只出现几秒或一关就没，无法从容抄写，也没有独立的复制/导出/确认页。

## 1. 底栏（Android）

业界共识是 **4 个左右** 的一级目的地，带标签、有呼吸感的间距；再多就会挤成一团。

| 应用 | 一级目的地 | 数量 | 来源 |
|---|---|---|---|
| Telegram Android 12.4（2026-02） | Chats、Contacts、Settings、Profile | **4** | [Telegram 官方博客 2026-02-09](https://telegram.org/blog/crafting-android-design-and-more/)；[Gadgets360 报道](https://www.gadgets360.com/apps/news/telegram-for-android-liquid-glass-ios-26-design-feature-interface-update-10974598)；实测纪要 [getappdesign.md/telegram](https://getappdesign.md/designs/telegram/download) |
| Signal Android | Chats、Calls、Stories（设置走顶栏头像，不占底栏） | **3** | [Signal-Android 主导航说明](https://deepwiki.com/signalapp/Signal-Android/6.1-main-navigation-and-adaptive-layout) |
| WhatsApp Android（稳定形态） | Chats、Updates、Communities、Calls（设置在 ⋯ 或顶栏，不占底栏） | **4** | [WABetaInfo：新底栏](https://wabetainfo.com/whatsapp-is-rolling-out-a-new-bottom-navigation-bar-on-android/)；部分实验会到 5 个（Contacts / You），官方仍在试，不是我们要学的上限 |

Telegram 的底栏是悬浮圆角条，选中项用浅色胶囊；每一项约占四分之一宽度，高度约 55dp，比系统手势条略抬高。WhatsApp 正在试去标签的图标-only 条，但稳定版仍是「图标 + 文字」。我们 **保留标签**，把底栏收成 4 项，并给足够的左右边距，避免挤在一块。

本应用采用的 4 项（与 Telegram 的「聊天 / 联系人 / 设置 / 资料」同构，资料并进设置，空出一位给文件）：

1. **聊天** — 会话列表；群组改为列表上方的筛选芯片（hook `LSMF_nav_groups` 仍可打开群组视图）。
2. **联系人**
3. **文件** — 内部分级：「传输」与「文件站」两个子页（保留 `LSMF_nav_transfers` / `LSMF_nav_files`）。
4. **设置** — 内部分级：个人资料、账号与恢复密钥、中转、诊断、关于等（保留 `LSMF_nav_profile` / `LSMF_nav_diagnostics` / `LSMF_nav_settings`）。

桌面端（WinUI 左侧栏、GTK/ADW 侧栏、XP）同样把一级入口收到这四类，其余进子页，避免再并列 7–8 个图标。

## 2. 顶栏

| 应用 | 顶栏左侧 | 右侧动作 | 来源 |
|---|---|---|---|
| Telegram Android 12.4 | 不再用蓝色汉堡顶栏；聊天页是可收起的搜索框，不再把产品名当作主标题 | ⋯（新建群组、深色模式、收藏消息等） | [Gadgets360](https://www.gadgets360.com/apps/news/telegram-for-android-liquid-glass-ios-26-design-feature-interface-update-10974598)；[TuttoAndroid](https://www.tuttoandroid.net/news/2026/02/08/telegram-nuova-ui-android-rollout-analisi-1138875/) |
| Signal Android | **用户头像**（不是应用名），另有未读角标 | 搜索；按模式出现「全部已读」等 | [MainToolbar 说明](https://deepwiki.com/signalapp/Signal-Android/6.1-main-navigation-and-adaptive-layout) |
| WhatsApp Android | 历史上在左侧放产品名（绿色），近期实验在减少文字负担 | 相机 / 搜索 / ⋯ | [TuttoAndroid 对 Telegram 与 WhatsApp 的对比](https://www.tuttoandroid.net/news/2026/02/08/telegram-nuova-ui-android-rollout-analisi-1138875/) |
| Signal Desktop | 左侧垂直标签：Chats、Calls、Stories、Settings；窄栏只留图标 | 设置已并进主窗口，不再弹独立窗 | [Signal Desktop 左侧导航](https://deepwiki.com/signalapp/Signal-Desktop/3.2-left-pane-and-navigation)；[菜单栏可隐藏](https://support.signal.org/hc/en-us/articles/360007320091-Unhide-the-menu-bar-on-Signal-Desktop) |

结论：现代通讯应用 **不再把软件全名当作顶栏主标题**。Signal 用头像，Telegram 用搜索，WhatsApp 虽曾用品牌名，但也在减负。按用户要求：标题栏 **放应用图标，不写软件名**；当前分区名（联系人、设置、文件）可以保留。顶栏右侧只留少数高频繁动作（搜索 + ⋯），「重连」「个人资料」等移入设置或 ⋯ 分级菜单，避免再挤在一行。

## 3. ⋯ 溢出菜单与分级

Material 设置规范：设置通常不是一级导航；概览页只放最常用项，其余进子页；15 项以上必须分组。见 [Android Settings pattern](https://developer.android.com/design/ui/mobile/guides/patterns/settings)。

Telegram 把旧汉堡菜单拆掉后，聊天页 ⋯ 只留下少数项（新建群组、深色模式、收藏消息等）。WhatsApp 的 ⋯ 典型也是短菜单（新建群组、新建广播、已星标、关联设备、设置），设置再分级：Settings → Chats → Chat backup → End-to-end encrypted backup。

硬规则（本轮）：

- 任何一个 ⋯ 菜单 **最多 5 个项目**（子菜单本身也算一个项目；子菜单内部同样 ≤ 5）。
- 多出来的动作全部改成 **分级页面** 或 **子菜单**。
- 会话行菜单同样 ≤ 5：置顶、已读/未读（合并为一项）、免打扰、信任、删除。

建议的聊天页 ⋯（5 项）：

1. 添加联系人 — 打开分级页（按 IP、我的二维码、链接、扫码、生成 PIN）
2. 创建群组
3. 切换会话
4. 刷新联系人
5. 扫描二维码（高频；其余如关于、检查更新、诊断、重连、资料一律进设置）

WinUI 会话 ⋯ 拆成 5 个入口，每个再最多 5 项：会话（置顶 / 归档 / 免打扰 / 分组 / 定时删除）、消息（历史 / 收藏 / 置顶消息 / 多选 / 投票）、联系人（资料 / 详情 / 二维码 / PIN / 复制指纹）、信任管理、清理（清空记录 / 忘记联系人）。

## 4. 恢复密钥必须是独立页面

现状：首启选账号模式、或在设置里点「显示恢复密钥」时，只弹 Toast / 状态栏 / 消息框，看几秒就没了。这和业界做法相反。

### Signal

- 路径：Settings → Backups → On-device backups → **View recovery key**，仔细保存后再点 Next，并 **再次确认** 刚抄下的密钥。
- 恢复时在独立流程中输入该密钥；没有密钥就不能恢复。
- 文档：[Android On-device Backups](https://support.signal.org/hc/en-us/articles/10066926526362-Android-On-device-Backups)；[Signal Secure Backups](https://support.signal.org/hc/en-us/articles/9708267671322-Signal-Secure-Backups)。

### WhatsApp

- 路径：⋯ → Settings → Chats → Chat backup → End-to-end encrypted backup → 64 位密钥（或密码 / passkey）。
- 密钥显示在 **专门的一页** 上，可复制；用户先写下来，点 Continue，再点 **“I saved my 64-digit key”** 才继续。
- 文档：[WhatsApp 备份说明](https://faq.whatsapp.com/481135090640375/?cms_platform=android)；[64 位密钥流程说明](https://mashtips.com/encrypt-whatsapp-backup-on-android/)；[忘记密码](https://faq.whatsapp.com/639067727894647/?cms_platform=android)。

本应用的恢复密钥页（五端一致）必须包含：

1. 标题「恢复密钥」，说明这是换机恢复账号和加密历史的唯一凭证，服务器看不到明文，丢失无法找回。
2. 以等宽、分组格式 **长期显示** 完整密钥（`RK1-XXXX-…`），不是几秒 Toast。
3. **复制**、**保存/导出到文件**。
4. 「我已抄下并妥善保管」确认后才能离开首启流程。
5. 之后可从 **设置 → 账号** 再次打开同一页。

首启选择账号模式、或从设备模式切到账号模式时，直接进入该页，不再用会消失的提示。

## 5. 应用图标规范

### Material 自适应图标

来源：[Adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive)。

- 前景、背景两层，均为 **108×108 dp**；关键图形必须落在中心 **66×66 dp** 安全区，外圈 18 dp 留给 OEM 遮罩和视差。
- 背景铺满、不透明；前景不要自带遮罩或投影。
- 另提供 **monochrome** 单色剪影（透明底），供 Android 13+ 主题化图标使用。
- 在 `mipmap-anydpi-v26/ic_launcher.xml` 中声明 `<background>`、`<foreground>`、`<monochrome>`。

### Windows Fluent 图标

来源：[Design guidelines for Windows app icons](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-design)、[Construct your Windows App's Icon](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction)。

- 一个清晰隐喻，最多两个；不要把文字写进图标。
- 先在 48×48 网格上对齐；外圆角 2 px、内圆角 1 px（按 48 计）。
- 剪影要在 16 px 仍可辨认；少用渐变和装饰。
- ICO 至少包含 **16、24、32、48、256**。Windows 先找精确尺寸，否则从更大的往下缩。
- 优先透明底；颜色要在浅色和深色任务栏上都过得去。

### 新图标的设计决定

- **单一隐喻**：对话气泡（通讯）。不再用三条横杠，也不再和 WinUI 各画一套。
- **颜色跟现有界面走**，不另起一套亮蓝：底 `#3F5F90`（accent），图形 `#F9F9FF`（window），安全区深色变体 `#264777`（onPrimaryContainer）。
- 图形居中、线条粗、圆角明确，保证 16 px 仍是「气泡」而不是色块。
- 交付：SVG 母版、Android 自适应三层、Windows ICO/PNG 多尺寸、XP ICO、Linux hicolor PNG、AppImage 图标，以及一张从小到大的 contact sheet，用于目视检查。

## 6. 本轮还要加的自动检查

- 截图流水线增加溢出/裁切检测：文字包围盒对容器、省略号/被切字形、OCR 对照。
- 断言：⋯ 菜单项 ≤ 5，Android 底栏目的地 ≤ 4。
- 浅色/深色截图必须人工逐张看过，不能只信 CI 绿。

在用户说「发版」之前，合进 main 即可，**不打 tag、不发 Release**。
