import React from "react";

export function WelcomeCard({ title, count }) {
  const accent = "#26a69a";

  return (
    <article className="welcome-card">
      <style>{`
        .welcome-card {
          border: 1px solid ${accent};
          padding: 12px;
        }
        .welcome-card strong {
          color: ${accent};
        }
      `}</style>
      <h2>{title}</h2>
      <p>
        <strong>{count}</strong> files highlighted
      </p>
    </article>
  );
}
