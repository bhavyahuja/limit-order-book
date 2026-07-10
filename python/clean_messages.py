#!/usr/bin/env python3
"""Clean raw tick / LOBSTER-style messages into engine events.csv.

Stdlib only. Output schema:
  timestamp,order_id,side,price_ticks,size,action
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

LOBSTER_NEW = 1
LOBSTER_CANCEL = 2
LOBSTER_DELETE = 3
LOBSTER_EXECUTE = 4
LOBSTER_EXECUTE_HIDDEN = 5

TYPE_MAP = {
    LOBSTER_NEW: "ADD",
    LOBSTER_CANCEL: "CANCEL",
    LOBSTER_DELETE: "CANCEL",
    LOBSTER_EXECUTE: "EXECUTE",
    LOBSTER_EXECUTE_HIDDEN: "EXECUTE",
}


def clean_lobster(rows: list[dict], price_tick_size: float = 0.01) -> list[dict]:
    out: list[dict] = []
    for r in rows:
        try:
            typ = int(float(r["type"]))
            action = TYPE_MAP.get(typ)
            if action is None:
                continue
            size = int(float(r["size"]))
            order_id = int(float(r["order_id"]))
            if size <= 0 or order_id <= 0:
                continue
            direction = int(float(r["direction"]))
            price = float(r["price"])
            # LOBSTER time is seconds from midnight as float → ns
            timestamp = int(round(float(r["time"]) * 1e9))
            out.append(
                {
                    "timestamp": timestamp,
                    "order_id": order_id,
                    "side": "B" if direction == 1 else "S",
                    "price_ticks": int(round(price / price_tick_size)),
                    "size": size,
                    "action": action,
                }
            )
        except (KeyError, ValueError):
            continue
    return out


def clean_engine(rows: list[dict]) -> list[dict]:
    out: list[dict] = []
    for r in rows:
        # allow aliases
        if "price" in r and "price_ticks" not in r:
            r = {**r, "price_ticks": r["price"]}
        if "qty" in r and "size" not in r:
            r = {**r, "size": r["qty"]}
        try:
            action = str(r["action"]).upper()
            if action not in {"ADD", "CANCEL", "EXECUTE"}:
                continue
            size = int(float(r["size"]))
            if size <= 0:
                continue
            out.append(
                {
                    "timestamp": int(float(r["timestamp"])),
                    "order_id": int(float(r["order_id"])),
                    "side": str(r["side"]).upper()[0],
                    "price_ticks": int(float(r["price_ticks"])),
                    "size": size,
                    "action": action,
                }
            )
        except (KeyError, ValueError, IndexError):
            continue
    return out


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["timestamp", "order_id", "side", "price_ticks", "size", "action"]
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--format", choices=["lobster", "engine"], default="lobster")
    parser.add_argument("--tick-size", type=float, default=0.01)
    args = parser.parse_args()

    raw = read_csv(args.input)
    if args.format == "lobster":
        clean = clean_lobster(raw, price_tick_size=args.tick_size)
    else:
        clean = clean_engine(raw)

    write_csv(args.output, clean)
    print(f"wrote {args.output} ({len(clean)} events)")


if __name__ == "__main__":
    main()
