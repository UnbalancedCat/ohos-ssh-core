# HarmonyOS SSH Core API Reference

`ssh_lib` 提供了一个基于 `EventEmitter` 的高级面向对象封装：`SshClient`。所有的耗时操作（如连接、执行命令、SFTP 文件传输）都返回 `Promise` 以便在 ArkTS 中使用 `async/await`。

## 导入

```typescript
import { SshClient, SshState, SftpFileInfo, SftpOpenFlags } from 'ssh_lib';
```

---

## 1. 生命周期与认证

### `new SshClient()`
创建一个新的 SSH 客户端实例，并在 C++ 层分配相应的原生上下文。

### `async connect(host: string, port: number, user: string, pass: string): Promise<void>`
使用密码进行认证连接。

### `async connectWithKey(host: string, port: number, user: string, privateKey: string, passphrase?: string): Promise<void>`
使用私钥进行认证连接。`privateKey` 为字符串格式的私钥内容（例如包含 `-----BEGIN OPENSSH PRIVATE KEY-----`）。如果私钥加密，需提供 `passphrase`。

### `dispose(): void`
立即断开 SSH 连接，关闭所有关联的 SFTP 会话和通道，终止后台读取线程，并释放 C++ 内存。**调用后不可复用该实例**。

### `getState(): SshState`
返回当前客户端的状态 (`DISCONNECTED`, `CONNECTING`, `CONNECTED`, `ERROR`)。

---

## 2. 交互式 Shell 与命令

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

## 3. SFTP 常规操作

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

### `async sftpMkdir(path: string): Promise<void>`
创建远端目录（默认权限 0755）。

### `async sftpRmdir(path: string): Promise<void>`
删除远端目录（目录必须为空）。

### `async sftpDelete(path: string): Promise<void>`
删除指定的远端文件。

### `async sftpRename(src: string, dst: string): Promise<void>`
将远端文件或目录从 `src` 重命名（移动）为 `dst`。

---

## 4. SFTP 快捷读写（全量内存缓冲）

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

## 5. SFTP 流式与分块传输（底层 API）

适用于几百MB的**大型文件**。利用以下 API 可以防止内存溢出，并方便地结合 UI 实现下载/上传进度条。

### `async sftpOpenFile(path: string, flags: number, mode: number): Promise<number>`
打开一个远程文件并返回一个文件描述符 (fd)。
- **`flags`**: 参考 `SftpOpenFlags` 枚举（如 `SftpOpenFlags.READ`, `SftpOpenFlags.WRITE | SftpOpenFlags.CREAT`）。
- **`mode`**: 文件权限位，如 `0644` (十进制通常写作 `420` 或 `0x01A4` depending on representation)。

### `async sftpRead(fd: number, size: number = 32768): Promise<ArrayBuffer>`
从指定的文件描述符中读取最多 `size` 字节的二进制数据。
如果返回的 `ArrayBuffer` 长度 (`byteLength`) 为 0，表示已到达文件末尾 (EOF)。

### `async sftpWrite(fd: number, data: ArrayBuffer): Promise<void>`
将二进制数据块写入指定的文件描述符中。

### `async sftpCloseFile(fd: number): Promise<void>`
关闭文件描述符，释放远端与本地句柄资源。使用流式 API 时必须确保在 `finally` 块中调用此方法。
