import { NextResponse } from "next/server";
import { db } from "@/lib/db";
import { authenticateDevice } from "@/lib/device-auth";

export async function GET(request: Request) {
  try {
    const device = await authenticateDevice(request);
    if (!device) return NextResponse.json({ success: false, error: "Unauthorized device" }, { status: 401 });
    const command = await db.deviceCommand.findFirst({ where: { deviceId: device.id, status: "PENDING" }, orderBy: { createdAt: "asc" } });
    if (!command) return NextResponse.json({ success: true, command: null });
    await db.deviceCommand.update({ where: { id: command.id }, data: { status: "PROCESSING", claimedAt: new Date() } });
    return NextResponse.json({ success: true, command: { id: command.id, command: command.type, ...((command.payload ?? {}) as object) } });
  } catch {
    return NextResponse.json({ success: false, error: "Command polling failed" }, { status: 503 });
  }
}

export async function POST(request: Request) {
  try {
    const device = await authenticateDevice(request);
    if (!device) return NextResponse.json({ success: false, error: "Unauthorized device" }, { status: 401 });
    const body = await request.json();
    const command = await db.deviceCommand.update({ where: { id: body.commandId, deviceId: device.id }, data: { status: body.status === "SUCCESS" ? "SUCCESS" : "FAILED", result: body, completedAt: new Date() } });
    return NextResponse.json({ success: true, commandId: command.id });
  } catch {
    return NextResponse.json({ success: false, error: "Invalid command result" }, { status: 400 });
  }
}
