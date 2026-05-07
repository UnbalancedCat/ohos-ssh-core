# HarmonyOS SSH Core API Reference

`ssh_lib` 提供了一个基于 `EventEmitter` 的高级面向对象封装：`SshClient`。所有的耗时操作（如连接、执行命令、SFTP 文件传输）都返回 `Promise` 以便在 ArkTS 中使用 `async/await`。

## 导入

```typescript
import {
  SshClient, SshState,
  SftpFileInfo, SftpOpenFlags,
  SshError, SshErrorCode, SshConnectOptions
} from 'ssh_lib';
```

---

## 1. 生命周期与认证

### `new SshClient()`
创建一个新的 SSH 客户端实例，并在 C++ 层分配相应的原生上下文。

### `async connect(host, port, user, pass, options?): Promise<void>`
使用密码进行认证连接。

| 参数 | 类型 | 说明 |
|------|------|------|
| `host` | `string` | 服务器地址 |
| `port` | `number` | SSH 端口号 |
| `user` | `string` | 用户名 |
| `pass` | `string` | 密码 |
| `options?` | `SshConnectOptions` | 可选的超时与心跳配置 |

### `async connectWithKey(host, port, user, privateKey, passphrase?, options?): Promise<void>`
使用私钥进行认证连接。`privateKey` 为字符串格式的私钥内容（例如包含 `-----BEGIN OPENSSH PRIVATE KEY-----`）。如果私钥加密，需提供 `passphrase`。

### `SshConnectOptions` 接口
```typescript
{
  timeoutSec?: number;        // TCP + 握手超时秒数（默认 10）
  keepaliveInterval?: number; // Keepalive 心跳间隔秒数（默认 30，设 0 禁用）
}
```

### `getHostKeyFingerprint(): string`
获取远端服务器公钥的 SHA-256 指纹。**必须在连接成功后调用。** 返回 Base64 编码的字符串，格式与 `ssh-keygen -lf` 的输出一致。
可用于实现首次连接确认（Trust On First Use, TOFU）或与本地存储的已知指纹进行比对。

### `dispose(): void`
立即断开 SSH 连接，关闭所有关联的 SFTP 会话和通道，终止后台读取线程，并释放 C++ 内存。**调用后不可复用该实例**。

### `getState(): SshState`
返回当前客户端的状态 (`DISCONNECTED`, `CONNECTING`, `CONNECTED`, `ERROR`)。

---

## 2. 错误处理

所有方法在出错时抛出 `SshError`（继承自 `Error`），包含一个结构化的错误码 `code` 属性。

### `SshErrorCode` 枚举
| 值 | 含义 |
|----|------|
| `UNKNOWN` | 未分类的错误 |
| `NETWORK_ERROR` | DNS 解析失败、TCP 连接失败、Socket 错误 |
| `AUTH_FAILED` | 密码或密钥认证失败 |
| `HANDSHAKE_FAILED` | SSH 协议握手失败 |
| `CHANNEL_ERROR` | Channel 打开、Shell 启动、PTY 请求失败 |
| `SFTP_ERROR` | SFTP 子系统相关错误 |
| `TIMEOUT` | 连接或操作超时 |
| `DISPOSED` | 在已释放的客户端上调用方法 |

### 使用示例
```typescript
try {
  await client.connect('192.168.1.1', 22, 'root', 'wrong_pass');
} catch (err) {
  if (err instanceof SshError) {
    if (err.code === SshErrorCode.AUTH_FAILED) {
      // 密码错误，提示用户重试
    } else if (err.code === SshErrorCode.TIMEOUT) {
      // 连接超时
    }
  }
}
```

---

## 3. 交互式 Shell 与命令

`SshClient` 继承自 `EventEmitter`。当开启 Shell 后，服务器返回的数据会通过事件抛出。

### 事件监听
- `client.on('data', (data: string) => void)`: 接收来自服务器终端的数据输出。
- `client.on('error', (err: Error) => void)`: 接收异步异常。
- `client.on('close', () => void)`: 连接断开时触发。

### `async startShell(cols: number = 80, rows: number = 24): Promise<void>`
开启一个带伪终端 (PTY) 的交互式 Shell。默认尺寸为 80x24。开启后，必须通过 `.on('data')` 监听服务器的响应。

### `writeToShell(data: string): void`
向当前的交互式 Shell 发送数据（如用户的按键、命令及换行符 `\n`）。非阻塞的同步调用。

### `resizePty(cols: number, rows: number): void`
动态调整远端伪终端的行列数（例如在设备屏幕旋转或终端组件大小改变时调用）。

### `async execCommand(command: string): Promise<string>`
执行单条命令并等待其执行完毕，返回完整的命令输出。**此方法不产生 PTY**，适合用于非交互式的自动化脚本。

---

## 4. SFTP 常规操作

所有 SFTP 操作前，必须先调用 `sftpInit()` 进行初始化。

### `async sftpInit(): Promise<void>`
在当前的 SSH 会话上初始化 SFTP 子系统。

### `async sftpShutdown(): Promise<void>`
关闭 SFTP 子系统，但不影响底层的 SSH 连接。

### `async sftpListDir(path: string): Promise<SftpFileInfo[]>`
读取远端目录，返回包含文件/文件夹详情的数组。
**返回类型 `SftpFileInfo`**:
```typescript
{
  name: string;        // 文件名
  size: number;        // 文件大小（字节）
  permissions: number; // 权限位（POSIX 标准格式）
  mtime: number;       // 最后修改时间（Unix时间戳）
  isDir: boolean;      // 是否是目录
}
```

### `async sftpStat(path: string): Promise<SftpFileInfo>`
获取指定文件或目录的属性信息。

### `async sftpChmod(path: string, mode: number): Promise<void>`
修改远端文件或目录的权限位。`mode` 为 POSIX 标准权限值（如 `0o755`、`0o644`）。

### `async sftpMkdir(path: string): Promise<void>`
创建远端目录（默认权限 0755）。

### `async sftpRmdir(path: string): Promise<void>`
删除远端目录（目录必须为空）。

### `async sftpDelete(path: string): Promise<void>`
删除指定的远端文件。

### `async sftpRename(src: string, dst: string): Promise<void>`
将远端文件或目录从 `src` 重命名（移动）为 `dst`。

---

## 5. SFTP 快捷读写（全量内存缓冲）

适用于中小型文件（如配置文件、脚本、日志片段、小图片等）。

### 文本模式（UTF-8）
- **`async sftpReadFile(remotePath: string): Promise<string>`**
  将远程文件作为 UTF-8 字符串全部读取并返回。
- **`async sftpWriteFile(remotePath: string, data: string): Promise<void>`**
  将 UTF-8 字符串全量写入远程文件（不存在则创建，存在则覆盖）。

### 二进制模式（ArrayBuffer）
- **`async sftpReadFileBytes(remotePath: string): Promise<ArrayBuffer>`**
  将远程文件读取为二进制 `ArrayBuffer`。
- **`async sftpWriteFileBytes(remotePath: string, data: ArrayBuffer): Promise<void>`**
  将二进制 `ArrayBuffer` 全量写入远程文件。

---

## 6. SFTP 流式与分块传输（底层 API）

适用于几百MB的**大型文件**。利用以下 API 可以防止内存溢出，并方便地结合 UI 实现下载/上传进度条。

### `async sftpOpenFile(path: string, flags: number, mode: number): Promise<number>`
打开一个远程文件并返回一个文件描述符 (fd)。
- **`flags`**: 参考 `SftpOpenFlags` 枚举（如 `SftpOpenFlags.READ`, `SftpOpenFlags.WRITE | SftpOpenFlags.CREAT`）。
- **`mode`**: 文件权限位，如 `0o644`。

### `async sftpRead(fd: number, size: number = 32768): Promise<ArrayBuffer>`
从指定的文件描述符中读取最多 `size` 字节的二进制数据。
如果返回的 `ArrayBuffer` 长度 (`byteLength`) 为 0，表示已到达文件末尾 (EOF)。

### `async sftpWrite(fd: number, data: ArrayBuffer): Promise<void>`
将二进制数据块写入指定的文件描述符中。

### `async sftpCloseFile(fd: number): Promise<void>`
关闭文件描述符，释放远端与本地句柄资源。使用流式 API 时必须确保在 `finally` 块中调用此方法。
