#!/usr/bin/env python3
import argparse
import json
import os
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run offline Chinese ASR on a raw 16k s16le mono PCM file."
    )
    parser.add_argument("pcm_path", help="Path to the recorded PCM file")
    parser.add_argument(
        "--model-dir",
        default=os.environ.get("QIBAN_ASR_MODEL_DIR", ""),
        help="Path to a Vosk model directory",
    )
    parser.add_argument(
        "--sample-rate",
        type=float,
        default=16000.0,
        help="PCM sample rate in Hz",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    if not args.model_dir:
        print("QIBAN_ASR_MODEL_DIR is not set and --model-dir was not provided.", file=sys.stderr)
        return 1

    if not os.path.isdir(args.model_dir):
        print(f"ASR model directory not found: {args.model_dir}", file=sys.stderr)
        return 1

    if not os.path.isfile(args.pcm_path):
        print(f"PCM file not found: {args.pcm_path}", file=sys.stderr)
        return 1

    try:
        from vosk import KaldiRecognizer, Model, SetLogLevel
    except ImportError as exc:
        print(f"Failed to import vosk: {exc}", file=sys.stderr)
        print("Run ./tools/audio/setup_local_voice.sh first.", file=sys.stderr)
        return 1

    SetLogLevel(-1)
    model = Model(args.model_dir)
    recognizer = KaldiRecognizer(model, args.sample_rate)
    chunks = []

    with open(args.pcm_path, "rb") as fp:
        while True:
            data = fp.read(4000)
            if not data:
                break
            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                text = result.get("text", "").strip()
                if text:
                    chunks.append(text)

    final_result = json.loads(recognizer.FinalResult())
    final_text = final_result.get("text", "").strip()
    if final_text:
        chunks.append(final_text)

    print(" ".join(chunks).strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
