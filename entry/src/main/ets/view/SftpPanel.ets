@Component
export struct SftpPanel {
  @Prop isConnected: boolean;
  @Prop sftpInitialized: boolean;
  @Prop currentPath: string;
  @State navInput: string = '';

  onSftpInit?: () => void;
  onSftpShutdown?: () => void;
  onSftpList?: () => void;
  onSftpNavigate?: (target: string) => void;
  onSftpRead?: () => void;
  onSftpBinaryRead?: () => void;

  build() {
    Column({ space: 6 }) {
      // Init / Shutdown
      Row({ space: 6 }) {
        Button($r('app.string.btn_sftp_init'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(this.isConnected && !this.sftpInitialized)
          .onClick(() => { this.onSftpInit?.(); })
        Button($r('app.string.btn_sftp_shutdown'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(this.sftpInitialized)
          .onClick(() => { this.onSftpShutdown?.(); })
      }.width('100%')

      if (this.sftpInitialized) {
        // Current path display
        Row({ space: 4 }) {
          SymbolGlyph($r('sys.symbol.folder'))
            .fontSize($r('app.float.sftp_path_font_size'))
            .fontColor([$r('app.color.sftp_path_text')])
          Text(this.currentPath)
            .fontSize($r('app.float.sftp_path_font_size'))
            .fontColor($r('app.color.sftp_path_text'))
            .fontWeight(FontWeight.Medium)
            .layoutWeight(1)
        }
        .width('100%')
        .padding({ top: 2, bottom: 2 })

        // Navigate: input + Go
        Row({ space: 6 }) {
          TextInput({ placeholder: $r('app.string.placeholder_sftp_navigate'), text: this.navInput })
            .placeholderFont({
              size: $r('app.float.input_font_size')
            })
            .onChange((val: string) => { this.navInput = val; })
            .layoutWeight(1)
            .height($r('app.float.nav_input_height'))
            .fontSize($r('app.float.input_font_size'))
            .onSubmit(() => {
              if (this.navInput) {
                this.onSftpNavigate?.(this.navInput);
                this.navInput = '';
              }
            })
          Button($r('app.string.btn_sftp_go'))
            .fontSize($r('app.float.btn_font_size'))
            .width($r('app.float.send_btn_width'))
            .height($r('app.float.btn_height'))
            .onClick(() => {
              if (this.navInput) {
                this.onSftpNavigate?.(this.navInput);
                this.navInput = '';
              }
            })
        }.width('100%')

        // List Dir
        Row({ space: 6 }) {
          Button($r('app.string.btn_sftp_list'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .layoutWeight(1)
            .onClick(() => { this.onSftpList?.(); })
        }.width('100%')

        // Read / Binary Read
        Row({ space: 6 }) {
          Button($r('app.string.btn_sftp_read'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .layoutWeight(1)
            .onClick(() => { this.onSftpRead?.(); })
          Button($r('app.string.btn_sftp_binary'))
            .fontSize($r('app.float.btn_font_size'))
            .height($r('app.float.btn_height'))
            .layoutWeight(1)
            .onClick(() => { this.onSftpBinaryRead?.(); })
        }.width('100%')
      }
    }
    .width('100%')
  }
}
