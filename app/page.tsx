"use client";

import { useEffect, useState } from "react";

type Dashboard = {
  students: number;
  groups: number;
  present: number;
  devicesOnline: number;
};

const navigation = ["Dashboard", "Students", "Groups", "Attendance", "Devices", "Settings"];

export default function Home() {
  const [active, setActive] = useState("Dashboard");
  const [dashboard, setDashboard] = useState<Dashboard | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    const apiUrl = (process.env.NEXT_PUBLIC_API_URL ?? "https://ilmhubdavomat.onrender.com").replace(/\/$/, "");
    fetch(`${apiUrl}/api/dashboard`)
      .then((response) => {
        if (!response.ok) throw new Error("Dashboard API unavailable");
        return response.json().then((payload) => payload.data ?? payload);
      })
      .then(setDashboard)
      .catch(() => setError("Connect the database to load live statistics."));
  }, []);

  const cards = [
    ["TOTAL STUDENTS", dashboard?.students ?? "--", "registered learners", "blue"],
    ["TOTAL GROUPS", dashboard?.groups ?? "--", "active groups", "gold"],
    ["PRESENT TODAY", dashboard?.present ?? "--", "server-confirmed events", "green"],
    ["DEVICES ONLINE", dashboard?.devicesOnline ?? "--", "last seen < 30 sec", "red"],
  ];

  return (
    <main className="shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">I</span><span>ILMHUB</span></div>
        <div className="eyebrow">ATTENDANCE CONTROL</div>
        <nav>{navigation.map((item) => <button className={active === item ? "nav-item active" : "nav-item"} key={item} onClick={() => setActive(item)}><span className="nav-dot" />{item}</button>)}</nav>
        <div className="sidebar-footer"><span className="status-dot" />Backend connection<br /><strong>Awaiting configuration</strong></div>
      </aside>
      <section className="workspace">
        <header className="topbar"><div><div className="crumb">CONTROL ROOM / 03 SEP 2026</div><h1>{active}</h1></div><div className="admin"><span className="avatar">A</span><span>Administrator</span><span className="chevron">⌄</span></div></header>
        {error && <div className="notice">{error}</div>}
        {active === "Dashboard" ? <>
          <section className="intro"><div><p className="kicker">THURSDAY, 03 SEPTEMBER 2026</p><h2>Good morning, admin.</h2><p className="muted">Your attendance network at a glance.</p></div><button className="outline-button" onClick={() => window.location.reload()}>Refresh data <span>↻</span></button></section>
          <section className="metrics">{cards.map(([label, value, detail, color]) => <article className={`metric ${color}`} key={label}><div className="metric-label">{label}</div><div className="metric-value">{value}</div><div className="metric-detail">{detail}</div></article>)}</section>
          <section className="lower-grid"><article className="panel live-panel"><div className="panel-head"><div><p className="kicker">STREAM</p><h3>Live attendance</h3></div><span className="live-badge"><span className="pulse" />LIVE</span></div><div className="empty-state"><div className="empty-icon">⌁</div><strong>Waiting for device events</strong><span>Attendance appears here after ESP32 confirms a fingerprint.</span></div></article><article className="panel health"><div className="panel-head"><div><p className="kicker">SYSTEM</p><h3>Health status</h3></div><span className="healthy">● READY</span></div>{["Backend API", "Database", "Realtime stream", "Device network"].map((name) => <div className="health-row" key={name}><span>{name}</span><span className="waiting">Not configured</span></div>)}</article></section>
        </> : <div className="panel placeholder"><p className="kicker">{active.toUpperCase()}</p><h2>{active} workspace</h2><p className="muted">Connect PostgreSQL to manage live records from this section.</p></div>}
      </section>
    </main>
  );
}
