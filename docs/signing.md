# Android 安装包签名

v0.7.3 第一次发出去的 `LanMessenger-v0.7.3-android.apk` 用的是 CI 机器当天生成的 **Android Debug** 证书（`CN=Android Debug, O=Android, C=US`，notBefore 2026-11-14 14:35 GMT）。包里有 v2 签名块，但主体不是开发者证书。部分手机的安装器会把它报成「软件包无效」或「缺乏开发者证书」，拒绝安装。

发布包必须用下面这张证书，不能再把 `release` 的 `signingConfig` 指回 `debug`。

| | |
|---|---|
| 主体 | `CN=LAN Secure Messenger, OU=FREC86, O=boochain, C=TW` |
| 算法 | RSA 2048，SHA256withRSA |
| 有效期 | 2026-11-14 至 2054-04-01 |
| SHA-256 | `34:DF:ED:11:3D:F0:E1:23:3C:68:AF:B9:42:D4:07:FD:69:77:12:7B:0E:2A:28:0C:BE:25:6D:22:95:60:17:EE` |
| SHA-1 | `5E:DA:35:C0:0A:F6:37:8F:69:CC:73:DC:3D:CB:48:92:D0:8C:8C:33` |
| 文件 | `editions/portable/android/app/signing/lsm-release.p12` |
| 别名与口令 | 同目录 `keystore.properties`（`alias=lsm`） |

这是旁加载用的发布证书，放在仓库里，是为了让本机 `assembleRelease` 和 GitHub Actions 打出同一张证书。换一把钥匙会导致已安装的用户无法覆盖升级。不要把它换成 debug，也不要在工作流里重新 `keytool -genkeypair`。

校验已签名 APK：

```bash
keytool -printcert -jarfile LanMessenger-v0.7.3-android.apk
```

主体必须是 `CN=LAN Secure Messenger`，SHA-256 必须与上表一致。
