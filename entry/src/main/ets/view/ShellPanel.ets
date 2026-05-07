@Component
export struct ShellPanel {
  @Prop isConnected: boolean;
  @Prop shellStarted: boolean;
  @Link shellInput: string;

  onExecUname?: () => void;
  onExecLs?: () => void;
  onStartShell?: () => void;
  onStopShell?: () => void;
  onSendShell?: () => void;

  build() {
    Column({ space: 6 }) {
      // Exec commands
      Row({ space: 6 }) {
        Button($r('app.string.btn_exec_uname'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(this.isConnected)
          .onClick(() => { this.onExecUname?.(); })
        Button($r('app.string.btn_exec_ls'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(this.isConnected)
          .onClick(() => { this.onExecLs?.(); })
      }.width('100%')

      // Start / Stop shell
      Row({ space: 6 }) {
        Button($r('app.string.btn_start_shell'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(this.isConnected && !this.shellStarted)
          .onClick(() => { this.onStartShell?.(); })
        Button($r('app.string.btn_stop_shell'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .fontColor($r('app.color.btn_disconnect_text'))
          .backgroundColor($r('app.color.btn_disconnect_bg'))
          .enabled(this.shellStarted)
          .onClick(() => { this.onStopShell?.(); })
      }.width('100%')

      // Shell input (only visible after shell started)
      if (this.shellStarted) {
        Row({ space: 6 }) {
          TextInput({ placeholder: $r('app.string.placeholder_shell_input'), text: this.shellInput })
            .placeholderFont({
              size: $r('app.float.input_font_size')
            })
            .onChange((val: string) => { this.shellInput = val; })
            .layoutWeight(1)
            .height($r('app.float.input_height'))
            .fontSize($r('app.float.input_font_size'))
            .onSubmit(() => { this.onSendShell?.(); })
          Button($r('app.string.btn_send'))
            .fontSize($r('app.float.btn_font_size'))
            .width($r('app.float.send_btn_width'))
            .height($r('app.float.btn_height'))
            .onClick(() => { this.onSendShell?.(); })
        }.width('100%')
      }
    }
    .width('100%')
  }
}
