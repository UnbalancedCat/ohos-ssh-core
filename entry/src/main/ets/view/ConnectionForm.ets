@Component
export struct ConnectionForm {
  @Link host: string;
  @Link port: string;
  @Link username: string;
  @Link password: string;
  @Link privateKey: string;
  @Link passphrase: string;
  @Prop isConnected: boolean;

  onConnectPass?: () => void;
  onConnectKey?: () => void;
  onDisconnect?: () => void;

  build() {
    Column({ space: 4 }) {
      // Host : Port
      Row() {
        Text($r('app.string.label_host'))
          .width($r('app.float.label_width'))
          .fontSize($r('app.float.label_font_size'))
          .fontColor($r('app.color.label_text'))
        TextInput({ text: this.host })
          .onChange((val: string) => { this.host = val; })
          .layoutWeight(1)
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
        Text(':')
          .fontSize($r('app.float.label_font_size'))
          .margin({ left: 4, right: 4 })
        TextInput({ text: this.port })
          .onChange((val: string) => { this.port = val; })
          .width($r('app.float.port_input_width'))
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
          .type(InputType.Number)
      }.width('100%')

      // Username
      Row() {
        Text($r('app.string.label_user'))
          .width($r('app.float.label_width'))
          .fontSize($r('app.float.label_font_size'))
          .fontColor($r('app.color.label_text'))
        TextInput({ text: this.username })
          .onChange((val: string) => { this.username = val; })
          .layoutWeight(1)
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
      }.width('100%')

      // Password
      Row() {
        Text($r('app.string.label_pass'))
          .width($r('app.float.label_width'))
          .fontSize($r('app.float.label_font_size'))
          .fontColor($r('app.color.label_text'))
        TextInput({ text: this.password })
          .onChange((val: string) => { this.password = val; })
          .layoutWeight(1)
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
          .type(InputType.Password)
      }.width('100%')

      // Private Key
      Row() {
        Text($r('app.string.label_key'))
          .width($r('app.float.label_width'))
          .fontSize($r('app.float.label_font_size'))
          .fontColor($r('app.color.label_text'))
        TextInput({ text: this.privateKey, placeholder: $r('app.string.placeholder_key') })
          .placeholderFont({
            size: $r('app.float.input_font_size')
          })
          .onChange((val: string) => { this.privateKey = val; })
          .layoutWeight(1)
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
      }.width('100%')

      // Passphrase
      Row() {
        Text($r('app.string.label_passphrase'))
          .width($r('app.float.label_width'))
          .fontSize($r('app.float.label_font_size'))
          .fontColor($r('app.color.label_text'))
        TextInput({ text: this.passphrase, placeholder: $r('app.string.placeholder_passphrase') })
          .placeholderFont({
            size: $r('app.float.input_font_size')
          })
          .onChange((val: string) => { this.passphrase = val; })
          .layoutWeight(1)
          .height($r('app.float.input_height'))
          .fontSize($r('app.float.input_font_size'))
          .type(InputType.Password)
      }
      .width('100%')

      // Connect / Disconnect buttons
      Row({ space: 6 }) {
        Button($r('app.string.btn_connect_pass'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(!this.isConnected)
          .onClick(() => { this.onConnectPass?.(); })
        Button($r('app.string.btn_connect_key'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .enabled(!this.isConnected)
          .onClick(() => { this.onConnectKey?.(); })
        Button($r('app.string.btn_disconnect'))
          .fontSize($r('app.float.btn_font_size'))
          .height($r('app.float.btn_height'))
          .layoutWeight(1)
          .fontColor($r('app.color.btn_disconnect_text'))
          .backgroundColor($r('app.color.btn_disconnect_bg'))
          .enabled(this.isConnected)
          .onClick(() => { this.onDisconnect?.(); })
      }
      .width('100%')
      .padding({top: 4})
    }
    .width('100%')
  }
}
