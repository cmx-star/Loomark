import { MakerDeb } from '@electron-forge/maker-deb'
import { MakerDMG } from '@electron-forge/maker-dmg'
import { MakerRpm } from '@electron-forge/maker-rpm'
import { MakerSquirrel } from '@electron-forge/maker-squirrel'
import { MakerZIP } from '@electron-forge/maker-zip'
import type { ForgeConfig } from '@electron-forge/shared-types'
import electronChecksums from 'electron/checksums.json'

const config: ForgeConfig = {
  packagerConfig: {
    appBundleId: 'io.md.loomark',
    download: {
      checksums: electronChecksums,
    },
    executableName: 'loomark',
    icon: 'resources/icons/icon',
    name: 'Loomark',
  },
  makers: [
    new MakerSquirrel(),
    new MakerZIP({}, ['darwin', 'linux', 'win32']),
    new MakerDMG(),
    new MakerDeb(),
    new MakerRpm(),
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
