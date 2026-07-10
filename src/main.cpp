#include "lob/csv_io.hpp"
#include "lob/order_book.hpp"
#include "lob/types.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string mode = "replay";  // replay | match
    std::string events_path;
    std::string snapshots_path = "snapshots.csv";
    std::string fills_path;
    std::size_t depth = 5;
    std::size_t pool_size = 1'000'000;
    std::size_t every_n = 1;  // snapshot every N events
};

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " --mode replay|match --events <path> [options]\n"
        << "  --snapshots <path>   output snapshots CSV (default: snapshots.csv)\n"
        << "  --fills <path>       write fills in match mode (optional)\n"
        << "  --depth <1-5>        book levels in snapshot (default: 5)\n"
        << "  --pool-size <n>      order memory pool capacity (default: 1000000)\n"
        << "  --every-n <n>        snapshot every N events (default: 1)\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };
        if (a == "--mode") {
            opt.mode = need("--mode");
        } else if (a == "--events") {
            opt.events_path = need("--events");
        } else if (a == "--snapshots") {
            opt.snapshots_path = need("--snapshots");
        } else if (a == "--fills") {
            opt.fills_path = need("--fills");
        } else if (a == "--depth") {
            opt.depth = static_cast<std::size_t>(std::stoul(need("--depth")));
        } else if (a == "--pool-size") {
            opt.pool_size = static_cast<std::size_t>(std::stoull(need("--pool-size")));
        } else if (a == "--every-n") {
            opt.every_n = static_cast<std::size_t>(std::stoull(need("--every-n")));
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(a));
        }
    }
    if (opt.events_path.empty()) {
        throw std::runtime_error("--events is required");
    }
    if (opt.mode != "replay" && opt.mode != "match") {
        throw std::runtime_error("--mode must be replay or match");
    }
    if (opt.depth < 1 || opt.depth > 5) {
        throw std::runtime_error("--depth must be in 1..5");
    }
    if (opt.every_n < 1) {
        throw std::runtime_error("--every-n must be >= 1");
    }
    return opt;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse_args(argc, argv);
        const auto events = lob::load_events(opt.events_path);

        lob::OrderBook book(opt.pool_size);
        lob::SnapshotWriter snapshots(opt.snapshots_path, opt.depth);
        std::vector<lob::Fill> all_fills;

        const auto t0 = std::chrono::steady_clock::now();
        std::size_t i = 0;
        for (const lob::Event& e : events) {
            if (opt.mode == "replay") {
                switch (e.action) {
                    case lob::Action::Add:
                        // Rest, but match if the add would cross (keeps real LOBSTER
                        // feeds consistent when a marketable limit appears as type-1).
                        book.add_limit(lob::OrderSpec{e.order_id, e.side, e.price, e.size});
                        break;
                    case lob::Action::Cancel:
                        book.cancel(e.order_id, e.size > 0 ? std::optional<int64_t>{e.size}
                                                           : std::nullopt);
                        break;
                    case lob::Action::Execute:
                        book.execute(e.order_id, e.size);
                        break;
                }
            } else {
                // match mode: ADD = limit (may match), CANCEL = cancel
                if (e.action == lob::Action::Cancel) {
                    book.cancel(e.order_id, e.size > 0 ? std::optional<int64_t>{e.size}
                                                       : std::nullopt);
                } else if (e.action == lob::Action::Add) {
                    std::vector<lob::Fill> fills;
                    book.add_limit(lob::OrderSpec{e.order_id, e.side, e.price, e.size}, &fills);
                    if (!opt.fills_path.empty()) {
                        all_fills.insert(all_fills.end(), fills.begin(), fills.end());
                    }
                }
                // EXECUTE ignored in match mode (engine produces its own fills)
            }

            ++i;
            if (i % opt.every_n == 0) {
                snapshots.write(e.timestamp, book);
            }
        }

        if (!opt.fills_path.empty()) {
            lob::write_fills(opt.fills_path, all_fills);
        }

        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "mode=" << opt.mode << " events=" << events.size()
                  << " orders_resting=" << book.order_count() << " elapsed_ms=" << ms
                  << " snapshots=" << opt.snapshots_path << '\n';

        const lob::BestBidAsk tba = book.top();
        if (tba.has_bid && tba.has_ask) {
            std::cout << "top bid=" << tba.bid_price << " x " << tba.bid_qty
                      << " ask=" << tba.ask_price << " x " << tba.ask_qty << '\n';
            if (opt.mode == "replay" && book.crossed()) {
                std::cerr << "warning: book is crossed after replay (check input data)\n";
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        print_usage(argv[0]);
        return 1;
    }
}
