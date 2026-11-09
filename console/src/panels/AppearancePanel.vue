<script setup lang="ts">
/* How this page looks, in this browser: the theme. */
import { Monitor, Moon, Sun } from "@lucide/vue"

import { cn } from "@/lib/utils"
import { appearance, type Theme } from "../session/appearance.ts"

const themes: { id: Theme; label: string; icon: typeof Sun }[] = [
  { id: "system", label: "System", icon: Monitor },
  { id: "light", label: "Light", icon: Sun },
  { id: "dark", label: "Dark", icon: Moon },
]

const choice = (on: boolean) =>
  cn(
    "flex flex-1 flex-col items-center gap-2 rounded-lg border px-3 py-3 text-sm transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
    on ? "border-primary bg-primary/5 font-medium text-foreground" : "text-muted-foreground hover:bg-muted/60 hover:text-foreground",
  )
</script>

<template>
  <div class="flex flex-col gap-7">
    <section class="flex flex-col gap-2">
      <h3 class="text-sm font-medium" id="theme-label">Theme</h3>
      <div class="flex gap-2" role="radiogroup" aria-labelledby="theme-label">
        <button
          v-for="t in themes"
          :key="t.id"
          type="button"
          role="radio"
          :aria-checked="appearance.theme === t.id"
          :data-theme-choice="t.id"
          :class="choice(appearance.theme === t.id)"
          @click="appearance.theme = t.id"
        >
          <component :is="t.icon" class="size-5" />
          {{ t.label }}
        </button>
      </div>
      <p class="text-xs text-muted-foreground">System follows your computer's light or dark mode.</p>
    </section>

  </div>
</template>
