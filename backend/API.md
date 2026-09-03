# ILMHUB API

All errors use `{ success: false, message, error }`; successful responses use `{ success: true, data }`.

## Public

`GET /health` returns service status. `GET /api/device/status`, `GET /api/students`, `GET /api/students/:id`, `GET /api/fingerprints`, `GET /api/attendance`, `GET /api/attendance/today`, and `GET /api/attendance/live` return database data.

## Admin actions

`POST /api/students` accepts `firstName`, `lastName`, and `groupName`. `PUT` and `DELETE /api/students/:id` update or soft-delete a student. `POST /api/students/:id/enroll` creates an enrollment command for the currently online device. `DELETE /api/fingerprints/:id` creates a physical sensor deletion command.

## Device authentication

Device routes require `Authorization: Bearer DEVICE_API_KEY` and `X-Device-Code`. The database stores the SHA-256 digest, never the raw key. `POST /api/device/heartbeat` updates liveness. Devices poll `GET /api/device/:deviceCode/enrollment/pending` and `GET /api/device/:deviceCode/fingerprint-jobs`; results are posted to `/api/device/enrollment/result` and `/api/device/fingerprint-job-result`. Attendance is posted to `POST /api/attendance` with `{ eventId, fingerprintId, confidence }`.
