#pragma once

#include "lob/types.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace lob {

// Fixed-capacity arena for Order nodes.
// allocate/deallocate recycle free_list_ slots — never ::new/::delete on the hot path.
class MemoryPool {
public:
    explicit MemoryPool(std::size_t capacity) : slots_(capacity) {
        free_list_.reserve(capacity);
        for (std::size_t i = 0; i < capacity; ++i) {
            free_list_.push_back(&slots_[capacity - 1 - i]);
        }
    }

    Order* allocate() {
        if (free_list_.empty()) {
            throw std::runtime_error("MemoryPool exhausted: increase --pool-size");
        }
        Order* o = free_list_.back();
        free_list_.pop_back();
        *o = Order{};
        o->active = true;
        return o;
    }

    void deallocate(Order* o) {
        if (o == nullptr) {
            return;
        }
        o->active = false;
        o->prev = nullptr;
        o->next = nullptr;
        free_list_.push_back(o);
    }

    std::size_t capacity() const { return slots_.size(); }
    std::size_t available() const { return free_list_.size(); }
    std::size_t in_use() const { return slots_.size() - free_list_.size(); }

private:
    std::vector<Order> slots_;
    std::vector<Order*> free_list_;
};

}  // namespace lob
