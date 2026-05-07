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
