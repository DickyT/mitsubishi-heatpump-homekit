// Kiri Bridge — Logs page.
// Lists log files, previews one, optionally tails the current one live.

import { useEffect, useState, useRef, useCallback } from "preact/hooks";
import type { JSX } from "preact";
import { Section, Btn } from "../components";
import { api } from "../api";
import type { LogList, LogFile } from "../types";

export function LogsPage(): JSX.Element {
  const [list, setList] = useState<LogList | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selectedName, setSelectedName] = useState<string>("");
  const [preview, setPreview] = useState<string>("Select a file above.");
  const [liveOpen, setLiveOpen] = useState(false);
  const [liveText, setLiveText] = useState<string>("Waiting for log output…");
  const liveOffset = useRef(0);
  const liveTimer = useRef<number | undefined>(undefined);

  const refresh = useCallback(async () => {
    try {
      const j = await api.logs();
      setList(j);
      setError(null);
      const current = j.logs.find((x) => x.current) ?? j.logs[0];
      if (current && current.name !== selectedName) {
        setSelectedName(current.name);
        loadPreview(current.name);
      }
    } catch (e: any) {
      setError("Failed to read logs: " + (e?.message ?? e));
    }
  }, [selectedName]);

  useEffect(() => { refresh(); }, []);

  async function loadPreview(name: string): Promise<void> {
    setSelectedName(name);
    setPreview("Reading " + name + "…");
    try {
      const text = await api.logFile(name);
      setPreview(text);
    } catch (e: any) {
      setPreview("Read failed: " + (e?.message ?? e));
    }
  }

  async function deleteOne(name: string): Promise<void> {
    if (!confirm("Delete log " + name + "?")) return;
    try {
      const j = await api.deleteFile(name);
      if (!j.ok) {
        alert("Delete failed: " + (j.message ?? j.error ?? "unknown"));
        return;
      }
      if (selectedName === name) {
        setSelectedName("");
        setPreview("Log deleted.");
      }
      refresh();
    } catch (e: any) {
      alert("Delete failed: " + (e?.message ?? e));
    }
  }

  async function clearCurrent(): Promise<void> {
    if (!confirm("Clear the current log? Logging will continue in the same file.")) return;
    try {
      const j = await api.clearLogs();
      if (!j.ok) {
        alert("Clear failed: " + (j.message ?? j.error ?? "unknown"));
        return;
      }
      setPreview("Current log cleared.");
      liveOffset.current = 0;
      refresh();
    } catch (e: any) {
      alert("Clear failed: " + (e?.message ?? e));
    }
  }

  async function deleteAll(): Promise<void> {
    if (!list?.logs.length) {
      alert("No log files.");
      return;
    }
    if (!confirm("Delete all historical logs and clear the current log? This cannot be undone.")) return;
    let failed = 0;
    for (const log of list.logs) {
      if (log.current) continue;
      try {
        const j = await api.deleteFile(log.name);
        if (!j.ok) failed++;
      } catch { failed++; }
    }
    try {
      const j = await api.clearLogs();
      if (!j.ok) failed++;
    } catch { failed++; }
    setSelectedName("");
    liveOffset.current = 0;
    setPreview(failed
      ? "Some logs could not be cleared. Refresh and try again."
      : "All logs deleted. Current log cleared.");
    refresh();
  }

  function openLive(): void {
    if (liveTimer.current !== undefined) clearInterval(liveTimer.current);
    setLiveOpen(true);
    setLiveText("");
    liveOffset.current = 0;
    pollLive();
    liveTimer.current = window.setInterval(pollLive, 1500);
  }

  function closeLive(): void {
    if (liveTimer.current !== undefined) clearInterval(liveTimer.current);
    liveTimer.current = undefined;
    setLiveOpen(false);
  }

  // cleanup on unmount
  useEffect(() => () => {
    if (liveTimer.current !== undefined) clearInterval(liveTimer.current);
  }, []);

  async function pollLive(): Promise<void> {
    try {
      const j = await api.liveLog(liveOffset.current);
      if (!j.ok) {
        setLiveText("Live failed: " + (j.error ?? "unknown"));
        return;
      }
      let next = j.reset ? "" : liveText;
      if (j.text) next += j.text;
      if (next.length > 24000) next = next.slice(-16000);
      setLiveText(next);
      liveOffset.current = j.nextOffset || 0;
    } catch (e: any) {
      setLiveText((t) => t + "\n[live error] " + (e?.message ?? e));
    }
  }

  return (
    <main>
      <h1>Logs</h1>
      <div class="subtitle">A new SPIFFS log file is created on every boot. When storage fills, old logs are removed first; if needed, the current log clears and writing continues.</div>

      <Section title="Current" action={<Btn variant="danger" compact onClick={deleteAll}>Delete All</Btn>}>
        <div class="spec-row"><span class="key">Status</span><span class="val">{list?.active ? "Writing" : "Disabled"}</span></div>
        <div class="spec-row"><span class="key">File</span><span class="val">{list?.current ?? "-"}</span></div>
        <div class="spec-row"><span class="key">Size</span><span class="val">{(list?.current_bytes ?? 0) + " bytes"}</span></div>
        <div class="spec-row"><span class="key">Level</span><span class="val">{list?.level ?? "-"}</span></div>
        <div class="btns">
          <Btn variant="primary" onClick={openLive}>Live tail</Btn>
          <Btn onClick={refresh}>Refresh</Btn>
        </div>
      </Section>

      <Section title="Files">
        {error && <div class="danger-banner">{error}</div>}
        {!list && !error && <div>Loading…</div>}
        {list && list.logs.length === 0 && <div>No log files.</div>}
        {list && list.logs.map((log) => (
          <FileRow
            key={log.name}
            log={log}
            onView={() => loadPreview(log.name)}
            onDelete={() => log.current ? clearCurrent() : deleteOne(log.name)}
          />
        ))}
      </Section>

      <Section title="Preview">
        <pre>{preview}</pre>
      </Section>

      {liveOpen && (
        <Section
          title="Live tail"
          action={<Btn compact onClick={closeLive}>Stop</Btn>}
        >
          <pre>{liveText}</pre>
        </Section>
      )}
    </main>
  );
}

function FileRow({ log, onView, onDelete }: { log: LogFile; onView: () => void; onDelete: () => void }): JSX.Element {
  return (
    <div class="listrow">
      <div class="listmeta">
        {log.name + (log.current ? " (current)" : "")}
        <small>{log.size + " bytes"}</small>
      </div>
      <div class="actions">
        <Btn compact onClick={onView}>View</Btn>
        <a class="btn compact" href={"/api/log/file?file=" + encodeURIComponent(log.name)}>Download</a>
        <Btn variant="danger" compact onClick={onDelete}>{log.current ? "Clear" : "Delete"}</Btn>
      </div>
    </div>
  );
}
