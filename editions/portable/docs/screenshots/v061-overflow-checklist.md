# v0.6.1 #1 气泡/列表裁切截图清单

发版前在真机/模拟器核对（本 CI 环境无 GUI 显示，未自动截图）：

| 平台 | 场景 | 期望 |
|------|------|------|
| WinUI | 长文本气泡 / 高 DPI 150% | 不横向裁切；MaxWidth≈720 |
| WinUI | 文件卡 | MaxWidth≈480，名完整换行 |
| GTK3/ADW | 窄窗 + 长中文 | CSS max-width 92%/720 |
| XP | 小窗聊天气泡 | 文本换行可见 |
| Android | 竖屏长消息 | maxW coerceAtMost 720 |

命令参考：`editions/portable/tests/adw_gui_shots.py`（需显示）。
