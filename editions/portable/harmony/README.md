# 局域网安全通讯 · HarmonyOS

这是 **0.7.4** 的鸿蒙工程。协议仍是 v5，加密和收发都在原来的 C 核心里（`editions/portable/src`），不是把安卓界面重写一遍，也不是新协议。

HarmonyOS 7 是纯血鸿蒙（API 26，SDK 26.0.0）。系统里没有安卓安装器，**双击现在 Release 里的 APK 会提示打不开**。这个目录编出来的是 HAP，不是 APK。

## 怎么装（只能这么装）

手机不接受没有华为签名的 HAP。调试证书还要绑定你这台手机的 UDID。所以仓库里**没有**一个谁下载谁都能装的包，也不要把 `.p12` / `.p7b` / `.cer` 交进来。

1. 安装 [DevEco Studio 26.0.0 Release](https://developer.huawei.com/consumer/cn/download/)（26.0.0.865）。它自带 HarmonyOS SDK 26、Hvigor 6.26.9、ohpm 26.0.0。
2. 用 DevEco **打开本目录** `editions/portable/harmony`，不要打开整个 git 仓库。
3. 工程已经按鸿蒙 7 配好，一般不用改：
   - `compatibleSdkVersion`：`5.0.0(12)`，HarmonyOS NEXT 5 / 6 / 7 都能装
   - `targetSdkVersion`：`26.0.0`，按鸿蒙 7 的系统行为适配
   - 没有写 `compileSdkVersion`，编译用 DevEco 自带的 SDK
   - 版本号字符串是 `26.0.0`，不要改成有的博客里的 `7.0.0(26)`
4. **File → Project Structure → Signing Configs**，勾选自动签名，登录华为开发者账号。第一次会让你把连上的手机登记成调试设备。
5. 手机：设置 → 关于手机 → 连续点版本号 7 次，打开开发者模式。开发者选项里打开 USB 调试。数据线连上电脑，手机上点允许。
6. DevEco 顶部选这台手机，点 Run。IDE 会编译、签名、安装并打开。

装好之后包名是 `io.github.frec86.lanmessenger`，版本名 0.7.4（versionCode 70499），图标和名字是「局域网安全通讯」。和安卓包名相同，系统不同，不会互相覆盖。已经装过调试证书安卓包的手机，和这个 HAP 无关，不用为了鸿蒙去卸安卓。

命令行要有本机 SDK 才能用，而且编出来的未签名包仍然装不上：

```bash
ohpm install
hvigorw assembleHap --mode module -p product=default -p buildMode=debug --no-daemon
```

## 只想先在鸿蒙 7 上聊天

卓易通是鸿蒙 7 上的安卓容器，可以安装已经发布的 [0.7.4 APK](https://github.com/boochain/lan-secure-messenger/releases/tag/v0.7.4)。那是过渡，不是这个鸿蒙工程，功能以安卓版为准。

## 这版界面

底栏 4 项：聊天、联系人、文件、设置。颜色用系统资源，跟着系统深浅色变。

- 第一次打开选设备模式或账号模式。账号模式的恢复密钥只在屏幕上出现，不写日志。
- 私聊没信任时不能发图片和文件，会提示：对方尚未信任，无法发送图片/文件。请先在联系人菜单中信任对方（或完成 PIN 配对）。
- 图片在应用里看，不另开一个系统窗口。
- 可以按 IP 或 `lsm://` 加联系人，信任、PIN 配对、建群、改名字、设中转。

反应、投票、引用、编辑撤回已经在同一套 C 接口里，这版屏幕还没有逐个按钮。不要把 `harmony` 写进 `docs/features.yml`，对照检查会因为没有钩子和测试把 CI 打红。

## 限制

- 编写这份工程的环境里没有 DevEco，也没有华为账号，**没有编出 HAP，更没有签过名**。
- 退到后台后进程可能被冻住，回到前台才继续收消息。
- 局域网自动发现用 UDP 组播。鸿蒙沙箱有可能拦组播。发现不到对方时，用 IP 手动添加，或走中转服务器。
- 聊天记录在应用自己的目录里。覆盖安装保留，卸载就没了。

## 和安卓共用的代码

`editions/portable/src/lsm_host.c`。安卓的 `jni.c` 和这里的 `napi_init.cpp` 都只做薄封装。改协议或 JSON 时改这一份，不要分两边各写一份。
