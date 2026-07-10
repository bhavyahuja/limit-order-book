#!/usr/bin/env python3
"""Generate synthetic LOBSTER-like message data and match-mode event streams.

Stdlib only. Timestamps are nanoseconds. Prices are integer ticks (1 tick = $0.01).

Replay generator keeps an uncrossed book and lets the mid drift with imbalance
so the OBI notebook has a visible (synthetic) signal to study.
"""

from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path


def _best_bid_ask(live: dict[int, tuple[str, int, int]]) -> tuple[int | None, int | None]:
    best_bid = None
    best_ask = None
    for side, px, _qty in live.values():
        if side == "B":
            best_bid = px if best_bid is None else max(best_bid, px)
        else:
            best_ask = px if best_ask is None else min(best_ask, px)
    return best_bid, best_ask


def _touch_sizes(live: dict[int, tuple[str, int, int]], bid: int, ask: int) -> tuple[int, int]:
    vb = sum(q for s, p, q in live.values() if s == "B" and p == bid)
    va = sum(q for s, p, q in live.values() if s == "S" and p == ask)
    return vb, va


def generate_replay_messages(
    n_events: int = 20_000,
    seed: int = 42,
    start_mid_ticks: int = 10_000,
) -> list[dict]:
    """Build a long-enough session (~few minutes) with moving mid + OBI pressure."""
    rng = random.Random(seed)
    rows: list[dict] = []
    live: dict[int, tuple[str, int, int]] = {}
    next_id = 1
    t = 0
    # Touch always one tick wide around mid: bid=mid, ask=mid+1 after seeding.
    mid = start_mid_ticks

    def emit(oid: int, side: str, px: int, size: int, action: str) -> None:
        nonlocal t
        rows.append(
            {
                "timestamp": t,
                "order_id": oid,
                "side": side,
                "price_ticks": px,
                "size": size,
                "action": action,
            }
        )
        t += rng.randint(200_000, 2_000_000)  # 0.2ms–2ms → ~20k events ≈ 0.5–2+ minutes

    def add_order(side: str, px: int, qty: int) -> None:
        nonlocal next_id
        emit(next_id, side, px, qty, "ADD")
        live[next_id] = (side, px, qty)
        next_id += 1

    def cancel_order(oid: int, qty: int | None = None) -> None:
        if oid not in live:
            return
        side, px, cur = live[oid]
        take = cur if qty is None else min(qty, cur)
        emit(oid, side, px, take, "CANCEL")
        if take >= cur:
            del live[oid]
        else:
            live[oid] = (side, px, cur - take)

    def execute_order(oid: int, qty: int) -> None:
        if oid not in live:
            return
        side, px, cur = live[oid]
        take = min(qty, cur)
        emit(oid, side, px, take, "EXECUTE")
        if take >= cur:
            del live[oid]
        else:
            live[oid] = (side, px, cur - take)

    # Seed 3 levels each side
    for depth in range(1, 4):
        for _ in range(15):
            add_order("B", mid - depth, rng.randint(20, 120))
            add_order("S", mid + depth, rng.randint(20, 120))

    while len(rows) < n_events:
        best_bid, best_ask = _best_bid_ask(live)
        if best_bid is None or best_ask is None:
            add_order("B", mid - 1, 50)
            add_order("S", mid + 1, 50)
            continue

        vb, va = _touch_sizes(live, best_bid, best_ask)
        if vb + va == 0:
            add_order("B", best_bid, rng.randint(30, 100))
            add_order("S", best_ask, rng.randint(30, 100))
            continue

        obi = (vb - va) / (vb + va)
        roll = rng.random()

        if roll < 0.50:
            # Refill / add size behind or at touch (never cross)
            if rng.random() < 0.5:
                px = rng.randint(best_ask - 6, best_ask - 1)
                add_order("B", max(1, px), rng.randint(10, 80))
            else:
                px = rng.randint(best_bid + 1, best_bid + 6)
                add_order("S", px, rng.randint(10, 80))
        elif roll < 0.72:
            oid = rng.choice(list(live.keys()))
            cancel_order(oid, None if rng.random() < 0.6 else rng.randint(1, live[oid][2]))
        elif roll < 0.88:
            # Trade against resting size at touch
            candidates = [
                oid
                for oid, (s, p, _q) in live.items()
                if (s == "B" and p == best_bid) or (s == "S" and p == best_ask)
            ]
            if candidates:
                oid = rng.choice(candidates)
                execute_order(oid, rng.randint(1, live[oid][2]))
        else:
            # Micro price move driven by OBI (synthetic teaching signal)
            if obi > 0.05 and rng.random() < min(0.9, 0.2 + abs(obi)):
                # Buy pressure → mid up: lift asks at best_ask, then new ask above
                ask_ids = [oid for oid, (s, p, _q) in live.items() if s == "S" and p == best_ask]
                for oid in ask_ids:
                    execute_order(oid, live[oid][2])
                mid = best_ask
                add_order("S", mid + 1, rng.randint(40, 120))
                add_order("B", mid, rng.randint(20, 80))
            elif obi < -0.05 and rng.random() < min(0.9, 0.2 + abs(obi)):
                bid_ids = [oid for oid, (s, p, _q) in live.items() if s == "B" and p == best_bid]
                for oid in bid_ids:
                    execute_order(oid, live[oid][2])
                mid = best_bid
                add_order("B", mid - 1, rng.randint(40, 120))
                add_order("S", mid, rng.randint(20, 80))

        # Trim deep book so pool stays sane
        if len(live) > 4000:
            deep = [
                oid
                for oid, (s, p, _q) in live.items()
                if (s == "B" and p < best_bid - 8) or (s == "S" and p > best_ask + 8)
            ]
            for oid in deep[:50]:
                cancel_order(oid)

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
        t += rng.randint(500_000, 5_000_000)

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
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, lineterminator="\n")
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

    fields = ["timestamp", "order_id", "side", "price_ticks", "size", "action"]
    write_csv(args.out_dir / "events_replay.csv", replay, fields)
    write_csv(args.out_dir / "events_match.csv", match, fields)
    write_csv(
        args.out_dir / "sample_lobster_messages.csv",
        lobster,
        ["time", "type", "order_id", "size", "price", "direction"],
    )

    span_s = (replay[-1]["timestamp"] - replay[0]["timestamp"]) / 1e9 if replay else 0
    print(f"wrote replay ({len(replay)} rows, span≈{span_s:.1f}s)")
    print(f"wrote match ({len(match)} rows)")
    print(f"wrote lobster-style ({len(lobster)} rows)")


if __name__ == "__main__":
    main()
