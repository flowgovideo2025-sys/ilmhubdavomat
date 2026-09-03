# ILMHUB Smart Attendance

Production web/API foundation for ILMHUB attendance devices. The web app uses Next.js App Router, PostgreSQL, and Prisma. The ESP8266 firmware remains in the separate PlatformIO project at `C:\Users\user\Documents\PlatformIO\Projects\ILMHUB_SMART_ATTENDANCE`.

## Local setup

1. Copy `.env.example` to `.env` and set real PostgreSQL credentials and secrets.
2. Run `npm install`.
3. Run `npx prisma generate`.
4. Run `npx prisma migrate dev --name init` during development, or `npx prisma migrate deploy` in production.
5. Run `npm run dev` and open `http://localhost:3000`.

The dashboard does not invent records. It shows an unavailable state until the database is configured. Device requests use `Authorization: Bearer <device-token>` and `X-Device-Code` headers. Store only the SHA-256 digest of each provisioned device token in `Device.apiKeyHash`.

## Device API

- `POST /api/devices/heartbeat`
- `GET /api/devices/commands`
- `POST /api/devices/commands`
- `POST /api/attendance`
- `GET /api/dashboard`

The attendance endpoint resolves the student only from the `(deviceId, fingerprintId)` mapping, alternates `CHECK_IN` and `CHECK_OUT`, and suppresses duplicate scans within five seconds. All timestamps are server timestamps.

## Deployment

Deploy the Next.js project to Vercel, configure the environment variables from `.env.example`, and run `npx prisma migrate deploy` against the production database. Provision a unique device token outside source control. Never place production tokens in firmware committed to git; provide them through a local provisioning build or protected device configuration.

## Firmware validation

From the PlatformIO project directory:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

The firmware preserves the standalone Adafruit Fingerprint enrollment/search flow and uses the configured 57600 baud R503 serial link. Hardware upload and sensor verification require the actual device connected.
