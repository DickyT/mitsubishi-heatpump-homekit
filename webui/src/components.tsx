// Kiri Bridge — shared UI primitives. All components are dumb/stateless;
// page-level state lives in signals (store.ts) or local component state.

import type { ComponentChildren, JSX } from "preact";

export function Section({ title, action, children }: { title?: string; action?: ComponentChildren; children: ComponentChildren }): JSX.Element {
  return (
    <section class="section">
      {title && (
        <div class="section-head">
          <h3>{title}</h3>
          {action}
        </div>
      )}
      {children}
    </section>
  );
}

export function SpecRow({ k, children, valueClass }: { k: string; children: ComponentChildren; valueClass?: string }): JSX.Element {
  return (
    <div class="spec-row">
      <span class="key">{k}</span>
      <span class={"val " + (valueClass ?? "")}>{children}</span>
    </div>
  );
}

export function Field({ label, children }: { label: string; children: ComponentChildren }): JSX.Element {
  return (
    <label class="field">
      <span class="field-label">{label}</span>
      {children}
    </label>
  );
}

export function Btn({
  variant = "default",
  compact = false,
  type = "button",
  ...rest
}: {
  variant?: "default" | "primary" | "danger";
  compact?: boolean;
  type?: "button" | "submit";
  children?: ComponentChildren;
} & Omit<JSX.HTMLAttributes<HTMLButtonElement>, "size">): JSX.Element {
  const cls = ["btn", variant !== "default" ? variant : "", compact ? "compact" : ""].filter(Boolean).join(" ");
  return <button {...rest} type={type} class={cls} />;
}

export function Modal({
  open,
  onClose,
  title,
  subtitle,
  size,
  children,
  actions,
  closable = true,
}: {
  open: boolean;
  onClose?: () => void;
  title: string;
  subtitle?: ComponentChildren;
  size?: "narrow" | "wide";
  children: ComponentChildren;
  actions?: ComponentChildren;
  closable?: boolean;
}): JSX.Element | null {
  if (!open) return null;
  return (
    <div class="modal" role="dialog" aria-modal="true">
      <div class="modal-backdrop" onClick={() => closable && onClose?.()} />
      <div class={"modal-panel " + (size ?? "")}>
        <div class="modal-header">
          <div>
            <h2>{title}</h2>
            {subtitle && <div class="modal-subtitle subtitle">{subtitle}</div>}
          </div>
        </div>
        {children}
        {actions && <div class="modal-actions">{actions}</div>}
      </div>
    </div>
  );
}
