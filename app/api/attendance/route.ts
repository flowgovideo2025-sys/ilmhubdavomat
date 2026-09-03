import { NextResponse } from "next/server";
import { db } from "@/lib/db";
import { authenticateDevice } from "@/lib/device-auth";

export async function POST(request: Request) {
  try {
    const device = await authenticateDevice(request);
    if (!device) return NextResponse.json({ success: false, error: "Unauthorized device" }, { status: 401 });
    const body = await request.json();
    const fingerprintId = Number.isInteger(body.fingerprintId) ? body.fingerprintId : null;
    const fingerprint = fingerprintId === null ? null : await db.fingerprint.findUnique({ where: { deviceId_sensorSlot: { deviceId: device.id, sensorSlot: fingerprintId } }, include: { student: true } });
    if (!fingerprint) {
      await db.unknownScan.create({ data: { deviceId: device.id, confidence: Number(body.confidence) || 0 } });
      return NextResponse.json({ success: false, error: "Unknown fingerprint", status: "UNKNOWN" }, { status: 404 });
    }
    const last = await db.attendance.findFirst({ where: { studentId: fingerprint.studentId, deviceId: device.id }, orderBy: { timestamp: "desc" } });
    if (last && Date.now() - last.timestamp.getTime() < 5000) return NextResponse.json({ success: true, duplicate: true, attendance: last });
    const type = last?.type === "ARRIVAL" ? "DEPARTURE" : "ARRIVAL";
    const attendance = await db.attendance.create({ data: { eventId: String(body.eventId || crypto.randomUUID()), studentId: fingerprint.studentId, deviceId: device.id, fingerprintId, confidence: Number(body.confidence) || 0, type }, include: { student: { include: { group: true } }, device: true } });
    return NextResponse.json({ success: true, attendance });
  } catch {
    return NextResponse.json({ success: false, error: "Attendance event rejected" }, { status: 400 });
  }
}
