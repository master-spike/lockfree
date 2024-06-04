#include "masterspike/lockfree/stack.hpp"

#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <thread>
#include <iostream>

namespace masterspike::test {

    static constexpr std::size_t test1_num_producer_threads = 4;
    static constexpr std::size_t test1_num_consumer_threads = 4;

    static constexpr int num_values_per_producer = 20040;

    void test1_consumer(masterspike::lockfree::stack<int>* stack, std::unique_ptr<std::vector<int>>* out_vec, std::atomic<int>* wait_for) {
        while (wait_for->load(std::memory_order_relaxed) == 0); 
        bool run = true;
        auto vec = std::make_unique<std::vector<int>>();
        while (run) {
            auto r = stack->try_pop();
            if (!r.has_value()) {
                if (wait_for->load() != 1) run = false;
            }
            else {
                vec->emplace_back(r.value());
            }
        }
        out_vec->swap(vec);
    }

    void test1_producer(masterspike::lockfree::stack<int>* stack, std::atomic<int>* wait_for, int offset) {
        while (wait_for->load(std::memory_order_relaxed) == 0); 
        for (int i = 0; i < num_values_per_producer; ++i) {
            stack->push(i * test1_num_producer_threads + offset);
        }
    }

    bool test1() {
        std::array<std::jthread, test1_num_consumer_threads> t1_consumers;
        std::array<std::jthread, test1_num_producer_threads> t1_producers;

        std::atomic<int> wait_for;
        std::array<std::unique_ptr<std::vector<int>>, test1_num_consumer_threads> output;

        lockfree::stack<int> stack;

        for (int i = 0; i < test1_num_producer_threads; ++i) {
            t1_producers[i] = std::jthread(test1_producer, &stack, &wait_for, i);
        }
        for (int i = 0; i < test1_num_consumer_threads; ++i) {
            t1_consumers[i] = std::jthread(test1_consumer, &stack, &(output[i]), &wait_for);
        }
        
        wait_for.store(1, std::memory_order_seq_cst);

        for (auto& jthr : t1_producers) {
            jthr.join();
        }

        wait_for.store(2, std::memory_order_seq_cst);

        for (auto& jthr : t1_consumers) {
            jthr.join();
        }

        std::vector<int> reaccumulate;
        for (auto& vptr : output) {
            for (int k : *vptr) {
                reaccumulate.push_back(k);
            }
        }

        std::sort(reaccumulate.begin(), reaccumulate.end());

        for (std::size_t i = 0; i < reaccumulate.size(); ++i) {
            if (reaccumulate[i] != i) {
                std::cout << "wrong value: reaccumulate[" << i << "] ==" << reaccumulate[i] << std::endl;
                return false;
            }
        }

        return true;

    }



}

int main (int argc, char** argv) {
    bool res = masterspike::test::test1();
    std::cout << res << std::endl;
    return !res;
}