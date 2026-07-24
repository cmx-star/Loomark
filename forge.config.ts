import type { ForgeConfig } from '@electron-forge/shared-types'
import electronChecksums from 'electron/checksums.json'

const config: ForgeConfig = {
  packagerConfig: {
    appBundleId: 'io.md.loomark',
    download: {
      checksums: electronChecksums,
    },
    icon: 'resources/icons/icon',
    name: 'Loomark',
  },
  makers: [
    { name: '@electron-forge/maker-squirrel' },
    { name: '@electron-forge/maker-zip', platforms: ['darwin', 'linux', 'win32'] },
    { name: '@electron-forge/maker-dmg' },
    { name: '@electron-forge/maker-deb' },
    { name: '@electron-forge/maker-rpm' },
  ],
  plugins: [
    {
      name: '@electron-forge/plugin-vite',
      config: {
        build: [
          { entry: 'electron/main.ts', config: 'vite.main.config.ts', target: 'main' },
          { entry: 'electron/preload.ts', config: 'vite.preload.config.ts', target: 'preload' },
        ],
        renderer: [{ name: 'main_window', config: 'vite.renderer.config.ts' }],
      },
    },
  ],
}

export default config
