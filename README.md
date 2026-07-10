# Limit Order Book Engine & OBI Study

C++20 limit-order-book with **O(1) cancel** (memory pool + hash map + FIFO price levels). Same core supports:

- **replay** — reconstruct the book from ADD/CANCEL/EXECUTE messages (LOBSTER-style)
- **match** — price-time priority matching for incoming limits

Python cleans messages; a short Jupyter notebook studies **order book imbalance (OBI)** vs short-horizon mid returns.

> HFT-*inspired* data structures and constraints — not a production HFT system.

## Resume one-liner

C++ limit-order-book with O(1) cancel (memory pool + hash map + FIFO price levels); replays tick data and runs a synthetic matcher; Python study of OBI vs short-horizon mid-price returns.

## Layout

```text
include/lob/     types, memory pool, order book, CSV I/O
src/             order_book.cpp, lob_engine CLI
tests/           Catch2 correctness + cancel microbenchmark
python/          sample data generator + message cleaner
notebooks/       OBI study
data/            generated CSVs (gitignored)
```

## Build

**Makefile** (no CMake install required):

```bash
make -j
make test
```

**CMake** (optional):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Binaries: `build/lob_engine`, `build/lob_tests`.

## Quick start (full pipeline)

```bash
# 1) Synthetic sample data (stdlib only — no pip required for generate/clean)
python3 python/generate_sample_data.py
python3 python/clean_messages.py \
  --input data/sample_lobster_messages.csv \
  --output data/events.csv \
  --format lobster

# Notebook deps (optional): pip install -r python/requirements.txt

# 3) Replay through the C++ engine
./build/lob_engine --mode replay \
  --events data/events.csv \
  --snapshots data/snapshots_replay.csv \
  --depth 5 \
  --every-n 10

# 4) Optional: matching mode
./build/lob_engine --mode match \
  --events data/events_match.csv \
  --snapshots data/snapshots_match.csv \
  --fills data/fills.csv \
  --depth 5

# 5) Notebook
jupyter notebook notebooks/obi_study.ipynb
```

### Price ticks

The engine stores **integer ticks only** (default: 1 tick = $0.01). Example: `$100.00 → 10000`.

### Event CSV schema

```text
timestamp,order_id,side,price_ticks,size,action
```

- `side`: `B` or `S`
- `action`: `ADD` | `CANCEL` | `EXECUTE`
- `timestamp`: integer nanoseconds (consistent unit)

## Tests & benchmark

```bash
./build/lob_tests
```

Covers FIFO fills, partial fills, mid-list cancel, level volume invariants, replay execute, multi-level match, and a cancel microbenchmark (`[bench]` tag).

Example on this machine (Release, single-threaded): **~29 M cancel ops/s** (~34 ns/op). Re-run `./build/lob_tests "[bench]"` and quote your number.

## Interview talking points

- Hash map `order_id → Order*` + intrusive DLL → cancel without scanning the price level
- Memory pool avoids allocator jitter in the hot path
- `std::map` for price levels is O(log n); a dense tick array can be O(1) top-of-book
- **Replay** applies exchange messages; **match** decides fills with price-time priority
- OBI may correlate with short-horizon returns and still not be tradeable (costs, adverse selection)

## License / data

Synthetic samples are generated locally. For real LOBSTER data, follow [LOBSTER](https://lobsterdata.com/) terms and point `clean_messages.py --format lobster` at your message file.
