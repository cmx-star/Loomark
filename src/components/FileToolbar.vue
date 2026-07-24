<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import type { EditorMode, ThemeName } from '@/domain/workspace'
import type { AppLocale } from '@/i18n'

const modes: EditorMode[] = ['source', 'reading', 'split']
const { t } = useI18n()

defineProps<{
  disabled: boolean
  mode: EditorMode
  locale: AppLocale
  saveDisabled: boolean
  theme: ThemeName
}>()

const emit = defineEmits<{
  open: []
  print: []
  save: []
  setMode: [mode: EditorMode]
  setLocale: [locale: AppLocale]
  setTheme: [theme: ThemeName]
}>()
</script>

<template>
  <header class="toolbar">
    <div class="brand">
      <span class="brand-mark" aria-hidden="true">M</span>
      <div>
        <strong>Loomark</strong>
        <span>{{ t('toolbar.subtitle') }}</span>
      </div>
    </div>
    <nav class="toolbar-actions" :aria-label="t('toolbar.actions')">
      <div class="mode-control" :aria-label="t('toolbar.mode')">
        <button v-for="option in modes" :key="option" type="button" :class="{ selected: mode === option }" :aria-pressed="mode === option" @click="emit('setMode', option)">{{ t(`editor.${option}`) }}</button>
      </div>
      <select class="theme-select" :value="theme" :aria-label="t('theme.label')" @change="emit('setTheme', ($event.target as HTMLSelectElement).value as ThemeName)">
        <option value="paper">{{ t('theme.paper') }}</option>
        <option value="night">{{ t('theme.night') }}</option>
      </select>
      <select class="theme-select" :value="locale" :aria-label="t('language.label')" @change="emit('setLocale', ($event.target as HTMLSelectElement).value as AppLocale)">
        <option value="zh-CN">{{ t('language.zh-CN') }}</option>
        <option value="en-US">{{ t('language.en-US') }}</option>
      </select>
      <button type="button" :disabled="disabled" @click="emit('open')">{{ t('toolbar.open') }}</button>
      <button type="button" :disabled="saveDisabled" @click="emit('save')">{{ t('toolbar.save') }}</button>
      <button type="button" class="icon-button" :disabled="disabled" :aria-label="t('toolbar.print')" :title="t('toolbar.print')" @click="emit('print')">⌘P</button>
    </nav>
  </header>
</template>

<style scoped>
.toolbar { align-items: center; border-bottom: 1px solid var(--line); display: flex; justify-content: space-between; min-height: 64px; padding: 0 20px; }
.brand { align-items: center; display: flex; gap: 10px; }
.brand-mark { align-items: center; background: var(--ink); color: var(--paper); display: inline-flex; font-family: Georgia, serif; font-size: 20px; font-weight: 700; height: 32px; justify-content: center; width: 32px; }
.brand strong, .brand span { display: block; }
.brand strong { font-size: 15px; }
.brand span { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 11px; }
.toolbar-actions { align-items: center; display: flex; gap: 8px; }
.toolbar button { background: var(--paper); border: 1px solid var(--line-strong); border-radius: 4px; color: var(--ink); cursor: pointer; font: inherit; min-height: 32px; padding: 0 10px; }
.toolbar button:hover:not(:disabled) { background: var(--accent-soft); border-color: var(--accent); }
.toolbar button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.toolbar button:disabled { cursor: wait; opacity: .55; }
.toolbar .icon-button { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; padding: 0 8px; }
.mode-control { background: var(--tabs); border: 1px solid var(--line-strong); border-radius: 4px; display: flex; overflow: hidden; }
.toolbar .mode-control button { border: 0; border-radius: 0; color: var(--muted); min-width: 58px; text-transform: capitalize; }
.toolbar .mode-control button + button { border-left: 1px solid var(--line-strong); }
.toolbar .mode-control button.selected { background: var(--paper); color: var(--ink); }
.theme-select { background: var(--paper); border: 1px solid var(--line-strong); border-radius: 4px; color: var(--ink); min-height: 32px; padding: 0 6px; }
@media (max-width: 760px) { .toolbar { gap: 10px; padding: 0 10px; } .brand span { display: none; } .toolbar-actions { gap: 4px; } .mode-control { display: none; } .toolbar button { padding: 0 7px; } }
</style>
