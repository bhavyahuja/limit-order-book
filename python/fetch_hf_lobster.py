#!/usr/bin/env python3
"""Download LOBSTER sample CSVs from Hugging Face (raw Hub files).

The datasets-server / first-rows API is unreliable here: LOBSTER CSVs have no
header, so HF treats the first row as column names. Download files directly.

Example:
  python3 python/fetch_hf_lobster.py --symbol AAPL --levels 10 --kind both
"""

from __future__ import annotations

import argparse
import json
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

DATASET = "totalorganfailure/lobster-data"
API_TREE = f"https://huggingface.co/api/datasets/{DATASET}/tree/main?recursive=1"
RESOLVE = f"https://huggingface.co/datasets/{DATASET}/resolve/main/"


def list_files() -> list[dict]:
    with urllib.request.urlopen(API_TREE, timeout=60) as resp:
        entries = json.load(resp)
    out = []
    for e in entries:
        path = e.get("path", "")
        if path.endswith(".csv") and ("message" in path or "orderbook" in path):
            out.append({"path": path, "size": e.get("size", 0)})
    return sorted(out, key=lambda x: x["path"])


def pick_file(files: list[dict], symbol: str, levels: int, kind: str) -> str:
    needle = f"_{symbol}_"
    candidates = [
        f
        for f in files
        if needle in f["path"] and f["path"].endswith(f"{kind}_{levels}.csv")
    ]
    if candidates:
        return candidates[0]["path"]
    raise SystemExit(f"No {kind} file for symbol={symbol} levels={levels}. Try --list.")


def download(path: str, dest: Path) -> None:
    url = RESOLVE + urllib.parse.quote(path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"GET {url}")
    try:
        with urllib.request.urlopen(url, timeout=300) as resp, dest.open("wb") as out:
            while True:
                chunk = resp.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"Download failed: HTTP {e.code} for {url}") from e
    print(f"wrote {dest} ({dest.stat().st_size:,} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument("--levels", type=int, default=10)
    parser.add_argument("--kind", choices=["message", "orderbook", "both"], default="both")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    files = list_files()
    if args.list:
        for f in files:
            print(f"{f['size']:10,}  {f['path']}")
        return

    out_dir = args.out_dir or (Path(__file__).resolve().parents[1] / "data" / "real")
    symbol = args.symbol.upper()
    kinds = ["message", "orderbook"] if args.kind == "both" else [args.kind]
    saved: dict[str, Path] = {}
    for kind in kinds:
        rel = pick_file(files, symbol, args.levels, kind)
        dest = out_dir / f"{symbol}_{kind}_{args.levels}.csv"
        download(rel, dest)
        saved[kind] = dest

    msg = saved.get("message")
    ob = saved.get("orderbook")
    print("Next:")
    if msg:
        print(
            f"  python3 python/clean_messages.py --input {msg} --output data/events_real.csv "
            f"--format lobster --price-unit lobster"
        )
        print(
            "  ./build/lob_engine --mode replay --events data/events_real.csv "
            "--snapshots data/snapshots_real_engine.csv --depth 5 --every-n 100 --pool-size 2000000"
        )
    if msg and ob:
        print(
            f"  python3 python/lobster_orderbook_to_snapshots.py --orderbook {ob} "
            f"--messages {msg} --output data/snapshots_real.csv --levels 5 --every-n 100"
        )
        print("  # Point notebooks/obi_study.ipynb at data/snapshots_real.csv")


if __name__ == "__main__":
    main()
