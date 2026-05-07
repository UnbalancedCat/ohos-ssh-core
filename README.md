# HarmonyOS SSH Core - ohos-ssh-core

[English](./README_en.md) | 简体中文

本项目是一个为 HarmonyOS 设计的 SSH 与 SFTP 客户端原生库。基于业界标准的 C/C++ 开源实现，通过 N-API 提供异步的 ArkTS 接口，使得鸿蒙应用能够原生具备远程服务器管理与文件传输能力。

项目分为 `ssh_lib` (核心 HAR 包) 和 `entry` (演示应用) 两个主要模块。

## 详细 API 文档

有关 ArkTS 接口的详细调用方法、参数说明及高级流式操作指南，请参阅同级目录下的 API 文档：
👉 **[API Reference (API.md)](./API.md)**

## 底层依赖与精简说明

为了在鸿蒙端提供最佳的兼容性与体积控制，本项目集成了以下三个开源库的预编译二进制或源码，并进行了必要的平台适配与多余组件（如测试用例、CLI 工具）的剔除：

1. **[libssh2](https://github.com/libssh2/libssh2)**
   - **用途**：提供底层的 SSH2 协议解析、通道复用、SFTP 子系统实现。
   - **适配与精简**：剔除了测试代码与构建脚本。通过 N-API 将其阻塞的 POSIX Socket 操作异步化，封装为 Promise 以防止阻塞应用主线程。
2. **[OpenSSL](https://github.com/openssl/openssl)** (主密码学后端)
   - **用途**：提供高性能的加密算法支持（如 RSA/Ed25519 密钥解析、数据流加解密）。
   - **适配与精简**：预编译了 HarmonyOS 对应架构（arm64-v8a 等）的静态库 (`libcrypto.a`, `libssl.a`)。为了精简安装包体积，移除了所有与核心加密无关的可执行文件、引擎插件 (engines) 与历史遗留配置脚本。
3. **[mbedTLS](https://github.com/Mbed-TLS/mbedtls)** (可选轻量级后端)
   - **用途**：作为 OpenSSL 的轻量级替代方案。
   - **适配**：可以在 `ssh_lib/src/main/cpp/CMakeLists.txt` 中通过切换 `USE_OPENSSL` 开关来进行后端替换，适用于对安装包体积有极度严苛要求的场景。

## 核心特性介绍

1. **多格式密钥认证**
   - 原生支持密码登录。
   - 依赖 OpenSSL，自动支持解析标准 OpenSSH 格式的私钥（如 `-----BEGIN OPENSSH PRIVATE KEY-----`）以及带密码保护（Passphrase）的私钥，开发者无需手动从中分离提取公钥。
2. **安全与连接管理**
   - **Host Key 指纹验证**：连接成功后可调用 `getHostKeyFingerprint()` 获取服务器公钥的 SHA-256 指纹（Base64 格式，与 `ssh-keygen -lf` 输出一致），支持实现 TOFU (Trust On First Use) 策略。
   - **连接超时**：通过 `SshConnectOptions.timeoutSec` 设置 TCP + 握手超时（默认 10 秒），避免网络不可达时长时间挂起。
   - **Keepalive 心跳**：通过 `SshConnectOptions.keepaliveInterval` 配置 `libssh2_keepalive_config`（默认 30 秒），防止 NAT 网关因空闲超时断开连接。
   - **结构化错误分类**：所有异常均抛出 `SshError` 实例，携带 `SshErrorCode` 枚举（如 `NETWORK_ERROR`、`AUTH_FAILED`、`TIMEOUT`），便于调用方精确处理不同类型的错误。
3. **交互式 Shell 通信**
   - 内部维护独立的 C++ Socket 读取线程。当终端数据到达时，利用 N-API 的 `napi_threadsafe_function` 将数据非阻塞地安全推送给 ArkTS 侧。
   - 开放了 PTY 大小调整 (`resizePty`) 接口，便于前端无缝集成类似 `xterm.js` 的终端 UI 组件。
4. **层次化的 SFTP 文件管理**
   - **常规文件操作**：提供目录读取、属性查询 (`stat`)、权限修改 (`chmod`)、创建/删除/重命名等标准 POSIX 语义接口。
   - **快捷读写**：提供将远端文件直接读取为 `string` 或 `ArrayBuffer` 的全量内存读取接口，适合配置文件等小文件操作。
   - **流式大文件传输**：暴露底层的 `open`, `read`, `write`, `close` 接口（返回文件句柄 `fd`），ArkTS 侧可以自行控制分块大小（Chunk）进行循环传输，有效控制内存峰值并易于实现下载/上传进度条功能。

## 可运行演示 (Demo)

`entry` 模块包含了一个完整的测试 UI，客观展示了 `ssh_lib` 的所有核心功能，可直接在鸿蒙模拟器或真机上运行。

- **入口文件**：[`entry/src/main/ets/pages/Index.ets`](./entry/src/main/ets/pages/Index.ets)
- **演示功能**：
  - 输入目标 IP、端口、用户名和认证凭据进行连接。
  - **Exec 模式**：执行单次非交互命令（如 `ls /`）并打印输出。
  - **Shell 模式**：启动 PTY 并允许用户在输入框中向服务器发送实时终端指令。
  - **SFTP 测试**：一键测试目录列出 (`sftpListDir`)、读取系统文本文件 (`/etc/hostname`)、以及二进制流式读取系统命令（如 `/bin/ls`）并进行 ELF 魔数验证。

## 如何在项目中引入

在你的 DevEco Studio 鸿蒙工程中，将本项目的 `ssh_lib` 作为本地共享包引入。在需要使用的模块的 `oh-package.json5` 中添加：

```json
"dependencies": {
  "ssh_lib": "file:../path/to/ssh_core/ssh_lib"
}
```
随后执行 `ohpm install` (或点击 IDE 的 Sync) 即可使用。

## 开源协议 (License)

本项目采用 [Apache License 2.0](./LICENSE) 开源协议。

本项目集成的第三方组件均遵循其原始的开源许可协议：
- **OpenSSL**: Apache License 2.0
- **mbedTLS**: Apache License 2.0
- **libssh2**: BSD 3-Clause License

详细的协议文本与版权声明请参阅根目录下的 [`LICENSE`](./LICENSE) 文件。
