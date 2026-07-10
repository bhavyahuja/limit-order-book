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
// - Bids: std::map with greater<> so begin() is best (highest) bid
// - Asks: std::map with less<> so begin() is best (lowest) ask
// - Cancel is O(1) via unordered_map<id, Order*>
// - Within a price: intrusive doubly-linked list (FIFO / price-time priority)
class OrderBook {
public:
    explicit OrderBook(std::size_t pool_capacity = 1'000'000);

    // Mode A (replay): rest without matching
    bool rest(const OrderSpec& spec);

    // Mode A: reduce or remove resting order by id
    bool cancel(uint64_t order_id, std::optional<int64_t> qty = std::nullopt);

    // Mode A: apply an exchange execute against a resting order
    bool execute(uint64_t order_id, int64_t qty);

    // Mode B (match): match incoming limit, rest any remainder
    // Fills (if non-null) are appended for the aggressor.
    bool add_limit(const OrderSpec& spec, std::vector<Fill>* fills = nullptr);

    BestBidAsk top() const;

    // Up to n levels from best on each side (empty slots omitted).
    void depth(std::size_t n, std::vector<DepthLevel>& bids_out,
               std::vector<DepthLevel>& asks_out) const;

    bool crossed() const;
    std::size_t order_count() const { return id_map_.size(); }
    std::size_t pool_in_use() const { return pool_.in_use(); }

    // Level volume must equal sum of order qtys (for tests).
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
