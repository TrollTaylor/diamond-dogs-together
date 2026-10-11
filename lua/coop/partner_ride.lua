
-- Exact retail PlayerStatus registers ON_HORSE/ON_VEHICLE/ON_HELICOPTER.
-- This producer adds no lifecycle wrapper or native call, and missing status
-- evidence means unknown (5), never an invented on-foot confirmation.
if type(e) == "table" then
    e.MgsCoopPartnerRide = function()
        local function field(t, k)
            local ok, v = pcall(function() return t and t[k] end)
            if ok then return v end
        end
        local query = field(PlayerInfo, "OrCheckStatus")
        if type(query) ~= "function" then return 5 end
        local values = {}
        for _, name in ipairs({"ON_HELICOPTER", "ON_VEHICLE", "ON_HORSE"}) do
            local status = field(PlayerStatus, name)
            if type(status) ~= "number" or status < 0 or status > 2147483647 or status % 1 ~= 0 then return 5 end
            local ok, value = pcall(query, {status})
            if not ok or type(value) ~= "boolean" then return 5 end
            values[name] = value
        end
        if values.ON_HELICOPTER then return 1 end
        if values.ON_HORSE then return 3 end
        if values.ON_VEHICLE then return 2 end
        -- Normal release uses only these established public statuses.
        -- The private capacity receipt separately reads qualified native ride
        -- storage; its eight code pins and owner gates do not cover a call here.
        return 0
    end
end
