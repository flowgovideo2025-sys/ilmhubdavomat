import { NextResponse } from "next/server";
import { db } from "@/lib/db";

export async function GET() {
  try {
    const groups = await db.group.findMany({ orderBy: { name: "asc" }, include: { _count: { select: { students: true } } } });
    return NextResponse.json({ success: true, groups });
  } catch {
    return NextResponse.json({ success: false, error: "Database is not configured" }, { status: 503 });
  }
}

export async function POST(request: Request) {
  try {
    const body = await request.json();
    const name = typeof body.name === "string" ? body.name.trim() : "";
    if (!name) return NextResponse.json({ success: false, error: "Group name is required" }, { status: 400 });
    const group = await db.group.create({ data: { name } });
    return NextResponse.json({ success: true, group }, { status: 201 });
  } catch {
    return NextResponse.json({ success: false, error: "Group could not be created" }, { status: 400 });
  }
}
