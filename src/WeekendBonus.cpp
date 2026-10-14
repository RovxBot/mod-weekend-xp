#include "WeekendBonusEngine.h"
#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Chat.h"
#include "Configuration/Config.h"
#include "DataMap.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "LootMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include <array>
#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

using namespace Acore::ChatCommands;

namespace WeekendBonus
{
    constexpr uint32 BuffSpellId = 46668; // Unused copy of Bonfire's Blessing
    std::string const StateKey = "mod-weekend-xp.weekend-bonus";

    constexpr std::array<char const*, 6> Names = {
        "No event", "Experience",         "Reputation",
        "Money",    "Battleground honor", "Dungeon Finder"};

    constexpr std::array<char const*, 6> ConfigNames = {
        "", "XP", "Reputation", "Money", "Battleground", "DungeonFinder"};

    struct Settings
    {
        bool enabled = true;
        bool announce = true;
        bool buffEnabled = true;
        bool alwaysActive = false;
        Event overrideEvent = Event::None;
        Selection selection = Selection::Random;
        uint32 seed = 20261014;
        Schedule schedule;
        std::array<double, 6> percentages{};
        std::vector<Event> pool;
    };

    struct Snapshot
    {
        Settings settings;
        Window window{};
        Event event = Event::None;
        time_t end = 0;

        bool Active() const
        {
            return settings.enabled && event != Event::None &&
                   (settings.alwaysActive ||
                    (window.active && std::time(nullptr) < end));
        }

        double Percent(Event requested) const
        {
            return Active() && event == requested
                       ? settings.percentages[static_cast<unsigned>(event)]
                       : 0.0;
        }
    };

    struct PlayerState : DataMap::Base
    {
        TimeTrackerSmall timer{0};
        HonorTracker honor;
        time_t honorDay = 0;
        bool honorInitialized = false;
        bool addonReady = false;
        uint32 questId = 0;
        uint32 questMoney = 0;
        double questPercent = 0.0;
    };

    PlayerState* State(Player* player)
    {
        return player->CustomData.GetDefault<PlayerState>(StateKey);
    }

    std::int64_t LocalCivilTime()
    {
        auto now = Acore::Time::TimeBreakdown(std::time(nullptr));
        return CivilTime(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                         now.tm_hour, now.tm_min, now.tm_sec);
    }

    time_t ToEpoch(std::int64_t civil)
    {
        using namespace std::chrono;
        sys_seconds point{seconds{civil}};
        auto date = floor<days>(point);
        year_month_day ymd{date};
        hh_mm_ss time{point - date};
        std::tm local{};
        local.tm_year = static_cast<int>(ymd.year()) - 1900;
        local.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
        local.tm_mday = static_cast<unsigned>(ymd.day());
        local.tm_hour = time.hours().count();
        local.tm_min = time.minutes().count();
        local.tm_sec = time.seconds().count();
        local.tm_isdst = -1;
        return std::mktime(&local);
    }

    class Manager
    {
    public:
        static Manager& Instance()
        {
            static Manager manager;
            return manager;
        }

        std::shared_ptr<Snapshot const> Current() const
        {
            return _snapshot.load();
        }

        void LoadConfig()
        {
            Settings settings;
            settings.enabled =
                sConfigMgr->GetOption<bool>("WeekendBonus.Enabled", true);
            settings.announce =
                sConfigMgr->GetOption<bool>("WeekendBonus.Announce", true);
            settings.buffEnabled =
                sConfigMgr->GetOption<bool>("WeekendBonus.BuffEnabled", true);
            settings.alwaysActive =
                sConfigMgr->GetOption<bool>("WeekendBonus.AlwaysActive", false);
            settings.seed = sConfigMgr->GetOption<uint32>(
                "WeekendBonus.RandomSeed", 20261014);
            auto mode = sConfigMgr->GetOption<std::string>(
                "WeekendBonus.Selection", "random");
            if (mode == "round-robin")
                settings.selection = Selection::RoundRobin;
            else if (mode != "random")
                LOG_WARN("module.weekendbonus",
                         "Unknown selection '{}'; using random.", mode);

            settings.schedule.startDay = std::min(
                sConfigMgr->GetOption<uint32>("WeekendBonus.StartDay", 5), 6u);
            settings.schedule.startHour = std::min(
                sConfigMgr->GetOption<uint32>("WeekendBonus.StartHour", 0),
                23u);
            settings.schedule.durationHours = std::clamp(
                sConfigMgr->GetOption<uint32>("WeekendBonus.DurationHours", 72),
                1u, 168u);
            auto forced =
                sConfigMgr->GetOption<uint32>("WeekendBonus.EventOverride", 0);
            if (forced <= static_cast<unsigned>(Event::DungeonFinder))
                settings.overrideEvent = static_cast<Event>(forced);

            for (unsigned i = 1; i < settings.percentages.size(); ++i)
            {
                std::string key = "WeekendBonus.";
                key += ConfigNames[i];
                auto percent =
                    sConfigMgr->GetOption<float>(key + ".BonusPercent", 50.0f);
                auto valid = ValidPercent(percent);
                settings.percentages[i] = std::round(valid * 100.0) / 100.0;
                if (percent != valid)
                    LOG_WARN("module.weekendbonus",
                             "{}.BonusPercent must be finite and between 0 and "
                             "1000.",
                             key);
                if (sConfigMgr->GetOption<bool>(key + ".Enabled", true) &&
                    settings.percentages[i] > 0.0)
                    settings.pool.push_back(static_cast<Event>(i));
            }
            if (settings.pool.empty() && settings.enabled)
                LOG_WARN("module.weekendbonus",
                         "No weekend bonus events are enabled.");
            std::lock_guard lock(_mutex);
            _settings = std::move(settings);
            Publish();
        }

        void Refresh()
        {
            std::lock_guard lock(_mutex);
            Publish();
        }

    private:
        void Publish()
        {
            auto next = std::make_shared<Snapshot>();
            next->settings = _settings;
            next->window = GetWindow(LocalCivilTime(), _settings.schedule);
            next->event = SelectEvent(_settings.pool, _settings.selection,
                                      next->window.week, _settings.seed);
            if (std::find(_settings.pool.begin(), _settings.pool.end(),
                          _settings.overrideEvent) != _settings.pool.end())
                next->event = _settings.overrideEvent;
            next->end = ToEpoch(next->window.end);

            auto previous = Current();
            if (!previous || previous->event != next->event ||
                previous->window.active != next->window.active ||
                previous->window.week != next->window.week)
                LOG_INFO("module.weekendbonus",
                         "Weekend event: {} (+{}%), active: {}.",
                         Names[static_cast<unsigned>(next->event)],
                         next->settings
                             .percentages[static_cast<unsigned>(next->event)],
                         next->Active());
            _snapshot.store(std::move(next));
        }

        std::mutex _mutex;
        Settings _settings;
        std::atomic<std::shared_ptr<Snapshot const>> _snapshot;
    };

    SpellInfo const* BuffSpell()
    {
        static std::unique_ptr<SpellInfo> const spell =
            []() -> std::unique_ptr<SpellInfo>
        {
            auto const* original = sSpellStore.LookupEntry(BuffSpellId);
            if (!original)
            {
                LOG_ERROR("module.weekendbonus",
                          "Missing cosmetic buff spell {}.", BuffSpellId);
                return nullptr;
            }
            SpellEntry cosmetic = *original;
            cosmetic.DurationIndex = 21;
            cosmetic.Effect[EFFECT_0] = SPELL_EFFECT_NONE;
            cosmetic.EffectApplyAuraName[EFFECT_0] = SPELL_AURA_NONE;
            cosmetic.Effect[EFFECT_2] = SPELL_EFFECT_APPLY_AURA;
            cosmetic.EffectApplyAuraName[EFFECT_2] = SPELL_AURA_DUMMY;
            cosmetic.EffectImplicitTargetA[EFFECT_2] = TARGET_UNIT_CASTER;
            auto result = std::make_unique<SpellInfo>(&cosmetic);
            result->_auraState = AURA_STATE_NONE;
            result->_spellSpecific = SPELL_SPECIFIC_NORMAL;
            result->AttributesCu |= SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED;
            return result;
        }();
        return spell.get();
    }

    void SendState(Player* player, Snapshot const& snapshot)
    {
        bool active = snapshot.Active();
        auto event = active ? snapshot.event : Event::None;
        auto percent =
            active ? snapshot.settings.percentages[static_cast<unsigned>(event)]
                   : 0.0;
        bool eligible =
            event != Event::Experience ||
            player->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        auto payload = Acore::StringFormat(
            "WKBONUS\tSTATE|1|{}|{}|{}|{}|{}|{}", static_cast<unsigned>(event),
            percent,
            active && !snapshot.settings.alwaysActive ? snapshot.end : 0,
            std::time(nullptr), eligible ? 1 : 0,
            snapshot.settings.buffEnabled ? 1 : 0);
        WorldPacket packet;
        ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON,
                                     player->GetGUID(), player->GetGUID(),
                                     payload, CHAT_TAG_NONE, player->GetName(),
                                     player->GetName());
        player->SendDirectMessage(&packet);
    }

    void SyncBuff(Player* player, Snapshot const& snapshot)
    {
        auto const* spell = BuffSpell();
        if (!spell)
            return;
        auto* aura = player->GetAura(BuffSpellId);
        // The addon supplies the actual event text; don't show a misleading
        // stock tooltip to clients that have not installed it.
        bool show = snapshot.Active() && snapshot.settings.buffEnabled &&
                    State(player)->addonReady;
        if (show && !aura && player->IsAlive())
            player->AddAura(spell, 1 << EFFECT_2, player);
        else if (!show && aura && aura->GetSpellInfo() == spell)
            aura->Remove();
    }

    void AwardBattlegroundHonor(Player* player, bool battleground,
                                Battleground* bg = nullptr)
    {
        auto* state = State(player);
        auto day = GameTime::GetGameTime().count() / 86400;
        if (state->honorDay != day)
        {
            // Use the core's UTC day boundary. A reset followed by a large
            // award can exceed yesterday's value, so a counter decrease alone
            // cannot reliably detect the reset.
            player->UpdateHonorFields();
            state->honor.Reset(0);
            state->honorDay = day;
        }
        auto contribution =
            player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION);
        if (!state->honorInitialized)
        {
            state->honor.Reset(contribution);
            state->honorInitialized = true;
            return;
        }
        if (!battleground)
        {
            state->honor.Reset(contribution);
            return;
        }
        auto snapshot = Manager::Instance().Current();
        double percent =
            snapshot ? snapshot->Percent(Event::Battleground) : 0.0;
        auto extra = state->honor.Consume(contribution, percent);
        if (!extra)
            return;
        auto before = player->GetHonorPoints();
        player->ModifyHonorPoints(static_cast<int32>(extra));
        auto after = player->GetHonorPoints();
        auto received = after > before ? after - before : 0;
        if (!received)
            return;
        player->ApplyModUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION, received,
                                    true);
        state->honor.last =
            player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION);
        if (bg)
            bg->UpdatePlayerScore(player, SCORE_BONUS_HONOR, received, false);
    }

    class WeekendBonusWorldScript : public WorldScript
    {
    public:
        WeekendBonusWorldScript()
            : WorldScript("WeekendBonusWorld",
                          {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP,
                           WORLDHOOK_ON_UPDATE})
        {
        }

        void OnStartup() override { Manager::Instance().LoadConfig(); }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            Manager::Instance().LoadConfig();
        }

        void OnUpdate(uint32 diff) override
        {
            _timer.Update(diff);
            if (_timer.Passed())
            {
                _timer.Reset(1000);
                Manager::Instance().Refresh();
            }
        }

    private:
        TimeTrackerSmall _timer{1000};
    };

    class WeekendBonusPlayerScript : public PlayerScript
    {
    public:
        WeekendBonusPlayerScript()
            : PlayerScript("WeekendBonusPlayer",
                           {PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE,
                            PLAYERHOOK_ON_GIVE_EXP,
                            PLAYERHOOK_ON_GIVE_REPUTATION,
                            PLAYERHOOK_ON_BEFORE_LOOT_MONEY,
                            PLAYERHOOK_ON_QUEST_COMPUTE_EXP,
                            PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
                            PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
                            PLAYERHOOK_CAN_PLAYER_USE_CHAT})
        {
        }

        void OnPlayerLogin(Player* player) override
        {
            player->UpdateHonorFields();
            State(player)->honor.Reset(
                player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));
            State(player)->honorDay = GameTime::GetGameTime().count() / 86400;
            State(player)->honorInitialized = true;
            auto snapshot = Manager::Instance().Current();
            if (snapshot && snapshot->Active() && snapshot->settings.announce)
                ChatHandler(player->GetSession())
                    .PSendSysMessage(
                        "Weekend bonus: {} +{}%. Type .weekendbonus status for "
                        "details.",
                        Names[static_cast<unsigned>(snapshot->event)],
                        snapshot->settings.percentages[static_cast<unsigned>(
                            snapshot->event)]);
        }

        void OnPlayerUpdate(Player* player, uint32 diff) override
        {
            AwardBattlegroundHonor(
                player, player->InBattleground() && !player->InArena(),
                player->GetBattleground());
            auto* state = State(player);
            state->timer.Update(diff);
            if (!state->timer.Passed())
                return;
            state->timer.Reset(5000);
            auto snapshot = Manager::Instance().Current();
            if (snapshot)
            {
                SyncBuff(player, *snapshot);
                if (state->addonReady)
                    SendState(player, *snapshot);
            }
        }

        void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/,
                            uint8 source) override
        {
            auto snapshot = Manager::Instance().Current();
            if (!snapshot || player->GetLevel() >=
                                 sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
                return;
            double percent = snapshot->Percent(Event::Experience);
            if (source == PlayerXPSource::XPSOURCE_QUEST_DF)
                percent += snapshot->Percent(Event::DungeonFinder);
            amount = Scale(amount, percent);
        }

        void OnPlayerGiveReputation(Player* /*player*/, int32 /*faction*/,
                                    float& amount,
                                    ReputationSource /*source*/) override
        {
            auto snapshot = Manager::Instance().Current();
            auto percent =
                snapshot ? snapshot->Percent(Event::Reputation) : 0.0;
            if (percent > 0.0 && amount > 0.0f)
                amount = std::min(
                    amount * (1.0 + percent / 100.0),
                    static_cast<double>(std::numeric_limits<int32>::max()));
        }

        void OnPlayerBeforeLootMoney(Player* /*player*/, Loot* loot) override
        {
            auto snapshot = Manager::Instance().Current();
            auto percent = snapshot ? snapshot->Percent(Event::Money) : 0.0;
            if (percent > 0.0 && loot)
                loot->gold = Scale(loot->gold, percent, MAX_MONEY_AMOUNT);
        }

        void OnPlayerQuestComputeXP(Player* player, Quest const* quest,
                                    uint32& /*xp*/) override
        {
            auto* state = State(player);
            state->questId = quest->GetQuestId();
            state->questMoney = player->GetMoney();
            auto snapshot = Manager::Instance().Current();
            state->questPercent =
                snapshot ? snapshot->Percent(Event::Money) : 0.0;
            if (snapshot && quest->IsDFQuest())
                state->questPercent += snapshot->Percent(Event::DungeonFinder);
        }

        void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
        {
            auto* state = State(player);
            if (state->questId != quest->GetQuestId())
                return;
            auto percent = state->questPercent;
            auto before = state->questMoney;
            state->questId = 0;
            state->questPercent = 0.0;
            // Use money actually received: max-level conversion, server rates,
            // reward costs, caps and play-time reductions have already applied.
            auto now = player->GetMoney();
            if (percent <= 0.0 || now <= before || now >= MAX_MONEY_AMOUNT)
                return;
            auto extra = std::min(Extra(now - before, percent),
                                  MAX_MONEY_AMOUNT - now - 1);
            if (extra && player->ModifyMoney(static_cast<int32>(extra)))
            {
                player->UpdateAchievementCriteria(
                    ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD, extra);
                ChatHandler(player->GetSession())
                    .PSendSysMessage("Weekend reward: {} extra copper (+{}%).",
                                     extra, percent);
            }
        }

        void OnPlayerBeforeSendChatMessage(Player* player, uint32& type,
                                           uint32& language,
                                           std::string& message) override
        {
            if (type != CHAT_MSG_WHISPER || language != LANG_ADDON ||
                message != "WKBONUS\tHELLO|1")
                return;
            auto* state = State(player);
            // The client also polls after /reload; rate-limit requests.
            if (state->addonReady && !state->timer.Passed())
                return;
            state->addonReady = true;
            state->timer.Reset(5000);
            if (auto snapshot = Manager::Instance().Current())
            {
                SendState(player, *snapshot);
                SyncBuff(player, *snapshot);
            }
        }

        bool OnPlayerCanUseChat(Player* /*player*/, uint32 type,
                                uint32 language, std::string& message,
                                Player* /*receiver*/) override
        {
            return !(type == CHAT_MSG_WHISPER && language == LANG_ADDON &&
                     message.compare(0, 8, "WKBONUS\t") == 0);
        }
    };

    class WeekendBonusBattlegroundScript : public AllBattlegroundScript
    {
    public:
        WeekendBonusBattlegroundScript()
            : AllBattlegroundScript(
                  "WeekendBonusBG",
                  {ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_BEFORE_ADD_PLAYER,
                   ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END_REWARD,
                   ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_REMOVE_PLAYER_AT_LEAVE})
        {
        }

        void OnBattlegroundBeforeAddPlayer(Battleground* /*bg*/,
                                           Player* player) override
        {
            player->UpdateHonorFields();
            auto* state = State(player);
            state->honor.Reset(
                player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));
            state->honorInitialized = true;
            state->honorDay = GameTime::GetGameTime().count() / 86400;
        }

        void OnBattlegroundEndReward(Battleground* bg, Player* player,
                                     TeamId /*winner*/) override
        {
            AwardBattlegroundHonor(player, !bg->isArena(), bg);
        }

        void OnBattlegroundRemovePlayerAtLeave(Battleground* bg,
                                               Player* player) override
        {
            // The core has already removed the score entry at this point.
            AwardBattlegroundHonor(player, !bg->isArena());
            State(player)->honor.Reset(
                player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));
        }
    };

    class WeekendBonusCommands : public CommandScript
    {
    public:
        WeekendBonusCommands() : CommandScript("WeekendBonusCommands") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable const commands = {
                {"weekendbonus status", Status, SEC_PLAYER, Console::Yes},
                {"weekendbonus calendar", Calendar, SEC_PLAYER, Console::Yes}};
            return commands;
        }

        static bool Status(ChatHandler* handler)
        {
            auto snapshot = Manager::Instance().Current();
            if (!snapshot || !snapshot->Active())
            {
                handler->SendSysMessage("No weekend bonus is active.");
                return true;
            }
            handler->PSendSysMessage(
                "Weekend bonus: {} +{}%.",
                Names[static_cast<unsigned>(snapshot->event)],
                snapshot->settings
                    .percentages[static_cast<unsigned>(snapshot->event)]);
            if (!snapshot->settings.alwaysActive)
                handler->PSendSysMessage("Ends at {} (server local time).",
                                         FormatTime(snapshot->end));
            return true;
        }

        static std::string FormatTime(time_t epoch)
        {
            auto local = Acore::Time::TimeBreakdown(epoch);
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
            return text;
        }

        static bool Calendar(ChatHandler* handler)
        {
            auto snapshot = Manager::Instance().Current();
            if (!snapshot || !snapshot->settings.enabled ||
                snapshot->settings.pool.empty())
            {
                handler->SendSysMessage("Weekend bonuses are disabled.");
                return true;
            }
            auto window =
                GetWindow(LocalCivilTime(), snapshot->settings.schedule);
            auto first = window.active ? 0 : 1;
            for (auto i = first; i < first + 5; ++i)
            {
                auto event = SelectEvent(
                    snapshot->settings.pool, snapshot->settings.selection,
                    window.week + i, snapshot->settings.seed);
                if (std::find(snapshot->settings.pool.begin(),
                              snapshot->settings.pool.end(),
                              snapshot->settings.overrideEvent) !=
                    snapshot->settings.pool.end())
                    event = snapshot->settings.overrideEvent;
                handler->PSendSysMessage(
                    "{}: {} +{}%.",
                    FormatTime(ToEpoch(window.start + i * 7 * 86400)),
                    Names[static_cast<unsigned>(event)],
                    snapshot->settings
                        .percentages[static_cast<unsigned>(event)]);
            }
            return true;
        }
    };
} // namespace WeekendBonus

void AddWeekendBonusScripts()
{
    new WeekendBonus::WeekendBonusWorldScript();
    new WeekendBonus::WeekendBonusPlayerScript();
    new WeekendBonus::WeekendBonusBattlegroundScript();
    new WeekendBonus::WeekendBonusCommands();
}
