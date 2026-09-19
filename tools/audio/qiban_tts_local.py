#!/usr/bin/env python3
import argparse
import asyncio
import os
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        description="Synthesize host-side TTS and normalize it to 16kHz s16le mono PCM."
    )
    parser.add_argument(
        "--text",
        default="",
        help="Text to synthesize directly",
    )
    parser.add_argument(
        "--request",
        default="",
        help="Path to a text request file",
    )
    parser.add_argument(
        "--voice",
        default=os.environ.get("QIBAN_TTS_VOICE", "zh-CN-XiaoxiaoNeural"),
        help="Edge TTS voice name",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Path to the normalized raw PCM output file",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=16000,
        help="Target sample rate in Hz",
    )
    return parser.parse_args()


def load_text(args):
    if args.text:
        return args.text.strip()
    if args.request:
        if not os.path.isfile(args.request):
            raise FileNotFoundError(f"Request text file not found: {args.request}")
        with open(args.request, "r", encoding="utf-8") as fp:
            return " ".join(fp.read().split())
    raise ValueError("Either --text or --request must be provided.")


def decode_mp3_to_pcm(mp3_path, output_path, target_rate):
    try:
        import miniaudio
    except ImportError as exc:
        print(f"Failed to import miniaudio: {exc}", file=sys.stderr)
        print("Run ./tools/audio/setup_local_voice.sh first.", file=sys.stderr)
        return 1

    decoded = miniaudio.decode_file(
        mp3_path,
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
        sample_rate=target_rate,
    )

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as fp:
        fp.write(decoded.samples.tobytes())
    return 0


async def synthesize_to_mp3(text, voice, mp3_path):
    try:
        import edge_tts
    except ImportError as exc:
        print(f"Failed to import edge-tts: {exc}", file=sys.stderr)
        print("Run ./tools/audio/setup_local_voice.sh first.", file=sys.stderr)
        return 1

    communicate = edge_tts.Communicate(
        text=text,
        voice=voice,
        rate="+0%",
        volume="+0%",
    )
    await communicate.save(mp3_path)
    return 0


def main():
    args = parse_args()

    text = load_text(args)
    if not text:
        print("TTS request text is empty.", file=sys.stderr)
        return 1

    if not args.voice:
        print("QIBAN_TTS_VOICE is not set and --voice was not provided.", file=sys.stderr)
        return 1

    tmp_mp3 = f"{args.output}.mp3"
    status = asyncio.run(synthesize_to_mp3(text, args.voice, tmp_mp3))
    if status != 0:
        return status

    try:
        status = decode_mp3_to_pcm(tmp_mp3, args.output, args.sample_rate)
        if status != 0:
            return status
    finally:
        if os.path.exists(tmp_mp3):
            os.unlink(tmp_mp3)

    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
