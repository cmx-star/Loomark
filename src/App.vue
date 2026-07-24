<script setup lang="ts">
import { computed, defineAsyncComponent } from 'vue'
import { useI18n } from 'vue-i18n'
import DocumentTabs from '@/components/DocumentTabs.vue'
import DocumentStatus from '@/components/DocumentStatus.vue'
import ExternalChangeNotice from '@/components/ExternalChangeNotice.vue'
import MarkdownPreview from '@/components/MarkdownPreview.vue'
import { diagnosticErrorMessage, diagnosticFailureReason, logDiagnostic } from '@/diagnostics'
import WorkspaceNavigator from '@/components/WorkspaceNavigator.vue'
import { useNativeMenu } from '@/composables/useNativeMenu'
import { useWorkspace } from '@/composables/useWorkspace'
import { useLocale } from '@/composables/useLocale'
import { supportsRichDocumentViews } from '@/domain/workspace'

const { t } = useI18n()
const { currentLocale } = useLocale()
const {
  activeDocument,
  activeExternalChange,
  browseDirectory,
  closeDocument,
  directoryError,
  directoryListing,
  documents,
  error,
  fileWatchError,
  isLoading,
  navigatorCollapsed,
  openPath,
  openDocument,
  recordEditorFailure,
  recordEditorInitialization,
  reloadExternalChange,
  saveActiveDocument,
  selectDocument,
  setMode,
  setNavigatorCollapsed,
  setTheme,
  theme,
  updateActiveDocument,
  clearExternalChange,
} = useWorkspace()

const CodeEditor = defineAsyncComponent({
  loader: async () => {
    logDiagnostic('info', 'editor.module-load.started')
    const component = await import('@/components/CodeEditor.vue')
    logDiagnostic('info', 'editor.module-load.completed')
    return component
  },
  timeout: 30_000,
  onError(cause, _retry, fail) {
    const message = diagnosticErrorMessage(cause)
    logDiagnostic('error', 'editor.module-load.failed', { reason: diagnosticFailureReason(cause) })
    recordEditorFailure(message)
    fail()
  },
})

function printDocument() {
  window.print()
}

async function openLogDirectory() {
  try {
    if (!window.loomark) throw new Error('Desktop file access is unavailable.')
    await window.loomark.openLogDirectory()
    logDiagnostic('info', 'log-directory.opened')
  } catch (cause) {
    logDiagnostic('error', 'log-directory.open.failed', { reason: diagnosticFailureReason(cause) })
  }
}

const activeMode = computed(() => activeDocument.value?.mode ?? 'source')
const hasActiveDocument = computed(() => activeDocument.value !== null)
const activeDocumentDirty = computed(() => activeDocument.value?.dirty === true)
const activeDocumentSupportsRichViews = computed(() =>
  activeDocument.value ? supportsRichDocumentViews(activeDocument.value) : false,
)

function closeActiveDocument() {
  if (activeDocument.value) closeDocument(activeDocument.value.id)
}

useNativeMenu({
  activeDocument: hasActiveDocument,
  closeActiveDocument,
  dirty: activeDocumentDirty,
  locale: currentLocale,
  mode: activeMode,
  navigatorCollapsed,
  openDocument,
  openLogDirectory,
  printDocument,
  saveActiveDocument,
  richViewsAvailable: activeDocumentSupportsRichViews,
  setLocale: (locale) => { currentLocale.value = locale },
  setMode,
  setNavigatorCollapsed,
  setTheme,
  theme,
})
</script>

<template>
  <main class="app-shell" :data-theme="theme">
    <WorkspaceNavigator :active-document-id="activeDocument?.id ?? null" :collapsed="navigatorCollapsed" :directory="directoryListing" :directory-error="directoryError" :documents="documents" @browse-directory="browseDirectory" @open-path="openPath" @select="selectDocument" @set-collapsed="setNavigatorCollapsed" />
    <section class="workspace-shell">
      <DocumentTabs :active-document-id="activeDocument?.id ?? null" :documents="documents" @close="closeDocument" @select="selectDocument" />
      <section class="document-area" :aria-label="t('app.workspace')">
        <ExternalChangeNotice v-if="activeExternalChange" :dirty="activeExternalChange.dirty" :path="activeExternalChange.path" @keep="clearExternalChange(activeExternalChange.path)" @reload="reloadExternalChange(activeExternalChange.path)" />
        <div v-if="fileWatchError" class="notice warning" role="status">{{ fileWatchError }}</div>
        <div v-if="error" class="notice error" role="alert">{{ error }}</div>
        <section v-else-if="activeDocument && !activeDocument.sourceReady" class="document-view progressive-preview" :aria-label="t('editor.sourceLabel')">
          <p class="preview-status">{{ t('app.progressivePreview') }}</p>
          <pre class="preview-content">{{ activeDocument.content }}</pre>
        </section>
        <section v-else-if="activeDocument" class="document-view" :class="`mode-${activeDocument.mode}`">
          <CodeEditor v-if="activeDocument.mode !== 'reading' && activeDocumentSupportsRichViews" :content="activeDocument.content" @failed="recordEditorFailure" @initialized="recordEditorInitialization" @update:content="updateActiveDocument" />
          <MarkdownPreview v-if="activeDocument.mode !== 'source'" :content="activeDocument.content" />
        </section>
        <div v-else class="empty-workspace">
          <p class="empty-label">{{ t('app.emptyLabel') }}</p>
          <p>{{ t('app.emptyText') }}</p>
        </div>
      </section>
      <DocumentStatus :document="activeDocument" :inspection="activeDocument" :phase="isLoading ? 'loading' : 'ready'" />
    </section>
  </main>
</template>

<style scoped>
.app-shell { background: var(--canvas); display: flex; height: 100%; min-width: 0; }
.workspace-shell { display: grid; flex: 1; grid-template-rows: auto minmax(0, 1fr) auto; min-width: 0; }
.document-area { display: flex; flex-direction: column; min-height: 0; overflow: hidden; }
.empty-workspace, .notice { margin: 40px auto; max-width: 520px; padding: 0 24px; }
.empty-workspace p { color: var(--muted); font-size: 15px; line-height: 1.65; }
.empty-workspace .empty-label { color: var(--accent); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; text-transform: uppercase; }
.notice { border-left: 3px solid var(--accent); color: var(--ink); line-height: 1.55; }
.notice.error { border-color: var(--danger); color: var(--danger); }
.notice.warning { color: var(--muted); }
.document-view { background: var(--surface); flex: 1; min-height: 0; }
.progressive-preview { display: flex; flex-direction: column; overflow: hidden; }
.preview-status { border-bottom: 1px solid var(--line); color: var(--muted); font-size: 12px; margin: 0; padding: 8px 14px; }
.preview-content { color: var(--ink); flex: 1; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 13px; line-height: 1.55; margin: 0; overflow: auto; padding: 18px 22px; white-space: pre-wrap; word-break: break-word; }
.mode-split { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); }
.mode-split > :last-child { border-left: 1px solid var(--line); overflow: auto; }
@media (max-width: 760px) { .mode-split { grid-template-columns: minmax(0, 1fr); } .mode-split > :last-child { border-left: 0; border-top: 1px solid var(--line); } }
</style>
