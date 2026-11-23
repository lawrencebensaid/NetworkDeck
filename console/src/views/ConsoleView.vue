<script setup lang="ts">
/*
 * The console: the top bar with the dock's lights, the rail, a panel beside the deck when one is open,
 * the deck itself; the settings, the connectors and the log open over it as dialogs.
 */
import { ref, watch } from "vue"

import DeckStage from "../components/DeckStage.vue"
import SidePanel from "../components/SidePanel.vue"
import SideRail from "../components/SideRail.vue"
import TopBar from "../components/TopBar.vue"
import ConnectorsDialog from "../dialogs/ConnectorsDialog.vue"
import LogsDialog from "../dialogs/LogsDialog.vue"
import SettingsDialog from "../dialogs/SettingsDialog.vue"
import MatterPanel from "../panels/MatterPanel.vue"
import NetworkDeckPanel from "../panels/NetworkDeckPanel.vue"
import { PANELS, type PanelId } from "../panels/panels.ts"
import { store } from "../session/store.ts"
import StreamDeckPanel from "../panels/StreamDeckPanel.vue"

const panel = ref<PanelId | null>(null)
const views = { deck: StreamDeckPanel, dock: NetworkDeckPanel, matter: MatterPanel }

const settingsOpen = ref(false)
const settingsSection = ref("system")
const connectorsOpen = ref(false)
const logsOpen = ref(false)

/*
 * First setup: a dock reached over its own hotspot that has no Wi-Fi saved has one thing left to do,
 * so land on the Wi-Fi list instead of an empty console. Once, on the first status.
 */
let landed = false
watch(
  () => store.status,
  (st) => {
    if (!st || landed) return
    landed = true
    if (st.link === "Hotspot" && !st.wifi_ssid && st.wifi_available) openSettings("wifi")
  },
  { immediate: true },
)

function toggle(id: PanelId) {
  panel.value = panel.value === id ? null : id
}
function openSettings(section?: string) {
  if (section) settingsSection.value = section
  settingsOpen.value = true
}
</script>

<template>
  <div class="flex h-dvh flex-col bg-background text-foreground">
    <TopBar @settings="openSettings" @panel="panel = $event" />
    <div class="flex min-h-0 flex-1 flex-col-reverse md:flex-row">
      <SideRail :panel="panel" @panel="toggle" @settings="openSettings" @connectors="connectorsOpen = true" @logs="logsOpen = true" />
      <div class="flex min-h-0 min-w-0 flex-1">
        <SidePanel :open="panel !== null" :title="panel ? PANELS[panel].title : ''" @close="panel = null">
          <component :is="views[panel]" v-if="panel" @settings="openSettings" />
        </SidePanel>
        <DeckStage />
      </div>
    </div>
    <SettingsDialog v-model:open="settingsOpen" v-model:section="settingsSection" />
    <ConnectorsDialog v-model:open="connectorsOpen" />
    <LogsDialog v-model:open="logsOpen" />
  </div>
</template>
