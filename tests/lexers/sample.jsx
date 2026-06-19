import React from "react";

export function Badge({ count }) {
  return <span className="badge">{count > 0 ? "ready" : "empty"}</span>;
}
