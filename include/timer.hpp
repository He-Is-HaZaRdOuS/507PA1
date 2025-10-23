#pragma once
#include <chrono>

class Timer {
public:
    Timer() { m_StartTimepoint = std::chrono::steady_clock::now(); }

    ~Timer() = default;

    double Stop() {
        m_EndTimepoint = std::chrono::steady_clock::now();

        const uint64_t start =
            std::chrono::time_point_cast<std::chrono::microseconds>(
                m_StartTimepoint)
                .time_since_epoch()
                .count();
        const uint64_t end =
            std::chrono::time_point_cast<std::chrono::microseconds>(m_EndTimepoint)
                .time_since_epoch()
                .count();

        const std::chrono::duration<uint64_t, std::ratio<1, 1000000>>::rep
            duration = (end - start);
        return static_cast<double>(duration) * 0.000001;
    }

private:
    std::chrono::time_point<std::chrono::steady_clock> m_StartTimepoint;
    std::chrono::time_point<std::chrono::steady_clock> m_EndTimepoint;
};