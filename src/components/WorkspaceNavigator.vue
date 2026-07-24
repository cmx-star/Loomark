<script setup lang="ts">
import { CollapsibleContent, CollapsibleRoot } from 'reka-ui'
import { ChevronUp, FileText, Folder, FolderOpen, PanelLeftClose, PanelLeftOpen } from '@lucide/vue'
import { ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import type { DirectoryListing } from '@/domain/directory'
import type { WorkspaceDocument } from '@/domain/workspace'

const props = defineProps<{
  activeDocumentId: string | null
  collapsed: boolean
  directory: DirectoryListing | null
  directoryError: string | null
  documents: WorkspaceDocument[]
}>()

const emit = defineEmits<{
  select: [id: string]
  openPath: [path: string]
  browseDirectory: [path: string]
  setCollapsed: [collapsed: boolean]
}>()

const { t } = useI18n()
const activeTab = ref<'open' | 'directory'>('directory')

watch(() => props.activeDocumentId, () => {
  activeTab.value = 'directory'
}, { immediate: true })

function toggleNavigator() {
  emit('setCollapsed', !props.collapsed)
}
</script>

<template>
  <aside class="navigator" :class="{ collapsed }" :aria-label="t('navigator.files')">
    <div class="navigator-header">
      <div v-if="!collapsed" class="navigator-title"><FolderOpen :size="16" aria-hidden="true" /><span>{{ activeTab === 'directory' ? t('navigator.currentDirectory') : t('navigator.files') }}</span></div>
      <button class="navigator-toggle" type="button" :aria-label="collapsed ? t('navigator.expand') : t('navigator.collapse')" :title="collapsed ? t('navigator.expand') : t('navigator.collapse')" @click="toggleNavigator">
        <PanelLeftOpen v-if="collapsed" :size="17" aria-hidden="true" />
        <PanelLeftClose v-else :size="17" aria-hidden="true" />
      </button>
    </div>
    <CollapsibleRoot class="navigator-collapsible" :open="!collapsed">
      <CollapsibleContent class="navigator-content">
        <div class="navigator-tabs" role="tablist" :aria-label="t('navigator.files')">
          <button class="navigator-tab" :class="{ active: activeTab === 'directory' }" type="button" role="tab" :aria-selected="activeTab === 'directory'" @click="activeTab = 'directory'">{{ t('navigator.currentDirectory') }}</button>
          <button class="navigator-tab" :class="{ active: activeTab === 'open' }" type="button" role="tab" :aria-selected="activeTab === 'open'" @click="activeTab = 'open'">{{ t('navigator.files') }}</button>
        </div>
        <section v-show="activeTab === 'open'" class="navigator-section tab-panel" role="tabpanel">
          <div v-if="documents.length" class="file-list" role="list">
            <button v-for="document in documents" :key="document.id" class="file-row" :class="{ active: document.id === activeDocumentId }" type="button" role="listitem" :aria-current="document.id === activeDocumentId ? 'page' : undefined" :title="document.path" @click="emit('select', document.id)">
              <FileText :size="16" aria-hidden="true" />
              <span class="file-name">{{ document.title }}</span>
              <span v-if="document.dirty" class="dirty-dot" :aria-label="t('tabs.unsaved')" />
            </button>
          </div>
          <p v-else class="navigator-empty">{{ t('navigator.empty') }}</p>
        </section>
        <section v-show="activeTab === 'directory'" class="navigator-section directory-section tab-panel" role="tabpanel">
          <div class="directory-heading">
            <h2 class="section-title">{{ t('navigator.currentDirectory') }}</h2>
            <button v-if="directory?.parentPath" class="directory-up" type="button" :aria-label="t('navigator.up')" :title="t('navigator.up')" @click="emit('browseDirectory', directory.parentPath)"><ChevronUp :size="15" aria-hidden="true" /></button>
          </div>
          <p v-if="directory" class="directory-path" :title="directory.path">{{ directory.path }}</p>
          <p v-if="directoryError" class="navigator-empty">{{ t('navigator.directoryUnavailable') }}</p>
          <div v-else-if="directory?.entries.length" class="file-list directory-list" role="list">
            <button v-for="entry in directory.entries" :key="entry.path" class="file-row" :class="{ active: !entry.isDirectory && entry.path === activeDocumentId }" type="button" role="listitem" :aria-current="!entry.isDirectory && entry.path === activeDocumentId ? 'page' : undefined" :title="entry.path" @click="entry.isDirectory ? emit('browseDirectory', entry.path) : emit('openPath', entry.path)">
              <Folder v-if="entry.isDirectory" :size="16" aria-hidden="true" />
              <FileText v-else :size="16" aria-hidden="true" />
              <span class="file-name">{{ entry.name }}</span>
            </button>
          </div>
          <p v-else-if="directory" class="navigator-empty">{{ t('navigator.directoryEmpty') }}</p>
          <p v-if="directory?.truncated" class="directory-truncated">{{ t('navigator.directoryTruncated', { count: 1000 }) }}</p>
        </section>
      </CollapsibleContent>
    </CollapsibleRoot>
  </aside>
</template>

<style scoped>
.navigator { background: var(--panel); border-right: 1px solid var(--line); display: flex; flex-direction: column; min-height: 0; overflow: hidden; transition: width 160ms ease; width: 252px; }
.navigator.collapsed { width: 46px; }
.navigator-header { align-items: center; border-bottom: 1px solid var(--line); display: flex; flex: 0 0 46px; justify-content: space-between; padding: 0 8px 0 13px; }
.navigator-title { align-items: center; color: var(--ink); display: flex; font-size: 12px; font-weight: 650; gap: 8px; min-width: 0; }
.navigator-toggle { align-items: center; background: transparent; border: 0; border-radius: 4px; color: var(--muted); cursor: pointer; display: inline-flex; flex: 0 0 30px; height: 30px; justify-content: center; padding: 0; }
.navigator-toggle:hover { background: var(--accent-soft); color: var(--accent); }
.navigator-toggle:focus-visible, .file-row:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
.navigator-collapsible { display: flex; flex: 1 1 auto; min-height: 0; }
.navigator-content { display: flex; flex: 1 1 auto; flex-direction: column; min-height: 0; }
.navigator-tabs { align-items: center; background: var(--tabs); border-bottom: 1px solid var(--line); display: grid; flex: 0 0 46px; gap: 4px; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); padding: 6px 8px; }
.navigator-tab { align-items: center; background: transparent; border: 1px solid transparent; border-radius: 4px; color: var(--muted); cursor: pointer; display: flex; font: inherit; font-size: 11px; font-weight: 600; height: 32px; justify-content: center; min-width: 0; overflow: hidden; padding: 0 8px; text-overflow: ellipsis; white-space: nowrap; }
.navigator-tab:hover { background: color-mix(in srgb, var(--surface) 52%, transparent); color: var(--ink); }
.navigator-tab.active { background: var(--surface); border-color: var(--line); box-shadow: inset 0 -2px 0 var(--accent); color: var(--ink); }
.navigator-tab:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
.navigator-section { display: flex; flex: 1 1 auto; flex-direction: column; min-height: 0; overflow-x: hidden; overflow-y: auto; padding: 10px 0; }
.section-title { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 10px; font-weight: 650; margin: 0; padding: 0 16px 7px; text-transform: uppercase; }
.directory-heading { align-items: center; display: flex; justify-content: space-between; padding-right: 8px; }
.directory-heading .section-title { padding-right: 0; }
.directory-up { align-items: center; background: transparent; border: 0; border-radius: 4px; color: var(--muted); cursor: pointer; display: inline-flex; height: 24px; justify-content: center; padding: 0; width: 24px; }
.directory-up:hover { background: var(--accent-soft); color: var(--accent); }
.directory-up:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
.file-list { padding: 0 8px; }
.file-row { align-items: center; background: transparent; border: 0; border-radius: 4px; color: var(--muted); cursor: pointer; display: flex; font: inherit; gap: 9px; min-height: 34px; padding: 0 8px; text-align: left; width: 100%; }
.file-row:hover { background: color-mix(in srgb, var(--surface) 45%, transparent); color: var(--ink); }
.file-row.active { background: var(--accent-soft); color: var(--ink); }
.file-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.dirty-dot { background: var(--accent); border-radius: 50%; height: 6px; margin-left: auto; width: 6px; }
.navigator-empty { color: var(--muted); font-size: 13px; line-height: 1.55; margin: 0; padding: 18px 16px; }
.directory-path { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 10px; margin: 0; overflow: hidden; padding: 0 16px 8px; text-overflow: ellipsis; white-space: nowrap; }
.directory-truncated { color: var(--muted); font-size: 11px; line-height: 1.4; margin: 0; padding: 8px 16px 0; }
.navigator-section { scrollbar-color: var(--line-strong) var(--panel); scrollbar-width: thin; }
.navigator-section::-webkit-scrollbar { height: 10px; width: 10px; }
.navigator-section::-webkit-scrollbar-track { background: var(--panel); }
.navigator-section::-webkit-scrollbar-thumb { background: var(--line-strong); border: 3px solid var(--panel); border-radius: 8px; }
.navigator-section:hover, .navigator-section:focus-within { scrollbar-color: var(--muted) var(--panel); }
.navigator-section:hover::-webkit-scrollbar-thumb, .navigator-section:focus-within::-webkit-scrollbar-thumb { background: var(--muted); }
@media (max-width: 760px) { .navigator { width: min(252px, 72vw); } .navigator.collapsed { width: 46px; } }
</style>
