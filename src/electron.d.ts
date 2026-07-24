import type { LoomarkDesktopApi } from '@/desktop-contract'

declare global {
  interface Window {
    loomark?: LoomarkDesktopApi
  }
}

export {}
