-- Included only after native archive mounting reports success.
do
    e.MgsCoop.archiveMounted = true
    e.MgsCoop.archiveLoadRequested = false
    if type(Script) == "table" and type(Script.LoadLibrary) == "function" then
        e.MgsCoop.archiveLoadRequested = true
        local ok = pcall(Script.LoadLibrary, "/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua")
        e.MgsCoop.archiveLoadCallSucceeded = ok
        -- Execution is proven separately by the archive script's own receipt.
        if type(loadstring) == "function" then
            if ok then loadstring("return 'MGS_COOP_ARCHIVE_LOAD_RETURNED_V1'")
            else loadstring("return 'MGS_COOP_ARCHIVE_LOAD_FAILED_V1'") end
        end
    end
    -- The game also uses dofile for scripts in its virtual filesystem. A
    -- protected direct read gives a useful failure if LoadLibrary only queues
    -- or silently declines a request at this point in startup.
    if type(dofile) == "function" then
        local ok, result = pcall(dofile, "/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua")
        e.MgsCoop.archiveDirectReadSucceeded = ok
        if type(loadstring) == "function" then
            if ok then loadstring("return 'MGS_COOP_ARCHIVE_FILE_RETURNED_V1'")
            else
                local detail = string.sub(tostring(result), 1, 160)
                detail = string.gsub(detail, "[\r\n]", " ")
                loadstring("return 'MGS_COOP_ARCHIVE_FILE_ERROR_V1' -- " .. detail)
            end
        end
    end
end
