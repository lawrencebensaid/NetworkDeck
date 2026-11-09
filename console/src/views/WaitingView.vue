<script setup lang="ts">
import { Spinner } from "@/components/ui/spinner"
import BrandMark from "../components/BrandMark.vue"

defineProps<{ unreachable: boolean }>()
</script>

<template>
  <main class="flex min-h-dvh flex-col items-center justify-center gap-4 bg-background p-6 text-center">
    <BrandMark :size="44" />
    <div class="flex items-center gap-2 text-muted-foreground">
      <Spinner class="size-4" />
      <span>{{ unreachable ? "The dock does not answer. Trying again…" : "Connecting to the dock…" }}</span>
    </div>
  </main>
</template>
