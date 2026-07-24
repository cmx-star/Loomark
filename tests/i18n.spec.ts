import { describe, expect, it } from 'vitest'
import { messages, resolveLocale } from '@/i18n'

describe('application locale', () => {
  it('defaults to Simplified Chinese for missing or unsupported preferences', () => {
    expect(resolveLocale(null)).toBe('zh-CN')
    expect(resolveLocale('fr-FR')).toBe('zh-CN')
  })

  it('keeps English as the explicit alternate locale and translates native menu labels', () => {
    expect(resolveLocale('en-US')).toBe('en-US')
    expect(messages['zh-CN'].toolbar.open).toBe('打开')
    expect(messages['en-US'].toolbar.open).toBe('Open')
    expect(messages['zh-CN'].menu.file).toBe('文件')
    expect(messages['zh-CN'].menu.toggleNavigator).toBe('显示/隐藏目录')
    expect(messages['zh-CN'].navigator.currentDirectory).toBe('当前目录')
    expect(messages['zh-CN'].navigator.up).toBe('上一级')
    expect(messages['en-US'].menu.file).toBe('File')
    expect(messages['en-US'].menu.toggleNavigator).toBe('Show or Hide Navigator')
    expect(messages['en-US'].navigator.currentDirectory).toBe('Current folder')
    expect(messages['en-US'].navigator.up).toBe('Up')
  })
})
