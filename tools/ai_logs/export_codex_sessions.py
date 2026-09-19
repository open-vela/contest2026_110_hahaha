#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sqlite3
import sys
from typing import Any


SECRET_PATTERNS = [
    (re.compile(r"\bgh[pousr]_[A-Za-z0-9_]{20,}\b"), "[REDACTED_GITHUB_TOKEN]"),
    (re.compile(r"\bsk-[A-Za-z0-9_-]{16,}\b"), "[REDACTED_API_KEY]"),
    (re.compile(r"(?i)(authorization\s*:\s*bearer\s+)[^\s\"']+"), r"\1[REDACTED]"),
    (re.compile(r"(?i)((?:password|passwd|secret|api[_-]?key|access[_-]?token|refresh[_-]?token)\s*[=:]\s*)[^\s\"']+"), r"\1[REDACTED]"),
    (re.compile(r"(密码\s*[：:=]?\s*)[^\s\"']+"), r"\1[REDACTED]"),
    (re.compile(r"((?:AMAP|MAPTILER)[_-]?KEY\s+(?:\"|'))[^\"']+", re.I), r"\1[REDACTED]"),
    (re.compile(r"(?i)([?&](?:key|api_key|token)=)[^&\s\"']+"), r"\1[REDACTED]"),
]


def redact(text: str) -> str:
    for pattern, replacement in SECRET_PATTERNS:
        text = pattern.sub(replacement, text)
    return text


def flatten_content(content: Any) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(
            item["text"] if isinstance(item, dict) and isinstance(item.get("text"), str) else item
            for item in content
            if isinstance(item, str) or isinstance(item, dict) and isinstance(item.get("text"), str)
        )
    return json.dumps(content, ensure_ascii=False)


def load_threads(db_path: Path, session_ids: set[str], terms: list[str]) -> list[sqlite3.Row]:
    connection = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    rows = connection.execute(
        """
        SELECT id, rollout_path, created_at, updated_at, title, cwd,
               tokens_used, model, first_user_message
        FROM threads ORDER BY created_at
        """
    ).fetchall()
    connection.close()

    selected: list[sqlite3.Row] = []
    lowered_terms = [term.lower() for term in terms]
    for row in rows:
        haystack = "\n".join(str(row[key] or "") for key in ("title", "first_user_message", "cwd")).lower()
        if row["id"] in session_ids or any(term in haystack for term in lowered_terms):
            if Path(row["rollout_path"]).is_file():
                selected.append(row)
    return selected


def safe_json_value(value: Any, key: str | None = None) -> Any:
    if key == "chars":
        return "[REDACTED_INTERACTIVE_INPUT]"
    if key and re.search(r"(?i)(password|passwd|secret|api[_-]?key|access[_-]?token|refresh[_-]?token)", key):
        return "[REDACTED]"
    if isinstance(value, str):
        return redact(value)
    if isinstance(value, list):
        return [safe_json_value(item) for item in value]
    if isinstance(value, dict):
        return {item_key: safe_json_value(item, item_key) for item_key, item in value.items()}
    return value


def export_session(row: sqlite3.Row, team_id: str, login: str, output_root: Path) -> dict[str, Any]:
    source = Path(row["rollout_path"])
    date = datetime.fromtimestamp(row["created_at"], timezone.utc).strftime("%Y-%m-%d")
    relative = Path(login) / date / f"codex__{row['id']}.jsonl"
    destination = output_root / relative
    destination.parent.mkdir(parents=True, exist_ok=True)

    events: list[dict[str, Any]] = []
    pending_tools: dict[str, dict[str, Any]] = {}
    model = row["model"] or "codex"
    token_usage: dict[str, int] = {}
    parse_errors = 0

    with source.open(encoding="utf-8") as handle:
        for line in handle:
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                parse_errors += 1
                continue
            timestamp = record.get("timestamp") or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            payload = record.get("payload", {})
            outer_type = record.get("type")
            payload_type = payload.get("type")

            if outer_type == "turn_context" and payload.get("model"):
                model = payload["model"]
            elif outer_type == "event_msg" and payload_type == "user_message":
                text = redact(str(payload.get("message", "")))
                if text:
                    events.append({"ts": timestamp, "role": "user", "text": text})
            elif outer_type == "event_msg" and payload_type == "agent_message":
                text = redact(str(payload.get("message", "")))
                if text:
                    events.append({"ts": timestamp, "role": "assistant", "model": model, "text": text})
            elif outer_type == "event_msg" and payload_type == "token_count":
                usage = (payload.get("info") or {}).get("total_token_usage") or {}
                token_usage = {
                    "tokens_in": int(usage.get("input_tokens", 0)),
                    "tokens_out": int(usage.get("output_tokens", 0)),
                    "tokens_total": int(usage.get("total_tokens", 0)),
                }
            elif outer_type == "response_item" and payload_type in {"custom_tool_call", "function_call"}:
                call_id = str(payload.get("call_id", ""))
                raw_input = payload.get("input", payload.get("arguments", ""))
                try:
                    tool_input: Any = json.loads(raw_input) if isinstance(raw_input, str) else raw_input
                except json.JSONDecodeError:
                    tool_input = raw_input
                pending_tools[call_id] = {
                    "ts": timestamp,
                    "role": "tool",
                    "tool_name": payload.get("name", "unknown"),
                    "tool_call_id": call_id,
                    "input": safe_json_value(tool_input),
                }
            elif outer_type == "response_item" and payload_type in {"custom_tool_call_output", "function_call_output"}:
                call_id = str(payload.get("call_id", ""))
                event = pending_tools.pop(call_id, {
                    "ts": timestamp,
                    "role": "tool",
                    "tool_name": "unknown",
                    "tool_call_id": call_id,
                    "input": {},
                })
                event["output"] = redact(flatten_content(payload.get("output", "")))
                events.append(event)

    events.extend(pending_tools.values())
    events.sort(key=lambda item: item["ts"])
    with destination.open("w", encoding="utf-8") as handle:
        for sequence, event in enumerate(events):
            output = {
                "schema_version": "1.0",
                "session_id": row["id"],
                "team_id": team_id,
                "github_login": login,
                "tool": "codex",
                "seq": sequence,
                **event,
            }
            handle.write(json.dumps(output, ensure_ascii=False, separators=(",", ":")) + "\n")

    return {
        "session_id": row["id"],
        "tool": "codex",
        "started_at": datetime.fromtimestamp(row["created_at"], timezone.utc).isoformat().replace("+00:00", "Z"),
        "last_event_at": datetime.fromtimestamp(row["updated_at"], timezone.utc).isoformat().replace("+00:00", "Z"),
        "event_count": len(events),
        "file_path": (Path("logs") / relative).as_posix(),
        "collection_mode": "codex-native-rollout-export",
        "health": "ok" if parse_errors == 0 else "partial",
        "skipped_invalid_records": parse_errors,
        **token_usage,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Export real Codex rollouts to contest AI Coding JSONL")
    parser.add_argument("--state-db", type=Path, default=Path.home() / ".codex/state_5.sqlite")
    parser.add_argument("--output-root", type=Path, required=True, help="Repository logs directory")
    parser.add_argument("--team-id", default="contest2026_110_hahaha")
    parser.add_argument("--github-login", required=True)
    parser.add_argument("--term", action="append", default=[])
    parser.add_argument("--session", action="append", default=[])
    args = parser.parse_args()

    rows = load_threads(args.state_db, set(args.session), args.term)
    if not rows:
        print("no matching Codex sessions", file=sys.stderr)
        return 1

    manifests = [export_session(row, args.team_id, args.github_login, args.output_root) for row in rows]
    login_root = args.output_root / args.github_login
    manifest = {
        "schema_version": "1.0",
        "team_id": args.team_id,
        "github_login": args.github_login,
        "generator": "tools/ai_logs/export_codex_sessions.py",
        "sessions": manifests,
        "session_count": len(manifests),
        "token_total": sum(item.get("tokens_total", 0) for item in manifests),
        "updated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }
    login_root.mkdir(parents=True, exist_ok=True)
    (login_root / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"exported_sessions={len(manifests)}")
    print(f"token_total={manifest['token_total']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
