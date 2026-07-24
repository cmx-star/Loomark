import { defineConfig } from 'vite'
import tailwindcss from '@tailwindcss/vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  base: './',
  plugins: [tailwindcss(), vue()],
  resolve: {
    alias: { '@': new URL('./src', import.meta.url).pathname },
    preserveSymlinks: false,
  },
})
