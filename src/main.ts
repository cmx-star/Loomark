import { createApp } from 'vue'
import App from './App.vue'
import './assets/main.css'
import { logDiagnostic } from './diagnostics'
import { i18n } from './i18n'

logDiagnostic('info', 'application.mount.started')
createApp(App).use(i18n).mount('#app')
logDiagnostic('info', 'application.mount.completed')
