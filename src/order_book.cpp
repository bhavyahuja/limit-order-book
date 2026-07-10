#include "lob/order_book.hpp"

#include <algorithm>
#include <utility>

namespace lob {

OrderBook::OrderBook(std::size_t pool_capacity) : pool_(pool_capacity) {
    id_map_.reserve(std::min(pool_capacity, static_cast<std::size_t>(1'000'000)));
}

template <typename Levels>
PriceLevel* OrderBook::find_level(Levels& levels, int64_t price) {
    auto it = levels.find(price);
    return it == levels.end() ? nullptr : &it->second;
}

template <typename Levels>
const PriceLevel* OrderBook::find_level(const Levels& levels, int64_t price) {
    auto it = levels.find(price);
    return it == levels.end() ? nullptr : &it->second;
}

PriceLevel& OrderBook::get_or_create_level(Side side, int64_t price) {
    if (side == Side::Bid) {
        auto [it, inserted] = bids_.try_emplace(price);
        if (inserted) {
            it->second.price = price;
        }
        return it->second;
    }
    auto [it, inserted] = asks_.try_emplace(price);
    if (inserted) {
        it->second.price = price;
    }
    return it->second;
}

void OrderBook::erase_level_if_empty(Side side, int64_t price) {
    if (side == Side::Bid) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.head == nullptr) {
            bids_.erase(it);
        }
        return;
    }
    auto it = asks_.find(price);
    if (it != asks_.end() && it->second.head == nullptr) {
        asks_.erase(it);
    }
}

void OrderBook::link_tail(PriceLevel& level, Order* order) {
    order->prev = level.tail;
    order->next = nullptr;
    if (level.tail != nullptr) {
        level.tail->next = order;
    } else {
        level.head = order;
    }
    level.tail = order;
    level.total_qty += order->qty;
}

void OrderBook::unlink(PriceLevel& level, Order* order) {
    if (order->prev != nullptr) {
        order->prev->next = order->next;
    } else {
        level.head = order->next;
    }
    if (order->next != nullptr) {
        order->next->prev = order->prev;
    } else {
        level.tail = order->prev;
    }
    level.total_qty -= order->qty;
    order->prev = nullptr;
    order->next = nullptr;
}

void OrderBook::reduce_order(Order* order, int64_t qty) {
    if (qty <= 0 || order == nullptr) {
        return;
    }
    PriceLevel* level = (order->side == Side::Bid)
                            ? find_level(bids_, order->price)
                            : find_level(asks_, order->price);
    if (level == nullptr) {
        return;
    }

    if (qty >= order->qty) {
        remove_order(order);
        return;
    }

    order->qty -= qty;
    level->total_qty -= qty;
}

void OrderBook::remove_order(Order* order) {
    PriceLevel* level = (order->side == Side::Bid)
                            ? find_level(bids_, order->price)
                            : find_level(asks_, order->price);
    if (level != nullptr) {
        unlink(*level, order);
        erase_level_if_empty(order->side, order->price);
    }
    id_map_.erase(order->id);
    pool_.deallocate(order);
}

bool OrderBook::rest(const OrderSpec& spec) {
    if (spec.qty <= 0 || id_map_.count(spec.id) != 0) {
        return false;
    }
    Order* order = pool_.allocate();
    order->id = spec.id;
    order->side = spec.side;
    order->price = spec.price;
    order->qty = spec.qty;

    PriceLevel& level = get_or_create_level(spec.side, spec.price);
    link_tail(level, order);
    id_map_[spec.id] = order;
    return true;
}

bool OrderBook::cancel(uint64_t order_id, std::optional<int64_t> qty) {
    auto it = id_map_.find(order_id);
    if (it == id_map_.end()) {
        return false;
    }
    Order* order = it->second;
    if (!qty.has_value() || *qty >= order->qty) {
        remove_order(order);
        return true;
    }
    if (*qty <= 0) {
        return false;
    }
    reduce_order(order, *qty);
    return true;
}

bool OrderBook::execute(uint64_t order_id, int64_t qty) {
    if (qty <= 0) {
        return false;
    }
    auto it = id_map_.find(order_id);
    if (it == id_map_.end()) {
        return false;
    }
    reduce_order(it->second, qty);
    return true;
}

bool OrderBook::add_limit(const OrderSpec& spec, std::vector<Fill>* fills) {
    if (spec.qty <= 0 || id_map_.count(spec.id) != 0) {
        return false;
    }

    int64_t remaining = spec.qty;

    if (spec.side == Side::Bid) {
        // Buy walks asks from best (lowest) while ask <= limit.
        // Re-read begin() each iteration — reduce_order may erase the level.
        while (remaining > 0 && !asks_.empty()) {
            auto level_it = asks_.begin();
            if (level_it->second.price > spec.price) {
                break;
            }
            Order* resting = level_it->second.head;
            if (resting == nullptr) {
                asks_.erase(level_it);
                continue;
            }
            const int64_t traded = std::min(remaining, resting->qty);
            if (fills != nullptr) {
                fills->push_back(Fill{spec.id, resting->id, resting->price, traded});
            }
            remaining -= traded;
            reduce_order(resting, traded);
        }
    } else {
        // Sell walks bids from best (highest) while bid >= limit.
        while (remaining > 0 && !bids_.empty()) {
            auto level_it = bids_.begin();
            if (level_it->second.price < spec.price) {
                break;
            }
            Order* resting = level_it->second.head;
            if (resting == nullptr) {
                bids_.erase(level_it);
                continue;
            }
            const int64_t traded = std::min(remaining, resting->qty);
            if (fills != nullptr) {
                fills->push_back(Fill{spec.id, resting->id, resting->price, traded});
            }
            remaining -= traded;
            reduce_order(resting, traded);
        }
    }

    if (remaining > 0) {
        OrderSpec rest_spec = spec;
        rest_spec.qty = remaining;
        return rest(rest_spec);
    }
    return true;
}

BestBidAsk OrderBook::top() const {
    BestBidAsk tba;
    if (!bids_.empty()) {
        tba.has_bid = true;
        tba.bid_price = bids_.begin()->second.price;
        tba.bid_qty = bids_.begin()->second.total_qty;
    }
    if (!asks_.empty()) {
        tba.has_ask = true;
        tba.ask_price = asks_.begin()->second.price;
        tba.ask_qty = asks_.begin()->second.total_qty;
    }
    return tba;
}

void OrderBook::depth(std::size_t n, std::vector<DepthLevel>& bids_out,
                      std::vector<DepthLevel>& asks_out) const {
    bids_out.clear();
    asks_out.clear();
    bids_out.reserve(n);
    asks_out.reserve(n);

    std::size_t i = 0;
    for (const auto& [price, level] : bids_) {
        if (i++ >= n) {
            break;
        }
        bids_out.push_back(DepthLevel{price, level.total_qty});
    }
    i = 0;
    for (const auto& [price, level] : asks_) {
        if (i++ >= n) {
            break;
        }
        asks_out.push_back(DepthLevel{price, level.total_qty});
    }
}

bool OrderBook::crossed() const {
    const BestBidAsk tba = top();
    return tba.has_bid && tba.has_ask && tba.bid_price >= tba.ask_price;
}

int64_t OrderBook::level_volume(Side side, int64_t price) const {
    if (side == Side::Bid) {
        const PriceLevel* level = find_level(bids_, price);
        return level == nullptr ? 0 : level->total_qty;
    }
    const PriceLevel* level = find_level(asks_, price);
    return level == nullptr ? 0 : level->total_qty;
}

int64_t OrderBook::sum_order_qty_at(Side side, int64_t price) const {
    const PriceLevel* level = (side == Side::Bid) ? find_level(bids_, price)
                                                  : find_level(asks_, price);
    if (level == nullptr) {
        return 0;
    }
    int64_t sum = 0;
    for (Order* o = level->head; o != nullptr; o = o->next) {
        sum += o->qty;
    }
    return sum;
}

const Order* OrderBook::find(uint64_t order_id) const {
    auto it = id_map_.find(order_id);
    return it == id_map_.end() ? nullptr : it->second;
}

}  // namespace lob
