import ssh from 'libssh_core.so';
import type { SshHandle } from 'libssh_core.so';
import { EventEmitter } from './EventEmitter';

export enum SshState {
  DISCONNECTED = 'DISCONNECTED',
  CONNECTING = 'CONNECTING',
  CONNECTED = 'CONNECTED',
  ERROR = 'ERROR'
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
  async connectWithKey(host: string, port: number, user: string, publicKey: string, privateKey: string, passphrase?: string): Promise<void> {
    if (!this.handle) throw new Error('Client is disposed');
    if (this.state !== SshState.DISCONNECTED) throw new Error('Client is already connected or connecting');

    this.state = SshState.CONNECTING;
    try {
      await ssh.connectWithKey(this.handle, host, port, user, publicKey, privateKey, passphrase);
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
