import { hilog } from "@kit.PerformanceAnalysisKit";
import { SshClient, SshState, SftpFileInfo } from "ssh_lib";

const DOMAIN = 0x0201;
const TAG = "SshSmokeTest";

@Entry
@Component
struct Index {
  // Connection parameters
  @State host: string = "192.168.1.1";
  @State port: string = "22";
  @State username: string = "root";
  @State password: string = "";
  @State privateKey: string = "";
  @State passphrase: string = "";

  // State
  @State logs: string[] = [];
  @State isConnected: boolean = false;
  @State shellStarted: boolean = false;
  @State shellInput: string = "";

  private client: SshClient = new SshClient();
  private scroller: Scroller = new Scroller();

  aboutToDisappear() {
    this.client.dispose();
  }

  private scrollToBottom() {
    setTimeout(() => {
      this.scroller.scrollEdge(Edge.Bottom);
    }, 50);
  }

  private log(msg: string) {
    const time = new Date().toLocaleTimeString();
    const line = `[${time}] ${msg}`;
    this.logs = [...this.logs, line];
    hilog.info(DOMAIN, TAG, "%{public}s", msg);
    this.scrollToBottom();
  }

  private logError(msg: string) {
    const time = new Date().toLocaleTimeString();
    const line = `[${time}] [ERROR] ${msg}`;
    this.logs = [...this.logs, line];
    hilog.error(DOMAIN, TAG, "%{public}s", msg);
    this.scrollToBottom();
  }

  private parseTerminalData(text: string): string[] {
    // 1. Remove ANSI color/control codes, including OSC (Operating System Commands like window titles) and BELL characters
    const ansiRegex =
      /\x1B\[[\x30-\x3F]*[\x20-\x2F]*[\x40-\x7E]|\x1B\][^\x07\x1B]*(?:\x07|\x1B\\)|\x1B[\x20-\x2F]*[\x40-\x7E]|\x07/g;
    let cleanText = text.replace(ansiRegex, "");

    // 2. Replace tabs with 4 spaces
    cleanText = cleanText.replace(/\t/g, "    ");

    // 3. Handle backspaces (\b, which is \x08)
    while (cleanText.includes("\x08")) {
      cleanText = cleanText.replace(/[^\x08]\x08/g, "");
      cleanText = cleanText.replace(/^\x08+/g, "");
    }

    // 4. Standardize newlines (convert \r\n to \n first)
    cleanText = cleanText.replace(/\r\n/g, "\n");

    // 5. Split by '\n' and then process '\r'
    let lines = cleanText.split("\n");
    lines = lines.map((line) => {
      // If line contains standalone '\r', simulate carriage return (keep text after last '\r')
      const rIndex = line.lastIndexOf("\r");
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
      Text("SSH Core Smoke Test (OOP)")
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
        .margin({ top: 16, bottom: 12 })

      // ===== Connection form =====
      Column() {
        Row() {
          Text("Host:")
            .width(80)
            .fontSize(14)
          TextInput({ text: this.host })
            .onChange((val: string) => {
              this.host = val;
            })
            .layoutWeight(1)
            .height(36)
          Text(":")
            .fontSize(14)
            .margin({ left: 4, right: 4 })
          TextInput({ text: this.port })
            .onChange((val: string) => {
              this.port = val;
            })
            .width(60)
            .height(36)
            .type(InputType.Number)
        }
        .width("100%")
        .margin({ bottom: 6 })
        Row() {
          Text("User:")
            .width(80)
            .fontSize(14)
          TextInput({ text: this.username })
            .onChange((val: string) => {
              this.username = val;
            })
            .layoutWeight(1)
            .height(36)
        }
        .width("100%")
        .margin({ bottom: 6 })
        Row() {
          Text("Pass:")
            .width(80)
            .fontSize(14)
          TextInput({ text: this.password })
            .onChange((val: string) => {
              this.password = val;
            })
            .layoutWeight(1)
            .height(36)
            .type(InputType.Password)
        }
        .width("100%")
        .margin({ bottom: 8 })
        Row() {
          Text("Key:")
            .width(80)
            .fontSize(14)
          TextArea({ text: this.privateKey, placeholder: "-----BEGIN PRIVATE KEY-----..." })
            .onChange((val: string) => {
              this.privateKey = val;
            })
            .layoutWeight(1)
            .height(60)
            .fontSize(12)
        }
        .width("100%")
        .margin({ bottom: 6 })
        Row() {
          Text("Passphrase:")
            .width(80)
            .fontSize(14)
          TextInput({ text: this.passphrase, placeholder: "Optional" })
            .onChange((val: string) => {
              this.passphrase = val;
            })
            .layoutWeight(1)
            .height(36)
            .type(InputType.Password)
        }
        .width("100%")
        .margin({ bottom: 8 })
      }
      .padding(12)
      .width("100%")

      // ===== Test buttons =====
      Column({ space: 8 }) {
        // Row 1: Connect
        Row({ space: 8 }) {
          Button("Connect (Pass)")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(!this.isConnected)
            .onClick(async () => {
              this.log(`Connecting to ${this.host}:${this.port} (Password)...`);
              try {
                if (this.client.getState() !== SshState.DISCONNECTED) {
                  this.client.dispose();
                }
                this.client = new SshClient();
                await this.client.connect(
                  this.host,
                  parseInt(this.port),
                  this.username,
                  this.password,
                );
                this.isConnected = true;
                const fp = this.client.getHostKeyFingerprint();
                this.log("[SUCCESS] connect OK - authenticated");
                this.log(`[Fingerprint] SHA-256: ${fp}`);
              } catch (e) {
                this.logError("connect: " + (e as Error).message);
              }
            })
          Button("Connect (Key)")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(!this.isConnected)
            .onClick(async () => {
              this.log(`Connecting to ${this.host}:${this.port} (Key)...`);
              try {
                if (this.client.getState() !== SshState.DISCONNECTED) {
                  this.client.dispose();
                }
                this.client = new SshClient();
                await this.client.connectWithKey(
                  this.host,
                  parseInt(this.port),
                  this.username,
                  this.privateKey,
                  this.passphrase,
                );
                this.isConnected = true;
                const fp = this.client.getHostKeyFingerprint();
                this.log("[SUCCESS] connectWithKey OK - authenticated");
                this.log(`[Fingerprint] SHA-256: ${fp}`);
              } catch (e) {
                this.logError("connectWithKey: " + (e as Error).message);
              }
            })
        }.width("100%")
        Row({ space: 8 }) {
          Button("Disconnect")
            .fontSize(13)
            .layoutWeight(1)
            .fontColor(Color.White)
            .backgroundColor(Color.Red)
            .enabled(this.isConnected)
            .onClick(() => {
              try {
                this.client.dispose();
                this.isConnected = false;
                this.shellStarted = false;
                this.log("[SUCCESS] disconnect OK");
              } catch (e) {
                this.logError("disconnect: " + JSON.stringify(e));
              }
            })
        }.width("100%")

        // Row 2: Exec command
        Row({ space: 8 }) {
          Button('Exec "uname -a"')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              this.log("Executing: uname -a");
              try {
                const output: string = await this.client.execCommand("uname -a");
                const lines = this.parseTerminalData(output);
                lines.forEach((line) => {
                  if (line) this.log("[Output] " + line);
                });
              } catch (e) {
                this.logError("exec: " + (e as Error).message);
              }
            })
          Button('Exec "ls /"')
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              this.log("Executing: ls /");
              try {
                const output: string = await this.client.execCommand("ls /");
                const lines = this.parseTerminalData(output);
                lines.forEach((line) => {
                  if (line) this.log("[Output] " + line);
                });
              } catch (e) {
                this.logError("exec: " + (e as Error).message);
              }
            })
        }.width("100%")

        // Row 3: Shell
        Row({ space: 8 }) {
          Button("Start Shell")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected && !this.shellStarted)
            .onClick(async () => {
              this.log("Starting shell (80x24)...");
              try {
                // Setup event listener
                this.client.on("data", (data) => {
                  if (typeof data === "string") {
                    const lines = this.parseTerminalData(data);
                    lines.forEach((line) => {
                      if (line) this.log("[Shell] " + line);
                    });
                  }
                });

                await this.client.startShell(80, 24);
                this.shellStarted = true;
                this.log("[SUCCESS] startShell OK - reading data");
              } catch (e) {
                this.logError("shell: " + (e as Error).message);
              }
            })
        }.width("100%")

        // Row 4: Shell input
        if (this.shellStarted) {
          Row({ space: 8 }) {
            TextInput({ placeholder: "Type command...", text: this.shellInput })
              .onChange((val: string) => {
                this.shellInput = val;
              })
              .layoutWeight(1)
              .height(36)
            Button("Send")
              .fontSize(13)
              .onClick(() => {
                if (!this.shellInput) return;
                try {
                  this.client.writeToShell(this.shellInput + "\n");
                  this.log("[Input] " + this.shellInput);
                  this.shellInput = "";
                } catch (e) {
                  this.logError("write: " + JSON.stringify(e));
                }
              })
          }.width("100%");
        }

        // Row 5: SFTP
        Row({ space: 8 }) {
          Button("SFTP Init")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              try {
                await this.client.sftpInit();
                this.log("[SUCCESS] SFTP initialized");
              } catch (e) {
                this.logError("sftpInit: " + (e as Error).message);
              }
            })
          Button("List /")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              try {
                const entries = await this.client.sftpListDir("/");
                this.log(`[SFTP] / has ${entries.length} entries:`);
                entries.forEach((f) => {
                  const tag = f.isDir ? "[DIR]" : "[FILE]";
                  this.log(`  ${tag} ${f.name} (${f.size} bytes)`);
                });
              } catch (e) {
                this.logError("sftpListDir: " + (e as Error).message);
              }
            })
          Button("Read hostname")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              try {
                const content = await this.client.sftpReadFile("/etc/hostname");
                this.log("[SFTP] /etc/hostname: " + content.trim());
              } catch (e) {
                this.logError("sftpReadFile: " + (e as Error).message);
              }
            })
        }.width("100%")

        // Row 6: SFTP Binary
        Row({ space: 8 }) {
          Button("Read /bin/ls (Binary)")
            .fontSize(13)
            .layoutWeight(1)
            .enabled(this.isConnected)
            .onClick(async () => {
              try {
                this.log("[SFTP] Reading /bin/ls as ArrayBuffer...");
                const startTime = Date.now();
                const buffer = await this.client.sftpReadFileBytes("/bin/ls");
                const elapsed = Date.now() - startTime;

                const size = buffer.byteLength;
                const view = new Uint8Array(buffer);
                const magic =
                  size >= 4
                    ? `0x${view[0].toString(16)}${view[1].toString(16)}${view[2].toString(16)}${view[3].toString(16)}`
                    : "unknown";

                this.log(`[SUCCESS] Read ${size} bytes in ${elapsed}ms.`);
                this.log(`[SFTP] Magic bytes: ${magic}`);
              } catch (e) {
                this.logError("sftpReadFileBytes: " + (e as Error).message);
              }
            })
        }.width("100%")
      }
      .padding({ left: 12, right: 12 })
      .width("100%")

      // ===== Log output =====
      Text("— Log Output —")
        .fontSize(13)
        .fontColor(Color.Gray)
        .margin({ top: 12, bottom: 4 })
      Scroll() {
        Column() {
          ForEach(this.logs, (line: string, idx: number) => {
            Text(line)
              .fontSize(11)
              .fontFamily("monospace")
              .fontColor(
                line.includes("[ERROR]")
                  ? Color.Red
                  : line.includes("[SUCCESS]")
                    ? Color.Green
                    : line.includes("[SHELL]")
                      ? Color.Gray
                      : Color.Black,
              )
              .width("100%")
              .padding({ left: 8, right: 8, top: 1, bottom: 1 });
          })
        }.width("100%")
      }
      .layoutWeight(1)
      .width("100%")
      .backgroundColor("#F5F5F5")
      .borderRadius(8)
      .margin({ left: 12, right: 12, bottom: 12 })

      // Clear log button
      Button("Clear Log")
        .fontSize(13)
        .margin({ bottom: 16 })
        .onClick(() => {
          this.logs = [];
        })
    }
    .height("100%")
    .width("100%");
  }
}
