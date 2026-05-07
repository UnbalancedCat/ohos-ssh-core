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
