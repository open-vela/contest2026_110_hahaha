from __future__ import annotations

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, Field


JobType = Literal["asr", "tts"]
JobStatus = Literal["pending", "processing", "done", "failed"]


class JobResponse(BaseModel):
    id: str
    job_type: JobType
    status: JobStatus
    device_id: str
    request_id: str
    created_at: datetime
    updated_at: datetime
    claimed_at: datetime | None = None
    completed_at: datetime | None = None
    worker_id: str | None = None
    error_message: str | None = None
    request_payload: dict[str, Any]
    result_payload: dict[str, Any]
    request_file_path: str | None = None
    result_file_path: str | None = None


class JobPendingResponse(BaseModel):
    items: list[JobResponse]


class TTSJobCreateRequest(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=64)
    request_id: str = Field(..., min_length=1, max_length=128)
    text: str = Field(..., min_length=1, max_length=512)
    voice: str | None = Field(default=None, max_length=128)
    priority: int = 0
    metadata: dict[str, Any] = Field(default_factory=dict)


class TelemetryUpsertRequest(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=64)
    timestamp: datetime
    longitude: float | None = None
    latitude: float | None = None
    speed_kmh: float | None = None
    battery_percent: int | None = Field(default=None, ge=0, le=100)
    nav_active: bool | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class TelemetryResponse(BaseModel):
    device_id: str
    timestamp: datetime
    longitude: float | None = None
    latitude: float | None = None
    speed_kmh: float | None = None
    battery_percent: int | None = None
    nav_active: bool | None = None
    metadata: dict[str, Any]


class JobResultResponse(BaseModel):
    job: JobResponse


class HealthResponse(BaseModel):
    status: Literal["ok"]
    app_name: str
    version: str


# ── OTA schemas ──────────────────────────────────────────────────────────


class OTAComponentInfo(BaseModel):
    name: str
    version: str
    sha256: str
    size: int


class OTAUpdateInfo(BaseModel):
    version: str
    build_time: str
    download_url: str
    sha256: str
    size: int
    min_firmware_version: str | None = None
    components: list[OTAComponentInfo] = []
    changelog: str = ""


class OTACheckResponse(BaseModel):
    device_id: str
    current_app_version: str
    app_update: OTAUpdateInfo | None = None
    firmware_update: OTAUpdateInfo | None = None


class OTADeviceStatusRequest(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=64)
    current_fw_version: str = "0.0.0"
    current_app_version: str = "0.0.0"
    last_update_time: datetime | None = None
    state: str = "idle"


class OTADeviceStatusResponse(BaseModel):
    device_id: str
    registered_at: datetime
