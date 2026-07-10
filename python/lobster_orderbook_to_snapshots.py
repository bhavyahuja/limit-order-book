#!/usr/bin/env python3
"""Convert a LOBSTER orderbook_*.csv (+ matching message times) into snapshots.csv.

LOBSTER orderbook rows align 1:1 with message rows. Each orderbook row is:
  ask_px_1, ask_sz_1, bid_px_1, bid_sz_1, ask_px_2, ask_sz_2, bid_px_2, bid_sz_2, ...

Prices are LOBSTER ints (dollars * 10000). This writer emits the same schema as
lob_engine snapshots so the OBI notebook can use official book state.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def lobster_px_to_ticks(px: int, tick_size: float = 0.01) -> int:
    dollars = px / 10000.0
    return int(round(dollars / tick_size))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orderbook", type=Path, required=True)
    parser.add_argument(
        "--messages",
        type=Path,
        required=True,
        help="Matching message CSV (for timestamps); headerless LOBSTER format",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--levels", type=int, default=5, help="Levels to write (1-5)")
    parser.add_argument("--every-n", type=int, default=100)
    parser.add_argument("--tick-size", type=float, default=0.01)
    args = parser.parse_args()
    levels = max(1, min(5, args.levels))

    with args.messages.open(newline="", encoding="utf-8") as mf:
        msg_times = [float(row[0]) for row in csv.reader(mf) if row]

    header = ["timestamp", "best_bid", "best_ask"]
    for i in range(1, levels + 1):
        header += [f"bid_px_{i}", f"bid_sz_{i}", f"ask_px_{i}", f"ask_sz_{i}"]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    n_out = 0
    with args.orderbook.open(newline="", encoding="utf-8") as obf, args.output.open(
        "w", newline="", encoding="utf-8"
    ) as out:
        writer = csv.writer(out, lineterminator="\n")
        writer.writerow(header)
        for idx, row in enumerate(csv.reader(obf)):
            if not row:
                continue
            if (idx + 1) % args.every_n != 0:
                continue
            if idx >= len(msg_times):
                break
            ts = int(round(msg_times[idx] * 1e9))
            # row layout: ask1, asz1, bid1, bsz1, ask2, asz2, bid2, bsz2, ...
            vals = [int(float(x)) for x in row]
            bid_levels = []
            ask_levels = []
            for lv in range(levels):
                base = lv * 4
                if base + 3 >= len(vals):
                    break
                ask_px, ask_sz, bid_px, bid_sz = vals[base : base + 4]
                ask_levels.append((lobster_px_to_ticks(ask_px, args.tick_size), ask_sz))
                bid_levels.append((lobster_px_to_ticks(bid_px, args.tick_size), bid_sz))

            best_bid = bid_levels[0][0] if bid_levels and bid_levels[0][1] > 0 else ""
            best_ask = ask_levels[0][0] if ask_levels and ask_levels[0][1] > 0 else ""
            line = [ts, best_bid, best_ask]
            for i in range(levels):
                if i < len(bid_levels) and bid_levels[i][1] > 0:
                    line += [bid_levels[i][0], bid_levels[i][1]]
                else:
                    line += ["", ""]
                if i < len(ask_levels) and ask_levels[i][1] > 0:
                    line += [ask_levels[i][0], ask_levels[i][1]]
                else:
                    line += ["", ""]
            writer.writerow(line)
            n_out += 1

    print(f"wrote {args.output} ({n_out} snapshots from {len(msg_times)} messages)")


if __name__ == "__main__":
    main()
