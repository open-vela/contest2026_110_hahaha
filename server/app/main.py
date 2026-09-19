from __future__ import annotations

import json
import sqlite3
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, File, Form, HTTPException, Query, UploadFile
from fastapi.responses import FileResponse

from .config import settings
from .schemas import (
    HealthResponse,
    JobPendingResponse,
    JobResponse,
    JobResultResponse,
    OTACheckResponse,
    OTADeviceStatusRequest,
    OTADeviceStatusResponse,
    OTAUpdateInfo,
    TTSJobCreateRequest,
    TelemetryResponse,
    TelemetryUpsertRequest,
)
from .storage import (
    complete_job,
    create_asr_job,
    create_tts_job,
    get_job,
    get_latest_telemetry,
    init_db,
    list_pending_jobs,
    upsert_telemetry,
)


@asynccontextmanager
async def lifespan(_: FastAPI):
    init_db()
    yield


app = FastAPI(
    title=settings.app_name,
    version=settings.app_version,
    lifespan=lifespan,
)


def _job_or_404(job_id: str) -> dict:
    job = get_job(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail=f"job not found: {job_id}")
    return job


@app.get("/healthz", response_model=HealthResponse)
async def healthz() -> HealthResponse:
    return HealthResponse(
        status="ok",
        app_name=settings.app_name,
        version=settings.app_version,
    )


@app.post("/api/asr/jobs", response_model=JobResponse, status_code=201)
async def create_asr_job_api(
    device_id: str = Form(...),
    request_id: str = Form(...),
    duration_s: int = Form(...),
    sample_rate_hz: int = Form(16000),
    channels: int = Form(1),
    bits_per_sample: int = Form(16),
    metadata: str = Form("{}"),
    audio_file: UploadFile = File(...),
) -> JobResponse:
    try:
        metadata_obj = json.loads(metadata)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=400, detail=f"invalid metadata JSON: {exc}") from exc

    try:
        job = create_asr_job(
            device_id=device_id,
            request_id=request_id,
            request_payload={
                "duration_s": duration_s,
                "sample_rate_hz": sample_rate_hz,
                "channels": channels,
                "bits_per_sample": bits_per_sample,
                "metadata": metadata_obj,
                "filename": audio_file.filename,
                "content_type": audio_file.content_type,
            },
            original_filename=audio_file.filename or "request_audio.pcm",
            audio_bytes=await audio_file.read(),
        )
    except sqlite3.IntegrityError as exc:
        raise HTTPException(status_code=409, detail="duplicate request_id for device/job_type") from exc

    return JobResponse.model_validate(job)


@app.post("/api/tts/jobs", response_model=JobResponse, status_code=201)
async def create_tts_job_api(payload: TTSJobCreateRequest) -> JobResponse:
    try:
        job = create_tts_job(
            device_id=payload.device_id,
            request_id=payload.request_id,
            request_payload={
                "text": payload.text,
                "voice": payload.voice,
                "priority": payload.priority,
                "metadata": payload.metadata,
            },
        )
    except sqlite3.IntegrityError as exc:
        raise HTTPException(status_code=409, detail="duplicate request_id for device/job_type") from exc

    return JobResponse.model_validate(job)


@app.get("/api/jobs/pending", response_model=JobPendingResponse)
async def list_pending_jobs_api(
    job_type: str = Query(..., pattern="^(asr|tts)$"),
    limit: int = Query(1, ge=1, le=20),
    claim: bool = Query(True),
    worker_id: str | None = Query(default=None),
) -> JobPendingResponse:
    if claim and not worker_id:
        raise HTTPException(status_code=400, detail="worker_id is required when claim=true")

    jobs = list_pending_jobs(job_type=job_type, limit=limit, claim=claim, worker_id=worker_id)
    return JobPendingResponse(items=[JobResponse.model_validate(job) for job in jobs])


@app.get("/api/jobs/{job_id}", response_model=JobResponse)
async def get_job_api(job_id: str) -> JobResponse:
    return JobResponse.model_validate(_job_or_404(job_id))


@app.post("/api/jobs/{job_id}/result", response_model=JobResultResponse)
async def complete_job_api(
    job_id: str,
    status: str = Form(...),
    result_payload: str = Form("{}"),
    error_message: str | None = Form(default=None),
    result_file: UploadFile | None = File(default=None),
) -> JobResultResponse:
    if status not in {"done", "failed"}:
        raise HTTPException(status_code=400, detail="status must be 'done' or 'failed'")

    try:
        result_payload_obj = json.loads(result_payload)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=400, detail=f"invalid result_payload JSON: {exc}") from exc

    result_content = None
    result_filename = None
    if result_file is not None:
        result_content = await result_file.read()
        result_filename = result_file.filename or "result.bin"

    job = complete_job(
        job_id=job_id,
        status=status,
        result_payload=result_payload_obj,
        error_message=error_message,
        result_filename=result_filename,
        result_content=result_content,
    )
    if job is None:
        raise HTTPException(status_code=404, detail=f"job not found: {job_id}")

    return JobResultResponse(job=JobResponse.model_validate(job))


@app.get("/api/jobs/{job_id}/request-file")
async def download_job_request_file(job_id: str):
    job = _job_or_404(job_id)
    path = job.get("request_file_path")
    if not path:
        raise HTTPException(status_code=404, detail="request file not found")
    return FileResponse(Path(path))


@app.get("/api/jobs/{job_id}/result-file")
async def download_job_result_file(job_id: str):
    job = _job_or_404(job_id)
    path = job.get("result_file_path")
    if not path:
        raise HTTPException(status_code=404, detail="result file not found")
    return FileResponse(Path(path))


@app.post("/api/device/telemetry", response_model=TelemetryResponse)
async def upsert_telemetry_api(payload: TelemetryUpsertRequest) -> TelemetryResponse:
    saved = upsert_telemetry(payload.model_dump(mode="json"))
    return TelemetryResponse.model_validate(saved)


@app.get("/api/device/{device_id}/latest", response_model=TelemetryResponse)
async def get_latest_telemetry_api(device_id: str) -> TelemetryResponse:
    telemetry = get_latest_telemetry(device_id)
    if telemetry is None:
        raise HTTPException(status_code=404, detail=f"device not found: {device_id}")
    return TelemetryResponse.model_validate(telemetry)


# ── OTA endpoints ────────────────────────────────────────────────────────


def _ota_load_latest_manifest() -> dict | None:
    """Load the latest OTA manifest from the packages directory."""

    manifest_path = settings.ota_packages_dir / "latest" / "manifest.json"
    if not manifest_path.exists():
        return None
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def _ota_ensure_dirs() -> None:
    settings.ota_dir.mkdir(parents=True, exist_ok=True)
    settings.ota_packages_dir.mkdir(parents=True, exist_ok=True)


def _semver_gt(a: str, b: str) -> bool:
    """Return True if version a > b (simple semver compare)."""

    def parse(v: str) -> tuple[int, ...]:
        return tuple(int(x) for x in v.split(".") if x.isdigit())

    return parse(a) > parse(b)


@app.get("/api/ota/check", response_model=OTACheckResponse)
async def ota_check(
    device_id: str = Query(..., min_length=1),
    app_version: str = Query("0.0.0"),
    fw_version: str = Query("0.0.0"),
) -> OTACheckResponse:
    """Check if an OTA update is available for the device."""

    manifest = _ota_load_latest_manifest()
    app_update = None
    fw_update = None

    if manifest and _semver_gt(manifest.get("version", "0.0.0"), app_version):
        app_update = OTAUpdateInfo(
            version=manifest["version"],
            build_time=manifest.get("build_time", ""),
            download_url=f"/api/ota/app/download?version={manifest['version']}",
            sha256=manifest.get("sha256", ""),
            size=manifest.get("size", 0),
            min_firmware_version=manifest.get("min_firmware_version"),
            components=[],
            changelog=manifest.get("changelog", ""),
        )

    return OTACheckResponse(
        device_id=device_id,
        current_app_version=app_version,
        app_update=app_update,
        firmware_update=fw_update,
    )


@app.get("/api/ota/app/download")
async def ota_app_download(version: str = Query(...)):
    """Download an app OTA package."""

    pkg_path = settings.ota_packages_dir / "latest" / "app_ota.zip"
    if not pkg_path.exists():
        raise HTTPException(status_code=404, detail="OTA package not found")
    return FileResponse(pkg_path, media_type="application/zip",
                        filename=f"app_ota_{version}.zip")


@app.post("/api/ota/device/status", response_model=OTADeviceStatusResponse)
async def ota_device_status(payload: OTADeviceStatusRequest) -> OTADeviceStatusResponse:
    """Register or update device OTA status."""

    _ota_ensure_dirs()
    status_path = settings.ota_dir / "devices" / f"{payload.device_id}.json"
    status_path.parent.mkdir(parents=True, exist_ok=True)

    now = datetime.now(timezone.utc)
    status_data = {
        "device_id": payload.device_id,
        "current_fw_version": payload.current_fw_version,
        "current_app_version": payload.current_app_version,
        "state": payload.state,
        "last_seen": now.isoformat(),
    }
    status_path.write_text(json.dumps(status_data, indent=2), encoding="utf-8")

    return OTADeviceStatusResponse(device_id=payload.device_id,
                                   registered_at=now)


@app.get("/api/ota/firmware/check")
async def ota_firmware_check(
    device_id: str = Query(..., min_length=1),
    fw_version: str = Query("0.0.0"),
) -> dict:
    """Check if a firmware OTA update is available."""

    manifest_path = settings.ota_packages_dir / "firmware" / "firmware_manifest.json"
    if not manifest_path.exists():
        return {
            "device_id": device_id,
            "current_fw_version": fw_version,
            "firmware_update": None,
        }

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    latest_version = manifest.get("version", "0.0.0")

    if not _semver_gt(latest_version, fw_version):
        return {
            "device_id": device_id,
            "current_fw_version": fw_version,
            "firmware_update": None,
        }

    return {
        "device_id": device_id,
        "current_fw_version": fw_version,
        "firmware_update": {
            "version": latest_version,
            "build_time": manifest.get("build_time", ""),
            "sha256": manifest.get("sha256", ""),
            "size": manifest.get("size", 0),
            "download_url": f"/api/ota/firmware/download?version={latest_version}",
            "type": manifest.get("type", "firmware_recovery"),
        },
    }


@app.get("/api/ota/firmware/download")
async def ota_firmware_download(version: str = Query("latest")):
    """Download firmware OTA package (ota.zip)."""

    # Try versioned package first, then latest
    pkg_dir = settings.ota_packages_dir / "firmware"
    pkg_path = pkg_dir / f"ota_{version}.zip"
    if not pkg_path.exists():
        pkg_path = pkg_dir / "ota.zip"
    if not pkg_path.exists():
        raise HTTPException(status_code=404,
                            detail="Firmware OTA package not found")
    return FileResponse(pkg_path, media_type="application/zip",
                        filename=f"firmware_ota_{version}.zip")
