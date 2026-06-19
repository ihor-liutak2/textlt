type PanelProps = {
  title: string;
  enabled: boolean;
  count: number;
};

export function StatusPanel({ title, enabled, count }: PanelProps) {
  const tone: "ready" | "idle" = enabled ? "ready" : "idle";

  return (
    <section className={`status-panel ${tone}`}>
      <style>{`
        .status-panel {
          display: grid;
          gap: 8px;
        }
        .status-panel.ready {
          color: #26a69a;
        }
      `}</style>
      <header>
        <h2>{title}</h2>
        <span>{enabled ? "Enabled" : "Disabled"}</span>
      </header>
      <p>Total: {count}</p>
    </section>
  );
}
