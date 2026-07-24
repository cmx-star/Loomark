import { isTauri } from '@tauri-apps/api/core'
import { getName } from '@tauri-apps/api/app'
import { CheckMenuItem, Menu, MenuItem, PredefinedMenuItem, Submenu } from '@tauri-apps/api/menu'
import { onBeforeUnmount, onMounted, watch, type Ref } from 'vue'
import { useI18n } from 'vue-i18n'
import type { AppLocale } from '@/i18n'
import type { EditorMode, ThemeName } from '@/domain/workspace'

interface NativeMenuOptions {
  activeDocument: Ref<boolean>
  dirty: Ref<boolean>
  locale: Ref<AppLocale>
  mode: Ref<EditorMode>
  navigatorCollapsed: Ref<boolean>
  theme: Ref<ThemeName>
  closeActiveDocument: () => void
  openDocument: () => Promise<void>
  printDocument: () => void
  saveActiveDocument: () => Promise<void>
  setLocale: (locale: AppLocale) => void
  setMode: (mode: EditorMode) => void
  setNavigatorCollapsed: (collapsed: boolean) => void
  setTheme: (theme: ThemeName) => void
}

function run(action: () => void | Promise<void>) {
  return () => void action()
}

export function useNativeMenu(options: NativeMenuOptions) {
  const { t } = useI18n({ useScope: 'global' })
  let activeMenu: Menu | null = null
  let generation = 0

  async function separator() {
    return PredefinedMenuItem.new({ item: 'Separator' })
  }

  async function rebuild() {
    if (!isTauri()) return
    const currentGeneration = ++generation
    const appName = await getName()
    const applicationMenu = await Submenu.new({
      text: appName,
      items: [
        await PredefinedMenuItem.new({ item: { About: null } }),
        await separator(),
        await PredefinedMenuItem.new({ item: 'Services' }),
        await separator(),
        await PredefinedMenuItem.new({ item: 'Hide' }),
        await PredefinedMenuItem.new({ item: 'HideOthers' }),
        await PredefinedMenuItem.new({ item: 'ShowAll' }),
        await separator(),
        await PredefinedMenuItem.new({ item: 'Quit' }),
      ],
    })
    const open = await MenuItem.new({ id: 'open', text: t('menu.open'), accelerator: 'CmdOrCtrl+O', action: run(options.openDocument) })
    const save = await MenuItem.new({ id: 'save', text: t('menu.save'), accelerator: 'CmdOrCtrl+S', enabled: options.dirty.value, action: run(options.saveActiveDocument) })
    const print = await MenuItem.new({ id: 'print', text: t('menu.print'), accelerator: 'CmdOrCtrl+P', enabled: options.activeDocument.value, action: run(options.printDocument) })
    const close = await MenuItem.new({ id: 'close-tab', text: t('menu.closeTab'), accelerator: 'CmdOrCtrl+W', enabled: options.activeDocument.value, action: run(options.closeActiveDocument) })
    const fileMenu = await Submenu.new({ text: t('menu.file'), items: [open, save, await separator(), print, await separator(), close] })

    const editMenu = await Submenu.new({
      text: t('menu.edit'),
      items: [
        await PredefinedMenuItem.new({ item: 'Undo', text: t('menu.undo') }),
        await PredefinedMenuItem.new({ item: 'Redo', text: t('menu.redo') }),
        await separator(),
        await PredefinedMenuItem.new({ item: 'Cut', text: t('menu.cut') }),
        await PredefinedMenuItem.new({ item: 'Copy', text: t('menu.copy') }),
        await PredefinedMenuItem.new({ item: 'Paste', text: t('menu.paste') }),
        await PredefinedMenuItem.new({ item: 'SelectAll', text: t('menu.selectAll') }),
      ],
    })

    const modeItems = await Promise.all((['source', 'reading', 'split'] as const).map((mode) =>
      CheckMenuItem.new({ id: `mode-${mode}`, text: t(`editor.${mode}`), checked: options.mode.value === mode, enabled: options.activeDocument.value, action: run(() => options.setMode(mode)) }),
    ))
    const navigator = await MenuItem.new({ id: 'toggle-navigator', text: t('menu.toggleNavigator'), accelerator: 'CmdOrCtrl+Shift+B', action: run(() => options.setNavigatorCollapsed(!options.navigatorCollapsed.value)) })
    const viewMenu = await Submenu.new({ text: t('menu.view'), items: [...modeItems, await separator(), navigator] })

    const themeItems = await Promise.all((['paper', 'night'] as const).map((theme) =>
      CheckMenuItem.new({ id: `theme-${theme}`, text: t(`theme.${theme}`), checked: options.theme.value === theme, action: run(() => options.setTheme(theme)) }),
    ))
    const appearanceMenu = await Submenu.new({ text: t('menu.appearance'), items: themeItems })

    const localeItems = await Promise.all((['zh-CN', 'en-US'] as const).map((locale) =>
      CheckMenuItem.new({ id: `locale-${locale}`, text: t(`language.${locale}`), checked: options.locale.value === locale, action: run(() => options.setLocale(locale)) }),
    ))
    const languageMenu = await Submenu.new({ text: t('menu.language'), items: localeItems })
    const windowMenu = await Submenu.new({ text: t('menu.window'), items: [await PredefinedMenuItem.new({ item: 'Fullscreen', text: t('menu.fullscreen') })] })
    const menu = await Menu.new({ items: [applicationMenu, fileMenu, editMenu, viewMenu, appearanceMenu, languageMenu, windowMenu] })

    if (currentGeneration !== generation) {
      await menu.close()
      return
    }
    const previousMenu = await menu.setAsAppMenu()
    const retiredMenu = previousMenu ?? activeMenu
    if (retiredMenu && retiredMenu.id !== menu.id) await retiredMenu.close()
    activeMenu = menu
  }

  onMounted(() => { void rebuild() })
  watch(
    [options.activeDocument, options.dirty, options.locale, options.mode, options.navigatorCollapsed, options.theme],
    () => { void rebuild() },
  )
  onBeforeUnmount(() => { void activeMenu?.close() })
}
