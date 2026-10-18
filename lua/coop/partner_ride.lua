
-- Exact retail PlayerStatus registers ON_HORSE/ON_VEHICLE/ON_HELICOPTER.
-- This producer adds no lifecycle wrapper or native call, and missing status
-- evidence means unknown (5), never an invented on-foot confirmation.
if type(e) == "table" then
    local function field(t, k)
        local ok, v = pcall(function() return t and t[k] end)
        if ok then return v end
    end
    local function integer(v, lo, hi) return type(v) == "number" and v == math.floor(v) and v >= lo and v <= hi end
    e.MgsCoopPartnerRide = function()
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
    -- The seated rider's sender (docs/anim-round1-seat.md): the local vehicle's
    -- GameObject id (vehicle type 50 only) and the local native seat, the
    -- GetRiderId index - 1 (the debug vehicle_seats census proved the layout),
    -- read once per vehicle and retried every 15 calls until the local player
    -- is listed. nil when not on a vehicle; seat 255 when unknown.
    local cache = {id = nil, seat = 255, wait = 0}
    e.MgsCoopPartnerRideSeat = function()
        local query, statusVeh = field(PlayerInfo, "OrCheckStatus"), field(PlayerStatus, "ON_VEHICLE")
        local statusHorse = field(PlayerStatus, "ON_HORSE")
        if type(query) ~= "function" then return nil end
        local onVeh = integer(statusVeh, 0, 2147483647) and select(2, pcall(query, {statusVeh})) == true
        local onHorse = integer(statusHorse, 0, 2147483647) and select(2, pcall(query, {statusHorse})) == true
        if not onVeh and not onHorse then cache.id = nil; return nil end
        local id = field(vars, "playerVehicleGameObjectId")
        if not integer(id, 0, 65534) or id == 65535 then cache.id = nil; return nil end
        if onHorse then
            cache.id = id; cache.seat = 0; return id, 0
        end
        if math.floor(id / 512) ~= 50 then cache.id = nil; return nil end
        if id ~= cache.id then cache.id, cache.seat, cache.wait = id, 255, 0 end
        if cache.seat == 255 then
            cache.wait = cache.wait - 1
            if cache.wait <= 0 then
                cache.wait = 15
                local send, byIndex, localIndex = field(GameObject, "SendCommand"), field(GameObject, "GetGameObjectIdByIndex"),
                    field(PlayerInfo, "GetLocalPlayerIndex")
                if type(send) == "function" and type(byIndex) == "function" and type(localIndex) == "function" then
                    local okIndex, index = pcall(localIndex)
                    local okId, me = false, nil
                    if okIndex and integer(index, 0, 3) then okId, me = pcall(byIndex, "TppPlayer2", index) end
                    local okRiders, riders = pcall(send, id, {id = "GetRiderId"})
                    if okId and integer(me, 0, 65534) and okRiders and type(riders) == "table" then
                        for i = 1, 4 do
                            if riders[i] == me then cache.seat = i - 1; break end
                        end
                    end
                end
            end
        end
        return id, cache.seat
    end
    -- The local PlayerStatus census of the receipt's st= (bit order fixed;
    -- a status the binding table lacks, or a refused query, stays unset).
    local STATUS_BITS = {"BINOCLE", "SUBJECT", "CBOX", "CBOX_EVADE", "TRASH_BOX", "TRASH_BOX_HALF_OPEN", "BEHIND", "CARRY",
        "CQC_CONTINUOUS", "ON_HORSE", "ON_VEHICLE", "ON_HELICOPTER"}
    e.MgsCoopPartnerStatusBits = function()
        local query = field(PlayerInfo, "OrCheckStatus")
        if type(query) ~= "function" then return 0 end
        local bits, place = 0, 1
        for _, name in ipairs(STATUS_BITS) do
            local status = field(PlayerStatus, name)
            if integer(status, 0, 2147483647) then
                local ok, value = pcall(query, {status})
                if ok and value == true then bits = bits + place end
            end
            place = place * 2
        end
        return bits
    end
end
