import { mount } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import CodeEditor from '@/components/CodeEditor.vue'
import { i18n } from '@/i18n'

const oneMiBSource = '# Loomark benchmark\n'.repeat(55_189)

afterEach(() => {
  vi.restoreAllMocks()
})

describe('CodeEditor', () => {
  it('initializes a 1 MiB source document once without reporting an editor failure', () => {
    vi.spyOn(console, 'info').mockImplementation(() => undefined)
    vi.stubGlobal('requestAnimationFrame', (callback: FrameRequestCallback) => window.setTimeout(() => callback(performance.now()), 0))
    vi.stubGlobal('cancelAnimationFrame', (id: number) => window.clearTimeout(id))

    const wrapper = mount(CodeEditor, {
      global: { plugins: [i18n] },
      props: { content: oneMiBSource },
    })

    expect(wrapper.emitted('initialized')).toHaveLength(1)
    expect(wrapper.emitted('failed')).toBeUndefined()
    wrapper.unmount()
  })
})
