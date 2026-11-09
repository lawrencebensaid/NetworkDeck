<script setup lang="ts">
/*
 * The firmware: what runs now, and installing an update. Drop (or pick) the .bin from a GitHub release;
 * the page reads the version out of the image first, so a wrong file is caught before it is sent. The
 * dock checks it again, and it is confirmed on the deck before the dock starts it.
 */
import { computed, ref } from "vue"
import { toast } from "vue-sonner"
import { FileUp, X } from "@lucide/vue"

import { Button } from "@/components/ui/button"
import { Progress } from "@/components/ui/progress"
import { Spinner } from "@/components/ui/spinner"
import { cn } from "@/lib/utils"
import { api } from "../session/api.ts"
import { store } from "../session/store.ts"

const RELEASES = "https://github.com/lawrencebensaid/NetworkDeck/releases/latest"
const PROJECT = "deck32" /* esp_app_desc_t.project_name of this firmware */

const file = ref<File | null>(null)
const version = ref("")
const problem = ref("")
const over = ref(false)
const progress = ref(0)
const stage = ref<"idle" | "uploading" | "confirming" | "restarting">("idle")
const busy = computed(() => stage.value !== "idle")
const input = ref<HTMLInputElement | null>(null)

function cstr(bytes: Uint8Array) {
  const end = bytes.indexOf(0)
  return new TextDecoder().decode(end < 0 ? bytes : bytes.subarray(0, end))
}

/* An ESP-IDF app image: 0xE9 first, and its esp_app_desc_t (magic 0xABCD5432) at byte 32. */
async function inspect(f: File) {
  file.value = f
  version.value = ""
  problem.value = ""
  const head = new Uint8Array(await f.slice(0, 256).arrayBuffer())
  const view = new DataView(head.buffer)
  if (f.name.endsWith("-full.bin")) {
    problem.value = "That is the image for a first install over USB. Use the one without -full."
    return
  }
  if (head.length < 112 || head[0] !== 0xe9 || view.getUint32(32, true) !== 0xabcd5432) {
    problem.value = "This isn't a firmware image. Use networkdeck-<version>.bin from a release."
    return
  }
  const project = cstr(head.subarray(80, 112))
  if (project !== PROJECT) {
    problem.value = `This image is for '${project}', not NetworkDeck.`
    return
  }
  version.value = cstr(head.subarray(48, 80))
}

function drop(e: DragEvent) {
  over.value = false
  if (busy.value) return
  const f = e.dataTransfer?.files?.[0]
  if (f) void inspect(f)
}

function clear() {
  file.value = null
  version.value = ""
  problem.value = ""
  if (input.value) input.value.value = ""
}

async function install() {
  if (!file.value || problem.value) return
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

const size = (n: number) => `${(n / 1024 / 1024).toFixed(1)} MB`
import { versionKind, versionLabel } from "@/lib/version"
</script>

<template>
  <div class="flex flex-col gap-5">
    <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
      <dt class="text-muted-foreground">Version</dt>
      <dd><span class="font-mono">{{ versionLabel(store.status?.build) }}</span> <span class="text-muted-foreground">· {{ versionKind(store.status?.build) }}</span></dd>
      <dt class="text-muted-foreground">Reported to the app</dt>
      <dd class="font-mono">{{ store.status?.firmware ?? "–" }}</dd>
    </dl>

    <div class="flex flex-col gap-2">
      <span class="text-sm font-medium">Install an update</span>
      <div
        v-if="!file"
        role="button"
        tabindex="0"
        data-testid="firmware-drop"
        :class="
          cn(
            'flex cursor-pointer flex-col items-center gap-2 rounded-xl border-2 border-dashed px-4 py-8 text-center text-sm transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50',
            over ? 'border-primary bg-primary/5' : 'border-border hover:bg-muted/50',
          )
        "
        @click="input?.click()"
        @keydown.enter.prevent="input?.click()"
        @keydown.space.prevent="input?.click()"
        @dragenter.prevent="over = true"
        @dragover.prevent="over = true"
        @dragleave.prevent="over = false"
        @drop.prevent="drop"
      >
        <FileUp class="size-6 text-muted-foreground" />
        <span class="font-medium">Drop the firmware file here</span>
        <span class="text-muted-foreground">or click to choose it</span>
      </div>
      <div v-else class="flex items-start gap-3 rounded-xl border px-4 py-3 text-sm" data-testid="firmware-file">
        <FileUp class="mt-0.5 size-5 shrink-0 text-muted-foreground" />
        <div class="min-w-0 flex-1">
          <div class="truncate font-medium">{{ file.name }}</div>
          <div v-if="problem" class="text-destructive">{{ problem }}</div>
          <div v-else-if="version" class="text-muted-foreground">{{ versionLabel(version) }} · {{ versionKind(version) }} · {{ size(file.size) }}</div>
        </div>
        <Button v-if="!busy" variant="ghost" size="icon-sm" aria-label="Choose another file" @click="clear"><X /></Button>
      </div>
      <input ref="input" type="file" accept=".bin" class="sr-only" tabindex="-1" aria-hidden="true" @change="($event.target as HTMLInputElement).files?.[0] && inspect(($event.target as HTMLInputElement).files![0]!)" />
      <p class="text-xs text-muted-foreground">
        Download <code>networkdeck-&lt;version&gt;.bin</code> from the
        <a :href="RELEASES" target="_blank" rel="noopener" class="underline underline-offset-2 hover:text-foreground">latest release</a>.
        If the new firmware does not start, the dock goes back to this one.
      </p>
    </div>
    <Progress v-if="stage === 'uploading'" :model-value="progress" />
    <p v-if="stage === 'confirming' && !store.viaHotspot" class="flex items-center gap-2 text-sm" role="status"><Spinner class="size-4" /> Checking it. Then confirm on the Stream Deck: press ✓.</p>
    <p v-if="stage === 'restarting'" class="flex items-center gap-2 text-sm" role="status"><Spinner class="size-4" /> Restarting with the new firmware…</p>
    <Button :disabled="!file || !!problem || !version || busy" @click="install">{{ version ? `Install ${versionLabel(version)}` : "Install" }}</Button>
  </div>
</template>
