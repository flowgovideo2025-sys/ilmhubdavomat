import { NextResponse } from "next/server";
import { db } from "@/lib/db";

export async function GET() {
  try {
    const start = new Date();
    start.setHours(0, 0, 0, 0);
    const [students, groups, present, devicesOnline] = await Promise.all([
      db.student.count({ where: { active: true } }),
      db.group.count(),
      db.attendance.findMany({ where: { timestamp: { gte: start }, type: "ARRIVAL" }, distinct: ["studentId"], select: { studentId: true } }),
      db.device.count({ where: { online: true, lastSeen: { gte: new Date(Date.now() - 30_000) } } }),
    ]);
    return NextResponse.json({ students, groups, present: present.length, devicesOnline });
  } catch {
    return NextResponse.json({ success: false, error: "Database is not configured" }, { status: 503 });
  }
}
