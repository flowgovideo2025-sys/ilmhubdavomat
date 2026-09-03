import { NextResponse } from "next/server";
import { db } from "@/lib/db";

export async function GET() {
  try {
    const students = await db.student.findMany({ where: { active: true }, orderBy: [{ lastName: "asc" }, { firstName: "asc" }], include: { group: true, fingerprints: { where: { status: "REGISTERED" }, select: { sensorSlot: true, deviceId: true } } } });
    return NextResponse.json({ success: true, students });
  } catch {
    return NextResponse.json({ success: false, error: "Database is not configured" }, { status: 503 });
  }
}

export async function POST(request: Request) {
  try {
    const body = await request.json();
    const firstName = typeof body.firstName === "string" ? body.firstName.trim() : "";
    const lastName = typeof body.lastName === "string" ? body.lastName.trim() : "";
    const groupId = typeof body.groupId === "string" ? body.groupId : "";
    if (!firstName || !lastName || !groupId) return NextResponse.json({ success: false, error: "First name, last name, and group are required" }, { status: 400 });
    const student = await db.student.create({ data: { firstName, lastName, groupId }, include: { group: true } });
    return NextResponse.json({ success: true, student }, { status: 201 });
  } catch {
    return NextResponse.json({ success: false, error: "Student could not be created" }, { status: 400 });
  }
}
