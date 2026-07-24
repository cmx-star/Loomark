import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-dialog'
import { computed, onBeforeUnmount, onMounted, shallowRef, watch } from 'vue'
import { i18n } from '@/i18n'
import type { DirectoryListing } from '@/domain/directory'
import type { DocumentInspection, LoadedDocument } from '@/domain/document'
import {
  closeWorkspaceDocument,
  createSession,
  createWorkspaceDocument,
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
  const isLoading = shallowRef(false)
  const navigatorCollapsed = shallowRef(false)
  const theme = shallowRef<ThemeName>('paper')

  const activeDocument = computed(() =>
    documents.value.find((document) => document.id === activeDocumentId.value) ?? null,
  )
  const hasUnsavedChanges = computed(() => documents.value.some((document) => document.dirty))
  let directoryRequest = 0

  async function requestDirectory(command: 'browse_markdown_directory' | 'list_markdown_siblings', path: string) {
    const request = ++directoryRequest
    directoryError.value = null
    try {
      const listing = await invoke<DirectoryListing>(command, { path })
      if (request === directoryRequest) directoryListing.value = listing
    } catch (cause) {
      if (request !== directoryRequest) return
      directoryListing.value = null
      directoryError.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  async function browseDirectory(path: string) {
    await requestDirectory('browse_markdown_directory', path)
  }

  function persistSession() {
    window.localStorage.setItem(
      SESSION_KEY,
      JSON.stringify(createSession(documents.value, activeDocumentId.value, theme.value, navigatorCollapsed.value)),
    )
  }

  async function loadPath(path: string, mode: EditorMode = 'source') {
    const existing = documents.value.find((document) => document.path === path)
    if (existing) {
      activeDocumentId.value = existing.id
      return
    }

    isLoading.value = true
    error.value = null
    try {
      const inspection = await invoke<DocumentInspection>('inspect_document', { path })
      if (inspection.strategy === 'unsupported') {
        error.value = i18n.global.t('errors.maxSize')
        return
      }
      const loaded = await invoke<LoadedDocument>('read_document', { path })
      const document = createWorkspaceDocument(loaded, mode)
      documents.value = [...documents.value, document]
      activeDocumentId.value = document.id
      persistSession()
    } catch (cause) {
      error.value = cause instanceof Error ? cause.message : String(cause)
    } finally {
      isLoading.value = false
    }
  }

  async function openDocument() {
    const selectedPath = await open({
      multiple: false,
      filters: [{ name: 'Markdown', extensions: ['md', 'markdown', 'mdown', 'mkdn', 'mdtxt'] }],
    })
    if (!selectedPath || Array.isArray(selectedPath)) return
    await loadPath(selectedPath)
  }

  function selectDocument(id: string) {
    activeDocumentId.value = id
    persistSession()
  }

  function closeDocument(id: string) {
    const index = documents.value.findIndex((document) => document.id === id)
    documents.value = closeWorkspaceDocument(documents.value, id)
    if (activeDocumentId.value === id) {
      activeDocumentId.value = documents.value[index - 1]?.id ?? documents.value[index]?.id ?? null
    }
    persistSession()
  }

  function updateActiveDocument(content: string) {
    const active = activeDocument.value
    if (!active) return
    documents.value = documents.value.map((document) =>
      document.id === active.id ? updateWorkspaceDocument(document, content) : document,
    )
  }

  function setMode(mode: EditorMode) {
    const active = activeDocument.value
    if (!active) return
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
  }

  async function saveActiveDocument() {
    const active = activeDocument.value
    if (!active || !active.dirty) return
    error.value = null
    try {
      await invoke('save_document', { path: active.path, content: active.content })
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

  async function restoreSession() {
    const session = readSession()
    if (!session) return
    theme.value = session.theme === 'night' ? 'night' : 'paper'
    navigatorCollapsed.value = session.navigatorCollapsed === true
    for (const document of session.documents) {
      await loadPath(document.path, document.mode)
    }
    if (session.activeDocumentId && documents.value.some((document) => document.id === session.activeDocumentId)) {
      activeDocumentId.value = session.activeDocumentId
    }
  }

  function handleSaveShortcut(event: KeyboardEvent) {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 's') {
      event.preventDefault()
      void saveActiveDocument()
    }
  }

  watch([documents, activeDocumentId, navigatorCollapsed, theme], persistSession, { deep: false })
  watch(
    () => activeDocument.value?.path,
    (path) => {
      if (path) {
        void requestDirectory('list_markdown_siblings', path)
        return
      }
      directoryRequest += 1
      directoryError.value = null
      directoryListing.value = null
    },
    { immediate: true },
  )
  onMounted(() => {
    void restoreSession()
    window.addEventListener('keydown', handleSaveShortcut)
  })
  onBeforeUnmount(() => window.removeEventListener('keydown', handleSaveShortcut))

  return {
    activeDocument,
    browseDirectory,
    closeDocument,
    directoryError,
    directoryListing,
    documents,
    error,
    hasUnsavedChanges,
    isLoading,
    navigatorCollapsed,
    openPath: loadPath,
    openDocument,
    recordEditorInitialization,
    saveActiveDocument,
    selectDocument,
    setMode,
    setNavigatorCollapsed,
    setTheme,
    theme,
    updateActiveDocument,
  }
}
