@Component
export struct CollapsibleSection {
  @Prop title: string | Resource = '';
  @Prop expanded: boolean = true;
  onToggle?: () => void;

  @BuilderParam content: () => void;

  build() {
    Column() {
      // Header bar — clickable to toggle
      Row() {
        SymbolGlyph(this.expanded
          ? $r('sys.symbol.chevron_down')
          : $r('sys.symbol.chevron_right'))
          .fontSize($r('app.float.section_arrow_size'))
          .fontColor([$r('app.color.section_arrow')])
          .width(20)
        Text(this.title)
          .fontSize($r('app.float.section_header_font_size'))
          .fontColor($r('app.color.section_header_text'))
          .fontWeight(FontWeight.Medium)
          .layoutWeight(1)
      }
      .width('100%')
      .height($r('app.float.section_header_height'))
      .padding({ left: 12, right: 12 })
      .backgroundColor($r('app.color.section_header_bg'))
      .borderRadius($r('app.float.section_border_radius'))
      .onClick(() => {
        this.onToggle?.();
      })

      // Content area — collapses via visibility
      if (this.expanded) {
        Column() {
          this.content();
        }
        .width('100%')
        .padding({ left: 12, right: 12, top: 6})
      }
    }
    .width('100%')
    .clip(true)
  }
}
