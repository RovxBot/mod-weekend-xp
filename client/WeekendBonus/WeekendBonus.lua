local PREFIX = "WKBONUS"
local MARKER_SPELL = 46668
local events = {
    [1] = { title = "Experience", icon = "Interface\\Icons\\Spell_Holy_ChampionsBond",
        description = "Bonus experience from kills, quests, exploration and battlegrounds." },
    [2] = { title = "Reputation", icon = "Interface\\Icons\\Achievement_Reputation_01",
        description = "Bonus to earned reputation. Reputation losses are unchanged." },
    [3] = { title = "Money", icon = "Interface\\Icons\\INV_Misc_Coin_01",
        description = "Bonus gold from loot and quest rewards, including shared loot." },
    [4] = { title = "Battleground Honor", icon = "Interface\\Icons\\Achievement_PVP_A_01",
        description = "Bonus honor from battleground kills, objectives and match rewards." },
    [5] = { title = "Dungeon Finder", icon = "Interface\\Icons\\INV_Helmet_08",
        description = "Bonus XP and gold from Random Dungeon Finder completion rewards. Items and emblems are unchanged." },
}

local state = { event = 0, percent = 0, ends = 0, serverTime = 0,
    received = 0, eligible = true, visible = true }
local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("UNIT_AURA")

local function PercentText()
    return string.format("%g", state.percent)
end

local function Remaining()
    if state.ends == 0 then return nil end
    return math.max(0, state.ends - state.serverTime - (GetTime() - state.received))
end

local function Active()
    local remaining = Remaining()
    return events[state.event] and (not remaining or remaining > 0)
end

local function UpdateIcons()
    for i = 1, (BUFF_MAX_DISPLAY or 32) do
        local button = _G["BuffButton" .. i]
        if button then
            local index = button:GetID()
            local marker = index and select(11, UnitBuff("player", index)) == MARKER_SPELL
            if marker and Active() and state.visible then
                local texture = _G[button:GetName() .. "Icon"]
                if texture then texture:SetTexture(events[state.event].icon) end
                if not button.weekendPercent then
                    button.weekendPercent = button:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmall")
                    button.weekendPercent:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", 1, -1)
                end
                button.weekendPercent:SetText("+" .. PercentText() .. "%")
                button.weekendPercent:Show()
            else
                if button.weekendPercent then button.weekendPercent:Hide() end
                if marker then
                    local texture = _G[button:GetName() .. "Icon"]
                    if texture then texture:SetTexture(select(3, UnitBuff("player", index))) end
                end
            end
        end
    end
end

local function ShowTooltip(tooltip, unit, index, filter, auraFunction)
    if select(11, auraFunction(unit, index, filter)) ~= MARKER_SPELL then return end
    tooltip:ClearLines()
    if not Active() then
        tooltip:AddLine("Weekend Bonus", 1, 0.82, 0)
        tooltip:AddLine("No weekend bonus is currently active.", 1, 1, 1, true)
    else
        local event = events[state.event]
        tooltip:AddLine(event.title .. " Weekend", 1, 0.82, 0)
        tooltip:AddLine("+" .. PercentText() .. "% " .. event.title, 0.3, 1, 0.3)
        tooltip:AddLine(event.description, 1, 1, 1, true)
        if UnitIsUnit(unit, "player") and not state.eligible then
            tooltip:AddLine("You are at the maximum level; XP bonuses do not apply.", 1, 0.5, 0.2, true)
        end
        local remaining = Remaining()
        if remaining then
            tooltip:AddLine("Ends in " .. SecondsToTime(remaining), 0.7, 0.7, 0.7)
        end
    end
    tooltip:Show()
end

hooksecurefunc(GameTooltip, "SetUnitBuff", function(tooltip, unit, index, filter)
    ShowTooltip(tooltip, unit, index, filter, UnitBuff)
end)
if GameTooltip.SetUnitAura then
    hooksecurefunc(GameTooltip, "SetUnitAura", function(tooltip, unit, index, filter)
        ShowTooltip(tooltip, unit, index, filter, UnitAura)
    end)
end
if BuffButton_Update then hooksecurefunc("BuffButton_Update", UpdateIcons) end

local function Receive(message, sender)
    if sender ~= UnitName("player") then return end
    local version, event, percent, ends, now, eligible, visible = string.match(message,
        "^STATE|(%d+)|(%d+)|([%d%.]+)|(%d+)|(%d+)|([01])|([01])$")
    version, event, percent, ends, now = tonumber(version), tonumber(event),
        tonumber(percent), tonumber(ends), tonumber(now)
    if version ~= 1 or not event or event > 5 or not percent
        or percent > 1000 or not ends or not now then return end
    if event > 0 and percent <= 0 then return end
    state.event, state.percent, state.ends, state.serverTime = event, percent, ends, now
    state.received = GetTime()
    state.eligible, state.visible = eligible == "1", visible == "1"
    UpdateIcons()
end

local function RequestState()
    SendAddonMessage(PREFIX, "HELLO|1", "WHISPER", UnitName("player"))
end

local poll, refresh = 0, 0
frame:SetScript("OnEvent", function(_, event, ...)
    if event == "PLAYER_ENTERING_WORLD" then
        RequestState()
        poll = 0
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, _, sender = ...
        if prefix == PREFIX then Receive(message, sender) end
    elseif event == "UNIT_AURA" and (...) == "player" then
        UpdateIcons()
    end
end)

frame:SetScript("OnUpdate", function(_, elapsed)
    poll, refresh = poll + elapsed, refresh + elapsed
    if refresh >= 1 then
        refresh = 0
        UpdateIcons()
    end
    if poll >= 30 then
        poll = 0
        RequestState()
    end
end)
