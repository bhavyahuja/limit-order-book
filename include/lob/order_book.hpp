#pragma once

#include "lob/memory_pool.hpp"
#include "lob/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

// Single-threaded limit order book.
//
// Layout:
//   - bids_/asks_: ordered price levels (best bid/ask = begin())
//   - each PriceLevel: intrusive FIFO doubly linked list of Order
//   - id_map_: order_id -> Order* for O(1) cancel / lookup
//   - pool_: pre-allocated Order storage
//
// Complexity (typical):
//   rest at a price: O(log n_levels) map + O(1) list append
//   cancel by id: O(1) hash + O(1) unlink (+ O(log n) if level erased)
//   match: O(k) for k resting orders filled
class OrderBook {
public:
    explicit OrderBook(std::size_t pool_capacity = 1'000'000);

    // Rest without matching (used after a partial aggressive match).
    bool rest(const OrderSpec& spec);

    // Full or partial cancel by id. qty nullopt => remove entire resting order.
    bool cancel(uint64_t order_id, std::optional<int64_t> qty = std::nullopt);

    // Apply an exchange execute message against a resting order (replay mode).
    bool execute(uint64_t order_id, int64_t qty);

    // Match incoming limit with price-time priority, then rest any remainder.
    bool add_limit(const OrderSpec& spec, std::vector<Fill>* fills = nullptr);

    BestBidAsk top() const;

    void depth(std::size_t n, std::vector<DepthLevel>& bids_out,
               std::vector<DepthLevel>& asks_out) const;

    bool crossed() const;
    std::size_t order_count() const { return id_map_.size(); }
    std::size_t pool_in_use() const { return pool_.in_use(); }

    int64_t level_volume(Side side, int64_t price) const;
    int64_t sum_order_qty_at(Side side, int64_t price) const;

    const Order* find(uint64_t order_id) const;

private:
    using BidLevels = std::map<int64_t, PriceLevel, std::greater<int64_t>>;
    using AskLevels = std::map<int64_t, PriceLevel, std::less<int64_t>>;

    MemoryPool pool_;
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<uint64_t, Order*> id_map_;

    PriceLevel& get_or_create_level(Side side, int64_t price);
    void erase_level_if_empty(Side side, int64_t price);
    void link_tail(PriceLevel& level, Order* order);
    void unlink(PriceLevel& level, Order* order);
    void reduce_order(Order* order, int64_t qty);
    void remove_order(Order* order);

    template <typename Levels>
    static PriceLevel* find_level(Levels& levels, int64_t price);

    template <typename Levels>
    static const PriceLevel* find_level(const Levels& levels, int64_t price);
};

}  // namespace lob
