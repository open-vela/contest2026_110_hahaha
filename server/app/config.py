from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    app_name: str = "Qiban Voice Relay Server"
    app_version: str = "0.1.0"
    host: str = os.environ.get("QIBAN_SERVER_HOST", "0.0.0.0")
    port: int = int(os.environ.get("QIBAN_SERVER_PORT", "8787"))
    data_dir_raw: str = os.environ.get("QIBAN_SERVER_DATA_DIR", "")
    database_path_raw: str = os.environ.get("QIBAN_SERVER_DB_PATH", "")
    data_dir: Path = Path(".")
    database_path: Path = Path(".")

    def __post_init__(self) -> None:
        default_data_dir = Path(__file__).resolve().parents[1] / "data"
        data_dir = Path(self.data_dir_raw).expanduser() if self.data_dir_raw else default_data_dir
        database_path = (
            Path(self.database_path_raw).expanduser()
            if self.database_path_raw
            else data_dir / "qiban_relay.sqlite3"
        )
        object.__setattr__(self, "data_dir", data_dir)
        object.__setattr__(self, "database_path", database_path)

    @property
    def uploads_dir(self) -> Path:
        return self.data_dir / "uploads"

    @property
    def job_files_dir(self) -> Path:
        return self.uploads_dir / "jobs"

    @property
    def telemetry_dir(self) -> Path:
        return self.data_dir / "telemetry"

    @property
    def ota_dir(self) -> Path:
        return self.data_dir / "ota"

    @property
    def ota_packages_dir(self) -> Path:
        return self.ota_dir / "packages"


settings = Settings()
