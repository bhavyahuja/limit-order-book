#!/usr/bin/env python3
"""Generate synthetic LOBSTER-like message data and match-mode event streams.

Uses only the Python standard library so the pipeline runs without pip/pandas.
Timestamps are nanoseconds from an arbitrary session start.
Prices are integer ticks (1 tick = $0.01).
"""

from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path


def generate_replay_messages(
    n_events: int = 20_000,
    seed: int = 42,
    start_mid_ticks: int = 10_000,
) -> list[dict]:
    rng = random.Random(seed)
    rows: list[dict] = []
    live: dict[int, tuple[str, int, int]] = {}
    next_id = 1
    t = 0
    mid = start_mid_ticks

    for side, offset in [("B", -1), ("B", -2), ("B", -3), ("S", 1), ("S", 2), ("S", 3)]:
        for _ in range(20):
            px = mid + offset
            qty = rng.randint(10, 99)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": next_id,
                    "side": side,
                    "price_ticks": px,
                    "size": qty,
                    "action": "ADD",
                }
            )
            live[next_id] = (side, px, qty)
            next_id += 1
            t += rng.randint(1_000, 50_000)

    while len(rows) < n_events:
        mid += rng.choice([-1, 0, 0, 0, 1])
        # Bias toward ADD so the book stays populated for snapshots / OBI.
        action_roll = rng.random()
        # Near the end, only ADD (keep resting liquidity for analysis).
        near_end = len(rows) > int(n_events * 0.9)

        if near_end or action_roll < 0.62 or not live:
            side = "B" if rng.random() < 0.5 else "S"
            px = mid - rng.randint(1, 5) if side == "B" else mid + rng.randint(1, 5)
            qty = rng.randint(5, 79)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": next_id,
                    "side": side,
                    "price_ticks": px,
                    "size": qty,
                    "action": "ADD",
                }
            )
            live[next_id] = (side, px, qty)
            next_id += 1
        elif action_roll < 0.85:
            oid = rng.choice(list(live.keys()))
            side, px, qty = live[oid]
            cancel_qty = qty if rng.random() < 0.7 else rng.randint(1, qty)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": oid,
                    "side": side,
                    "price_ticks": px,
                    "size": cancel_qty,
                    "action": "CANCEL",
                }
            )
            if cancel_qty >= qty:
                del live[oid]
            else:
                live[oid] = (side, px, qty - cancel_qty)
        else:
            oid = rng.choice(list(live.keys()))
            side, px, qty = live[oid]
            exec_qty = qty if rng.random() < 0.5 else rng.randint(1, qty)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": oid,
                    "side": side,
                    "price_ticks": px,
                    "size": exec_qty,
                    "action": "EXECUTE",
                }
            )
            if exec_qty >= qty:
                del live[oid]
            else:
                live[oid] = (side, px, qty - exec_qty)

        bid_near = sum(q for s, p, q in live.values() if s == "B" and p >= mid - 1)
        ask_near = sum(q for s, p, q in live.values() if s == "S" and p <= mid + 1)
        if bid_near + ask_near > 0:
            imb = (bid_near - ask_near) / (bid_near + ask_near)
            if rng.random() < min(0.15, abs(imb) * 0.2):
                mid += 1 if imb > 0 else -1

        t += rng.randint(10_000, 200_000)

    return rows[:n_events]


def generate_match_messages(
    n_events: int = 5_000,
    seed: int = 7,
    start_mid_ticks: int = 10_000,
) -> list[dict]:
    rng = random.Random(seed)
    rows: list[dict] = []
    live: dict[int, tuple[str, int, int]] = {}
    next_id = 1
    t = 0
    mid = start_mid_ticks

    for _ in range(n_events):
        if live and rng.random() < 0.15:
            oid = rng.choice(list(live.keys()))
            side, px, qty = live.pop(oid)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": oid,
                    "side": side,
                    "price_ticks": px,
                    "size": qty,
                    "action": "CANCEL",
                }
            )
        else:
            side = "B" if rng.random() < 0.5 else "S"
            if rng.random() < 0.7:
                px = mid - rng.randint(1, 4) if side == "B" else mid + rng.randint(1, 4)
            else:
                px = mid + rng.randint(0, 2) if side == "B" else mid - rng.randint(0, 2)
            qty = rng.randint(1, 39)
            rows.append(
                {
                    "timestamp": t,
                    "order_id": next_id,
                    "side": side,
                    "price_ticks": px,
                    "size": qty,
                    "action": "ADD",
                }
            )
            live[next_id] = (side, px, qty)
            next_id += 1
            mid += rng.choice([-1, 0, 0, 1])
        t += rng.randint(50_000, 500_000)

    return rows


def to_lobster_raw(rows: list[dict]) -> list[dict]:
    out = []
    for r in rows:
        direction = 1 if r["side"] == "B" else -1
        price = r["price_ticks"] / 100.0
        if r["action"] == "ADD":
            typ = 1
        elif r["action"] == "CANCEL":
            typ = 3
        else:
            typ = 4
        out.append(
            {
                "time": r["timestamp"] / 1e9,
                "type": typ,
                "order_id": r["order_id"],
                "size": r["size"],
                "price": price,
                "direction": direction,
            }
        )
    return out


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "data",
    )
    parser.add_argument("--n-replay", type=int, default=20_000)
    parser.add_argument("--n-match", type=int, default=5_000)
    args = parser.parse_args()

    replay = generate_replay_messages(n_events=args.n_replay)
    match = generate_match_messages(n_events=args.n_match)
    lobster = to_lobster_raw(replay)

    replay_path = args.out_dir / "events_replay.csv"
    match_path = args.out_dir / "events_match.csv"
    lobster_path = args.out_dir / "sample_lobster_messages.csv"

    fields = ["timestamp", "order_id", "side", "price_ticks", "size", "action"]
    write_csv(replay_path, replay, fields)
    write_csv(match_path, match, fields)
    write_csv(
        lobster_path,
        lobster,
        ["time", "type", "order_id", "size", "price", "direction"],
    )

    print(f"wrote {replay_path} ({len(replay)} rows)")
    print(f"wrote {match_path} ({len(match)} rows)")
    print(f"wrote {lobster_path} ({len(lobster)} rows)")


if __name__ == "__main__":
    main()
