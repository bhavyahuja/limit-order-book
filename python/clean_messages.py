#!/usr/bin/env python3
"""Clean raw tick / LOBSTER-style messages into engine events.csv.

Stdlib only. Output schema:
  timestamp,order_id,side,price_ticks,size,action

LOBSTER message files have NO header. Columns are:
  time, type, order_id, size, price, direction

Price in official LOBSTER files is an integer = dollars * 10000
(e.g. 5853300 → $585.33). Use --price-unit lobster for those files.
Synthetic samples from generate_sample_data.py use dollar floats → --price-unit dollar.
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

LOBSTER_COLUMNS = ["time", "type", "order_id", "size", "price", "direction"]


def price_to_ticks(price: float, price_unit: str, tick_size: float) -> int:
    """Convert LOBSTER/synthetic price field to integer ticks for the engine."""
    if price_unit == "lobster":
        # LOBSTER stores dollars * 10000 as int; tick_size default $0.01 → /100
        dollars = float(price) / 10000.0
        return int(round(dollars / tick_size))
    if price_unit == "dollar":
        return int(round(float(price) / tick_size))
    if price_unit == "ticks":
        return int(round(float(price)))
    raise ValueError(f"Unknown price_unit: {price_unit}")


def clean_lobster(
    rows: list[dict],
    price_tick_size: float = 0.01,
    price_unit: str = "dollar",
) -> list[dict]:
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
            price_ticks = price_to_ticks(float(r["price"]), price_unit, price_tick_size)
            timestamp = int(round(float(r["time"]) * 1e9))
            out.append(
                {
                    "timestamp": timestamp,
                    "order_id": order_id,
                    "side": "B" if direction == 1 else "S",
                    "price_ticks": price_ticks,
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


def looks_headerless_lobster(first_line: str) -> bool:
    """True if first line looks like data (time starts with digit), not a header."""
    if not first_line or "time" in first_line.lower():
        return False
    return first_line[0].isdigit()


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as f:
        peek = f.readline()
        f.seek(0)
        if looks_headerless_lobster(peek.strip()):
            return list(csv.DictReader(f, fieldnames=LOBSTER_COLUMNS))
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["timestamp", "order_id", "side", "price_ticks", "size", "action"]
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--format", choices=["lobster", "engine"], default="lobster")
    parser.add_argument("--tick-size", type=float, default=0.01)
    parser.add_argument(
        "--price-unit",
        choices=["dollar", "lobster", "ticks"],
        default="dollar",
        help="dollar=float dollars (synthetic); lobster=int dollars*10000; ticks=already ticks",
    )
    parser.add_argument(
        "--max-rows",
        type=int,
        default=0,
        help="Optional cap on cleaned events (0 = all). Useful for a first real-data smoke test.",
    )
    args = parser.parse_args()

    raw = read_csv(args.input)
    if args.format == "lobster":
        clean = clean_lobster(
            raw, price_tick_size=args.tick_size, price_unit=args.price_unit
        )
    else:
        clean = clean_engine(raw)

    if args.max_rows > 0:
        clean = clean[: args.max_rows]

    write_csv(args.output, clean)
    print(f"wrote {args.output} ({len(clean)} events)")


if __name__ == "__main__":
    main()
