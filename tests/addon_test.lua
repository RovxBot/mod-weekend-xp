local frame, hooks, clock = {}, {}, 10
local requests, marker = 0, true
local function object()
    return {
        SetPoint = function() end,
        SetText = function(self, text) self.text = text end,
        Show = function(self) self.visible = true end,
        Hide = function(self) self.visible = false end,
    }
end
function CreateFrame()
    frame.RegisterEvent = function() end
    frame.SetScript = function(self, key, fn) self[key] = fn end
    return frame
end
function GetTime() return clock end
function UnitName() return "Tester" end
function UnitIsUnit(a, b) return a == b end
function SecondsToTime(seconds) return tostring(math.floor(seconds)) .. " seconds" end
function SendAddonMessage(prefix, message, channel, name)
    assert(prefix == "WKBONUS" and message == "HELLO|1")
    assert(channel == "WHISPER" and name == "Tester")
    requests = requests + 1
end
function UnitBuff(_, index)
    if index == 1 then
        return "Stock spell", nil, "stock-icon", 0, nil, 0, 0, "player", nil, nil,
            marker and 46668 or 123
    end
end
UnitAura = UnitBuff
GameTooltip = {
    ClearLines = function(self) self.lines = {} end,
    AddLine = function(self, line) table.insert(self.lines, line) end,
    Show = function() end,
    SetUnitAura = function() end,
}
BuffButton1 = object()
BuffButton1.GetID = function() return 1 end
BuffButton1.GetName = function() return "BuffButton1" end
BuffButton1.CreateFontString = object
BuffButton1Icon = { SetTexture = function(self, value) self.texture = value end }
function BuffButton_Update() end
function hooksecurefunc(target, name, callback)
    if type(target) == "string" then hooks[target] = name
    else hooks[name] = callback end
end

dofile(arg[1])
local function message(text, sender)
    frame.OnEvent(frame, "CHAT_MSG_ADDON", "WKBONUS", text, "WHISPER", sender or "Tester")
end
local function tooltip()
    hooks.SetUnitBuff(GameTooltip, "player", 1)
    return table.concat(GameTooltip.lines, "\n")
end
frame.OnEvent(frame, "PLAYER_ENTERING_WORLD")
assert(requests == 1)
for event, title in ipairs({"Experience", "Reputation", "Money", "Battleground Honor", "Dungeon Finder"}) do
    message("STATE|1|" .. event .. "|50|1200|1000|1|1")
    assert(BuffButton1.weekendPercent.text == "+50%")
    assert(BuffButton1.weekendPercent.visible)
    assert(BuffButton1Icon.texture ~= "stock-icon")
    assert(string.find(tooltip(), title .. " Weekend", 1, true))
    assert(string.find(tooltip(), "+50% " .. title, 1, true))
    assert(string.find(tooltip(), "Ends in 200 seconds", 1, true))
end
message("STATE|1|1|62.5|1200|1000|0|1")
assert(BuffButton1.weekendPercent.text == "+62.5%")
assert(string.find(tooltip(), "maximum level", 1, true))
message("STATE|1|3|100|1200|1000|1|1", "SomeoneElse")
assert(BuffButton1.weekendPercent.text == "+62.5%")
for _, invalid in ipairs({"STATE|2|3|100|1200|1000|1|1", "STATE|1|6|50|0|1000|1|1",
    "STATE|1|1|1001|0|1000|1|1", "STATE|1|1|NaN|0|1000|1|1", "garbage"}) do
    message(invalid)
    assert(BuffButton1.weekendPercent.text == "+62.5%")
end
clock = 210
frame.OnUpdate(frame, 1)
assert(not BuffButton1.weekendPercent.visible)
assert(BuffButton1Icon.texture == "stock-icon")
assert(string.find(tooltip(), "No weekend bonus", 1, true))
message("STATE|1|4|50|0|1000|1|1")
assert(BuffButton1.weekendPercent.visible)
message("STATE|1|4|50|0|1000|1|0")
assert(not BuffButton1.weekendPercent.visible)
message("STATE|1|0|0|0|1000|1|1")
assert(not BuffButton1.weekendPercent.visible)
marker = false
GameTooltip.lines = {"Untouched regular buff"}
hooks.SetUnitBuff(GameTooltip, "player", 1)
assert(GameTooltip.lines[1] == "Untouched regular buff")
frame.OnUpdate(frame, 30)
assert(requests == 2)
print("Addon protocol, event text, icons, expiry and eligibility checks passed.")
