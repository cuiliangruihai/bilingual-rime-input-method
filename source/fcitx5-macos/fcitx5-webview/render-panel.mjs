import { chromium } from '@playwright/test'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'

const root = dirname(fileURLToPath(import.meta.url))
const browser = await chromium.launch({
  headless: true,
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
})
const page = await browser.newPage({ viewport: { width: 1448, height: 1086 }, deviceScaleFactor: 1 })
await page.goto(`file://${join(root, 'dist', 'index.html')}`)

await page.evaluate(() => {
  window.fcitx.setHost('macOS', 26)
  window.fcitx.setTheme(1)
  window.fcitx.setStyle(JSON.stringify({
    Basic: { DefaultTheme: 'macOS 26' },
    Background: { Blur: 'False', BlurRadius: '20', KeepPanelColorWhenHasImage: 'True', ImageUrl: '', Shadow: 'True' },
    LightMode: {
      OverrideDefault: 'True', BorderColor: '#d5d9df', HighlightColor: '#dceeff', HighlightHoverColor: '#dceeff',
      HighlightTextColor: '#151a22', HighlightTextPressColor: '#151a22', HighlightLabelColor: '#4f7eaf',
      HighlightCommentColor: '#536b8b', HighlightMarkColor: '#4f7eaf', LabelColor: '#6b7685',
      CommentColor: '#5e6d82', PagingButtonColor: '#5e6876', DisabledPagingButtonColor: '#b6bdc7',
      DividerColor: '#e8ebf0', PanelColor: '#ffffff', AuxColor: '#161e2a',
      PreeditColorPreCaret: '#161e2a', PreeditColorCaret: '#1479ff', PreeditColorPostCaret: '#161e2a', TextColor: '#141b27',
    },
    DarkMode: { OverrideDefault: 'False', SameWithLightMode: 'True' },
    Font: {
      TextFontFamily: { 0: 'PingFang SC', 1: 'sans-serif' }, TextFontSize: '28', TextFontWeight: '600',
      LabelFontFamily: { 0: 'SF Pro Text', 1: 'sans-serif' }, LabelFontSize: '18', LabelFontWeight: '400',
      CommentFontFamily: { 0: 'SF Pro Text', 1: 'sans-serif' }, CommentFontSize: '16', CommentFontWeight: '400',
      PreeditFontFamily: { 0: 'SF Pro Text', 1: 'sans-serif' }, PreeditFontSize: '24', PreeditFontWeight: '400',
    },
    Size: {
      OverrideDefault: 'True', BorderRadius: '18', BorderWidth: '1', BottomPadding: '3', HighlightRadius: '14',
      HorizontalDividerWidth: '1', LabelTextGap: '8', LeftPadding: '10', RightPadding: '10', TopPadding: '3',
      VerticalMinWidth: '200', ScrollCellWidth: '146', Margin: '8',
    },
    Highlight: { HoverBehavior: 'None', MarkText: '', MarkStyle: 'None' },
    ScrollMode: { Animation: 'False', MaxRowCount: '1', MaxColumnCount: '3', ShowScrollBar: 'False' },
    Typography: { VerticalCommentsAlignRight: 'False', PagingButtonsStyle: 'Arrow' },
    Caret: { Style: 'Blink', Text: '|' },
    Advanced: { UserCss: '' },
  }))
  window.fcitx.updateInputPanel([['nihao', 0]], true, [], [], [])
  window.fcitx.setCandidates([
    { text: '你好', label: '1', comment: 'hello', actions: [], spaceBetweenComment: true },
    { text: '你好吗', label: '2', comment: 'how are you', actions: [], spaceBetweenComment: true },
    { text: '拟好', label: '3', comment: 'draft', actions: [], spaceBetweenComment: true },
  ], 0, true, false, true, 0, false, false, [])
})
await page.screenshot({ path: join(root, '..', '..', '..', 'panel-current.png') })
await browser.close()
