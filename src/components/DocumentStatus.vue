<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { formatBytes, type DocumentInspection, type LoadedDocument } from '@/domain/document'

const props = defineProps<{
  document: LoadedDocument | null
  inspection: DocumentInspection | null
  phase: string
}>()

const metrics = computed(() => props.document ?? props.inspection)
const { locale, t } = useI18n()
const strategy = computed(() => t(`status.${metrics.value?.strategy ?? 'waiting'}`))
const numberFormat = computed(() => new Intl.NumberFormat(locale.value))

function formatMetric(value: number | undefined): string {
  return value === undefined ? '-' : numberFormat.value.format(value)
}
</script>

<template>
  <footer class="status-bar" aria-live="polite">
    <div class="status-summary">
      <span>{{ t('status.loadReport') }}</span>
      <strong :data-testid="'load-strategy'">{{ strategy }}</strong>
    </div>
    <dl v-if="metrics" class="metrics">
      <div><dt>{{ t('status.size') }}</dt><dd>{{ formatBytes(metrics.byteSize) }}</dd></div>
      <div><dt>{{ t('status.lines') }}</dt><dd>{{ formatMetric(metrics.lineCount) }}</dd></div>
      <div><dt>{{ t('status.longestLine') }}</dt><dd>{{ metrics.longestLineBytes === undefined ? '-' : `${formatMetric(metrics.longestLineBytes)} B` }}</dd></div>
      <div><dt>{{ t('status.preflight') }}</dt><dd>{{ metrics.preflightMilliseconds.toFixed(1) }} ms</dd></div>
      <div><dt>{{ t('status.read') }}</dt><dd>{{ metrics.readMilliseconds?.toFixed(1) ?? '-' }} ms</dd></div>
      <div><dt>{{ t('status.editor') }}</dt><dd>{{ metrics.editorMilliseconds?.toFixed(1) ?? '-' }} ms</dd></div>
    </dl>
    <p v-else class="empty-status">{{ t('status.empty') }}</p>
    <p class="phase">{{ phase === 'loading' ? t('status.loading') : t('status.ready') }}</p>
  </footer>
</template>

<style scoped>
.status-bar { align-items: center; background: var(--panel); border-top: 1px solid var(--line); display: flex; flex: 0 0 30px; gap: 18px; min-height: 0; overflow: hidden; padding: 0 12px; white-space: nowrap; }
.status-summary { align-items: center; display: flex; flex: 0 0 auto; gap: 8px; }
.status-summary span, .phase, dt { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 10px; text-transform: uppercase; }
.status-summary strong { color: var(--accent); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 10px; text-transform: uppercase; }
.metrics { align-items: center; display: flex; gap: 14px; margin: 0; min-width: 0; overflow: auto; }
.metrics div { align-items: center; display: flex; gap: 5px; }
dt, dd { margin: 0; }
dd { color: var(--ink); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; }
.empty-status { color: var(--muted); font-size: 11px; margin: 0; overflow: hidden; text-overflow: ellipsis; }
.phase { margin: 0 0 0 auto; }
@media (max-width: 760px) { .status-bar { gap: 10px; } .status-summary span, .metrics div:nth-child(n + 3) { display: none; } }
</style>
