// guids.hpp — CLSID for the RivePeek preview handler and shared registration
// constants.
#pragma once
#include <initguid.h>
#include <guiddef.h>

// {7B2E9C14-5F3A-4D6B-9E21-3C8A7F0D5B42}  — RivePeek .riv Preview Handler.
DEFINE_GUID(CLSID_RivePreviewHandler,
            0x7b2e9c14, 0x5f3a, 0x4d6b, 0x9e, 0x21, 0x3c, 0x8a, 0x7f, 0x0d, 0x5b, 0x42);

// String forms used by the registrar (must match the GUID above).
#define RIVEPEEK_CLSID_STR L"{7B2E9C14-5F3A-4D6B-9E21-3C8A7F0D5B42}"

// Well-known 64-bit Preview Handler Surrogate Host (prevhost.exe). Setting this
// as the AppID on our CLSID makes Windows load the handler in that isolated
// process instead of explorer.exe.
#define RIVEPEEK_SURROGATE_APPID_STR L"{6d2b345a-3aa5-4614-b7ce-69ae4657d472}"

// Standard Shell preview-handler shell-extension interface GUID. The .riv file
// class points its ShellEx\<this> subkey at our CLSID.
#define SHELLEX_PREVIEWHANDLER_STR L"{8895b1c6-b41f-4c1c-a562-0d564250836f}"

#define RIVEPEEK_FRIENDLY_NAME L"RivePeek Rive (.riv) Preview Handler"
