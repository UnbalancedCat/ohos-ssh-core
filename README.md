# HarmonyOS SSH Core (ssh_core)

本项目是一个为 HarmonyOS (ArkTS/C++) 设计的生产级 SSH 与 SFTP 客户端核心库。它基于业界标准的 `libssh2` 构建，并采用高性能的 `OpenSSL 3.0` 作为密码学后端，通过 N-API 提供安全、稳定、异步的 ArkTS 接口。

## 项目结构

本项目基于多模块架构设计：

- **`ssh_lib` (HAR 静态共享包)**：核心库模块。
  - `src/main/cpp`：C++ N-API 桥接层。负责管理原生 Socket、线程安全的后台数据读取（用于 Shell）、以及异步的 N-API 任务。
  - `src/main/cpp/third_party`：预编译的 `libssh2` 和 `OpenSSL` 静态库（按架构分类存储）。
  - `src/main/ets`：ArkTS 侧的高级面向对象封装（`SshClient`、`EventEmitter`）。
- **`entry` (HAP 应用程序)**：测试与演示模块。
  - 包含了一个直观的 Smoke Test UI，用于测试连接、执行命令、交互式 Shell 通信以及 SFTP 文件系统管理。

## 核心特性

1. **全面的认证方式支持**
   - 支持常规的密码登录。
   - 支持公钥/私钥认证（`connectWithKey`）。底层由 OpenSSL 自动从私钥（如 `BEGIN OPENSSH PRIVATE KEY`）推导公钥，无需额外提取。
2. **交互式 Shell 与 PTY**
   - 支持动态请求并重置 PTY 大小（`resizePty`），为 Terminal UI（如 xterm.js）提供完美支持。
   - 内部维护独立的 C++ 读取线程，并通过 N-API 的 `napi_threadsafe_function` 以极低的延迟将终端数据抛给 ArkTS 侧的事件监听器。
3. **SFTP 全功能文件管理**
   - 包含完整的目录/文件操作：列出目录 (`sftpListDir`)、创建/删除目录、重命名、删除文件、获取文件状态信息 (`sftpStat`)。
   - **文本与二进制双模传输**：提供便捷的 `string` 文本读写，以及全量的 `ArrayBuffer` 二进制读写接口。
   - **流式大文件传输**：暴露底层的 POSIX 风格接口 (`open`, `read`, `write`, `close`)，允许 ArkTS 侧进行内存友好的分块（Chunk）读写，轻松实现上传/下载进度条。
4. **稳定与内存安全**
   - 所有的 N-API 方法均采用 `napi_async_work` 防止阻塞主线程。
   - C++ 上下文 (`SshContext`) 严格管理生命周期，确保 Socket、句柄以及后台线程在断开连接或应用退出时被安全释放。

## 如何接入

其他鸿蒙项目如果需要使用该库，可以通过本地 HAR 依赖引入：
在工程的 `oh-package.json5` 中添加：
```json
"dependencies": {
  "ssh_lib": "file:../path/to/ssh_core/ssh_lib"
}
```

## 编译与运行

本项目使用 DevEco Studio 标准的 CMake + Hvigor 构建流程。
- 打开 DevEco Studio，同步项目后直接运行 `entry` 模块即可在模拟器或真机上测试功能。
- 在 `ssh_lib` 的 `CMakeLists.txt` 中，可以通过修改构建变量（如 `USE_OPENSSL`）来定制底层的加密引擎。
