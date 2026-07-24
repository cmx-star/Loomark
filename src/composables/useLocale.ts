import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { persistLocale, resolveLocale, type AppLocale } from '@/i18n'

export function useLocale() {
  const { locale } = useI18n({ useScope: 'global' })
  const currentLocale = computed<AppLocale>({
    get: () => resolveLocale(locale.value),
    set: (value) => {
      locale.value = value
      persistLocale(value)
    },
  })

  return { currentLocale }
}
