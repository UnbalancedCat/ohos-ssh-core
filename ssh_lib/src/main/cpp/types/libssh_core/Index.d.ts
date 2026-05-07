// SSH context handle (opaque JS object, wraps C++ SshContext pointer)
export interface SshHandle {}

/** Create a new SSH context */
export function createContext(): SshHandle;

/** Connect to host and authenticate with password (async) */
export const connect: (handle: SshHandle, host: string, port: number, user: string, pass: string, timeoutSec?: number, keepaliveInterval?: number) => Promise<void>;
export const connectWithKey: (handle: SshHandle, host: string, port: number, user: string, privateKey: string, passphrase?: string, timeoutSec?: number, keepaliveInterval?: number) => Promise<void>;

/** Execute a single command and return its full output (async) */
export function execCommand(
  handle: SshHandle,
  command: string
): Promise<string>;

/** Start an interactive shell session with data callback (async) */
export function startShell(
  handle: SshHandle,
  onData: (data: string) => void,
  cols: number,
  rows: number
): Promise<void>;

/** Write data to the shell channel (sync) */
export function writeToShell(
  handle: SshHandle,
  data: string
): void;

/** Resize the PTY terminal (sync) */
export function resizePty(
  handle: SshHandle,
  cols: number,
  rows: number
): void;

/** Disconnect and release all resources (sync) */
export function disconnect(handle: SshHandle): void;

/** Get SHA-256 fingerprint of the server's host key (hex string, 64 chars). Must be called after connect. */
export function getHostKeyFingerprint(handle: SshHandle): string;

// ======= SFTP =======

export interface SftpFileInfo {
  name: string;
  size: number;
  permissions: number;
  mtime: number;
  isDir: boolean;
}

/** Initialize SFTP subsystem on the current session */
export function sftpInit(handle: SshHandle): Promise<void>;

/** Shutdown SFTP subsystem */
export function sftpShutdown(handle: SshHandle): Promise<void>;

/** List directory contents, returns JSON string */
export function sftpListDir(handle: SshHandle, path: string): Promise<string>;

/** Read a remote text file */
export function sftpReadFile(handle: SshHandle, remotePath: string): Promise<string>;

/** Write text data to a remote file (creates or overwrites) */
export function sftpWriteFile(handle: SshHandle, remotePath: string, data: string): Promise<void>;

/** Delete a remote file */
export function sftpDelete(handle: SshHandle, path: string): Promise<void>;

/** Create a remote directory */
export function sftpMkdir(handle: SshHandle, path: string): Promise<void>;

/** Remove a remote directory (must be empty) */
export function sftpRmdir(handle: SshHandle, path: string): Promise<void>;

/** Rename or move a remote file/directory */
export function sftpRename(handle: SshHandle, src: string, dst: string): Promise<void>;

/** Get file/directory attributes, returns JSON string */
export function sftpStat(handle: SshHandle, path: string): Promise<string>;

/** Set file permissions (chmod) */
export function sftpChmod(handle: SshHandle, path: string, mode: number): Promise<void>;

/** Open a file and return a file descriptor */
export function sftpOpenFile(handle: SshHandle, path: string, flags: number, mode: number): Promise<number>;

/** Read data from an open file descriptor */
export function sftpRead(handle: SshHandle, fd: number, size: number): Promise<ArrayBuffer>;

/** Write data to an open file descriptor */
export function sftpWrite(handle: SshHandle, fd: number, data: ArrayBuffer): Promise<void>;

/** Close an open file descriptor */
export function sftpCloseFile(handle: SshHandle, fd: number): Promise<void>;
