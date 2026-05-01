// Kiri Bridge — global signals.
// Polled status is shared across all pages so admin/root see one source of truth.

import { signal, computed } from "@preact/signals";
import type { Status } from "./types";
import { api } from "./api";

export const status = signal<Status | null>(null);
export const statusError = signal<string | null>(null);
export const lastFetchAt = signal<number>(0);
export type PollingMode = 5000 | 15000 | 0;

const POLL_STORAGE_KEY = "kiri.polling.mode";
const DEFAULT_POLLING_MODE: PollingMode = 5000;

export const deviceName = computed(() =>
  (status.value?.config?.device_name) || status.value?.device || "Kiri Bridge"
);

let pollTimer: number | undefined;
let nextPollAt = 0;
export const pollingMode = signal<PollingMode>(readPollingMode());
export const pollingActive = signal(false);

function readPollingMode(): PollingMode {
  try {
    const stored = window.localStorage.getItem(POLL_STORAGE_KEY);
    if (stored === "5000") return 5000;
    if (stored === "15000") return 15000;
    if (stored === "0") return 0;
  } catch {
    // localStorage may be unavailable in some captive/webview contexts.
  }
  return DEFAULT_POLLING_MODE;
}

function writePollingMode(mode: PollingMode): void {
  try {
    window.localStorage.setItem(POLL_STORAGE_KEY, String(mode));
  } catch {
    // Best effort; polling still works for this session.
  }
}

function canPoll(): boolean {
  return pollingMode.value > 0 && document.visibilityState === "visible" && document.hasFocus();
}

function scheduleNextPoll(delayMs: number): void {
  stopPollingTimer();
  if (!canPoll()) {
    pollingActive.value = false;
    return;
  }
  pollingActive.value = true;
  pollTimer = window.setTimeout(async () => {
    if (!canPoll()) {
      pollingActive.value = false;
      return;
    }
    await fetchStatusOnce();
    nextPollAt = Date.now() + pollingMode.value;
    scheduleNextPoll(pollingMode.value);
  }, Math.max(0, delayMs));
}

function stopPollingTimer(): void {
  if (pollTimer !== undefined) {
    clearTimeout(pollTimer);
    pollTimer = undefined;
  }
}

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

export function startPolling(): void {
  stopPollingTimer();
  if (document.visibilityState === "visible" && document.hasFocus() && lastFetchAt.value === 0) {
    fetchStatusOnce().finally(() => {
      if (canPoll()) {
        nextPollAt = Date.now() + pollingMode.value;
        scheduleNextPoll(pollingMode.value);
      }
    });
    return;
  }
  if (!canPoll()) {
    pollingActive.value = false;
    return;
  }
  fetchStatusOnce().finally(() => {
    nextPollAt = Date.now() + pollingMode.value;
    scheduleNextPoll(pollingMode.value);
  });
}

export function stopPolling(): void {
  stopPollingTimer();
  pollingActive.value = false;
}

export function setPollingMode(mode: PollingMode): void {
  pollingMode.value = mode;
  writePollingMode(mode);
  if (mode === 0) {
    stopPolling();
    return;
  }
  nextPollAt = Date.now() + mode;
  scheduleNextPoll(mode);
}

export function initPolling(): void {
  const onVisibility = () => {
    if (!canPoll()) {
      pollingActive.value = false;
      stopPollingTimer();
      return;
    }
    const now = Date.now();
    if (lastFetchAt.value === 0 || now >= nextPollAt) {
      fetchStatusOnce().finally(() => {
        nextPollAt = Date.now() + pollingMode.value;
        scheduleNextPoll(pollingMode.value);
      });
      return;
    }
    scheduleNextPoll(nextPollAt - now);
  };
  document.addEventListener("visibilitychange", onVisibility);
  window.addEventListener("focus", onVisibility);
  window.addEventListener("blur", onVisibility);
  startPolling();
}
