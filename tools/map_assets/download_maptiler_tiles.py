#!/usr/bin/env python3

import argparse
import concurrent.futures
import math
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


DEFAULT_MIN_LON = 112.88000000
DEFAULT_MIN_LAT = 28.17000000
DEFAULT_MAX_LON = 113.02000000
DEFAULT_MAX_LAT = 28.31000000
DEFAULT_ZOOMS = tuple(range(8, 19))
DEFAULT_STYLE = "streets-v2"
USER_AGENT = "qiban-offline-map/0.1"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Download raster tiles from MapTiler for qiban_ui offline map."
    )
    parser.add_argument(
        "--key",
        default=os.environ.get("MAPTILER_KEY", ""),
        help="MapTiler API key. Defaults to MAPTILER_KEY.",
    )
    parser.add_argument(
        "--output-root",
        required=True,
        help="Directory where z/x/y/tile.png will be written.",
    )
    parser.add_argument("--min-lon", type=float, default=DEFAULT_MIN_LON)
    parser.add_argument("--min-lat", type=float, default=DEFAULT_MIN_LAT)
    parser.add_argument("--max-lon", type=float, default=DEFAULT_MAX_LON)
    parser.add_argument("--max-lat", type=float, default=DEFAULT_MAX_LAT)
    parser.add_argument(
        "--zoom",
        type=int,
        nargs="+",
        default=list(DEFAULT_ZOOMS),
        help="One or more zoom levels. Default: 8 9 10 11 12 13 14 15 16 17 18",
    )
    parser.add_argument(
        "--style",
        default=DEFAULT_STYLE,
        help="MapTiler style name. Default: streets-v2",
    )
    parser.add_argument(
        "--parallel",
        type=int,
        default=8,
        help="Parallel download workers. Default: 8",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Redownload tiles even if tile.png already exists.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print tile coverage without downloading.",
    )
    return parser.parse_args()


def lon_to_tile_x(lon, zoom):
    return int(math.floor((lon + 180.0) / 360.0 * (2 ** zoom)))


def lat_to_tile_y(lat, zoom):
    lat_rad = math.radians(lat)
    return int(
        math.floor(
            (1.0 - math.log(math.tan(lat_rad) + (1.0 / math.cos(lat_rad))) / math.pi)
            / 2.0
            * (2 ** zoom)
        )
    )


def build_jobs(args):
    jobs = []
    for zoom in args.zoom:
        x0 = lon_to_tile_x(args.min_lon, zoom)
        x1 = lon_to_tile_x(args.max_lon, zoom)
        y0 = lat_to_tile_y(args.max_lat, zoom)
        y1 = lat_to_tile_y(args.min_lat, zoom)
        print(f"z{zoom} x={x0}..{x1} y={y0}..{y1}")
        for tile_x in range(x0, x1 + 1):
            for tile_y in range(y0, y1 + 1):
                tile_path = (
                    Path(args.output_root)
                    / str(zoom)
                    / str(tile_x)
                    / str(tile_y)
                    / "tile.png"
                )
                if tile_path.exists() and not args.overwrite:
                    continue
                jobs.append((zoom, tile_x, tile_y, tile_path))
    return jobs


def download_one(job, key, style):
    zoom, tile_x, tile_y, tile_path = job
    url = (
        f"https://api.maptiler.com/maps/{style}/256/"
        f"{zoom}/{tile_x}/{tile_y}.png?key={key}"
    )
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    tile_path.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    with open(tile_path, "wb") as file:
        file.write(data)
    return zoom, tile_x, tile_y


def main():
    args = parse_args()
    if not args.key and not args.dry_run:
      print("MapTiler key is required. Set --key or MAPTILER_KEY.", file=sys.stderr)
      return 2

    jobs = build_jobs(args)
    print(f"pending: {len(jobs)}")
    if args.dry_run or not jobs:
        return 0

    downloaded = 0
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.parallel)) as executor:
        future_map = {
            executor.submit(download_one, job, args.key, args.style): job for job in jobs
        }
        for future in concurrent.futures.as_completed(future_map):
            job = future_map[future]
            try:
                zoom, tile_x, tile_y = future.result()
                downloaded += 1
                print(f"downloaded z={zoom} x={tile_x} y={tile_y}")
            except (urllib.error.URLError, OSError, ValueError) as err:
                failed += 1
                zoom, tile_x, tile_y, _ = job
                print(
                    f"failed z={zoom} x={tile_x} y={tile_y}: {err}",
                    file=sys.stderr,
                )

    print(f"downloaded: {downloaded}")
    print(f"failed: {failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
