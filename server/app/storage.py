from __future__ import annotations

import json
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .config import settings


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def ensure_storage_layout() -> None:
    settings.data_dir.mkdir(parents=True, exist_ok=True)
    settings.uploads_dir.mkdir(parents=True, exist_ok=True)
    settings.job_files_dir.mkdir(parents=True, exist_ok=True)
    settings.telemetry_dir.mkdir(parents=True, exist_ok=True)


def connect_db() -> sqlite3.Connection:
    ensure_storage_layout()
    conn = sqlite3.connect(settings.database_path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_db() -> None:
    with connect_db() as conn:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS jobs (
              id TEXT PRIMARY KEY,
              job_type TEXT NOT NULL,
              status TEXT NOT NULL,
              device_id TEXT NOT NULL,
              request_id TEXT NOT NULL,
              worker_id TEXT,
              error_message TEXT,
              request_payload TEXT NOT NULL,
              result_payload TEXT NOT NULL,
              request_file_path TEXT,
              result_file_path TEXT,
              created_at TEXT NOT NULL,
              updated_at TEXT NOT NULL,
              claimed_at TEXT,
              completed_at TEXT
            );

            CREATE UNIQUE INDEX IF NOT EXISTS idx_jobs_request_unique
              ON jobs(job_type, device_id, request_id);

            CREATE INDEX IF NOT EXISTS idx_jobs_pending
              ON jobs(job_type, status, created_at);

            CREATE TABLE IF NOT EXISTS telemetry_latest (
              device_id TEXT PRIMARY KEY,
              timestamp TEXT NOT NULL,
              longitude REAL,
              latitude REAL,
              speed_kmh REAL,
              battery_percent INTEGER,
              nav_active INTEGER,
              metadata TEXT NOT NULL,
              updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS telemetry_history (
              id TEXT PRIMARY KEY,
              device_id TEXT NOT NULL,
              timestamp TEXT NOT NULL,
              longitude REAL,
              latitude REAL,
              speed_kmh REAL,
              battery_percent INTEGER,
              nav_active INTEGER,
              metadata TEXT NOT NULL,
              created_at TEXT NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_telemetry_history_device_ts
              ON telemetry_history(device_id, timestamp DESC);
            """
        )


def save_binary_job_file(job_id: str, kind: str, filename: str, content: bytes) -> Path:
    suffix = Path(filename).suffix or ".bin"
    job_dir = settings.job_files_dir / job_id
    job_dir.mkdir(parents=True, exist_ok=True)
    path = job_dir / f"{kind}{suffix}"
    path.write_bytes(content)
    return path


def save_text_job_file(job_id: str, kind: str, filename: str, content: str) -> Path:
    suffix = Path(filename).suffix or ".txt"
    job_dir = settings.job_files_dir / job_id
    job_dir.mkdir(parents=True, exist_ok=True)
    path = job_dir / f"{kind}{suffix}"
    path.write_text(content, encoding="utf-8")
    return path


def row_to_job_dict(row: sqlite3.Row) -> dict[str, Any]:
    return {
        "id": row["id"],
        "job_type": row["job_type"],
        "status": row["status"],
        "device_id": row["device_id"],
        "request_id": row["request_id"],
        "created_at": row["created_at"],
        "updated_at": row["updated_at"],
        "claimed_at": row["claimed_at"],
        "completed_at": row["completed_at"],
        "worker_id": row["worker_id"],
        "error_message": row["error_message"],
        "request_payload": json.loads(row["request_payload"]),
        "result_payload": json.loads(row["result_payload"]),
        "request_file_path": row["request_file_path"],
        "result_file_path": row["result_file_path"],
    }


def create_asr_job(
    *,
    device_id: str,
    request_id: str,
    request_payload: dict[str, Any],
    original_filename: str,
    audio_bytes: bytes,
) -> dict[str, Any]:
    job_id = str(uuid.uuid4())
    now = utc_now()
    request_path = save_binary_job_file(job_id, "request_audio", original_filename, audio_bytes)

    with connect_db() as conn:
      conn.execute(
          """
          INSERT INTO jobs (
            id, job_type, status, device_id, request_id, worker_id, error_message,
            request_payload, result_payload, request_file_path, result_file_path,
            created_at, updated_at, claimed_at, completed_at
          ) VALUES (?, 'asr', 'pending', ?, ?, NULL, NULL, ?, '{}', ?, NULL, ?, ?, NULL, NULL)
          """,
          (
              job_id,
              device_id,
              request_id,
              json.dumps(request_payload, ensure_ascii=False),
              str(request_path),
              now,
              now,
          ),
      )
      row = conn.execute("SELECT * FROM jobs WHERE id = ?", (job_id,)).fetchone()
    return row_to_job_dict(row)


def create_tts_job(
    *,
    device_id: str,
    request_id: str,
    request_payload: dict[str, Any],
) -> dict[str, Any]:
    job_id = str(uuid.uuid4())
    now = utc_now()
    request_path = save_text_job_file(
        job_id,
        "request_text",
        "request.txt",
        str(request_payload.get("text", "")),
    )

    with connect_db() as conn:
      conn.execute(
          """
          INSERT INTO jobs (
            id, job_type, status, device_id, request_id, worker_id, error_message,
            request_payload, result_payload, request_file_path, result_file_path,
            created_at, updated_at, claimed_at, completed_at
          ) VALUES (?, 'tts', 'pending', ?, ?, NULL, NULL, ?, '{}', ?, NULL, ?, ?, NULL, NULL)
          """,
          (
              job_id,
              device_id,
              request_id,
              json.dumps(request_payload, ensure_ascii=False),
              str(request_path),
              now,
              now,
          ),
      )
      row = conn.execute("SELECT * FROM jobs WHERE id = ?", (job_id,)).fetchone()
    return row_to_job_dict(row)


def get_job(job_id: str) -> dict[str, Any] | None:
    with connect_db() as conn:
        row = conn.execute("SELECT * FROM jobs WHERE id = ?", (job_id,)).fetchone()
    return row_to_job_dict(row) if row else None


def list_pending_jobs(job_type: str, limit: int, claim: bool, worker_id: str | None) -> list[dict[str, Any]]:
    with connect_db() as conn:
        rows = conn.execute(
            """
            SELECT * FROM jobs
            WHERE job_type = ? AND status = 'pending'
            ORDER BY created_at ASC
            LIMIT ?
            """,
            (job_type, limit),
        ).fetchall()
        jobs = [row_to_job_dict(row) for row in rows]

        if claim and jobs:
            now = utc_now()
            claimed_ids = [job["id"] for job in jobs]
            conn.executemany(
                """
                UPDATE jobs
                SET status = 'processing',
                    worker_id = ?,
                    claimed_at = ?,
                    updated_at = ?
                WHERE id = ? AND status = 'pending'
                """,
                [(worker_id, now, now, job_id) for job_id in claimed_ids],
            )
            conn.commit()
            refreshed = []
            for job_id in claimed_ids:
                row = conn.execute("SELECT * FROM jobs WHERE id = ?", (job_id,)).fetchone()
                if row is not None:
                    refreshed.append(row_to_job_dict(row))
            return refreshed

    return jobs


def complete_job(
    *,
    job_id: str,
    status: str,
    result_payload: dict[str, Any],
    error_message: str | None,
    result_filename: str | None = None,
    result_content: bytes | None = None,
) -> dict[str, Any] | None:
    result_path = None
    if result_content is not None and result_filename is not None:
        result_path = save_binary_job_file(job_id, "result", result_filename, result_content)

    now = utc_now()
    with connect_db() as conn:
        cursor = conn.execute(
            """
            UPDATE jobs
            SET status = ?,
                error_message = ?,
                result_payload = ?,
                result_file_path = COALESCE(?, result_file_path),
                completed_at = ?,
                updated_at = ?
            WHERE id = ?
            """,
            (
                status,
                error_message,
                json.dumps(result_payload, ensure_ascii=False),
                str(result_path) if result_path else None,
                now,
                now,
                job_id,
            ),
        )
        if cursor.rowcount == 0:
            return None
        row = conn.execute("SELECT * FROM jobs WHERE id = ?", (job_id,)).fetchone()
    return row_to_job_dict(row) if row else None


def upsert_telemetry(payload: dict[str, Any]) -> dict[str, Any]:
    now = utc_now()
    history_id = str(uuid.uuid4())
    metadata_json = json.dumps(payload.get("metadata", {}), ensure_ascii=False)

    with connect_db() as conn:
        conn.execute(
            """
            INSERT INTO telemetry_history (
              id, device_id, timestamp, longitude, latitude, speed_kmh,
              battery_percent, nav_active, metadata, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                history_id,
                payload["device_id"],
                payload["timestamp"],
                payload.get("longitude"),
                payload.get("latitude"),
                payload.get("speed_kmh"),
                payload.get("battery_percent"),
                None if payload.get("nav_active") is None else int(payload["nav_active"]),
                metadata_json,
                now,
            ),
        )
        conn.execute(
            """
            INSERT INTO telemetry_latest (
              device_id, timestamp, longitude, latitude, speed_kmh,
              battery_percent, nav_active, metadata, updated_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
              timestamp = excluded.timestamp,
              longitude = excluded.longitude,
              latitude = excluded.latitude,
              speed_kmh = excluded.speed_kmh,
              battery_percent = excluded.battery_percent,
              nav_active = excluded.nav_active,
              metadata = excluded.metadata,
              updated_at = excluded.updated_at
            """,
            (
                payload["device_id"],
                payload["timestamp"],
                payload.get("longitude"),
                payload.get("latitude"),
                payload.get("speed_kmh"),
                payload.get("battery_percent"),
                None if payload.get("nav_active") is None else int(payload["nav_active"]),
                metadata_json,
                now,
            ),
        )

    return get_latest_telemetry(payload["device_id"])


def get_latest_telemetry(device_id: str) -> dict[str, Any] | None:
    with connect_db() as conn:
        row = conn.execute(
            """
            SELECT device_id, timestamp, longitude, latitude, speed_kmh,
                   battery_percent, nav_active, metadata
            FROM telemetry_latest
            WHERE device_id = ?
            """,
            (device_id,),
        ).fetchone()

    if row is None:
        return None

    return {
        "device_id": row["device_id"],
        "timestamp": row["timestamp"],
        "longitude": row["longitude"],
        "latitude": row["latitude"],
        "speed_kmh": row["speed_kmh"],
        "battery_percent": row["battery_percent"],
        "nav_active": None if row["nav_active"] is None else bool(row["nav_active"]),
        "metadata": json.loads(row["metadata"]),
    }
