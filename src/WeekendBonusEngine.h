#ifndef WEEKEND_BONUS_ENGINE_H
#define WEEKEND_BONUS_ENGINE_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace WeekendBonus
{
    enum class Event : std::uint8_t
    {
        None,
        Experience,
        Reputation,
        Money,
        Battleground,
        DungeonFinder
    };

    enum class Selection
    {
        Random,
        RoundRobin
    };

    struct Schedule
    {
        unsigned startDay = 5; // Sunday = 0
        unsigned startHour = 0;
        unsigned durationHours = 72;
    };

    struct Window
    {
        std::int64_t week;
        std::int64_t start;
        std::int64_t end;
        bool active;
    };

    // These are local civil seconds, deliberately independent of UTC offsets.
    // The runtime converts the boundaries to epoch time using the server TZ.
    inline std::int64_t CivilTime(int year, unsigned month, unsigned day,
                                  unsigned hour = 0, unsigned minute = 0,
                                  unsigned second = 0)
    {
        using namespace std::chrono;
        auto date = sys_days{year_month_day{std::chrono::year{year},
                                            std::chrono::month{month},
                                            std::chrono::day{day}}};
        return duration_cast<seconds>(date.time_since_epoch()).count() +
               hour * 3600 + minute * 60 + second;
    }

    inline std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor)
    {
        auto quotient = value / divisor;
        return quotient - (value % divisor < 0 ? 1 : 0);
    }

    inline Window GetWindow(std::int64_t now, Schedule const& schedule)
    {
        constexpr std::int64_t weekSeconds = 7 * 86400;
        // 1970-01-01 was Thursday (4).
        auto anchor =
            static_cast<std::int64_t>((schedule.startDay + 3) % 7) * 86400 +
            schedule.startHour * 3600;
        auto week = FloorDivide(now - anchor, weekSeconds);
        auto start = anchor + week * weekSeconds;
        auto end = start + schedule.durationHours * 3600;
        return {week, start, end, now >= start && now < end};
    }

    inline std::uint64_t Hash(std::uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    inline std::vector<Event> ShuffledCycle(std::vector<Event> pool,
                                            std::int64_t cycle,
                                            std::uint64_t seed)
    {
        auto cycleSeed = Hash(seed ^ static_cast<std::uint64_t>(cycle));
        std::sort(
            pool.begin(), pool.end(),
            [cycleSeed](Event left, Event right)
            {
                auto leftKey = Hash(cycleSeed ^ static_cast<unsigned>(left));
                auto rightKey = Hash(cycleSeed ^ static_cast<unsigned>(right));
                return leftKey == rightKey ? left < right : leftKey < rightKey;
            });
        return pool;
    }

    inline Event SelectEvent(std::vector<Event> const& pool, Selection mode,
                             std::int64_t week, std::uint64_t seed)
    {
        if (pool.empty())
            return Event::None;

        auto count = static_cast<std::int64_t>(pool.size());
        auto cycle = FloorDivide(week, count);
        auto index = static_cast<std::size_t>(week - cycle * count);
        if (mode == Selection::RoundRobin)
            return pool[index];

        // Two-event pools alternate; randomize which event starts the cycle.
        if (count <= 2)
            return pool[(index + Hash(seed) % pool.size()) % pool.size()];

        auto events = ShuffledCycle(pool, cycle, seed);
        auto previous = ShuffledCycle(pool, cycle - 1, seed);
        // Swapping the first two entries leaves the last entry unchanged,
        // so adjacent cycles cannot repeat an event across their boundary.
        if (events.front() == previous.back())
            std::swap(events[0], events[1]);
        return events[index];
    }

    inline double ValidPercent(double percent)
    {
        return std::isfinite(percent) ? std::clamp(percent, 0.0, 1000.0) : 0.0;
    }

    inline std::uint32_t
    Scale(std::uint32_t amount, double percent,
          std::uint32_t cap = std::numeric_limits<std::uint32_t>::max())
    {
        long double result = amount * (1.0L + ValidPercent(percent) / 100.0L);
        return static_cast<std::uint32_t>(
            std::min(result, static_cast<long double>(cap)));
    }

    inline std::uint32_t
    Extra(std::uint32_t amount, double percent,
          std::uint32_t cap = std::numeric_limits<std::int32_t>::max())
    {
        long double result = amount * (ValidPercent(percent) / 100.0L);
        return static_cast<std::uint32_t>(
            std::min(result, static_cast<long double>(cap)));
    }

    // Acore rewards honor from kills AND objectives through the contribution
    // counter. Our bonus is never fed back into the next native reward delta.
    struct HonorTracker
    {
        std::uint32_t last = 0;
        double fraction = 0.0;

        void Reset(std::uint32_t contribution)
        {
            last = contribution;
            fraction = 0.0;
        }

        std::uint32_t Consume(std::uint32_t contribution, double percent)
        {
            // A daily honor reset may lower this counter.
            auto gained =
                contribution >= last ? contribution - last : contribution;
            last = contribution;
            if (percent <= 0.0)
            {
                fraction = 0.0;
                return 0;
            }
            auto total = gained * (ValidPercent(percent) / 100.0) + fraction;
            auto bonus = static_cast<std::uint32_t>(std::min(
                std::floor(total),
                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
            fraction = total - std::floor(total);
            return bonus;
        }
    };
} // namespace WeekendBonus

#endif
