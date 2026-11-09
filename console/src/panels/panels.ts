export type PanelId = "network" | "wifi" | "firmware" | "security" | "diagnostics";

export const PANELS: Record<PanelId, { title: string }> = {
  network: { title: "Network" },
  wifi: { title: "Wi-Fi" },
  firmware: { title: "Firmware" },
  security: { title: "Password" },
  diagnostics: { title: "Diagnostics" },
};

/* Wide enough for a panel beside the deck. */
export const WIDE_QUERY = "(min-width: 768px)";
