// Kiri Bridge — top nav (desktop) + bottom floating pill (mobile) + footer.

import type { JSX } from "preact";
import { useComputed } from "@preact/signals";
import { currentPage, navigate, type PageId } from "./router";
import { status } from "./store";

const TABS: { id: PageId; label: string; svg: JSX.Element }[] = [
  {
    id: "control",
    label: "Control",
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
        <path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z" />
        <polyline points="9 22 9 12 15 12 15 22" />
      </svg>
    ),
  },
  {
    id: "logs",
    label: "Logs",
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
        <path d="M4 19h16" /><path d="M4 15h16" /><path d="M4 11h16" /><path d="M4 7h10" />
      </svg>
    ),
  },
  {
    id: "admin",
    label: "Admin",
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
        <circle cx="12" cy="12" r="3" />
        <path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z" />
      </svg>
    ),
  },
];

export function Tabs(): JSX.Element {
  const active = currentPage.value;
  return (
    <nav class="tabs" aria-label="Main navigation">
      {TABS.map(({ id, label, svg }) => (
        <button
          key={id}
          class={active === id ? "active" : ""}
          onClick={() => navigate(id)}
          aria-current={active === id ? "page" : undefined}
        >
          {svg}{label}
        </button>
      ))}
    </nav>
  );
}

export function SiteFooter(): JSX.Element {
  const version = useComputed(() => status.value?.version ?? "--");
  return (
    <footer class="site-footer">
      <div class="footer-brand">
        <span class="footer-mark" aria-hidden="true" />
        <span><strong>Kiri Bridge</strong> — open-source HomeKit controller for Mitsubishi heat pumps</span>
      </div>
      <div class="footer-meta">
        <a href="https://kiri.dkt.moe" target="_blank" rel="noopener noreferrer">kiri.dkt.moe</a>
        <span>v<b>{version.value}</b></span>
      </div>
    </footer>
  );
}
