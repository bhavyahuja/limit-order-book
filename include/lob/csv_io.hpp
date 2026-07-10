#pragma once

#include "lob/order_book.hpp"
#include "lob/types.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lob {

inline void strip_cr(std::string& s) {
    if (!s.empty() && s.back() == '\r') {
        s.pop_back();
    }
}

inline std::vector<std::string> split_csv_line(std::string line) {
    strip_cr(line);
    std::vector<std::string> fields;
    std::string field;
    std::istringstream ss(line);
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

inline Event parse_event_line(const std::string& line, int line_no) {
    const auto fields = split_csv_line(line);
    if (fields.size() < 6) {
        throw std::runtime_error("Bad event CSV at line " + std::to_string(line_no) +
                                 ": expected 6 columns");
    }
    Event e;
    e.timestamp = std::stoll(fields[0]);
    e.order_id = std::stoull(fields[1]);
    if (fields[2].empty()) {
        throw std::runtime_error("Empty side at line " + std::to_string(line_no));
    }
    e.side = side_from_char(fields[2][0]);
    e.price = std::stoll(fields[3]);
    e.size = std::stoll(fields[4]);
    e.action = action_from_string(fields[5]);
    return e;
}

inline std::vector<Event> load_events(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open events file: " + path);
    }
    std::vector<Event> events;
    std::string line;
    int line_no = 0;
    bool header_checked = false;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }
        if (!header_checked) {
            header_checked = true;
            if (line.find("timestamp") != std::string::npos) {
                continue;
            }
        }
        events.push_back(parse_event_line(line, line_no));
    }
    return events;
}

class SnapshotWriter {
public:
    SnapshotWriter(const std::string& path, std::size_t depth_levels)
        : depth_levels_(depth_levels), out_(path) {
        if (!out_) {
            throw std::runtime_error("Cannot open snapshots file: " + path);
        }
        out_ << "timestamp,best_bid,best_ask";
        for (std::size_t i = 1; i <= depth_levels_; ++i) {
            out_ << ",bid_px_" << i << ",bid_sz_" << i << ",ask_px_" << i << ",ask_sz_" << i;
        }
        out_ << '\n';
    }

    void write(int64_t timestamp, const OrderBook& book) {
        const BestBidAsk tba = book.top();
        out_ << timestamp << ',';
        if (tba.has_bid) {
            out_ << tba.bid_price;
        }
        out_ << ',';
        if (tba.has_ask) {
            out_ << tba.ask_price;
        }

        std::vector<DepthLevel> bids;
        std::vector<DepthLevel> asks;
        book.depth(depth_levels_, bids, asks);

        for (std::size_t i = 0; i < depth_levels_; ++i) {
            out_ << ',';
            if (i < bids.size()) {
                out_ << bids[i].price << ',' << bids[i].qty;
            } else {
                out_ << ',';
            }
            out_ << ',';
            if (i < asks.size()) {
                out_ << asks[i].price << ',' << asks[i].qty;
            } else {
                out_ << ',';
            }
        }
        out_ << '\n';
    }

private:
    std::size_t depth_levels_;
    std::ofstream out_;
};

inline void write_fills(const std::string& path, const std::vector<Fill>& fills) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot open fills file: " + path);
    }
    out << "aggressor_id,resting_id,price,qty\n";
    for (const Fill& f : fills) {
        out << f.aggressor_id << ',' << f.resting_id << ',' << f.price << ',' << f.qty << '\n';
    }
}

}  // namespace lob
