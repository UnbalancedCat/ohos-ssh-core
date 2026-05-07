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

  /**
   * Connect to the remote host with password authentication.
   */
  async connect(host: string, port: number, user: string, pass: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.DISCONNECTED) throw new Error('Client is already connected or connecting');

    this.state = SshState.CONNECTING;
    try {
      await ssh.connect(this.handle, host, port, user, pass);
      this.state = SshState.CONNECTED;
    } catch (err) {
      this.state = SshState.ERROR;
      const error = new Error((err as Error).message ?? 'SSH connect failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Connect to the remote host with public key authentication.
   */
  async connectWithKey(host: string, port: number, user: string, privateKey: string, passphrase?: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.DISCONNECTED) throw new Error('Client is already connected or connecting');

    this.state = SshState.CONNECTING;
    try {
      await ssh.connectWithKey(this.handle, host, port, user, privateKey, passphrase);
      this.state = SshState.CONNECTED;
    } catch (err) {
      this.state = SshState.ERROR;
      const error = new Error((err as Error).message ?? 'SSH key connect failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Execute a single command and return its output.
   * This uses a separate non-interactive channel.
   */
  async execCommand(command: string): Promise<string> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.CONNECTED) throw new Error('Client is not connected');

    try {
      return await ssh.execCommand(this.handle, command);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'Exec failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Start an interactive shell session.
   * Uses a PTY channel. Data from the server will be emitted via 'data' event.
   */
  async startShell(cols: number = 80, rows: number = 24): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.CONNECTED) throw new Error('Client is not connected');

    const onData = (data: string) => {
      this.emit('data', data);
    };

    try {
      await ssh.startShell(this.handle, onData, cols, rows);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'Shell start failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Write data (like keystrokes) to the interactive shell.
   */
  writeToShell(data: string): void {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.CONNECTED) throw new Error('Client is not connected');

    try {
      ssh.writeToShell(this.handle, data);
    } catch (err) {
      this.emit('error', new Error((err as Error).message ?? 'Write failed'));
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
      this.emit('error', new Error((err as Error).message ?? 'Resize failed'));
    }
  }

  // ===== SFTP Operations =====

  /**
   * Initialize the SFTP subsystem. Must be called after connect.
   */
  async sftpInit(): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.CONNECTED) throw new Error('Client is not connected');

    try {
      await ssh.sftpInit(this.handle);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP init failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Shutdown the SFTP subsystem.
   */
  async sftpShutdown(): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpShutdown(this.handle);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP shutdown failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * List directory contents.
   */
  async sftpListDir(path: string): Promise<SftpFileInfo[]> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      const json = await ssh.sftpListDir(this.handle, path);
      return JSON.parse(json) as SftpFileInfo[];
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP listDir failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Read a remote text file.
   */
  async sftpReadFile(remotePath: string): Promise<string> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      return await ssh.sftpReadFile(this.handle, remotePath);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP readFile failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Write text data to a remote file (creates or overwrites).
   */
  async sftpWriteFile(remotePath: string, data: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpWriteFile(this.handle, remotePath, data);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP writeFile failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Delete a remote file.
   */
  async sftpDelete(path: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpDelete(this.handle, path);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP delete failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Create a remote directory (mode 0755).
   */
  async sftpMkdir(path: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpMkdir(this.handle, path);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP mkdir failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Remove a remote directory (must be empty).
   */
  async sftpRmdir(path: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpRmdir(this.handle, path);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP rmdir failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Rename or move a remote file/directory.
   */
  async sftpRename(src: string, dst: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpRename(this.handle, src, dst);
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP rename failed');
      this.emit('error', error);
      throw error;
    }
  }

  /**
   * Get file/directory attributes.
   */
  async sftpStat(path: string): Promise<SftpFileInfo> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      const json = await ssh.sftpStat(this.handle, path);
      return JSON.parse(json) as SftpFileInfo;
    } catch (err) {
      const error = new Error((err as Error).message ?? 'SFTP stat failed');
      this.emit('error', error);
      throw error;
    }
  }

  // ===== SFTP Phase 2: Binary & Streaming Operations =====

  /**
   * Open a remote file and return a file descriptor (fd).
   */
  async sftpOpenFile(path: string, flags: number, mode: number): Promise<number> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      return await ssh.sftpOpenFile(this.handle, path, flags, mode);
    } catch (err) {
      throw new Error((err as Error).message ?? 'SFTP openFile failed');
    }
  }

  /**
   * Read binary data from an open file descriptor.
   * Returns an ArrayBuffer. An empty ArrayBuffer (byteLength === 0) indicates EOF.
   */
  async sftpRead(fd: number, size: number = 32768): Promise<ArrayBuffer> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      return await ssh.sftpRead(this.handle, fd, size);
    } catch (err) {
      throw new Error((err as Error).message ?? 'SFTP read failed');
    }
  }

  /**
   * Write binary data to an open file descriptor.
   */
  async sftpWrite(fd: number, data: ArrayBuffer): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpWrite(this.handle, fd, data);
    } catch (err) {
      throw new Error((err as Error).message ?? 'SFTP write failed');
    }
  }

  /**
   * Close an open file descriptor.
   */
  async sftpCloseFile(fd: number): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    try {
      await ssh.sftpCloseFile(this.handle, fd);
    } catch (err) {
      throw new Error((err as Error).message ?? 'SFTP closeFile failed');
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
