import { SshClient, SshState, SftpFileInfo } from 'ssh_lib';
import { Logger } from '../common/utils/Logger';
import { TerminalParser } from '../common/utils/TerminalParser';
import { CollapsibleSection } from '../view/CollapsibleSection';
import { ConnectionForm } from '../view/ConnectionForm';
import { ShellPanel } from '../view/ShellPanel';
import { SftpPanel } from '../view/SftpPanel';
import { LogConsole } from '../view/LogConsole';

@Entry
@Component
struct Index {
  // Connection parameters
  @State host: string = '192.168.1.1';
  @State port: string = '22';
  @State username: string = 'root';
  @State password: string = '';
  @State privateKey: string = '';
  @State passphrase: string = '';

  // Connection state
  @State @Watch('onConnectionStateChange') isConnected: boolean = false;
  @State shellStarted: boolean = false;
  @State shellInput: string = '';

  // Collapsible section state
  @State expandConnection: boolean = true;

  // Bottom panel mode: 'none' | 'shell' | 'sftp' — mutually exclusive
  @State activeMode: string = 'none';

  // SFTP state
  @State sftpInitialized: boolean = false;
  @State sftpCurrentPath: string = '/';

  // Log
  @State logs: string[] = [];

  private client: SshClient = new SshClient();

  aboutToDisappear() {
    this.client.dispose();
  }

  onConnectionStateChange() {
    if (this.isConnected) {
      this.expandConnection = false;
      // Don't auto-select mode, let user choose
    } else {
      this.expandConnection = true;
      this.activeMode = 'none';
    }
  }

  // --- Logging ---

  private log(msg: string) {
    const time = new Date().toLocaleTimeString();
    this.logs = [...this.logs, `[${time}] ${msg}`];
    Logger.info(msg);
  }

  private logError(msg: string) {
    const time = new Date().toLocaleTimeString();
    this.logs = [...this.logs, `[${time}] [ERROR] ${msg}`];
    Logger.error(msg);
  }

  // --- Connection Actions ---

  private async handleConnectPass() {
    this.log(`Connecting to ${this.host}:${this.port} (Password)...`);
    try {
      if (this.client.getState() !== SshState.DISCONNECTED) {
        this.client.dispose();
      }
      this.client = new SshClient();
      await this.client.connect(this.host, parseInt(this.port), this.username, this.password);
      this.isConnected = true;
      const fp = this.client.getHostKeyFingerprint();
      this.log('[SUCCESS] connect OK - authenticated');
      this.log(`[Fingerprint] SHA-256: ${fp}`);
    } catch (e) {
      this.logError('connect: ' + (e as Error).message);
    }
  }

  private async handleConnectKey() {
    this.log(`Connecting to ${this.host}:${this.port} (Key)...`);
    try {
      if (this.client.getState() !== SshState.DISCONNECTED) {
        this.client.dispose();
      }
      this.client = new SshClient();
      await this.client.connectWithKey(
        this.host, parseInt(this.port), this.username,
        this.privateKey, this.passphrase
      );
      this.isConnected = true;
      const fp = this.client.getHostKeyFingerprint();
      this.log('[SUCCESS] connectWithKey OK - authenticated');
      this.log(`[Fingerprint] SHA-256: ${fp}`);
    } catch (e) {
      this.logError('connectWithKey: ' + (e as Error).message);
    }
  }

  private handleDisconnect() {
    try {
      this.client.dispose();
      this.isConnected = false;
      this.shellStarted = false;
      this.sftpInitialized = false;
      this.sftpCurrentPath = '/';
      this.activeMode = 'none';
      this.log('[SUCCESS] disconnect OK');
    } catch (e) {
      this.logError('disconnect: ' + JSON.stringify(e));
    }
  }

  // --- Shell Actions ---

  private async handleExecUname() {
    this.log('Executing: uname -a');
    try {
      const output = await this.client.execCommand('uname -a');
      TerminalParser.parseTerminalData(output).forEach((line) => {
        if (line) this.log('[Output] ' + line);
      });
    } catch (e) {
      this.logError('exec: ' + (e as Error).message);
    }
  }

  private async handleExecLs() {
    this.log('Executing: ls /');
    try {
      const output = await this.client.execCommand('ls /');
      TerminalParser.parseTerminalData(output).forEach((line) => {
        if (line) this.log('[Output] ' + line);
      });
    } catch (e) {
      this.logError('exec: ' + (e as Error).message);
    }
  }

  private async handleStartShell() {
    this.log('Starting shell (80x24)...');
    try {
      this.client.on('data', (data) => {
        if (typeof data === 'string') {
          TerminalParser.parseTerminalData(data).forEach((line) => {
            if (line) this.log('[Shell] ' + line);
          });
        }
      });
      await this.client.startShell(80, 24);
      this.shellStarted = true;
      this.log('[SUCCESS] startShell OK - reading data');
    } catch (e) {
      this.logError('shell: ' + (e as Error).message);
    }
  }

  private handleStopShell() {
    this.log('Stopping shell...');
    try {
      // Dispose and recreate client to cleanly stop shell
      // Keep connection info but need to reconnect
      this.client.dispose();
      this.shellStarted = false;
      this.isConnected = false;
      this.sftpInitialized = false;
      this.activeMode = 'none';
      this.log('[SUCCESS] Shell stopped. Please reconnect.');
    } catch (e) {
      this.logError('stopShell: ' + (e as Error).message);
    }
  }

  private handleSendShell() {
    if (!this.shellInput) return;
    this.log('[Input] ' + this.shellInput);
    try {
      this.client.writeToShell(this.shellInput + '\n');
      this.shellInput = '';
    } catch (e) {
      this.logError('send: ' + (e as Error).message);
    }
  }

  // --- SFTP Actions ---

  private async handleSftpInit() {
    this.log('Initializing SFTP...');
    try {
      await this.client.sftpInit();
      this.sftpInitialized = true;
      this.sftpCurrentPath = '/';
      this.log('[SUCCESS] SFTP initialized.');
    } catch (e) {
      this.logError('sftpInit: ' + (e as Error).message);
    }
  }

  private async handleSftpShutdown() {
    this.log('Shutting down SFTP...');
    try {
      await this.client.sftpShutdown();
      this.sftpInitialized = false;
      this.log('[SUCCESS] SFTP shut down.');
    } catch (e) {
      this.logError('sftpShutdown: ' + (e as Error).message);
    }
  }

  private async handleSftpList() {
    this.log(`Listing SFTP ${this.sftpCurrentPath} ...`);
    try {
      const files: SftpFileInfo[] = await this.client.sftpListDir(this.sftpCurrentPath);
      this.log(`[SUCCESS] Found ${files.length} items.`);
      files.forEach(f => {
        const type = f.isDir ? 'DIR' : 'FILE';
        this.log(`  [${type}] ${f.name} (size: ${f.size})`);
      });
    } catch (e) {
      this.logError('sftpListDir: ' + (e as Error).message);
    }
  }

  private handleSftpNavigate(target: string) {
    if (target.startsWith('/')) {
      this.sftpCurrentPath = target;
    } else {
      if (this.sftpCurrentPath.endsWith('/')) {
        this.sftpCurrentPath = this.sftpCurrentPath + target;
      } else {
        this.sftpCurrentPath = this.sftpCurrentPath + '/' + target;
      }
    }
    const parts = this.sftpCurrentPath.split('/').filter(p => p !== '');
    const resolved: string[] = [];
    for (const part of parts) {
      if (part === '..') {
        if (resolved.length > 0) resolved.pop();
      } else if (part !== '.') {
        resolved.push(part);
      }
    }
    this.sftpCurrentPath = '/' + resolved.join('/');
    this.log(`[SFTP] Navigate → ${this.sftpCurrentPath}`);
    this.handleSftpList();
  }

  private async handleSftpRead() {
    this.log('Reading /etc/hostname ...');
    try {
      const content = await this.client.sftpReadFile('/etc/hostname');
      this.log(`[SUCCESS] File content:\n${content}`);
    } catch (e) {
      this.logError('sftpReadFile: ' + (e as Error).message);
    }
  }

  private async handleSftpBinaryRead() {
    this.log('Binary streaming /bin/ls ...');
    try {
      const startTime = Date.now();
      const buffer = await this.client.sftpReadFileBytes('/bin/ls');
      const elapsed = Date.now() - startTime;
      const size = buffer.byteLength;
      const view = new Uint8Array(buffer);
      const magic = size >= 4
        ? `0x${view[0].toString(16)}${view[1].toString(16)}${view[2].toString(16)}${view[3].toString(16)}`
        : 'unknown';
      this.log(`[SUCCESS] Read ${size} bytes in ${elapsed}ms.`);
      this.log(`[SFTP] Magic bytes: ${magic}`);
    } catch (e) {
      this.logError('sftpReadFileBytes: ' + (e as Error).message);
    }
  }

  // --- Build ---

  build() {
    Column() {
      // ===== Title =====
      Text($r('app.string.app_title'))
        .fontSize($r('app.float.title_font_size'))
        .fontWeight(FontWeight.Bold)
        .fontColor($r('app.color.title_text'))
        .margin({ top: 8, bottom: 6 })

      // ===== Connection Section (collapsible, at top) =====
      Column() {
        CollapsibleSection({
          title: $r('app.string.section_connection'),
          expanded: this.expandConnection,
          onToggle: () => { this.expandConnection = !this.expandConnection; }
        }) {
          ConnectionForm({
            host: $host,
            port: $port,
            username: $username,
            password: $password,
            privateKey: $privateKey,
            passphrase: $passphrase,
            isConnected: this.isConnected,
            onConnectPass: (): Promise<void> => this.handleConnectPass(),
            onConnectKey: (): Promise<void> => this.handleConnectKey(),
            onDisconnect: (): void => this.handleDisconnect()
          })
        }
      }
      .padding({ left: $r('app.float.page_padding'), right: $r('app.float.page_padding') })
      .width('100%')

      // ===== Log area (fills remaining space) =====
      LogConsole({ logs: $logs })

      // ===== Bottom control bar =====
      Column() {
        // Mode tabs + Clear
        Row({ space: 6 }) {
          Button($r('app.string.tab_shell'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .layoutWeight(1)
            .fontColor(this.activeMode === 'shell'
              ? $r('app.color.tab_active_text')
              : $r('app.color.tab_inactive_text'))
            .backgroundColor(this.activeMode === 'shell'
              ? $r('app.color.tab_active_bg')
              : $r('app.color.tab_inactive_bg'))
            .enabled(this.isConnected)
            .onClick(() => {
              this.activeMode = this.activeMode === 'shell' ? 'none' : 'shell';
            })
            .borderRadius($r('app.float.section_border_radius'))
          Button($r('app.string.tab_sftp'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .layoutWeight(1)
            .fontColor(this.activeMode === 'sftp'
              ? $r('app.color.tab_active_text')
              : $r('app.color.tab_inactive_text'))
            .backgroundColor(this.activeMode === 'sftp'
              ? $r('app.color.tab_active_bg')
              : $r('app.color.tab_inactive_bg'))
            .enabled(this.isConnected && !this.shellStarted)
            .onClick(() => {
              this.activeMode = this.activeMode === 'sftp' ? 'none' : 'sftp';
            })
            .borderRadius($r('app.float.section_border_radius'))
          Button($r('app.string.btn_clear_log'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .width($r('app.float.send_btn_width'))
            .onClick(() => { this.logs = []; })
        }
        .width('100%')
        .padding({ top: 4, bottom: 10 })

        // Active panel content
        if (this.activeMode === 'shell') {
          ShellPanel({
            isConnected: this.isConnected,
            shellStarted: this.shellStarted,
            shellInput: $shellInput,
            onExecUname: (): Promise<void> => this.handleExecUname(),
            onExecLs: (): Promise<void> => this.handleExecLs(),
            onStartShell: (): Promise<void> => this.handleStartShell(),
            onStopShell: (): void => this.handleStopShell(),
            onSendShell: (): void => this.handleSendShell()
          })
        } else if (this.activeMode === 'sftp') {
          SftpPanel({
            isConnected: this.isConnected,
            sftpInitialized: this.sftpInitialized,
            currentPath: this.sftpCurrentPath,
            onSftpInit: (): Promise<void> => this.handleSftpInit(),
            onSftpShutdown: (): Promise<void> => this.handleSftpShutdown(),
            onSftpList: (): Promise<void> => this.handleSftpList(),
            onSftpNavigate: (target: string): void => this.handleSftpNavigate(target),
            onSftpRead: (): Promise<void> => this.handleSftpRead(),
            onSftpBinaryRead: (): Promise<void> => this.handleSftpBinaryRead()
          })
        }
      }
      .padding({ left: $r('app.float.page_padding'), right: $r('app.float.page_padding'), top: 6, bottom: 8 })
      .width('100%')
    }
    .height('100%')
    .width('100%')
    .backgroundColor($r('app.color.page_bg'))
  }
}
