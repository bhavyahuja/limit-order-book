# Limit Order Book Engine & OBI Study

A small end-to-end project that:

1. Keeps a **limit order book** in fast C++ (add / cancel / trade),
2. Replays **real LOBSTER** stock messages (and can match synthetic orders too),
3. Checks a simple idea: **when buyers stack more size at the best price than sellers, does the mid price tend to tick up shortly after?**

That idea is called **Order Book Imbalance (OBI)**. This is a learning / portfolio project with HFT-*inspired* data structures — not a production trading system and not a claim of free money.

## Resume one-liner

C++ limit-order-book with O(1) cancel (memory pool + hash map + FIFO price levels); replays LOBSTER tick data; studies top-of-book imbalance vs short-horizon mid returns on real AAPL sample data.

## What is a limit order book? (30 seconds)

People send orders like “buy 100 shares at $585.00” or “sell at $585.10”. The exchange lines them up by price:

- **Bids** = buyers (highest buy price is “best bid”)
- **Asks** = sellers (lowest sell price is “best ask”)
- **Mid** = average of best bid and best ask

Your C++ code is a mini version of that scoreboard. Canceling an order in the middle of a long queue is done in **O(1)** time using a hash map + linked list, with orders taken from a **memory pool** (no `new`/`delete` in the hot path).

Two modes, same book:

| Mode | Meaning |
|------|---------|
| **replay** | Rebuild the book from historical ADD / CANCEL / EXECUTE messages (research path) |
| **match** | You decide trades when a buy crosses a sell, FIFO at each price (engine path) |

## Project layout

```text
include/lob/   order book, memory pool, types, CSV helpers
src/           engine library + lob_engine CLI
tests/         correctness tests + cancel microbenchmark
python/        fetch real LOBSTER, clean messages, sample generator
notebooks/     OBI study
data/          CSVs (gitignored); data/real/ for downloads
```

## Build

```bash
make -j
make test
```

Binaries: `build/lob_engine`, `build/lob_tests`.

Optional CMake: `cmake -S . -B build && cmake --build build -j`.

## Quick start — real AAPL data (recommended)

Uses the public LOBSTER sample day on Hugging Face
([`totalorganfailure/lobster-data`](https://huggingface.co/datasets/totalorganfailure/lobster-data)),
AAPL 2012-06-21. Do **not** rely on the HF `first-rows` API (LOBSTER files have no header). Download raw files:

```bash
# 1) Download message + official orderbook (~110 MB total)
python3 python/fetch_hf_lobster.py --symbol AAPL --levels 10 --kind both

# 2) Clean messages for the C++ engine
python3 python/clean_messages.py \
  --input data/real/AAPL_message_10.csv \
  --output data/events_real.csv \
  --format lobster --price-unit lobster

# 3) Replay through C++ (exercises the engine)
./build/lob_engine --mode replay \
  --events data/events_real.csv \
  --snapshots data/snapshots_real_engine.csv \
  --depth 5 --every-n 100 --pool-size 2000000

# 4) Official LOBSTER book → snapshots for the notebook (best for OBI)
python3 python/lobster_orderbook_to_snapshots.py \
  --orderbook data/real/AAPL_orderbook_10.csv \
  --messages data/real/AAPL_message_10.csv \
  --output data/snapshots_real.csv --levels 5 --every-n 100

# 5) Open the study (SNAP_PATH should be data/snapshots_real.csv)
jupyter notebook notebooks/obi_study.ipynb
```

LOBSTER stores price as an integer = dollars × 10000 (e.g. `5853300` → $585.33). That is why cleaning uses `--price-unit lobster`.

## Quick start — synthetic data (no download)

```bash
python3 python/generate_sample_data.py
python3 python/clean_messages.py \
  --input data/sample_lobster_messages.csv \
  --output data/events.csv --format lobster
./build/lob_engine --mode replay \
  --events data/events.csv \
  --snapshots data/snapshots_replay.csv --depth 5 --every-n 10
```

Synthetic data plants a strong OBI→price link on purpose so you can debug the pipeline. Prefer **real** snapshots when you talk about research results.

## What the notebook measures

At each snapshot:

\[
\mathrm{OBI} = \frac{V_{\mathrm{bid}} - V_{\mathrm{ask}}}{V_{\mathrm{bid}} + V_{\mathrm{ask}}}
\]

- Near **+1** → much more size on the best bid (buy-heavy)
- Near **−1** → sell-heavy
- Then we look at the **mid price a little later** (100 ms, 1 s, 5 s) and ask: do high OBI and later upticks move together?

We only quote correlations on the **last 30% of the day** (time-based test set), so we are not scoring on the same period we “peeked” at.

## Results (real AAPL, 2012-06-21)

From official LOBSTER orderbook snapshots (`data/snapshots_real.csv`):

| | |
|--|--|
| Snapshots used | ~4,000 (every 100th message) |
| Session length | ~6.5 hours |
| Mid range | about $577.5 – $588.2 (stored as integer ticks) |

**Test-set correlation of OBI vs forward mid return:**

| Horizon | Pearson r (test) | Plain English |
|---------|------------------|---------------|
| 100 ms | ~**0.06** | Tiny positive link |
| 1 s | ~**0.05** | Still tiny, same direction |
| 5 s | ~**0.01** | Basically gone |

### Are these “good” results?

**Yes — for what this project is trying to show.**

- A huge correlation (like 0.7) on **real** data would be suspicious. Microstructure signals are usually **weak**.
- Seeing a **small positive** r at 100 ms / 1 s that **fades by 5 s** is a classic pattern: imbalance may carry a little short-horizon information, then it dies out.
- This is **not** a trading strategy. We ignored fees, the bid–ask spread you’d pay to trade, queue position, and latency. Correlation ≠ profit.

On **synthetic** data you may see r ≈ 0.7 — that only proves the pipeline works; that link was baked into the generator.

## Engine checks

```bash
./build/lob_tests
```

Covers FIFO fills, partial fills, cancel in the middle of a queue, level volume, multi-level match, and a cancel microbenchmark.

Example (Release, single-threaded, this machine): about **28–30 million cancel ops/s**. Re-run `./build/lob_tests "[bench]"` and quote *your* number.

On the real AAPL message replay, the engine finishes hundreds of thousands of events in tens of milliseconds and keeps an uncrossed top of book when marketable ADDs are matched locally.

## Interview talking points

- Why hash map + intrusive linked list → **O(1) cancel** without scanning the price level
- Why a **memory pool** avoids allocator jitter
- `std::map` for price levels is O(log n); a dense tick array could make top-of-book O(1)
- **Replay** vs **match**: rebuild from exchange messages vs decide fills yourself
- On real AAPL, OBI↔return correlation is **small and short-lived** — interesting for research honesty, not a strategy pitch

## Event CSV schema (engine input)

```text
timestamp,order_id,side,price_ticks,size,action
```

- `side`: `B` or `S`
- `action`: `ADD` | `CANCEL` | `EXECUTE`
- `timestamp`: integer nanoseconds
- `price_ticks`: integer (no floating-point money in C++)

## License / data

Synthetic files are generated locally. Real sample data comes from the Hugging Face LOBSTER mirror above — follow LOBSTER / dataset terms. Do not commit large CSVs under `data/` (gitignored).
