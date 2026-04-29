// Kiri Bridge — global signals.
// Polled status is shared across all pages so admin/root see one source of truth.

import { signal, computed } from "@preact/signals";
import type { Status } from "./types";
import { api } from "./api";

export const status = signal<Status | null>(null);
export const statusError = signal<string | null>(null);
export const lastFetchAt = signal<number>(0);

export const deviceName = computed(() =>
  (status.value?.config?.device_name) || status.value?.device || "Kiri Bridge"
);

let pollTimer: number | undefined;
let pollIntervalMs = 5000;
// pause polling while user is editing form on the control page
export const pollPaused = signal(false);

export async function fetchStatusOnce(force = false): Promise<void> {
  try {
    const s = force ? await api.refreshCn105() : await api.status();
    if (s && s.ok === false && (s as any).error) {
      statusError.value = String((s as any).error);
      return;
    }
    status.value = s as Status;
    statusError.value = null;
    lastFetchAt.value = Date.now();
  } catch (e: any) {
    statusError.value = e?.message ?? String(e);
  }
}

export function startPolling(intervalMs: number): void {
  pollIntervalMs = intervalMs;
  stopPolling();
  fetchStatusOnce();
  pollTimer = window.setInterval(() => {
    if (!pollPaused.value) fetchStatusOnce();
  }, pollIntervalMs);
}

export function stopPolling(): void {
  if (pollTimer !== undefined) {
    clearInterval(pollTimer);
    pollTimer = undefined;
  }
}
