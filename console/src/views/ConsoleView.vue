<script setup lang="ts">
/*
 * The console: the top bar with the dock's lights, the rail, a panel beside the deck when one is open,
 * the deck itself; the settings and the integrations open over it as dialogs.
 */
import { ref } from "vue"

import DeckStage from "../components/DeckStage.vue"
import SidePanel from "../components/SidePanel.vue"
import SideRail from "../components/SideRail.vue"
import TopBar from "../components/TopBar.vue"
import IntegrationsDialog from "../dialogs/IntegrationsDialog.vue"
import SettingsDialog from "../dialogs/SettingsDialog.vue"
import DiagnosticsPanel from "../panels/DiagnosticsPanel.vue"
import MatterPanel from "../panels/MatterPanel.vue"
import { PANELS, type PanelId } from "../panels/panels.ts"
import StreamDeckPanel from "../panels/StreamDeckPanel.vue"

const panel = ref<PanelId | null>(null)
const views = { deck: StreamDeckPanel, matter: MatterPanel, diagnostics: DiagnosticsPanel }

const settingsOpen = ref(false)
const settingsSection = ref("network")
const integrationsOpen = ref(false)

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
      <SideRail :panel="panel" @panel="toggle" @settings="openSettings" @integrations="integrationsOpen = true" />
      <div class="flex min-h-0 min-w-0 flex-1">
        <SidePanel :open="panel !== null" :title="panel ? PANELS[panel].title : ''" @close="panel = null">
          <component :is="views[panel]" v-if="panel" />
        </SidePanel>
        <DeckStage />
      </div>
    </div>
    <SettingsDialog v-model:open="settingsOpen" v-model:section="settingsSection" />
    <IntegrationsDialog v-model:open="integrationsOpen" />
  </div>
</template>
