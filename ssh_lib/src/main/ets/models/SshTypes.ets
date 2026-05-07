export enum SshState {
  DISCONNECTED = 'DISCONNECTED',
  CONNECTING = 'CONNECTING',
  CONNECTED = 'CONNECTED',
  ERROR = 'ERROR'
}

export interface SshConnectOptions {
  /** TCP + handshake timeout in seconds (default: 10) */
  timeoutSec?: number;
  /** Keepalive interval in seconds (default: 30, set 0 to disable) */
  keepaliveInterval?: number;
}
