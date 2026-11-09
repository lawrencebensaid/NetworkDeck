<script setup lang="ts">
/* The firmware: what runs now, and installing a new .bin (checked, then confirmed on the deck). */
import { ref } from "vue"
import { toast } from "vue-sonner"

import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import { Spinner } from "@/components/ui/spinner"
import { api } from "../session/api.ts"
import { store } from "../session/store.ts"

const file = ref<File | null>(null)
const progress = ref(0)
const stage = ref<"idle" | "uploading" | "confirming" | "restarting">("idle")

async function install() {
  if (!file.value) return
  stage.value = "uploading"
  progress.value = 0
  const r = await api.update(file.value, (f) => {
    progress.value = Math.round(f * 100)
    if (f >= 1) stage.value = "confirming"
  })
  if (r.ok) {
    stage.value = "restarting"
    toast.success(r.message)
    setTimeout(() => location.reload(), 12000)
  } else {
    stage.value = "idle"
    toast.error(r.message)
  }
}
</script>

<template>
  <div class="flex flex-col gap-5">
    <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
      <dt class="text-muted-foreground">Build</dt>
      <dd class="font-mono">{{ store.status?.build ?? "–" }}</dd>
      <dt class="text-muted-foreground">Reported to the app</dt>
      <dd class="font-mono">{{ store.status?.firmware ?? "–" }}</dd>
    </dl>
    <div class="flex flex-col gap-2">
      <label class="text-sm font-medium" for="fw">Install an update</label>
      <input
        id="fw"
        type="file"
        accept=".bin"
        class="text-sm file:mr-3 file:rounded-md file:border file:border-input file:bg-background file:px-3 file:py-1.5 file:text-sm file:text-foreground"
        :disabled="stage !== 'idle'"
        @change="file = ($event.target as HTMLInputElement).files?.[0] ?? null"
      />
      <p class="text-xs text-muted-foreground">Use <code>networkdeck-&lt;version&gt;.bin</code> from a release. If the new firmware does not start, the dock goes back to this one.</p>
    </div>
    <Progress v-if="stage === 'uploading'" :model-value="progress" />
    <p v-if="stage === 'confirming' && !store.viaHotspot" class="flex items-center gap-2 text-sm" role="status"><Spinner class="size-4" /> Checking it. Then confirm on the Stream Deck: press ✓.</p>
    <p v-if="stage === 'restarting'" class="flex items-center gap-2 text-sm" role="status"><Spinner class="size-4" /> Restarting with the new firmware…</p>
    <Button :disabled="!file || stage !== 'idle'" @click="install">Install</Button>
  </div>
</template>
