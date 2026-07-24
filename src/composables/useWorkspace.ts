import { computed, onBeforeUnmount, onMounted, shallowRef, watch } from 'vue'
import { diagnosticDocumentName, diagnosticErrorMessage, diagnosticFailureReason, logDiagnostic } from '@/diagnostics'
import { i18n } from '@/i18n'
import type { DirectoryListing } from '@/domain/directory'
import type { DocumentInspection, DocumentMetrics, LoadedDocument } from '@/domain/document'
import {
  applyProgressiveMetrics,
  closeWorkspaceDocument,
  createSession,
  createProgressiveWorkspaceDocument,
  createWorkspaceDocument,
  hasExternalContentChanged,
  reloadWorkspaceDocument,
  supportsRichDocumentViews,
  type EditorMode,
  type ThemeName,
  type WorkspaceDocument,
  type WorkspaceSession,
  updateWorkspaceDocument,
} from '@/domain/workspace'

const SESSION_KEY = 'loomark.workspace.v1'
const LEGACY_SESSION_KEY = 'marko.workspace.v1'

function parseSession(value: string | null): WorkspaceSession | null {
  try {
    if (!value) return null
    const session = JSON.parse(value) as WorkspaceSession
    if (!Array.isArray(session.documents)) return null
    return session
  } catch {
    return null
  }
}

function readSession(): WorkspaceSession | null {
  const currentSession = parseSession(window.localStorage.getItem(SESSION_KEY))
  if (currentSession) return currentSession

  const legacyValue = window.localStorage.getItem(LEGACY_SESSION_KEY)
  const legacySession = parseSession(legacyValue)
  if (!legacySession || !legacyValue) return null
  window.localStorage.setItem(SESSION_KEY, legacyValue)
  window.localStorage.removeItem(LEGACY_SESSION_KEY)
  return legacySession
}

export function useWorkspace() {
  const documents = shallowRef<WorkspaceDocument[]>([])
  const activeDocumentId = shallowRef<string | null>(null)
  const directoryError = shallowRef<string | null>(null)
  const directoryListing = shallowRef<DirectoryListing | null>(null)
  const error = shallowRef<string | null>(null)
  const fileWatchError = shallowRef<string | null>(null)
  const isLoading = shallowRef(false)
  const navigatorCollapsed = shallowRef(false)
  const theme = shallowRef<ThemeName>('paper')
  const externalChangePaths = shallowRef<string[]>([])

  const activeDocument = computed(() =>
    documents.value.find((document) => document.id === activeDocumentId.value) ?? null,
  )
  const hasUnsavedChanges = computed(() => documents.value.some((document) => document.dirty))
  const activeExternalChange = computed(() => {
    const document = activeDocument.value
    return document && externalChangePaths.value.includes(document.path) ? document : null
  })
  let directoryRequest = 0
  let unlistenDocumentChanges: (() => void) | null = null
  let watcherStarted = false
  const processingExternalPaths = new Set<string>()

  async function requestDirectory(command: 'browse' | 'siblings', path: string) {
    const request = ++directoryRequest
    directoryError.value = null
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      const listing = command === 'browse'
        ? await desktop.browseMarkdownDirectory(path)
        : await desktop.listMarkdownSiblings(path)
      if (request === directoryRequest) directoryListing.value = listing
    } catch (cause) {
      if (request !== directoryRequest) return
      directoryListing.value = null
      directoryError.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  async function browseDirectory(path: string) {
    await requestDirectory('browse', path)
  }

  function persistSession() {
    window.localStorage.setItem(
      SESSION_KEY,
      JSON.stringify(createSession(documents.value, activeDocumentId.value, theme.value, navigatorCollapsed.value)),
    )
  }

  function clearExternalChange(path: string) {
    externalChangePaths.value = externalChangePaths.value.filter((changedPath) => changedPath !== path)
  }

  function markExternalChange(path: string) {
    if (!documents.value.some((document) => document.path === path)) return
    if (externalChangePaths.value.includes(path)) return
    externalChangePaths.value = [...externalChangePaths.value, path]
  }

  async function handleDocumentFileChange(path: string) {
    if (processingExternalPaths.has(path)) return
    const existing = documents.value.find((document) => document.path === path)
    if (!existing || !existing.sourceReady) return

    processingExternalPaths.add(path)
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      const loaded = await desktop.readDocument(path)
      const current = documents.value.find((document) => document.path === path)
      if (current && hasExternalContentChanged(current, loaded.content)) markExternalChange(path)
    } catch (cause) {
      error.value = cause instanceof Error ? cause.message : String(cause)
    } finally {
      processingExternalPaths.delete(path)
    }
  }

  async function syncDocumentWatcher() {
    if (!watcherStarted || !window.loomark) return
    try {
      await window.loomark.watchDocuments(documents.value.map((document) => document.path))
      fileWatchError.value = null
    } catch (cause) {
      fileWatchError.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  async function startDocumentWatcher() {
    if (!window.loomark) return
    try {
      unlistenDocumentChanges = window.loomark.onDocumentChanged((path) => {
        void handleDocumentFileChange(path)
      })
      watcherStarted = true
      await syncDocumentWatcher()
    } catch (cause) {
      fileWatchError.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  async function loadPath(path: string, mode: EditorMode = 'source') {
    const existing = documents.value.find((document) => document.path === path)
    if (existing) {
      activeDocumentId.value = existing.id
      logDiagnostic('info', 'document.load.reused-open-document', { document: diagnosticDocumentName(path) })
      return
    }

    isLoading.value = true
    error.value = null
    logDiagnostic('info', 'document.load.started', { document: diagnosticDocumentName(path), mode })
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      const inspection = await desktop.inspectDocument(path)
      logDiagnostic('info', 'document.inspect.completed', {
        byteSize: inspection.byteSize,
        document: diagnosticDocumentName(path),
        milliseconds: inspection.preflightMilliseconds.toFixed(1),
        strategy: inspection.strategy,
      })
      if (inspection.strategy === 'unsupported') {
        error.value = i18n.global.t('errors.maxSize')
        logDiagnostic('warn', 'document.load.unsupported', { byteSize: inspection.byteSize, document: diagnosticDocumentName(path) })
        return
      }
      if (inspection.strategy === 'progressive') {
        const preview = await desktop.readDocumentPreview(path)
        logDiagnostic('info', 'document.preview.completed', { characters: preview.length, document: diagnosticDocumentName(path) })
        const document = createProgressiveWorkspaceDocument(inspection, preview)
        documents.value = [...documents.value, document]
        activeDocumentId.value = document.id
        persistSession()
        void measureProgressiveDocument(document.id, path)
        return
      }
      const loaded = await desktop.readDocument(path)
      logDiagnostic('info', 'document.read.completed', {
        document: diagnosticDocumentName(path),
        milliseconds: loaded.readMilliseconds?.toFixed(1),
      })
      const document = createWorkspaceDocument(loaded, mode)
      documents.value = [...documents.value, document]
      activeDocumentId.value = document.id
      persistSession()
      logDiagnostic('info', 'document.load.completed', { document: diagnosticDocumentName(path), sourceReady: document.sourceReady })
    } catch (cause) {
      const message = diagnosticErrorMessage(cause)
      error.value = message
      logDiagnostic('error', 'document.load.failed', { document: diagnosticDocumentName(path), reason: diagnosticFailureReason(cause) })
    } finally {
      isLoading.value = false
      logDiagnostic('info', 'document.load.settled', { document: diagnosticDocumentName(path), isLoading: false })
    }
  }

  async function measureProgressiveDocument(id: string, path: string) {
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      const metrics = await desktop.measureDocument(path)
      documents.value = documents.value.map((document) =>
        document.id === id && !document.sourceReady
          ? applyProgressiveMetrics(document, metrics)
          : document,
      )
      logDiagnostic('info', 'document.progressive-metrics.completed', {
        document: diagnosticDocumentName(path),
        milliseconds: metrics.readMilliseconds?.toFixed(1),
      })
    } catch (cause) {
      logDiagnostic('warn', 'document.progressive-metrics.failed', { document: diagnosticDocumentName(path), reason: diagnosticFailureReason(cause) })
    }
  }

  async function openDocument() {
    const selectedPath = await window.loomark?.selectMarkdownFile()
    if (!selectedPath) return
    await loadPath(selectedPath)
  }

  function selectDocument(id: string) {
    activeDocumentId.value = id
    persistSession()
  }

  function closeDocument(id: string) {
    const index = documents.value.findIndex((document) => document.id === id)
    const document = documents.value[index]
    documents.value = closeWorkspaceDocument(documents.value, id)
    if (document) clearExternalChange(document.path)
    if (activeDocumentId.value === id) {
      activeDocumentId.value = documents.value[index - 1]?.id ?? documents.value[index]?.id ?? null
    }
    persistSession()
  }

  function updateActiveDocument(content: string) {
    const active = activeDocument.value
    if (!active || !active.sourceReady) return
    documents.value = documents.value.map((document) =>
      document.id === active.id ? updateWorkspaceDocument(document, content) : document,
    )
  }

  function setMode(mode: EditorMode) {
    const active = activeDocument.value
    if (!active || (mode !== 'source' && !supportsRichDocumentViews(active))) return
    documents.value = documents.value.map((document) =>
      document.id === active.id ? { ...document, mode } : document,
    )
    persistSession()
  }

  function setTheme(nextTheme: ThemeName) {
    theme.value = nextTheme
    persistSession()
  }

  function setNavigatorCollapsed(collapsed: boolean) {
    navigatorCollapsed.value = collapsed
    persistSession()
  }

  function recordEditorInitialization(milliseconds: number) {
    const active = activeDocument.value
    if (!active) return
    documents.value = documents.value.map((document) =>
      document.id === active.id ? { ...document, editorMilliseconds: milliseconds } : document,
    )
    logDiagnostic('info', 'editor.initialization.recorded', { document: diagnosticDocumentName(active.path), milliseconds: milliseconds.toFixed(1) })
  }

  function recordEditorFailure(message: string) {
    const active = activeDocument.value
    error.value = i18n.global.t('errors.editorInitialization')
    logDiagnostic('error', 'editor.initialization.reported-failed', {
      document: active ? diagnosticDocumentName(active.path) : 'none',
      reason: diagnosticFailureReason(message),
    })
  }

  async function saveActiveDocument() {
    const active = activeDocument.value
    if (!active || !active.sourceReady || !active.dirty) return
    error.value = null
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      await desktop.saveDocument(active.path, active.content)
      documents.value = documents.value.map((document) =>
        document.id === active.id
          ? { ...document, originalContent: document.content, dirty: false }
          : document,
      )
      persistSession()
    } catch (cause) {
      error.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  async function reloadExternalChange(path: string) {
    if (processingExternalPaths.has(path)) return
    const existing = documents.value.find((document) => document.path === path)
    if (!existing) return

    processingExternalPaths.add(path)
    isLoading.value = true
    error.value = null
    try {
      const desktop = window.loomark
      if (!desktop) throw new Error('Desktop file access is unavailable.')
      const loaded = await desktop.readDocument(path)
      documents.value = documents.value.map((document) =>
        document.path === path ? reloadWorkspaceDocument(document, loaded) : document,
      )
      clearExternalChange(path)
      persistSession()
    } catch (cause) {
      error.value = cause instanceof Error ? cause.message : String(cause)
    } finally {
      isLoading.value = false
      processingExternalPaths.delete(path)
    }
  }

  async function restoreSession() {
    const session = readSession()
    if (!session) {
      logDiagnostic('info', 'session.restore.skipped')
      return
    }
    logDiagnostic('info', 'session.restore.started', { documents: session.documents.length })
    theme.value = session.theme === 'night' ? 'night' : 'paper'
    navigatorCollapsed.value = session.navigatorCollapsed === true
    for (const document of session.documents) {
      await loadPath(document.path, document.mode)
    }
    if (session.activeDocumentId && documents.value.some((document) => document.id === session.activeDocumentId)) {
      activeDocumentId.value = session.activeDocumentId
    }
    logDiagnostic('info', 'session.restore.completed', { documents: documents.value.length })
  }

  function handleSaveShortcut(event: KeyboardEvent) {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 's') {
      event.preventDefault()
      void saveActiveDocument()
    }
  }

  watch([documents, activeDocumentId, navigatorCollapsed, theme], persistSession, { deep: false })
  watch(
    () => documents.value.map((document) => document.path).join('\u0000'),
    () => { void syncDocumentWatcher() },
  )
  watch(
    () => activeDocument.value?.path,
    (path) => {
      if (path) {
        void requestDirectory('siblings', path)
        return
      }
      directoryRequest += 1
      directoryError.value = null
      directoryListing.value = null
    },
    { immediate: true },
  )
  onMounted(() => {
    window.addEventListener('keydown', handleSaveShortcut)
    void restoreSession().finally(() => { void startDocumentWatcher() })
  })
  onBeforeUnmount(() => {
    window.removeEventListener('keydown', handleSaveShortcut)
    unlistenDocumentChanges?.()
    if (watcherStarted && window.loomark) void window.loomark.watchDocuments([])
  })

  return {
    activeDocument,
    activeExternalChange,
    browseDirectory,
    closeDocument,
    directoryError,
    directoryListing,
    documents,
    error,
    fileWatchError,
    hasUnsavedChanges,
    isLoading,
    navigatorCollapsed,
    openPath: loadPath,
    openDocument,
    recordEditorInitialization,
    recordEditorFailure,
    reloadExternalChange,
    saveActiveDocument,
    selectDocument,
    setMode,
    setNavigatorCollapsed,
    setTheme,
    theme,
    updateActiveDocument,
    clearExternalChange,
  }
}
