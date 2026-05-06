// SSH context handle (opaque JS object, wraps C++ SshContext pointer)
export interface SshHandle {}

/** Create a new SSH context */
export function createContext(): SshHandle;

/** Connect to host and authenticate with password (async) */
export function connect(
  handle: SshHandle,
  host: string,
  port: number,
  username: string,
  password: string
): Promise<void>;

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
