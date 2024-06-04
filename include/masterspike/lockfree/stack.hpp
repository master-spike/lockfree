#pragma once

#include "masterspike/lockfree/hazard_ptr.hpp"

#include <atomic>
#include <optional>

namespace masterspike::lockfree {

namespace detail {

template <typename T>
struct stack_node {
    stack_node(T&& elem) : m_elem(std::forward<T>(elem)) {}
    stack_node(T const& elem) : m_elem(elem) {}

    T m_elem;
    std::atomic<stack_node<T>*> m_next{nullptr};
};

}

template <typename T>
class stack {
public:
    stack() = default;

    void push(T&& t) {
        detail::stack_node<T>* new_node = new detail::stack_node<T>(std::forward<T>(t));
        push_node(new_node);
    }
    void push(T const& t) {
        detail::stack_node<T>* new_node = new detail::stack_node<T>(t);
        push_node(new_node);
    }

    std::optional<T> try_pop () {
        while (true) {
            detail::stack_node<T>* curr_top = nullptr;
            curr_top = m_top.load(std::memory_order_relaxed);
            if (curr_top == nullptr) {
                // empty stack, return nullopt
                return std::nullopt;
            }
            auto haz = hazard_ptr(curr_top); // try protect curr_top

            // curr_top might have been deallocated and destroyed between loading
            // and creating the hazard pointer, this way we ensure that curr_top
            // is still valid and at the top of the stack
            if (curr_top != m_top.load(std::memory_order_seq_cst)) continue;

            detail::stack_node<T>* below = curr_top->m_next.load(std::memory_order_relaxed);

            if(m_top.compare_exchange_weak(curr_top, below, std::memory_order_seq_cst)){
                std::optional<T> result(std::move(curr_top->m_elem));
                retire_pointer<detail::stack_node<T>>(curr_top, [](auto ptr) {
                    delete ptr;
                });
                return result;
            }
        }
        return std::nullopt;
    }

private:

    void push_node (detail::stack_node<T>* new_node) {
        bool succeeded = false;
        while (!succeeded) {
            detail::stack_node<T>* curr_top = m_top.load(std::memory_order_relaxed);
            
            // make sure node points down to next element
            new_node->m_next.store(curr_top, std::memory_order_relaxed);
                
            if (m_top.compare_exchange_weak(curr_top, new_node, std::memory_order_seq_cst)) {
                succeeded = true;
            }
        }
    }

    std::atomic<detail::stack_node<T>*> m_top{nullptr};
};


}
