import { NextResponse } from "next/server";
import { db } from "@/lib/db";
import { authenticateDevice } from "@/lib/device-auth";

export async function POST(request: Request) {
  try {
    const device = await authenticateDevice(request);
    if (!device) return NextResponse.json({ success: false, error: "Unauthorized device" }, { status: 401 });
    const body = await request.json();
    const updated = await db.device.update({ where: { id: device.id }, data: { ipAddress: body.ip, firmwareVersion: body.firmwareVersion, lastSeen: new Date(), online: true } });
    return NextResponse.json({ success: true, deviceId: updated.id, serverTime: new Date().toISOString() });
  } catch {
    return NextResponse.json({ success: false, error: "Invalid heartbeat" }, { status: 400 });
  }
}
