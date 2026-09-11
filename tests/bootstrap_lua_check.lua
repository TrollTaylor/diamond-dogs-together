-- Isolated Lua 5.1 fixture. These tables are stubs, not game ABI evidence.
local function run(path, receipts, archiveMode)
    local constants = setmetatable({}, { __index = function() return 0 end })
    local environment = {
        Fox = { StrCode32 = function() return 0 end },
        GameObject = {}, TppGameObject = constants, TppGameMode = {},
        TppNetworkUtil = {}, bit = {},
        loadstring = function(source)
            receipts[#receipts+1] = source
            return loadstring(source)
        end,
    }
    setmetatable(environment, { __index = _G })
    -- Suppress the interpreter's physical filesystem fallback except in the
    -- explicitly simulated virtual filesystem cases below.
    environment.dofile = false
    if archiveMode == "direct" or archiveMode == "direct_failure" then
        environment.dofile = function(internalPath)
            assert(internalPath == "/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua")
            if archiveMode == "direct_failure" then error("fixture direct read failed") end
            local archive = assert(loadfile(arg[4]))
            setfenv(archive, environment)
            return archive()
        end
    elseif archiveMode then
        environment.Script = { LoadLibrary = function(internalPath)
            assert(internalPath == "/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua")
            if archiveMode == "failure" then error("fixture missing archive") end
            local archive = assert(loadfile(arg[4]))
            setfenv(archive, environment)
            return archive()
        end }
    end
    local chunk = assert(loadfile(path))
    setfenv(chunk, environment)
    return assert(chunk())
end
local originalReceipts, patchedReceipts = {}, {}
local original = run(arg[1], originalReceipts)
local patched = run(arg[2], patchedReceipts)
assert(original.MgsCoop == nil)
assert(type(patched.MgsCoop) == "table")
assert(patched.MgsCoop.bootstrapVersion == 1)
assert(patched.MgsCoop.maxPlayers == 4)
assert(patched.MgsCoop.gameplayEnabled == false)
assert(#originalReceipts == 0 and #patchedReceipts == 1)
assert(patchedReceipts[1] == "return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'")
assert(patched.IsTypeString("x") and not patched.IsTypeString(1))
assert(#patched.requires == #original.requires)
assert(#patched._requireList == #original._requireList)
for k, v in pairs(original) do assert(type(patched[k]) == type(v), k) end
for i, v in ipairs(original.requires) do assert(patched.requires[i] == v) end
for i, v in ipairs(original._requireList) do assert(patched._requireList[i] == v) end
local packages = patched.MgsCoop.GetPlayerPackages()
assert(#packages == 2)
packages[1] = "changed"
assert(patched.MgsCoop.GetPlayerPackages()[1] ~= "changed")
print("Lua 5.1 bootstrap executed; original exports and dependency lists retained; one execution receipt; gameplay disabled")
if arg[3] then
    for _, mode in ipairs({"success", "failure"}) do
        local receipts = {}
        local result = run(arg[3], receipts, mode)
        assert(result.MgsCoop.archiveMounted and result.MgsCoop.archiveLoadRequested)
        assert(result.MgsCoop.archiveLoadCallSucceeded == (mode == "success"))
        assert(result.MgsCoop.gameplayEnabled == false)
        assert(#result.requires == #original.requires)
        if mode == "success" then
            assert(#receipts == 3)
            assert(receipts[2] == "return 'MGS_COOP_ARCHIVE_EXECUTED_V1'")
            assert(receipts[3] == "return 'MGS_COOP_ARCHIVE_LOAD_RETURNED_V1'")
        else
            assert(#receipts == 2)
            assert(receipts[2] == "return 'MGS_COOP_ARCHIVE_LOAD_FAILED_V1'")
        end
    end
    local receipts = {}
    local unavailable = run(arg[3], receipts)
    assert(not unavailable.MgsCoop.archiveLoadRequested and #receipts == 1)
    for _, mode in ipairs({"direct", "direct_failure"}) do
        local directReceipts = {}
        local result = run(arg[3], directReceipts, mode)
        assert(not result.MgsCoop.archiveLoadRequested)
        assert(result.MgsCoop.archiveDirectReadSucceeded == (mode == "direct"))
        if mode == "direct" then
            assert(#directReceipts == 3)
            assert(directReceipts[2] == "return 'MGS_COOP_ARCHIVE_EXECUTED_V1'")
            assert(directReceipts[3] == "return 'MGS_COOP_ARCHIVE_FILE_RETURNED_V1'")
        else
            assert(#directReceipts == 2)
            assert(string.find(directReceipts[2], "fixture direct read failed", 1, true))
        end
    end
    print("Archive bootstrap: successful execution receipt, handled load failure and missing Script API checked; no engine or actor calls")
end
