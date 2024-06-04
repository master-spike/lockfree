#include "masterspike/lockfree/hazard_ptr.hpp"


#include <chrono>
#include <atomic>
#include <thread>
#include <array>
#include <span>

#include <iostream>



namespace masterspike::test {

    class retirable_obj {
    public:
        static retirable_obj* make_retirable_obj(std::atomic<int>* in_dc) {
            return new retirable_obj(in_dc); 
        }
        ~retirable_obj() {
            if (destruction_counter) destruction_counter->fetch_add(1, std::memory_order_seq_cst);
        }
    private:
        std::atomic<int>* destruction_counter;

        retirable_obj(std::atomic<int>* in_dc) : destruction_counter(in_dc) {

        }

        retirable_obj(retirable_obj const&) = delete;
        retirable_obj& operator=(retirable_obj const&) = delete;
        retirable_obj(retirable_obj&&) = delete;
        retirable_obj& operator=(retirable_obj&&) = delete;


    };

    void run_test1_t1(retirable_obj* protect, std::atomic<int>* out_sig, std::atomic<int>* in_sig) {
        while(in_sig->load(std::memory_order_seq_cst) != 0);
        auto hz = lockfree::hazard_ptr(protect);
        out_sig->store(1);
        while(in_sig->load(std::memory_order_seq_cst) != 1);
        hz.free();
        out_sig->store(3);
        while(in_sig->load(std::memory_order_seq_cst) != 3);
    }

    void run_test1_t2(std::span<retirable_obj*> vec, std::atomic<int>* out_sig, std::atomic<int>* in_sig) {
        while (out_sig->load(std::memory_order_seq_cst) == 0);
        for (auto const& p : vec) {
            lockfree::retire_pointer<retirable_obj>(p, [](auto p) {
                delete p;
            });
        }
        out_sig->store(2);
        while(in_sig->load(std::memory_order::relaxed) == 0);
    }
    std::atomic<int> out_sig{0};
    std::atomic<int> in_sig{-1};
    std::atomic<int> destruction_counter;

    std::vector<retirable_obj*> vec;

    bool test1() {
        using namespace std::chrono_literals;
        std::jthread t1;
        std::jthread t2;
        
        vec.reserve(2500);

        for (int i = 0; i < 2500; ++i) {
            vec.emplace_back(retirable_obj::make_retirable_obj(&destruction_counter));
        }

        t1 = std::jthread(run_test1_t1, vec[0], &out_sig, &in_sig);
        t2 = std::jthread(run_test1_t2, std::span(vec.begin(), vec.end()), &out_sig, &in_sig);

        in_sig.store(0);

        while(out_sig.load(std::memory_order_seq_cst) != 2);

        std::cout << destruction_counter.load(std::memory_order_seq_cst) << std::endl;
        in_sig.store(1);
        t2.join();
        std::cout << destruction_counter.load(std::memory_order_seq_cst) << std::endl;
        in_sig.store(3);
        t1.join();

        return true;
    };







}



int main(int argc, char** argv) {
    masterspike::test::test1();
}



