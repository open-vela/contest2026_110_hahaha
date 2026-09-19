#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convert a small OSM PBF road extract into a compact binary graph for the map demo.

The script uses `osmium export` to decode PBF on the PC side, then writes a simple
little-endian binary format that embedded code can read sequentially:

    char     magic[8]   = "EBNAV001"
    uint32   node_count
    uint32   edge_count
    int32    min_lon_e7
    int32    min_lat_e7
    int32    max_lon_e7
    int32    max_lat_e7
    uint32   reserved

    node[node_count]:
        int32 lon_e7
        int32 lat_e7

    edge[edge_count]:
        uint32 from_node
        uint32 to_node
        uint32 length_mm
        uint16 flags      bit0: oneway directed edge
        uint16 road_class
"""

import argparse
import json
import math
import os
import shlex
import struct
import subprocess
import tempfile
from pathlib import Path


MAGIC = b"EBNAV001"
FLAG_ONEWAY = 0x0001
FLAG_BICYCLE_DESIGNATED = 0x0002  # OSM 标注自行车允许或专用时写入该标志，设备端会降低规划代价。
FLAG_CYCLEWAY = 0x0004  # OSM 标注自行车道时写入该标志，设备端会按最高优先级规划。
FLAG_BICYCLE_FORBIDDEN = 0x0008  # OSM 标注自行车禁行时写入该标志，设备端会跳过该边。
ROAD_CLASS_ORDER = {
    "motorway": 1,
    "motorway_link": 1,
    "trunk": 2,
    "trunk_link": 2,
    "primary": 3,
    "primary_link": 3,
    "secondary": 4,
    "secondary_link": 4,
    "tertiary": 5,
    "tertiary_link": 5,
    "unclassified": 6,
    "residential": 7,
    "service": 8,
    "living_street": 9,
    "cycleway": 10,
    "footway": 11,
    "path": 12,
    "track": 13,
    "pedestrian": 14,
    "construction": 15,
}

BICYCLE_ALLOWED_VALUES = {"yes", "designated", "permissive", "official"}  # 可明确视为自行车可通行的 OSM 标签值。
BICYCLE_FORBIDDEN_VALUES = {"no", "private", "use_sidepath"}  # 可明确视为自行车不可通行的 OSM 标签值。
CYCLEWAY_VALUES = {
    "lane",
    "opposite_lane",
    "track",
    "opposite_track",
    "share_busway",
    "opposite_share_busway",
    "shared_lane",
    "crossing",
}  # 可视为道路带有自行车道设施的 OSM cycleway 标签值。
REVERSE_BICYCLE_VALUES = {"no", "false", "0"}  # 单行道中允许自行车逆向通行的 oneway:bicycle 标签值。
OPPOSITE_CYCLEWAY_VALUES = {"opposite", "opposite_lane", "opposite_track", "opposite_share_busway"}  # 表示存在自行车逆向设施的 cycleway 标签值。


def parse_args():
    parser = argparse.ArgumentParser(description="Convert OSM roads PBF to map demo binary graph.")
    parser.add_argument("--input", required=True, help="Input .osm.pbf file.")
    parser.add_argument("--output", required=True, help="Output road_graph.bin path.")
    parser.add_argument("--meta", required=True, help="Output road_graph.json path.")
    parser.add_argument(
        "--osmium",
        default="micromamba run -n osm -- osmium",
        help="Osmium command. Default uses the micromamba osm environment.",
    )
    return parser.parse_args()


def run_osmium_export(osmium_command, input_path, geojson_path):
    command = shlex.split(osmium_command, posix=False) + [
        "export",
        "-f",
        "geojson",
        "--geometry-types=linestring",
        "-o",
        str(geojson_path),
        "--overwrite",
        str(input_path),
    ]
    subprocess.run(command, check=True, shell=False)


def coord_to_e7(value):
    return int(round(value * 10000000.0))


def haversine_mm(lon1, lat1, lon2, lat2):
    radius_m = 6371008.8
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    delta_phi = math.radians(lat2 - lat1)
    delta_lambda = math.radians(lon2 - lon1)
    a = math.sin(delta_phi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(delta_lambda / 2.0) ** 2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    return int(round(radius_m * c * 1000.0))


def is_oneway(properties):
    value = str(properties.get("oneway", "")).lower()
    return value in ("yes", "true", "1")


def allows_reverse_bicycle(properties):
    """判断单行道路段是否允许自行车逆向通行。"""
    oneway_bicycle = str(properties.get("oneway:bicycle", "")).lower()
    cycleway_values = {
        str(properties.get(key, "")).lower()
        for key in ("cycleway", "cycleway:left", "cycleway:right", "cycleway:both")
    }

    return oneway_bicycle in REVERSE_BICYCLE_VALUES or bool(cycleway_values.intersection(OPPOSITE_CYCLEWAY_VALUES))


def road_flags(properties):
    """根据 OSM 道路属性生成设备端自行车导航标志位。"""
    flags = FLAG_ONEWAY if is_oneway(properties) else 0
    highway = str(properties.get("highway", "")).lower()
    bicycle = str(properties.get("bicycle", "")).lower()
    access = str(properties.get("access", "")).lower()
    cycleway_values = {
        str(properties.get(key, "")).lower()
        for key in ("cycleway", "cycleway:left", "cycleway:right", "cycleway:both")
    }

    if highway == "cycleway" or cycleway_values.intersection(CYCLEWAY_VALUES):
        flags |= FLAG_CYCLEWAY | FLAG_BICYCLE_DESIGNATED

    if bicycle in BICYCLE_ALLOWED_VALUES:
        flags |= FLAG_BICYCLE_DESIGNATED

    if bicycle in BICYCLE_FORBIDDEN_VALUES or access == "no":
        flags |= FLAG_BICYCLE_FORBIDDEN

    return flags


def road_class(properties):
    highway = str(properties.get("highway", "")).lower()
    return ROAD_CLASS_ORDER.get(highway, 0)


def load_graph_from_geojson(geojson_path):
    with open(geojson_path, "r", encoding="utf-8") as file:
        data = json.load(file)

    nodes = []
    node_index = {}
    edges = []
    min_lon = None
    min_lat = None
    max_lon = None
    max_lat = None
    road_feature_count = 0

    def get_node_id(lon, lat):
        nonlocal min_lon, min_lat, max_lon, max_lat
        key = (coord_to_e7(lon), coord_to_e7(lat))
        if key not in node_index:
            node_index[key] = len(nodes)
            nodes.append(key)
        min_lon = lon if min_lon is None else min(min_lon, lon)
        min_lat = lat if min_lat is None else min(min_lat, lat)
        max_lon = lon if max_lon is None else max(max_lon, lon)
        max_lat = lat if max_lat is None else max(max_lat, lat)
        return node_index[key]

    for feature in data.get("features", []):
        geometry = feature.get("geometry") or {}
        if geometry.get("type") != "LineString":
            continue

        coordinates = geometry.get("coordinates") or []
        if len(coordinates) < 2:
            continue

        properties = feature.get("properties") or {}
        reverse_allowed = not is_oneway(properties) or allows_reverse_bicycle(properties)
        flags = road_flags(properties)
        class_id = road_class(properties)
        road_feature_count += 1

        for start, end in zip(coordinates, coordinates[1:]):
            lon1, lat1 = start[:2]
            lon2, lat2 = end[:2]
            from_node = get_node_id(lon1, lat1)
            to_node = get_node_id(lon2, lat2)
            length_mm = haversine_mm(lon1, lat1, lon2, lat2)
            if length_mm <= 0:
                continue
            edges.append((from_node, to_node, length_mm, flags, class_id))
            if reverse_allowed:
                edges.append((to_node, from_node, length_mm, flags, class_id))

    bounds = {
        "min_lon": min_lon,
        "min_lat": min_lat,
        "max_lon": max_lon,
        "max_lat": max_lat,
    }
    return nodes, edges, bounds, road_feature_count


def write_graph(output_path, nodes, edges, bounds):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    min_lon_e7 = coord_to_e7(bounds["min_lon"] or 0.0)
    min_lat_e7 = coord_to_e7(bounds["min_lat"] or 0.0)
    max_lon_e7 = coord_to_e7(bounds["max_lon"] or 0.0)
    max_lat_e7 = coord_to_e7(bounds["max_lat"] or 0.0)

    with open(output_path, "wb") as file:
        file.write(MAGIC)
        file.write(struct.pack("<IIiiiiI", len(nodes), len(edges), min_lon_e7, min_lat_e7, max_lon_e7, max_lat_e7, 0))
        for lon_e7, lat_e7 in nodes:
            file.write(struct.pack("<ii", lon_e7, lat_e7))
        for from_node, to_node, length_mm, flags, class_id in edges:
            file.write(struct.pack("<IIIHH", from_node, to_node, length_mm, flags, class_id))


def write_meta(meta_path, input_path, output_path, nodes, edges, bounds, road_feature_count):
    meta_path.parent.mkdir(parents=True, exist_ok=True)
    road_class_counts = {}  # 按道路等级统计边数量，方便检查转换结果是否符合预期。
    flag_counts = {
        "oneway": 0,
        "bicycle_designated": 0,
        "cycleway": 0,
        "bicycle_forbidden": 0,
    }  # 按自行车导航标志统计边数量，方便确认 OSM 自行车标签是否已写入二进制路网。
    for _from_node, _to_node, _length_mm, flags, class_id in edges:
        class_key = str(class_id)
        road_class_counts[class_key] = road_class_counts.get(class_key, 0) + 1
        if flags & FLAG_ONEWAY:
            flag_counts["oneway"] += 1
        if flags & FLAG_BICYCLE_DESIGNATED:
            flag_counts["bicycle_designated"] += 1
        if flags & FLAG_CYCLEWAY:
            flag_counts["cycleway"] += 1
        if flags & FLAG_BICYCLE_FORBIDDEN:
            flag_counts["bicycle_forbidden"] += 1

    meta = {
        "format": "EBNAV001",
        "input": str(input_path),
        "output": str(output_path),
        "node_count": len(nodes),
        "edge_count": len(edges),
        "road_feature_count": road_feature_count,
        "bounds": bounds,
        "node_record_size": 8,
        "edge_record_size": 16,
        "flags": {
            "0x0001": "oneway directed edge",
            "0x0002": "bicycle allowed/designated edge",
            "0x0004": "cycleway-tagged edge",
            "0x0008": "bicycle forbidden edge",
        },
        "road_class": ROAD_CLASS_ORDER,
        "road_class_counts": road_class_counts,
        "flag_counts": flag_counts,
    }
    with open(meta_path, "w", encoding="utf-8") as file:
        json.dump(meta, file, ensure_ascii=False, indent=2)
        file.write("\n")


def main():
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)
    meta_path = Path(args.meta)

    with tempfile.TemporaryDirectory() as temp_dir:
        geojson_path = Path(temp_dir) / "roads.geojson"
        run_osmium_export(args.osmium, input_path, geojson_path)
        nodes, edges, bounds, road_feature_count = load_graph_from_geojson(geojson_path)

    write_graph(output_path, nodes, edges, bounds)
    write_meta(meta_path, input_path, output_path, nodes, edges, bounds, road_feature_count)

    print("input:", input_path)
    print("output:", output_path)
    print("meta:", meta_path)
    print("roads:", road_feature_count)
    print("nodes:", len(nodes))
    print("edges:", len(edges))
    print("size:", os.path.getsize(output_path))


if __name__ == "__main__":
    main()
