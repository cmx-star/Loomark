<script setup lang="ts">
import { X } from '@lucide/vue'
import { useI18n } from 'vue-i18n'
import type { WorkspaceDocument } from '@/domain/workspace'

const { t } = useI18n()

defineProps<{
  activeDocumentId: string | null
  documents: WorkspaceDocument[]
}>()

const emit = defineEmits<{
  close: [id: string]
  select: [id: string]
}>()
</script>

<template>
  <nav v-if="documents.length" class="tabs" :aria-label="t('tabs.openDocuments')">
    <div v-for="document in documents" :key="document.id" class="tab" :class="{ active: document.id === activeDocumentId }">
      <button class="tab-select" type="button" :aria-current="document.id === activeDocumentId ? 'page' : undefined" @click="emit('select', document.id)">
        <span class="tab-title">{{ document.title }}</span><span v-if="document.dirty" class="dirty" :aria-label="t('tabs.unsaved')">*</span>
      </button>
      <button class="tab-close" type="button" :aria-label="t('tabs.close', { title: document.title })" :title="t('tabs.close', { title: document.title })" @click="emit('close', document.id)"><X :size="15" aria-hidden="true" /></button>
    </div>
  </nav>
</template>

<style scoped>
.tabs { align-items: stretch; background: var(--tabs); border-bottom: 1px solid var(--line); display: flex; flex: 0 0 38px; min-height: 0; overflow-x: auto; }
.tab { align-items: center; border-right: 1px solid var(--line); display: flex; max-width: 240px; min-width: 150px; }
.tab.active { background: var(--surface); box-shadow: inset 0 2px 0 var(--accent); }
.tab-select, .tab-close { background: transparent; border: 0; color: var(--muted); cursor: pointer; font: inherit; }
.tab-select { align-items: center; display: flex; flex: 1; gap: 5px; min-width: 0; padding: 9px 6px 9px 12px; text-align: left; }
.tab-title { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tab-close { align-items: center; display: inline-flex; justify-content: center; padding: 8px 11px; }
.tab-close:hover, .tab-select:hover { color: var(--ink); }
.dirty { color: var(--accent); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
</style>
