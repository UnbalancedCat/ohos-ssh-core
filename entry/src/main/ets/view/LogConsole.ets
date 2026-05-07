@Component
export struct LogConsole {
  @Link @Watch('onLogsChange') logs: string[];
  private scroller: Scroller = new Scroller();

  onLogsChange() {
    setTimeout(() => {
      this.scroller.scrollEdge(Edge.Bottom);
    }, 50);
  }

  private getLogColor(line: string): ResourceColor {
    if (line.includes('[ERROR]')) return $r('app.color.log_text_error');
    if (line.includes('[SUCCESS]')) return $r('app.color.log_text_success');
    if (line.includes('[Shell]') || line.includes('[Output]')) return $r('app.color.log_text_shell');
    if (line.includes('[Fingerprint]')) return $r('app.color.log_text_fingerprint');
    return $r('app.color.log_text_normal');
  }

  build() {
    Column() {
      Text($r('app.string.log_title'))
        .fontSize($r('app.float.log_title_font_size'))
        .fontColor($r('app.color.log_title'))
        .margin({ top: 4, bottom: 2 })

      Scroll(this.scroller) {
        Column() {
          ForEach(this.logs, (line: string) => {
            Text(line)
              .fontSize($r('app.float.log_font_size'))
              .fontFamily('monospace')
              .fontColor(this.getLogColor(line))
              .wordBreak(WordBreak.BREAK_ALL)
              .width('100%')
              .padding({
                left: $r('app.float.log_padding_h'),
                right: $r('app.float.log_padding_h'),
                top: $r('app.float.log_padding_v'),
                bottom: $r('app.float.log_padding_v')
              })
          })
        }
        .width('100%')
        .alignItems(HorizontalAlign.Start)
      }
      .scrollable(ScrollDirection.Vertical)
      .align(Alignment.TopStart)
      .layoutWeight(1)
      .width('100%')
      .backgroundColor($r('app.color.log_bg'))
      .borderRadius($r('app.float.log_border_radius'))
      .padding({ top: 4, bottom: 4 })
    }
    .layoutWeight(1)
    .width('100%')
    .padding({
      left: $r('app.float.log_margin'),
      right: $r('app.float.log_margin')
    })
  }
}
