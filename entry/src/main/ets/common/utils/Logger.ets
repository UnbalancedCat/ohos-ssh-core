import { hilog } from "@kit.PerformanceAnalysisKit";

const DOMAIN = 0x0201;
const TAG = "SshApp";

/**
 * Common logging utility.
 */
export class Logger {
  static info(msg: string): void {
    hilog.info(DOMAIN, TAG, "%{public}s", msg);
  }

  static error(msg: string): void {
    hilog.error(DOMAIN, TAG, "%{public}s", msg);
  }
}
