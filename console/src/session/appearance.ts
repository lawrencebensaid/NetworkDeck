/*
 * Appearance, per browser: the theme (follow the system, or always light or dark). Kept in
 * localStorage; index.html applies it before the page draws so it doesn't flash.
 */
import { reactive, watch } from "vue"

export type Theme = "system" | "light" | "dark"

const THEME_KEY = "nd-theme"

function read<T>(key: string, fallback: T): T {
  try {
    const v = localStorage.getItem(key)
    return v ? (JSON.parse(v) as T) : fallback
  } catch {
    return fallback
  }
}
function write(key: string, v: unknown) {
  try {
    localStorage.setItem(key, JSON.stringify(v))
  } catch {
    /* private window: the choice lasts until the page closes */
  }
}

export const appearance = reactive({
  theme: read<Theme>(THEME_KEY, "system"),
})

const media = typeof matchMedia === "function" ? matchMedia("(prefers-color-scheme: dark)") : null

function apply() {
  const dark = appearance.theme === "dark" || (appearance.theme === "system" && !!media?.matches)
  document.documentElement.classList.toggle("dark", dark)
}

media?.addEventListener("change", apply)
watch(
  () => appearance.theme,
  (t) => {
    write(THEME_KEY, t)
    apply()
  },
  { immediate: true },
)
