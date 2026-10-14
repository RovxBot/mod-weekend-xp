#include "WeekendBonusEngine.h"
#include <cassert>
#include <iostream>
#include <set>

using namespace WeekendBonus;

int main()
{
    Schedule schedule;
    auto thursday = GetWindow(CivilTime(2026, 10, 15, 23, 59, 59), schedule);
    auto friday = GetWindow(CivilTime(2026, 10, 16), schedule);
    auto sunday = GetWindow(CivilTime(2026, 10, 18, 23, 59, 59), schedule);
    auto monday = GetWindow(CivilTime(2026, 10, 19), schedule);
    assert(!thursday.active && friday.active && sunday.active &&
           !monday.active);
    assert(friday.week == sunday.week && sunday.week == monday.week);
    assert(thursday.week + 1 == friday.week);
    assert(friday.start == CivilTime(2026, 10, 16));
    assert(friday.end == CivilTime(2026, 10, 19));
    assert(GetWindow(CivilTime(2027, 1, 1), schedule).active);
    assert(GetWindow(CivilTime(2024, 3, 1), schedule).active);
    assert(GetWindow(CivilTime(1969, 12, 26), schedule).active);
    schedule = {6, 18, 30};
    assert(!GetWindow(CivilTime(2026, 10, 17, 17, 59, 59), schedule).active);
    assert(GetWindow(CivilTime(2026, 10, 17, 18), schedule).active);
    assert(GetWindow(CivilTime(2026, 10, 18, 23, 59, 59), schedule).active);
    assert(!GetWindow(CivilTime(2026, 10, 19), schedule).active);
    schedule.durationHours = 168;
    assert(GetWindow(CivilTime(2026, 10, 23, 18), schedule).active);

    std::vector<Event> pool = {Event::Experience, Event::Reputation,
                               Event::Money, Event::Battleground,
                               Event::DungeonFinder};
    assert(SelectEvent({}, Selection::Random, 0, 1) == Event::None);
    assert(SelectEvent({Event::Money}, Selection::Random, 99, 1) ==
           Event::Money);
    for (int week = -100; week < 100; ++week)
    {
        auto event = SelectEvent(pool, Selection::RoundRobin, week, 42);
        auto index = week - FloorDivide(week, 5) * 5;
        assert(event == pool[index]);
    }
    bool differentSeed = false;
    for (std::uint64_t seed = 0; seed < 20; ++seed)
    {
        for (int count = 2; count <= 5; ++count)
        {
            std::vector<Event> enabled(pool.begin(), pool.begin() + count);
            for (int cycle = -30; cycle < 30; ++cycle)
            {
                std::set<Event> seen;
                for (int slot = 0; slot < count; ++slot)
                {
                    auto week = cycle * count + slot;
                    auto event =
                        SelectEvent(enabled, Selection::Random, week, seed);
                    assert(std::find(enabled.begin(), enabled.end(), event) !=
                           enabled.end());
                    assert(seen.insert(event).second);
                    assert(event ==
                           SelectEvent(enabled, Selection::Random, week, seed));
                    assert(event != SelectEvent(enabled, Selection::Random,
                                                week - 1, seed));
                    differentSeed |=
                        event !=
                        SelectEvent(enabled, Selection::Random, week, seed + 1);
                }
            }
        }
    }
    assert(differentSeed);

    assert(Scale(100, 50) == 150);
    assert(Scale(101, 50) == 151);
    assert(Scale(0, 1000) == 0);
    assert(Scale(100, 0) == 100);
    assert(Scale(100, -50) == 100);
    assert(Scale(100, std::numeric_limits<double>::quiet_NaN()) == 100);
    assert(Scale(0xffffffff, 50) == 0xffffffff);
    assert(Scale(2000000000, 50, 0x7fffffff) == 0x7fffffff);
    assert(Extra(100, 50) == 50);
    assert(Extra(101, 50) == 50);
    assert(Extra(0xffffffff, 1000) == 0x7fffffff);
    assert(ValidPercent(1001) == 1000);
    assert(ValidPercent(std::numeric_limits<double>::infinity()) == 0);

    HonorTracker honor;
    honor.Reset(100);
    assert(honor.Consume(101, 50) == 0);
    assert(honor.Consume(102, 50) == 1);
    // The runtime moves last past its own bonus; it cannot become another
    // bonus.
    honor.last = 103;
    assert(honor.Consume(103, 50) == 0);
    assert(honor.Consume(123, 50) == 10);
    honor.last = 133;
    assert(honor.Consume(133, 50) == 0);
    // Daily reset: only honor earned after midnight is rewarded.
    assert(honor.Consume(10, 50) == 5);
    // The runtime explicitly resets the baseline at the core's UTC midnight,
    // including when the first new-day award is larger than yesterday's total.
    honor.Reset(0);
    assert(honor.Consume(2000, 50) == 1000);
    honor.Reset(10);
    assert(honor.Consume(110, 0) == 0);
    assert(honor.Consume(120, 50) == 5);
    assert(honor.Consume(0xffffffff, 1000) == 0x7fffffff);

    std::cout
        << "Rotation, calendar boundaries, overflow and honor checks passed.\n";
}
