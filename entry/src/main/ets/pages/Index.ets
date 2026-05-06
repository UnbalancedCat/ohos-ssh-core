import { hilog } from '@kit.PerformanceAnalysisKit';
import ssh from 'libssh_core.so';
import { SshHandle } from 'libssh_core.so';

const DOMAIN = 0x0201;
const TAG = 'SshSmokeTest';

@Entry
@Component
struct Index {
  // Connection parameters
  @State host: string = '192.168.1.1';
  @State port: string = '22';
  @State username: string = 'root';
  @State password: string = '';

  // State
  @State logs: string[] = [];
  @State isConnected: boolean = false;
  @State shellStarted: boolean = false;
  @State shellInput: string = '';
  @State hasHandle: boolean = false;

  private handle: SshHandle | null = null;
  private scroller: Scroller = new Scroller();

  private scrollToBottom() {
    setTimeout(() => {
      this.scroller.scrollEdge(Edge.Bottom);
    }, 50);
  }

  private log(msg: string) {
    const time = new Date().toLocaleTimeString();
    const line = `[${time}] ${msg}`;
    this.logs = [...this.logs, line];
    hilog.info(DOMAIN, TAG, '%{public}s', msg);
    this.scrollToBottom();
  }

  private logError(msg: string) {
    const time = new Date().toLocaleTimeString();
    const line = `[${time}] ❌ ${msg}`;
    this.logs = [...this.logs, line];
    hilog.error(DOMAIN, TAG, '%{public}s', msg);
    this.scrollToBottom();
  }

  private parseTerminalData(text: string): string[] {
    // 1. Remove ANSI color/control codes, including OSC (Operating System Commands like window titles) and BELL characters
    const ansiRegex = /\x1B\[[\x30-\x3F]*[\x20-\x2F]*[\x40-\x7E]|\x1B\][^\x07\x1B]*(?:\x07|\x1B\\)|\x1B[\x20-\x2F]*[\x40-\x7E]|\x07/g;
    let cleanText = text.replace(ansiRegex, '');

    // 2. Replace tabs with 4 spaces
    cleanText = cleanText.replace(/\t/g, '    ');

    // 3. Handle backspaces (\b, which is \x08)
    while (cleanText.includes('\x08')) {
      cleanText = cleanText.replace(/[^\x08]\x08/g, '');
      cleanText = cleanText.replace(/^\x08+/g, '');
    }

    // 4. Standardize newlines (convert \r\n to \n first)
    cleanText = cleanText.replace(/\r\n/g, '\n');

    // 5. Split by '\n' and then process '\r'
    let lines = cleanText.split('\n');
    lines = lines.map(line => {
      // If line contains standalone '\r', simulate carriage return (keep text after last '\r')
      const rIndex = line.lastIndexOf('\r');
      if (rIndex !== -1) {
        return line.substring(rIndex + 1);
      }
      return line;
    });

    return lines;
  }

  build() {
    Column() {
      // Title
      Text('SSH Core Smoke Test')
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .margin({ top: 16, bottom: 12 })

      // ===== Connection form =====
      Column() {
        Row() {
          Text('Host:').width(80).fontSize(14)
          TextInput({ text: this.host })
            .onChange((val: string) => { this.host = val; })
            .layoutWeight(1)
            .height(36)
          Text(':').fontSize(14).margin({ left: 4, right: 4 })
          TextInput({ text: this.port })
            .onChange((val: string) => { this.port = val; })
            .width(60)
            .height(36)
            .type(InputType.Number)
        }.width('100%').margin({ bottom: 6 })

        Row() {
          Text('User:').width(80).fontSize(14)
          TextInput({ text: this.username })
            .onChange((val: string) => { this.username = val; })
            .layoutWeight(1)
            .height(36)
        }.width('100%').margin({ bottom: 6 })

        Row() {
          Text('Pass:').width(80).fontSize(14)
          TextInput({ text: this.password })
            .onChange((val: string) => { this.password = val; })
            .layoutWeight(1)
            .height(36)
            .type(InputType.Password)
        }.width('100%').margin({ bottom: 8 })
      }
      .padding(12)
      .width('100%')

      // ===== Test buttons =====
      Column({ space: 8 }) {
        // Row 1: Context + Connect + Disconnect
        Row({ space: 8 }) {
          Button('1. Create')
            .fontSize(13)
            .layoutWeight(1)
            .onClick(() => {
              try {
                this.handle = ssh.createContext();
                this.hasHandle = true;
                this.log('✅ createContext OK');
              } catch (e) {
                this.logError('createContext: ' + JSON.stringify(e));
              }
            })

          Button('2. Connect')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.hasHandle && !this.isConnected)
            .onClick(async () => {
              if (!this.handle) return;
              this.log(`Connecting to ${this.host}:${this.port}...`);
              try {
                await ssh.connect(
                  this.handle,
                  this.host,
                  parseInt(this.port),
                  this.username,
                  this.password
                );
                this.isConnected = true;
                this.log('✅ connect OK — authenticated');
              } catch (e) {
                this.logError('connect: ' + (e as Error).message);
              }
            })

          Button('Disconnect')
            .fontSize(13)
            .layoutWeight(1)
            .fontColor(Color.White)
            .backgroundColor(Color.Red)
            .enabled(this.hasHandle)
            .onClick(() => {
              if (!this.handle) return;
              try {
                ssh.disconnect(this.handle);
                this.isConnected = false;
                this.shellStarted = false;
                this.handle = null;
                this.hasHandle = false;
                this.log('✅ disconnect OK');
              } catch (e) {
                this.logError('disconnect: ' + JSON.stringify(e));
              }
            })
        }.width('100%')

        // Row 2: Exec command
        Row({ space: 8 }) {
          Button('3. Exec "uname -a"')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              if (!this.handle) return;
              this.log('Executing: uname -a');
              try {
                const output = await ssh.execCommand(this.handle, 'uname -a');
                const lines = this.parseTerminalData(output);
                lines.forEach(line => {
                  if (line) this.log('📤 Output: ' + line);
                });
              } catch (e) {
                this.logError('exec: ' + (e as Error).message);
              }
            })

          Button('Exec "ls /"')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              if (!this.handle) return;
              this.log('Executing: ls /');
              try {
                const output = await ssh.execCommand(this.handle, 'ls /');
                const lines = this.parseTerminalData(output);
                lines.forEach(line => {
                  if (line) this.log('📤 Output: ' + line);
                });
              } catch (e) {
                this.logError('exec: ' + (e as Error).message);
              }
            })
        }.width('100%')

        // Row 3: Shell
        Row({ space: 8 }) {
          Button('4. Start Shell')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected && !this.shellStarted)
            .onClick(async () => {
              if (!this.handle) return;
              this.log('Starting shell (80x24)...');
              try {
                await ssh.startShell(this.handle, (data: string) => {
                  const lines = this.parseTerminalData(data);
                  lines.forEach(line => {
                    if (line) this.log('🖥️ ' + line);
                  });
                }, 80, 24);
                this.shellStarted = true;
                this.log('✅ startShell OK — reading data');
              } catch (e) {
                this.logError('shell: ' + (e as Error).message);
              }
            })
        }.width('100%')

        // Row 4: Shell input
        if (this.shellStarted) {
          Row({ space: 8 }) {
            TextInput({ placeholder: 'Type command...' , text: this.shellInput })
              .onChange((val: string) => { this.shellInput = val; })
              .layoutWeight(1)
              .height(36)
            Button('Send')
              .fontSize(13)
              .onClick(() => {
                if (!this.handle || !this.shellInput) return;
                try {
                  ssh.writeToShell(this.handle, this.shellInput + '\n');
                  this.log('⌨️ Sent: ' + this.shellInput);
                  this.shellInput = '';
                } catch (e) {
                  this.logError('write: ' + JSON.stringify(e));
                }
              })
          }.width('100%')
        }
      }
      .padding({ left: 12, right: 12 })
      .width('100%')

      // ===== Log output =====
      Text('— Log Output —')
        .fontSize(13)
        .fontColor(Color.Gray)
        .margin({ top: 12, bottom: 4 })

      Scroll(this.scroller) {
        Column() {
          ForEach(this.logs, (line: string, idx: number) => {
            Text(line)
              .fontSize(11)
              .fontFamily('monospace')
              .fontColor(line.includes('❌') ? Color.Red : Color.Black)
              .width('100%')
              .padding({ left: 8, right: 8, top: 1, bottom: 1 })
          })
        }.width('100%')
      }
      .layoutWeight(1)
      .width('95%')
      .backgroundColor('#F5F5F5')
      .borderRadius(8)
      .margin({ left: 12, right: 12, bottom: 12 })

      // Clear log button
      Button('Clear Log')
        .fontSize(13)
        .margin({ bottom: 16 })
        .onClick(() => { this.logs = []; })
    }
    .height('100%')
    .width('100%')
  }
}
