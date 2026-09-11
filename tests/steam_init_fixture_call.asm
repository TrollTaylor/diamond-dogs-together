; Original AMD64 call-site fixture with real IAT CALL and unwind metadata.
; The exported labels intentionally enclose the CALL, not a callee's entry.
option casemap:none
EXTERN __imp_FixtureSteamInit:QWORD
PUBLIC FixtureCall
PUBLIC FixtureBeforeCall
PUBLIC FixtureAfterCall
PUBLIC FixtureIatAddress
.code
FixtureCall PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
FixtureBeforeCall LABEL BYTE
    call QWORD PTR [__imp_FixtureSteamInit]
FixtureAfterCall LABEL BYTE
    test al, al
    add rsp, 28h
    ret
FixtureCall ENDP
FixtureIatAddress PROC
    lea rax, __imp_FixtureSteamInit
    ret
FixtureIatAddress ENDP
END
