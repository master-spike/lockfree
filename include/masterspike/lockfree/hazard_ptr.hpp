#pragma once

#include <set>
#include <atomic>
#include <functional>
#include <utility>

namespace masterspike::lockfree {

namespace detail {

struct alignas(64) hazard_linked_list_node {
    std::atomic<const void*> m_hptr;
    std::atomic<hazard_linked_list_node*> m_next;
    hazard_linked_list_node(const void* in_hptr) : m_hptr(in_hptr), m_next(nullptr)
    {};
};

inline std::atomic<hazard_linked_list_node*> g_hll_front = nullptr;

class hazard_linked_list {
public:

    static bool is_protected(const void* ptr) {
        hazard_linked_list_node* curr_node = g_hll_front.load(std::memory_order_relaxed);
        while (curr_node != nullptr) {
            if (curr_node->m_hptr.load(std::memory_order_relaxed) == ptr) return true;
            curr_node = curr_node->m_next.load(std::memory_order_relaxed);
        }
        return false;
    }

    static std::set<const void*> protected_ptrs() {
        hazard_linked_list_node* curr_node = g_hll_front.load(std::memory_order_seq_cst);
        std::set<const void*> ptrs;
        while (curr_node != nullptr) {
            auto hptr = curr_node->m_hptr.load(std::memory_order_relaxed);
            if (hptr) ptrs.emplace(hptr);
            curr_node = curr_node->m_next.load(std::memory_order_relaxed);
        }
        return ptrs;
    }

    static hazard_linked_list_node* protect(const void* ptr) {
        static thread_local hazard_linked_list_node* last_ptr = nullptr;
        if (last_ptr == nullptr || last_ptr->m_hptr.load(std::memory_order_relaxed) != nullptr) {
            auto new_node = new hazard_linked_list_node(ptr);
            last_ptr = new_node;
            bool succeeded = false;
            while(!succeeded) {
                auto front = g_hll_front.load(std::memory_order_relaxed);
                new_node->m_next.store(front, std::memory_order_relaxed);
                succeeded = g_hll_front.compare_exchange_weak(front, new_node, std::memory_order_seq_cst);
            }
            return new_node;
        }
        else {
            last_ptr->m_hptr.store(ptr, std::memory_order_seq_cst);
            return last_ptr;
        }
    }
    static void release(hazard_linked_list_node* node, const void* ptr) {
        node->m_hptr.compare_exchange_strong(ptr, nullptr, std::memory_order_seq_cst);
    }
};


class retired_block {
private:
    static constexpr std::size_t kReclaimSize = 1000;
    std::array<std::pair<void*, std::function<void(void*)>>, 1024> m_retired = {std::pair{nullptr, nullptr}};
    std::size_t m_size = 0;
    void reclaim_all() {
        while(m_size > 0) {
            reclaim_some();
        }
    }
    void reclaim_some() {
        std::set<const void*> hazardous = hazard_linked_list::protected_ptrs();
        auto retired_end = m_retired.begin() + m_size;
        std::sort(m_retired.begin(), retired_end, [](auto const& left, auto const& right) {
            return left.first < right.first;
        });
        auto jt = m_retired.begin();
        retired_end = m_retired.begin() + m_size;

        std::size_t num_removed = 0;
        for (auto it = hazardous.begin(); jt < retired_end && it != hazardous.end(); ++it) {
            while (jt < retired_end && jt->first < *it) {
                jt->second(jt->first);
                jt->first = nullptr;
                jt->second = nullptr;
                ++jt; ++num_removed;
            }
            while (jt < retired_end && jt->first == *it) {

                ++jt;
            }
        }
        while(jt < retired_end) {
            jt->second(jt->first);
            jt->first = nullptr;
            jt->second = nullptr;
            ++jt; ++num_removed;
        }

        std::remove_if(m_retired.begin(), retired_end, [](auto const& p) {
            return p.first == nullptr;
        });
        m_size -= num_removed;
    }
public:
    retired_block() = default;
    retired_block(retired_block&&) = delete;
    retired_block(retired_block const&) = delete;
    retired_block& operator=(retired_block&&) = delete;
    retired_block& operator=(retired_block const&) = delete;

    void retire(void* ptr, std::function<void(void*)> deleter) {
        m_retired[m_size] = std::pair(ptr, deleter);
        ++m_size;
        while (m_size >= kReclaimSize) {
            reclaim_some();
        }
    }

    ~retired_block() {
        reclaim_all();
    }

    

};

/*
* Each thread has it's own `retired_block` instance.
*/

thread_local inline retired_block tl_retired;

}

template<typename T>
void retire_pointer(T* ptr, std::function<void(T*)> deleter) {
    retire_pointer<void>(reinterpret_cast<void*>(ptr), [deleter](void* p) {
        deleter(reinterpret_cast<T*>(p));
    });
}

template<>
void retire_pointer<void>(void* ptr, std::function<void(void*)> deleter) {
    if (!ptr) return;
    detail::tl_retired.retire(ptr, deleter);
}


class hazard_ptr {
public:
    hazard_ptr(const void* ptr) : m_ptr(ptr){
        m_hazard_node = detail::hazard_linked_list::protect(ptr);
    }

    hazard_ptr(const hazard_ptr&) = delete;
    hazard_ptr& operator=(const hazard_ptr&) = delete;
    
    hazard_ptr(hazard_ptr&& hp) : m_ptr(nullptr) {
        std::swap(m_ptr, hp.m_ptr);
        std::swap(m_hazard_node, hp.m_hazard_node);
    }
    hazard_ptr& operator=(hazard_ptr&& hp) {
        std::swap(m_ptr, hp.m_ptr);
        std::swap(m_hazard_node, hp.m_hazard_node);
        return *this;
    }

    ~hazard_ptr() {
        free();
    }

    void free() {
        if (m_ptr) detail::hazard_linked_list::release(m_hazard_node, m_ptr);
        m_ptr = nullptr;
    }

private:
    const void* m_ptr;
    detail::hazard_linked_list_node* m_hazard_node;
};


}
