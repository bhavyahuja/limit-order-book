# Product Requirements Document

## Project Name

Limit Order Book (LOB) Engine & Microstructure Signal Study

## Resume one-liner

C++ limit-order-book with O(1) cancel (memory pool + hash map + FIFO price levels); replays tick data and runs a synthetic matcher; Python study of order-book imbalance vs short-horizon mid-price returns.

## 1. Objective

Build a **solid, interview-explainable** quantitative pipeline that:

1. Cleans raw tick / LOBSTER-style message data in Python.
2. Maintains book state in a **C++20** core designed with low-latency constraints (no alloc in the hot path, O(1) cancel, price-time priority).
3. Supports **two modes on the same core**:
   - **Replay / reconstruct** — apply historical add / cancel / execute messages to rebuild the book (research path).
   - **Match** — accept limit orders and match them against the resting book with price-time priority (engine path).
4. Exports top-of-book / depth snapshots and runs a **simple, honest** Order Book Imbalance (OBI) study in a Jupyter notebook.

**Tone for interviews:** This is a learning / portfolio project with HFT-*inspired* data structures and constraints — not a claim of production HFT or ultra-low-latency networking.

## 2. Who this is for

- Quant developer / low-latency engineering interviews (data structures, pools, complexity, correctness).
- Quant research interviews (LOB reconstruction, OBI, forward returns, no look-ahead).
- You are new to quant: every piece should be understandable; complexity stays in the C++ book core, not in a sprawling research stack.

## 3. Tech stack

| Layer | Choice | Why |
| --- | --- | --- |
| Ingestion | Python 3, Pandas, NumPy | Easy cleaning and CSV I/O |
| Core engine | C++20, CMake, header-friendly modules | Interview signal; control over memory |
| Tests | C++ unit tests (e.g. Catch2 or GoogleTest) | Correctness > cleverness |
| Bridge | CLI: C++ binary reads clean CSV, writes snapshot CSV | No pybind required for v1 |
| Analysis | Jupyter, NumPy, SciPy, Matplotlib | OBI + correlation / IC |

**Out of scope (do not build):** networking, FPGA, multi-asset, full backtester, ML models, GUI, live exchange connectivity.

## 4. Architecture

```text
raw CSV  →  Python cleaner  →  events.csv
                                    ↓
                         C++ LOB engine (replay | match)
                                    ↓
                            snapshots.csv
                                    ↓
                         Jupyter OBI notebook
```

### Shared C++ core (one book)

Both modes mutate the same structures:

- **`Order`** — `order_id`, `side`, `price` (integer ticks), `remaining_qty`, intrusive `prev` / `next` for FIFO within a level.
- **`PriceLevel`** — price, total volume, head/tail of the order DLL.
- **`Book` side maps** — bids and asks keyed by price (e.g. `std::map` with descending bids / ascending asks, or equivalent). Best bid/ask from `begin()` — **O(log n)** level insert/erase is acceptable; **never** `std::sort` the live book.
- **`std::unordered_map<uint64_t, Order*>`** — id → order for **O(1)** cancel / lookup.
- **Memory pool** — fixed array (or arena) of `Order` pre-allocated at startup; **no `new` / `delete` in the hot loop**.

### Mode A — Replay / reconstruct

Input events already happened on an exchange. The engine **does not re-decide matches**; it applies messages:

| action | meaning |
| --- | --- |
| `ADD` | Rest a new limit order at price/size |
| `CANCEL` | Remove or reduce by `order_id` |
| `EXECUTE` | Reduce resting size for a trade (full or partial) |

Use this mode for LOBSTER-style or cleaned historical feeds. Maintain invariants after each event (see §6).

### Mode B — Synthetic match

Input is a stream of **limit orders** (and optional cancels). The engine **matches**:

1. Incoming buy walks asks from best (lowest) ask upward while `ask_price <= limit_price`.
2. Incoming sell walks bids from best (highest) bid downward while `bid_price >= limit_price`.
3. At each level, fill FIFO (head of DLL first); partial fills allowed.
4. Unfilled remainder rests on the book as a new order (unless qty is zero).
5. Emit fill records (aggressor id, resting id, price, qty) for debugging / tests.

Same pool, maps, and cancel path as Mode A.

## 5. Phases (build order)

### Phase 0 — Repo skeleton

- CMake C++ project, `src/`, `include/`, `tests/`, `python/`, `notebooks/`, `data/` (gitignored samples).
- README with build + run commands (filled when code exists).
- Document integer **price ticks** (e.g. price in 0.01 USD → store `10000` for $100.00) — **no floating-point money in the engine**.

### Phase 1 — Ingestion (Python)

**Input:** LOBSTER sample message CSV and/or a small crypto tick CSV (document which; prefer one primary public sample).

**Process:**

- Map exchange action codes → `ADD` / `CANCEL` / `EXECUTE`.
- Keep only needed columns; drop obvious junk rows.
- Optional: filter to continuous session if timestamps allow (document the rule).
- Convert price to integer ticks; size to integer shares/lots.

**Output:** `events.csv`

```text
timestamp,order_id,side,price_ticks,size,action
```

- `side`: `B` or `S`
- `action`: `ADD` | `CANCEL` | `EXECUTE`
- `timestamp`: integer ns or µs (consistent; document unit)

Keep the cleaner dumb and readable — the C++ core must not parse messy strings.

### Phase 2 — C++ LOB core

**APIs (conceptual):**

- `bool add(OrderSpec)` — Mode B may match first; Mode A only rests.
- `bool cancel(order_id, qty_optional)` — full or partial; O(1) find via hash map.
- `bool execute(order_id, qty)` — Mode A only; reduce resting order.
- `BestBidAsk top() const`
- `DepthLevel depth(side, n_levels) const` — for multi-level OBI later
- `void snapshot_to(writer)` — write current top (and optional N levels)

**Telemetry:** Prefer **event-driven** snapshots (after each event, or every N events). Optional wall-clock / timestamp-bucket downsample (e.g. 100ms) only in a post-step or flag — do not make 100ms the only view of the book.

**Output:** `snapshots.csv`

```text
timestamp,best_bid,best_ask,bid_size_1,ask_size_1[, bid_size_2, ask_size_2, ...]
```

Include at least top-of-book; support **1–5 levels** via a CLI flag for the notebook.

### Phase 3 — Correctness tests & microbenchmark (required for resume)

**Invariant / unit tests:**

- Empty book → add → cancel restores empty.
- FIFO: two orders same price; aggressor fills the first resting order first.
- Partial fill leaves remainder at head correctly.
- Cancel middle of DLL leaves neighbors linked.
- Replay: book never crossed after Mode A events (`best_bid < best_ask` when both exist), except document any data quirks.
- Volume at a price level equals sum of order sizes at that level.

**Benchmark (simple):**

- Preload N orders, time M cancels by id → report throughput and p50/p99 if easy (or mean ns/op).
- One number in the README: e.g. “X million cancel ops/s on this machine” — honest, single-threaded.

### Phase 4 — OBI notebook (quant signal, keep small)

**Input:** `snapshots.csv` (+ mid from best bid/ask).

**Signal:**

\[
\mathrm{OBI}_t = \frac{V^{bid}_t - V^{ask}_t}{V^{bid}_t + V^{ask}_t}
\]

Use top-of-book first; optionally average or sum over N levels (same formula on aggregated volumes).

**Labels (no look-ahead):**

- Mid \( m_t = (best\_bid + best\_ask) / 2 \)
- Forward return \( r_{t\to t+h} = (m_{t+h} - m_t) / m_t \) for a few horizons \( h \) (e.g. 1s, 5s — or N events if clock is irregular). Align so features at \( t \) only use data ≤ \( t \).

**Outputs (enough for a resume bullet, not a paper):**

1. Time-series plot: mid vs OBI.
2. Scatter: OBI vs forward return for one primary horizon.
3. Pearson and Spearman correlation; mention sample size.
4. Simple train/test split in time (e.g. first 70% / last 30%) — report correlation on **test** only.
5. Short written conclusion: does OBI help here? limitations (costs, queue position, one symbol/day).

**Do not:** claim profitability, ignore costs, or tune on the full sample.

## 6. Technical constraints (“deep” rules)

1. **No `new` / `delete` in the hot path** — pool/arena allocated at startup.
2. **No `std::sort` on the live book** — ordered price map or tick-indexed structure only.
3. **Integer ticks for price; integer qty** — floats only in the notebook for returns/plots.
4. **Single-threaded engine** for v1 — state clearly; simpler and still interview-relevant.
5. **Price-time priority** in Mode B; FIFO DLL within each `PriceLevel`.
6. **Honest framing** — “HFT-inspired LOB,” not “production HFT system.”

## 7. Deliverables checklist

- [ ] `PRD.md` (this file)
- [ ] CMake C++ library + CLI (`replay` / `match` modes)
- [ ] Python cleaner → `events.csv`
- [ ] `snapshots.csv` writer
- [ ] Unit tests for FIFO, cancel, partial fill, uncrossed book
- [ ] Microbenchmark numbers in README
- [ ] Jupyter notebook: OBI vs forward mid returns + test-set correlations
- [ ] README: how to build, run sample data, interpret results, interview talking points

## 8. Suggested interview talking points

- Why hash map + intrusive DLL gives O(1) cancel without scanning the level.
- Why a memory pool avoids allocator latency jitter.
- Why `std::map` for price levels is O(log n) and when a tick array would be O(1).
- Difference between **reconstructing** a book from LOBSTER vs **matching** orders yourself.
- Why OBI can correlate with short-horizon returns yet still not be a tradeable strategy (costs, adverse selection, non-stationarity).

## 9. Success criteria

The project is done when you can:

1. Run cleaner → C++ replay on a sample day → notebook without manual file hacking.
2. Explain every major struct and the cancel path on a whiteboard.
3. Show a failing test if FIFO is broken, and a passing suite otherwise.
4. Quote one benchmark and one test-set OBI correlation with a cautious interpretation.

## 10. Non-goals (explicit)

- Multi-threaded matching, lock-free queues, kernel bypass.
- Market orders as a separate type beyond “limit that crosses” in Mode B (optional later).
- Full order-type blitz (iceberg, hidden, pegged).
- Portfolio / PnL backtest with fees (notebook may note costs qualitatively only).
