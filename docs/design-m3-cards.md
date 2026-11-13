# 卡片与图标（Material 3）

这一轮只改已经写在规范里的东西，不另起一套视觉。

依据（2026-11-13 对照）：

- [Cards specs](https://m3.material.io/components/cards/specs)：filled 卡片用 `surface-container-highest`，圆角是 shape medium **12dp**，内边距 **16dp**。选中用容器色，不发明新形状。
- [Lists specs](https://m3.material.io/components/lists/specs)：双行行高 **72dp**，左右 **16dp**，前置槽 **40dp**，图标 **24dp**。
- [Icon buttons](https://m3.material.io/components/icon-buttons/specs)：图标 24dp，默认容器是圆的，小尺寸可见圆为 40dp。
- [Cards guidelines](https://m3.material.io/components/cards/guidelines)：选项放进卡片，选中态用勾，而不是再排一列单选文字。

颜色只用现有色板角色（Android `Theme.kt` 的 `chip` / `pCont` / `secCont`，WinUI 的 `CardBackground` 与 `AccentFill`）。不加新的十六进制。

页面上的做法：

- 说明文字收进卡片，图标在前，标题一行，补充说明用次要文字，最多两到三行。
- 互斥选项（身份方式、主题、传输、文件页的「传输 / 文件站」）做成可选卡片：未选是填充卡片加 1dp 描边，选中是 primary container、2dp 强调描边、右侧勾。
- 设置里的动作是方形卡片：上面 40dp 圆标，下面短标签。
- 会话、联系人、传输、文件、诊断条目同样是 12dp 卡片，不再是贴在底色上的纯文字行。诊断按字段拆开，不再一整块等宽转储。

各端：

- Android：`Cards.kt`，设置 / 首启 / 恢复密钥 / 文件 / 联系人 / 会话 / 诊断。
- WinUI：`ChoiceCardRadioStyle`，主题、传输、身份、首启。设置卡片圆角从 Fluent 的 8 改为规范的 12，描边保留，避免和页面糊在一起。
- GTK 3 与 libadwaita：首启的两个身份选项改成带图标的卡片行，说明文字还在，钩子 `LSMF_account_mode` 仍在单选控件上。
- Windows XP：继续用系统单选。Owner-draw 卡片会和已经审过的经典主题打架，这一轮不动。

合进 main 即可，不打 tag、不发 Release。
