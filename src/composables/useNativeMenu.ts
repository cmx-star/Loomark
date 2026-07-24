import { onBeforeUnmount, onMounted, watch, type Ref } from 'vue'
import { useI18n } from 'vue-i18n'
import type { NativeMenuAction, NativeMenuLabels } from '@/desktop-contract'
import type { AppLocale } from '@/i18n'
import type { EditorMode, ThemeName } from '@/domain/workspace'

interface NativeMenuOptions {
  activeDocument: Ref<boolean>
  dirty: Ref<boolean>
  locale: Ref<AppLocale>
  mode: Ref<EditorMode>
  navigatorCollapsed: Ref<boolean>
  richViewsAvailable: Ref<boolean>
  theme: Ref<ThemeName>
  closeActiveDocument: () => void
  openDocument: () => Promise<void>
  openLogDirectory: () => Promise<void>
  printDocument: () => void
  saveActiveDocument: () => Promise<void>
  setLocale: (locale: AppLocale) => void
  setMode: (mode: EditorMode) => void
  setNavigatorCollapsed: (collapsed: boolean) => void
  setTheme: (theme: ThemeName) => void
}

export function useNativeMenu(options: NativeMenuOptions) {
  const { t } = useI18n({ useScope: 'global' })
  let removeMenuActionListener: (() => void) | null = null

  function labels(): NativeMenuLabels {
    return {
      appearance: t('menu.appearance'), closeTab: t('menu.closeTab'), copy: t('menu.copy'), cut: t('menu.cut'),
      edit: t('menu.edit'), enUS: t('language.en-US'), file: t('menu.file'), fullscreen: t('menu.fullscreen'),
      language: t('menu.language'), night: t('theme.night'), open: t('menu.open'), openLogDirectory: t('menu.openLogDirectory'),
      paper: t('theme.paper'), paste: t('menu.paste'), print: t('menu.print'), reading: t('editor.reading'), redo: t('menu.redo'),
      save: t('menu.save'), selectAll: t('menu.selectAll'), source: t('editor.source'), split: t('editor.split'),
      toggleNavigator: t('menu.toggleNavigator'), undo: t('menu.undo'), view: t('menu.view'), window: t('menu.window'), zhCN: t('language.zh-CN'),
    }
  }

  async function rebuild() {
    if (!window.loomark) return
    await window.loomark.updateNativeMenu({
      activeDocument: options.activeDocument.value,
      dirty: options.dirty.value,
      labels: labels(),
      locale: options.locale.value,
      mode: options.mode.value,
      navigatorCollapsed: options.navigatorCollapsed.value,
      richViewsAvailable: options.richViewsAvailable.value,
      theme: options.theme.value,
    })
  }

  function handleMenuAction(action: NativeMenuAction) {
    if (action === 'open') void options.openDocument()
    else if (action === 'open-log-directory') void options.openLogDirectory()
    else if (action === 'save') void options.saveActiveDocument()
    else if (action === 'print') options.printDocument()
    else if (action === 'close-tab') options.closeActiveDocument()
    else if (action === 'mode-source') options.setMode('source')
    else if (action === 'mode-reading') options.setMode('reading')
    else if (action === 'mode-split') options.setMode('split')
    else if (action === 'toggle-navigator') options.setNavigatorCollapsed(!options.navigatorCollapsed.value)
    else if (action === 'theme-paper') options.setTheme('paper')
    else if (action === 'theme-night') options.setTheme('night')
    else if (action === 'locale-zh-CN') options.setLocale('zh-CN')
    else if (action === 'locale-en-US') options.setLocale('en-US')
  }

  onMounted(() => {
    removeMenuActionListener = window.loomark?.onNativeMenuAction(handleMenuAction) ?? null
    void rebuild()
  })
  watch(
    [options.activeDocument, options.dirty, options.locale, options.mode, options.navigatorCollapsed, options.richViewsAvailable, options.theme],
    () => { void rebuild() },
  )
  onBeforeUnmount(() => { removeMenuActionListener?.() })
}
