<script setup lang="ts">
import { RefreshCw, ShieldAlert } from '@lucide/vue'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

defineProps<{
  dirty: boolean
  path: string
}>()

const emit = defineEmits<{
  keep: []
  reload: []
}>()
</script>

<template>
  <aside class="external-change-notice" role="alert">
    <ShieldAlert :size="18" aria-hidden="true" />
    <div class="external-change-copy">
      <p class="external-change-title">{{ t('externalChange.title') }}</p>
      <p class="external-change-description">{{ t(dirty ? 'externalChange.dirtyDescription' : 'externalChange.cleanDescription') }}</p>
      <p class="external-change-path" :title="path">{{ path }}</p>
    </div>
    <div class="external-change-actions">
      <button class="notice-button secondary" type="button" @click="emit('keep')">{{ t('externalChange.keep') }}</button>
      <button class="notice-button primary" type="button" @click="emit('reload')"><RefreshCw :size="15" aria-hidden="true" />{{ t('externalChange.reload') }}</button>
    </div>
  </aside>
</template>

<style scoped>
.external-change-notice { align-items: center; background: var(--accent-soft); border-bottom: 1px solid var(--line); color: var(--ink); display: flex; gap: 12px; min-height: 70px; padding: 10px 18px; }
.external-change-copy { flex: 1; min-width: 0; }
.external-change-title, .external-change-description, .external-change-path { margin: 0; }
.external-change-title { font-size: 13px; font-weight: 700; }
.external-change-description { color: var(--muted); font-size: 12px; line-height: 1.45; margin-top: 2px; }
.external-change-path { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 11px; margin-top: 3px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.external-change-actions { display: flex; flex: 0 0 auto; gap: 8px; }
.notice-button { align-items: center; border: 1px solid var(--line-strong); border-radius: 4px; cursor: pointer; display: inline-flex; font-size: 12px; gap: 6px; min-height: 30px; padding: 5px 10px; }
.notice-button.secondary { background: var(--surface); color: var(--ink); }
.notice-button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
.notice-button:focus-visible { outline: 2px solid var(--ink); outline-offset: 2px; }
@media (max-width: 760px) { .external-change-notice { align-items: flex-start; flex-wrap: wrap; } .external-change-actions { margin-left: 30px; } }
</style>
