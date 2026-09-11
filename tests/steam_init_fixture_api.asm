; Original inert API wrapper. Preserve the bool in AL and deliberately poison
; upper EAX, which a caller is not allowed to interpret as the bool result.
option casemap:none
EXTERN FixtureSteamInitBody:PROC
PUBLIC FixtureSteamInit
.code
FixtureSteamInit PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    call FixtureSteamInitBody
    movzx eax, al
    or eax, 0A5A50000h
    add rsp, 28h
    ret
FixtureSteamInit ENDP
END
