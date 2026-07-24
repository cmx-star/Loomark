import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-dialog'
import { computed, shallowRef } from 'vue'
import type { DocumentInspection, LoadedDocument } from '@/domain/document'

type LoadPhase = 'idle' | 'preflighting' | 'preview' | 'loading' | 'ready' | 'unsupported' | 'error'

export function useDocumentLoader() {
  const inspection = shallowRef<DocumentInspection | null>(null)
  const document = shallowRef<LoadedDocument | null>(null)
  const preview = shallowRef('')
  const phase = shallowRef<LoadPhase>('idle')
  const error = shallowRef<string | null>(null)

  const isLoading = computed(() => phase.value === 'preflighting' || phase.value === 'loading')

  async function openDocument() {
    const selectedPath = await open({
      multiple: false,
      filters: [{ name: 'Markdown', extensions: ['md', 'markdown', 'mdown', 'mkdn', 'mdtxt'] }],
    })
    if (!selectedPath || Array.isArray(selectedPath)) return
    await loadPath(selectedPath)
  }

  async function loadPath(path: string) {
    phase.value = 'preflighting'
    error.value = null
    document.value = null
    preview.value = ''

    try {
      const nextInspection = await invoke<DocumentInspection>('inspect_document', { path })
      inspection.value = nextInspection
      if (nextInspection.strategy === 'unsupported') {
        phase.value = 'unsupported'
        return
      }

      if (nextInspection.strategy === 'progressive') {
        preview.value = await invoke<string>('read_document_preview', { path })
        phase.value = 'preview'
      }

      phase.value = 'loading'
      document.value = await invoke<LoadedDocument>('read_document', { path })
      phase.value = 'ready'
    } catch (cause) {
      phase.value = 'error'
      error.value = cause instanceof Error ? cause.message : String(cause)
    }
  }

  function recordEditorInitialization(milliseconds: number) {
    if (!document.value) return
    document.value = { ...document.value, editorMilliseconds: milliseconds }
  }

  return {
    document,
    error,
    inspection,
    isLoading,
    openDocument,
    phase,
    preview,
    recordEditorInitialization,
  }
}
