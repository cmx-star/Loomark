import { mount } from '@vue/test-utils'
import { defineComponent, nextTick, ref } from 'vue'
import { describe, expect, it, vi } from 'vitest'
import { useNativeMenu } from '@/composables/useNativeMenu'
import { i18n } from '@/i18n'
import type { LoomarkDesktopApi, NativeMenuAction, NativeMenuState } from '@/desktop-contract'

describe('Electron native menu bridge', () => {
  it('sends localized menu state and dispatches a native menu action back to Vue', async () => {
    let menuActionListener: ((action: NativeMenuAction) => void) | undefined
    const updateNativeMenu = vi.fn<(state: NativeMenuState) => Promise<void>>().mockResolvedValue()
    const setMode = vi.fn()

    window.loomark = {
      onNativeMenuAction(listener) {
        menuActionListener = listener
        return () => { menuActionListener = undefined }
      },
      updateNativeMenu,
    } as Pick<LoomarkDesktopApi, 'onNativeMenuAction' | 'updateNativeMenu'> as LoomarkDesktopApi

    const Harness = defineComponent({
      setup() {
        useNativeMenu({
          activeDocument: ref(true),
          closeActiveDocument: vi.fn(),
          dirty: ref(false),
          locale: ref('zh-CN'),
          mode: ref('source'),
          navigatorCollapsed: ref(false),
          openDocument: vi.fn().mockResolvedValue(undefined),
          openLogDirectory: vi.fn().mockResolvedValue(undefined),
          printDocument: vi.fn(),
          richViewsAvailable: ref(true),
          saveActiveDocument: vi.fn().mockResolvedValue(undefined),
          setLocale: vi.fn(),
          setMode,
          setNavigatorCollapsed: vi.fn(),
          setTheme: vi.fn(),
          theme: ref('paper'),
        })
        return () => null
      },
    })

    const wrapper = mount(Harness, { global: { plugins: [i18n] } })
    await nextTick()

    expect(updateNativeMenu).toHaveBeenCalledWith(expect.objectContaining({
      activeDocument: true,
      labels: expect.objectContaining({ file: '文件', source: '源码' }),
      locale: 'zh-CN',
    }))
    menuActionListener?.('mode-reading')
    expect(setMode).toHaveBeenCalledWith('reading')

    wrapper.unmount()
    expect(menuActionListener).toBeUndefined()
  })
})
