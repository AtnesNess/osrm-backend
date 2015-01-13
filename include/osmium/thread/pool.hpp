#ifndef OSMIUM_THREAD_POOL_HPP
#define OSMIUM_THREAD_POOL_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <thread>
#include <type_traits>
#include <vector>

#include <osmium/thread/function_wrapper.hpp>
#include <osmium/thread/queue.hpp>
#include <osmium/thread/util.hpp>
#include <osmium/util/config.hpp>

namespace osmium {

    /**
     * @brief Threading-related low-level code
     */
    namespace thread {

        /**
         *  Thread pool.
         */
        class Pool {

            /**
             * This class makes sure all pool threads will be joined when
             * the pool is destructed.
             */
            class thread_joiner {

                std::vector<std::thread>& m_threads;

            public:

                explicit thread_joiner(std::vector<std::thread>& threads) :
                    m_threads(threads) {
                }

                ~thread_joiner() {
                    for (auto& thread : m_threads) {
                        if (thread.joinable()) {
                            thread.join();
                        }
                    }
                }

            }; // class thread_joiner

            std::atomic<bool> m_done;
            osmium::thread::Queue<function_wrapper> m_work_queue;
            std::vector<std::thread> m_threads;
            thread_joiner m_joiner;
            int m_num_threads;

            void worker_thread() {
                osmium::thread::set_thread_name("_osmium_worker");
                while (!m_done) {
                    function_wrapper task;
                    m_work_queue.wait_and_pop_with_timeout(task);
                    if (task) {
                        task();
                    }
                }
            }

            /**
             * Create thread pool with the given number of threads. If
             * num_threads is 0, the number of threads is read from
             * the environment variable OSMIUM_POOL_THREADS. The default
             * value in that case is -2.
             *
             * If the number of threads is a negative number, it will be
             * set to the actual number of cores on the system plus the
             * given number, ie it will leave a number of cores unused.
             *
             * In all cases the minimum number of threads in the pool is 1.
             */
            explicit Pool(int num_threads, size_t max_queue_size) :
                m_done(false),
                m_work_queue(max_queue_size, "work"),
                m_threads(),
                m_joiner(m_threads),
                m_num_threads(num_threads) {

                if (m_num_threads == 0) {
                    m_num_threads = osmium::config::get_pool_threads();
                }

                if (m_num_threads <= 0) {
                    m_num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) + m_num_threads);
                }

                try {
                    for (int i=0; i < m_num_threads; ++i) {
                        m_threads.push_back(std::thread(&Pool::worker_thread, this));
                    }
                } catch (...) {
                    m_done = true;
                    throw;
                }
            }

        public:

            static constexpr int default_num_threads = 0;
            static constexpr size_t max_work_queue_size = 10;

            static Pool& instance() {
                static Pool pool(default_num_threads, max_work_queue_size);
                return pool;
            }

            ~Pool() {
                m_done = true;
            }

            size_t queue_size() const {
                return m_work_queue.size();
            }

            bool queue_empty() const {
                return m_work_queue.empty();
            }

            template <typename TFunction>
            std::future<typename std::result_of<TFunction()>::type> submit(TFunction f) {

                typedef typename std::result_of<TFunction()>::type result_type;

                std::packaged_task<result_type()> task(std::move(f));
                std::future<result_type> future_result(task.get_future());
                m_work_queue.push(std::move(task));

                return future_result;
            }

        }; // class Pool

    } // namespace thread

} // namespace osmium

#endif // OSMIUM_THREAD_POOL_HPP
