import { createHash, timingSafeEqual } from "node:crypto";
import { db } from "@/lib/db";

function digest(value: string) {
  return createHash("sha256").update(value).digest("hex");
}

export async function authenticateDevice(request: Request) {
  const authorization = request.headers.get("authorization") ?? "";
  const token = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
  const deviceCode = request.headers.get("x-device-code") ?? "";
  if (!token || !deviceCode) return null;
  const device = await db.device.findUnique({ where: { deviceCode } });
  if (!device) return null;
  const actual = Buffer.from(digest(token));
  const expected = Buffer.from(device.apiKeyHash);
  if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) return null;
  return device;
}
