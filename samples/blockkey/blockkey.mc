; Message table for blockkey.
;
; Compiled by mc.exe, turned into a resource by rc.exe and linked into
; blockkey.exe.  install-service.cmd points the event log source at the
; executable, so the Event Viewer shows these texts instead of "the
; description for event ID ... cannot be found".
;
; The numeric ids below are mirrored in blockkey.cpp (see the event_id enum
; there): keep the two in step when adding a message.

LanguageNames=(English=0x409:MSG00409)

MessageIdTypedef=DWORD

MessageId=1
SymbolicName=BLOCKKEY_INFORMATION
Language=English
blockkey: %1
.

MessageId=2
SymbolicName=BLOCKKEY_WARNING
Language=English
blockkey warning: %1
.

MessageId=3
SymbolicName=BLOCKKEY_ERROR
Language=English
blockkey error: %1
.
