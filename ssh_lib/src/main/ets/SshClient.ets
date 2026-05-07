import ssh from 'libssh_core.so';
import type { SshHandle } from 'libssh_core.so';
import { EventEmitter } from './EventEmitter';

export enum SshState {
  DISCONNECTED = 'DISCONNECTED',
  CONNECTING = 'CONNECTING',
  CONNECTED = 'CONNECTED',
  ERROR = 'ERROR'
}

export interface SftpFileInfo {
  name: string;
  size: number;
  permissions: number;
  mtime: number;
  isDir: boolean;
}

export enum SftpOpenFlags {
  READ = 0x00000001,
  WRITE = 0x00000002,
  APPEND = 0x00000004,
  CREAT = 0x00000008,
  TRUNC = 0x00000010,
  EXCL = 0x00000020
}

// ===== Stage 5: Structured Error Classification =====

export enum SshErrorCode {
  UNKNOWN = 'UNKNOWN',
  NETWORK_ERROR = 'NETWORK_ERROR',
  AUTH_FAILED = 'AUTH_FAILED',
  HANDSHAKE_FAILED = 'HANDSHAKE_FAILED',
  CHANNEL_ERROR = 'CHANNEL_ERROR',
  SFTP_ERROR = 'SFTP_ERROR',
  TIMEOUT = 'TIMEOUT',
  DISPOSED = 'DISPOSED',
}

export class SshError extends Error {
  code: SshErrorCode;
  constructor(message: string, code: SshErrorCode) {
    super(message);
    this.code = code;
  }
}

export interface SshConnectOptions {
  /** TCP + handshake timeout in seconds (default: 10) */
  timeoutSec?: number;
  /** Keepalive interval in seconds (default: 30, set 0 to disable) */
  keepaliveInterval?: number;
}

/**
 * High-level ArkTS Object-Oriented wrapper for the native SSH core library.
 * Emits:
 * - 'data': (data: string) => void  // Terminal data from server
 * - 'error': (err: Error) => void   // Asynchronous errors
 * - 'close': () => void             // Connection closed
 */
export class SshClient extends EventEmitter {
  private handle: SshHandle | null = null;
  private state: SshState = SshState.DISCONNECTED;

  constructor() {
    super();
    this.handle = ssh.createContext();
  }

  private classifyError(msg: string): SshErrorCode {
    const lower = msg.toLowerCase();
    if (lower.includes('dns') || lower.includes('tcp connect') || lower.includes('socket'))
      return SshErrorCode.NETWORK_ERROR;
    if (lower.includes('authentication') || lower.includes('auth'))
      return SshErrorCode.AUTH_FAILED;
    if (lower.includes('handshake'))
      return SshErrorCode.HANDSHAKE_FAILED;
    if (lower.includes('channel') || lower.includes('shell') || lower.includes('pty'))
      return SshErrorCode.CHANNEL_ERROR;
    if (lower.includes('sftp'))
      return SshErrorCode.SFTP_ERROR;
    if (lower.includes('timeout') || lower.includes('timed out'))
      return SshErrorCode.TIMEOUT;
    return SshErrorCode.UNKNOWN;
  }

  /**
   * Connect to the remote host with password authentication.
   */
  async connect(host: string, port: number, user: string, pass: string,
                options?: SshConnectOptions): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.DISCONNECTED) throw new SshError('Client is already connected or connecting', SshErrorCode.UNKNOWN);

    this.state = SshState.CONNECTING;
    try {
      const timeout = options?.timeoutSec ?? 10;
      const keepalive = options?.keepaliveInterval ?? 30;
      await ssh.connect(this.handle, host, port, user, pass, timeout, keepalive);
      this.state = SshState.CONNECTED;
    } catch (err) {
      this.state = SshState.ERROR;
      const msg = (err as Error).message ?? 'SSH connect failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Connect to the remote host with public key authentication.
   */
  async connectWithKey(host: string, port: number, user: string, privateKey: string,
                       passphrase?: string, options?: SshConnectOptions): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.DISCONNECTED) throw new SshError('Client is already connected or connecting', SshErrorCode.UNKNOWN);

    this.state = SshState.CONNECTING;
    try {
      const timeout = options?.timeoutSec ?? 10;
      const keepalive = options?.keepaliveInterval ?? 30;
      await ssh.connectWithKey(this.handle, host, port, user, privateKey, passphrase, timeout, keepalive);
      this.state = SshState.CONNECTED;
    } catch (err) {
      this.state = SshState.ERROR;
      const msg = (err as Error).message ?? 'SSH key connect failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Get the SHA-256 fingerprint of the remote server's host key.
   * Must be called after a successful connect/connectWithKey.
   * Returns a 64-character lowercase hex string.
   */
  getHostKeyFingerprint(): string {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.CONNECTED) throw new SshError('Client is not connected', SshErrorCode.UNKNOWN);
    return ssh.getHostKeyFingerprint(this.handle);
  }

  /**
   * Execute a single command and return its output.
   * This uses a separate non-interactive channel.
   */
  async execCommand(command: string): Promise<string> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.CONNECTED) throw new SshError('Client is not connected', SshErrorCode.UNKNOWN);

    try {
      return await ssh.execCommand(this.handle, command);
    } catch (err) {
      const msg = (err as Error).message ?? 'Exec failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Start an interactive shell session.
   * Uses a PTY channel. Data from the server will be emitted via 'data' event.
   */
  async startShell(cols: number = 80, rows: number = 24): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.CONNECTED) throw new SshError('Client is not connected', SshErrorCode.UNKNOWN);

    const onData = (data: string) => {
      this.emit('data', data);
    };

    try {
      await ssh.startShell(this.handle, onData, cols, rows);
    } catch (err) {
      const msg = (err as Error).message ?? 'Shell start failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Write data (like keystrokes) to the interactive shell.
   */
  writeToShell(data: string): void {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.CONNECTED) throw new SshError('Client is not connected', SshErrorCode.UNKNOWN);

    try {
      ssh.writeToShell(this.handle, data);
    } catch (err) {
      const msg = (err as Error).message ?? 'Write failed';
      this.emit('error', new SshError(msg, this.classifyError(msg)));
    }
  }

  /**
   * Resize the interactive shell's pseudo-terminal (PTY).
   */
  resizePty(cols: number, rows: number): void {
    if (!this.handle) return;
    if (this.state !== SshState.CONNECTED) return;

    try {
      ssh.resizePty(this.handle, cols, rows);
    } catch (err) {
      const msg = (err as Error).message ?? 'Resize failed';
      this.emit('error', new SshError(msg, this.classifyError(msg)));
    }
  }

  // ===== SFTP Operations =====

  /**
   * Initialize the SFTP subsystem. Must be called after connect.
   */
  async sftpInit(): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    if (this.state !== SshState.CONNECTED) throw new SshError('Client is not connected', SshErrorCode.UNKNOWN);

    try {
      await ssh.sftpInit(this.handle);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP init failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Shutdown the SFTP subsystem.
   */
  async sftpShutdown(): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpShutdown(this.handle);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP shutdown failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * List directory contents.
   */
  async sftpListDir(path: string): Promise<SftpFileInfo[]> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      const json = await ssh.sftpListDir(this.handle, path);
      return JSON.parse(json) as SftpFileInfo[];
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP listDir failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Read a remote text file.
   */
  async sftpReadFile(remotePath: string): Promise<string> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      return await ssh.sftpReadFile(this.handle, remotePath);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP readFile failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Write text data to a remote file (creates or overwrites).
   */
  async sftpWriteFile(remotePath: string, data: string): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpWriteFile(this.handle, remotePath, data);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP writeFile failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Delete a remote file.
   */
  async sftpDelete(path: string): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpDelete(this.handle, path);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP delete failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Create a remote directory (mode 0755).
   */
  async sftpMkdir(path: string): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpMkdir(this.handle, path);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP mkdir failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Remove a remote directory (must be empty).
   */
  async sftpRmdir(path: string): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpRmdir(this.handle, path);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP rmdir failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Rename or move a remote file/directory.
   */
  async sftpRename(src: string, dst: string): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpRename(this.handle, src, dst);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP rename failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Get file/directory attributes.
   */
  async sftpStat(path: string): Promise<SftpFileInfo> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      const json = await ssh.sftpStat(this.handle, path);
      return JSON.parse(json) as SftpFileInfo;
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP stat failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Change permissions of a remote file or directory.
   * @param path Remote path
   * @param mode Permission bits (e.g. 0o755, 0o644)
   */
  async sftpChmod(path: string, mode: number): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpChmod(this.handle, path, mode);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP chmod failed';
      const error = new SshError(msg, this.classifyError(msg));
      this.emit('error', error);
      throw error;
    }
  }

  // ===== SFTP Phase 2: Binary & Streaming Operations =====

  /**
   * Open a remote file and return a file descriptor (fd).
   */
  async sftpOpenFile(path: string, flags: number, mode: number): Promise<number> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      return await ssh.sftpOpenFile(this.handle, path, flags, mode);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP openFile failed';
      throw new SshError(msg, this.classifyError(msg));
    }
  }

  /**
   * Read binary data from an open file descriptor.
   * Returns an ArrayBuffer. An empty ArrayBuffer (byteLength === 0) indicates EOF.
   */
  async sftpRead(fd: number, size: number = 32768): Promise<ArrayBuffer> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      return await ssh.sftpRead(this.handle, fd, size);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP read failed';
      throw new SshError(msg, this.classifyError(msg));
    }
  }

  /**
   * Write binary data to an open file descriptor.
   */
  async sftpWrite(fd: number, data: ArrayBuffer): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpWrite(this.handle, fd, data);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP write failed';
      throw new SshError(msg, this.classifyError(msg));
    }
  }

  /**
   * Close an open file descriptor.
   */
  async sftpCloseFile(fd: number): Promise<void> {
    if (!this.handle) throw new SshError('Client is disposed', SshErrorCode.DISPOSED);
    try {
      await ssh.sftpCloseFile(this.handle, fd);
    } catch (err) {
      const msg = (err as Error).message ?? 'SFTP closeFile failed';
      throw new SshError(msg, this.classifyError(msg));
    }
  }

  // --- High-level Binary Helpers ---

  /**
   * Read the entire contents of a remote file as an ArrayBuffer.
   */
  async sftpReadFileBytes(remotePath: string): Promise<ArrayBuffer> {
    // Open for reading
    const fd = await this.sftpOpenFile(remotePath, SftpOpenFlags.READ, 0);
    const chunks: Uint8Array[] = [];
    let totalLength = 0;

    try {
      while (true) {
        const buffer = await this.sftpRead(fd, 32768);
        if (buffer.byteLength === 0) break; // EOF
        
        const chunk = new Uint8Array(buffer);
        chunks.push(chunk);
        totalLength += chunk.length;
      }
    } finally {
      await this.sftpCloseFile(fd);
    }

    // Concatenate chunks
    const result = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return result.buffer;
  }

  /**
   * Write an ArrayBuffer to a remote file, overwriting it if it exists.
   */
  async sftpWriteFileBytes(remotePath: string, data: ArrayBuffer): Promise<void> {
    // Open for writing, create if not exists, truncate
    const flags = SftpOpenFlags.WRITE | SftpOpenFlags.CREAT | SftpOpenFlags.TRUNC;
    // Default mode: 0644 (rw-r--r--)
    const mode = 0x0180 | 0x0020 | 0x0004; // S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

    const fd = await this.sftpOpenFile(remotePath, flags, mode);
    try {
      const chunkSize = 32768;
      const uint8Data = new Uint8Array(data);
      for (let offset = 0; offset < uint8Data.length; offset += chunkSize) {
        const end = Math.min(offset + chunkSize, uint8Data.length);
        const chunk = uint8Data.buffer.slice(offset, end);
        await this.sftpWrite(fd, chunk);
      }
    } finally {
      await this.sftpCloseFile(fd);
    }
  }

  /**
   * Get the current connection state.
   */
  getState(): SshState {
    return this.state;
  }

  /**
   * Disconnect the session, stop all background threads, and free native resources.
   */
  dispose(): void {
    if (this.handle) {
      try {
        ssh.disconnect(this.handle);
      } catch (e) {
        // Ignore disconnect errors
      }
      this.handle = null;
    }
    this.state = SshState.DISCONNECTED;
    this.emit('close');
    this.removeAllListeners();
  }
}
