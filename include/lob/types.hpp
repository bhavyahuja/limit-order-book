#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lob {

enum class Side : uint8_t { Bid = 0, Ask = 1 };

enum class Action : uint8_t { Add = 0, Cancel = 1, Execute = 2 };

inline Side side_from_char(char c) {
    return (c == 'B' || c == 'b') ? Side::Bid : Side::Ask;
}

inline char side_to_char(Side s) {
    return s == Side::Bid ? 'B' : 'S';
}

inline std::string_view trim_ascii(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' ||
                          s.back() == '\t')) {
        s.remove_suffix(1);
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    return s;
}

inline Action action_from_string(std::string_view s) {
    s = trim_ascii(s);
    if (s == "ADD" || s == "Add" || s == "add") {
        return Action::Add;
    }
    if (s == "CANCEL" || s == "Cancel" || s == "cancel") {
        return Action::Cancel;
    }
    if (s == "EXECUTE" || s == "Execute" || s == "execute") {
        return Action::Execute;
    }
    throw std::runtime_error("Unknown action: " + std::string(s));
}

inline const char* action_to_string(Action a) {
    switch (a) {
        case Action::Add:
            return "ADD";
        case Action::Cancel:
            return "CANCEL";
        case Action::Execute:
            return "EXECUTE";
    }
    return "ADD";
}

struct Order {
    uint64_t id = 0;
    Side side = Side::Bid;
    int64_t price = 0;  // integer ticks (no floating-point money)
    int64_t qty = 0;
    Order* prev = nullptr;
    Order* next = nullptr;
    bool active = false;
};

struct PriceLevel {
    int64_t price = 0;
    int64_t total_qty = 0;
    Order* head = nullptr;
    Order* tail = nullptr;
};

struct OrderSpec {
    uint64_t id = 0;
    Side side = Side::Bid;
    int64_t price = 0;
    int64_t qty = 0;
};

struct BestBidAsk {
    bool has_bid = false;
    bool has_ask = false;
    int64_t bid_price = 0;
    int64_t ask_price = 0;
    int64_t bid_qty = 0;
    int64_t ask_qty = 0;
};

struct DepthLevel {
    int64_t price = 0;
    int64_t qty = 0;
};

struct Fill {
    uint64_t aggressor_id = 0;
    uint64_t resting_id = 0;
    int64_t price = 0;
    int64_t qty = 0;
};

struct Event {
    int64_t timestamp = 0;  // nanoseconds (or any consistent integer unit)
    uint64_t order_id = 0;
    Side side = Side::Bid;
    int64_t price = 0;
    int64_t size = 0;
    Action action = Action::Add;
};

}  // namespace lob
