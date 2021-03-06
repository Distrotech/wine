/*
 * MACDRV initialization code
 *
 * Copyright 1998 Patrik Stridvall
 * Copyright 2000 Alexandre Julliard
 * Copyright 2011, 2012, 2013 Ken Thomases for CodeWeavers Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include "config.h"

#include <Security/AuthSession.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include "macdrv.h"
#include "winuser.h"
#include "winreg.h"
#include "wine/server.h"

WINE_DEFAULT_DEBUG_CHANNEL(macdrv);

#ifndef kIOPMAssertionTypePreventUserIdleDisplaySleep
#define kIOPMAssertionTypePreventUserIdleDisplaySleep CFSTR("PreventUserIdleDisplaySleep")
#endif
#ifndef kCFCoreFoundationVersionNumber10_7
#define kCFCoreFoundationVersionNumber10_7      635.00
#endif

#define IS_OPTION_TRUE(ch) \
    ((ch) == 'y' || (ch) == 'Y' || (ch) == 't' || (ch) == 'T' || (ch) == '1')

C_ASSERT(NUM_EVENT_TYPES <= sizeof(macdrv_event_mask) * 8);

DWORD thread_data_tls_index = TLS_OUT_OF_INDEXES;

int topmost_float_inactive = TOPMOST_FLOAT_INACTIVE_NONFULLSCREEN;
int capture_displays_for_fullscreen = 0;
BOOL skip_single_buffer_flushes = FALSE;
BOOL allow_vsync = TRUE;
BOOL allow_set_gamma = TRUE;
int left_option_is_alt = 0;
int right_option_is_alt = 0;


/**************************************************************************
 *              debugstr_cf
 */
const char* debugstr_cf(CFTypeRef t)
{
    CFStringRef s;
    const char* ret;

    if (!t) return "(null)";

    if (CFGetTypeID(t) == CFStringGetTypeID())
        s = t;
    else
        s = CFCopyDescription(t);
    ret = CFStringGetCStringPtr(s, kCFStringEncodingUTF8);
    if (ret) ret = debugstr_a(ret);
    if (!ret)
    {
        const UniChar* u = CFStringGetCharactersPtr(s);
        if (u)
            ret = debugstr_wn((const WCHAR*)u, CFStringGetLength(s));
    }
    if (!ret)
    {
        UniChar buf[200];
        int len = min(CFStringGetLength(s), sizeof(buf)/sizeof(buf[0]));
        CFStringGetCharacters(s, CFRangeMake(0, len), buf);
        ret = debugstr_wn(buf, len);
    }
    if (s != t) CFRelease(s);
    return ret;
}


/***********************************************************************
 *              set_app_icon
 */
static void set_app_icon(void)
{
    CFArrayRef images = create_app_icon_images();
    if (images)
    {
        macdrv_set_application_icon(images);
        CFRelease(images);
    }
}


/***********************************************************************
 *              get_config_key
 *
 * Get a config key from either the app-specific or the default config
 */
static inline DWORD get_config_key(HKEY defkey, HKEY appkey, const char *name,
                                   char *buffer, DWORD size)
{
    if (appkey && !RegQueryValueExA(appkey, name, 0, NULL, (LPBYTE)buffer, &size)) return 0;
    if (defkey && !RegQueryValueExA(defkey, name, 0, NULL, (LPBYTE)buffer, &size)) return 0;
    return ERROR_FILE_NOT_FOUND;
}


/***********************************************************************
 *              setup_options
 *
 * Set up the Mac driver options.
 */
static void setup_options(void)
{
    char buffer[MAX_PATH + 16];
    HKEY hkey, appkey = 0;
    DWORD len;

    /* @@ Wine registry key: HKCU\Software\Wine\Mac Driver */
    if (RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\Mac Driver", &hkey)) hkey = 0;

    /* open the app-specific key */

    len = GetModuleFileNameA(0, buffer, MAX_PATH);
    if (len && len < MAX_PATH)
    {
        HKEY tmpkey;
        char *p, *appname = buffer;
        if ((p = strrchr(appname, '/'))) appname = p + 1;
        if ((p = strrchr(appname, '\\'))) appname = p + 1;
        strcat(appname, "\\Mac Driver");
        /* @@ Wine registry key: HKCU\Software\Wine\AppDefaults\app.exe\Mac Driver */
        if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\AppDefaults", &tmpkey))
        {
            if (RegOpenKeyA(tmpkey, appname, &appkey)) appkey = 0;
            RegCloseKey(tmpkey);
        }
    }

    if (!get_config_key(hkey, appkey, "WindowsFloatWhenInactive", buffer, sizeof(buffer)))
    {
        if (!strcmp(buffer, "none"))
            topmost_float_inactive = TOPMOST_FLOAT_INACTIVE_NONE;
        else if (!strcmp(buffer, "all"))
            topmost_float_inactive = TOPMOST_FLOAT_INACTIVE_ALL;
        else
            topmost_float_inactive = TOPMOST_FLOAT_INACTIVE_NONFULLSCREEN;
    }

    if (!get_config_key(hkey, appkey, "CaptureDisplaysForFullscreen", buffer, sizeof(buffer)))
        capture_displays_for_fullscreen = IS_OPTION_TRUE(buffer[0]);

    if (!get_config_key(hkey, appkey, "SkipSingleBufferFlushes", buffer, sizeof(buffer)))
        skip_single_buffer_flushes = IS_OPTION_TRUE(buffer[0]);

    if (!get_config_key(hkey, appkey, "AllowVerticalSync", buffer, sizeof(buffer)))
        allow_vsync = IS_OPTION_TRUE(buffer[0]);

    if (!get_config_key(hkey, appkey, "AllowSetGamma", buffer, sizeof(buffer)))
        allow_set_gamma = IS_OPTION_TRUE(buffer[0]);

    if (!get_config_key(hkey, appkey, "LeftOptionIsAlt", buffer, sizeof(buffer)))
        left_option_is_alt = IS_OPTION_TRUE(buffer[0]);
    if (!get_config_key(hkey, appkey, "RightOptionIsAlt", buffer, sizeof(buffer)))
        right_option_is_alt = IS_OPTION_TRUE(buffer[0]);

    if (appkey) RegCloseKey(appkey);
    if (hkey) RegCloseKey(hkey);
}


/***********************************************************************
 *              process_attach
 */
static BOOL process_attach( HINSTANCE instance )
{
    SessionAttributeBits attributes;
    OSStatus status;

    status = SessionGetInfo(callerSecuritySession, NULL, &attributes);
    if (status != noErr || !(attributes & sessionHasGraphicAccess))
        return FALSE;

    setup_options();

    if ((thread_data_tls_index = TlsAlloc()) == TLS_OUT_OF_INDEXES) return FALSE;

    macdrv_err_on = ERR_ON(macdrv);
    if (macdrv_start_cocoa_app(GetTickCount64()))
    {
        ERR("Failed to start Cocoa app main loop\n");
        return FALSE;
    }

    set_app_icon();
    macdrv_clipboard_process_attach();
    IME_RegisterClasses( instance );

    return TRUE;
}


/***********************************************************************
 *              thread_detach
 */
static void thread_detach(void)
{
    struct macdrv_thread_data *data = macdrv_thread_data();

    if (data)
    {
        macdrv_destroy_event_queue(data->queue);
        if (data->keyboard_layout_uchr)
            CFRelease(data->keyboard_layout_uchr);
        HeapFree(GetProcessHeap(), 0, data);
    }
}


/***********************************************************************
 *              set_queue_display_fd
 *
 * Store the event queue fd into the message queue
 */
static void set_queue_display_fd(int fd)
{
    HANDLE handle;
    int ret;

    if (wine_server_fd_to_handle(fd, GENERIC_READ | SYNCHRONIZE, 0, &handle))
    {
        MESSAGE("macdrv: Can't allocate handle for event queue fd\n");
        ExitProcess(1);
    }
    SERVER_START_REQ(set_queue_fd)
    {
        req->handle = wine_server_obj_handle(handle);
        ret = wine_server_call(req);
    }
    SERVER_END_REQ;
    if (ret)
    {
        MESSAGE("macdrv: Can't store handle for event queue fd\n");
        ExitProcess(1);
    }
    CloseHandle(handle);
}


/***********************************************************************
 *              macdrv_init_thread_data
 */
struct macdrv_thread_data *macdrv_init_thread_data(void)
{
    struct macdrv_thread_data *data = macdrv_thread_data();

    if (data) return data;

    if (!(data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data))))
    {
        ERR("could not create data\n");
        ExitProcess(1);
    }

    if (!(data->queue = macdrv_create_event_queue(macdrv_handle_event)))
    {
        ERR("macdrv: Can't create event queue.\n");
        ExitProcess(1);
    }

    data->keyboard_layout_uchr = macdrv_copy_keyboard_layout(&data->keyboard_type, &data->iso_keyboard);
    macdrv_compute_keyboard_layout(data);

    set_queue_display_fd(macdrv_get_event_queue_fd(data->queue));
    TlsSetValue(thread_data_tls_index, data);

    return data;
}


/***********************************************************************
 *              DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    BOOL ret = TRUE;

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        ret = process_attach( hinst );
        break;
    case DLL_THREAD_DETACH:
        thread_detach();
        break;
    }
    return ret;
}

/***********************************************************************
 *              SystemParametersInfo (MACDRV.@)
 */
BOOL CDECL macdrv_SystemParametersInfo( UINT action, UINT int_param, void *ptr_param, UINT flags )
{
    switch (action)
    {
    case SPI_GETSCREENSAVEACTIVE:
        if (ptr_param)
        {
            CFDictionaryRef assertionStates;
            IOReturn status = IOPMCopyAssertionsStatus(&assertionStates);
            if (status == kIOReturnSuccess)
            {
                CFNumberRef count = CFDictionaryGetValue(assertionStates, kIOPMAssertionTypeNoDisplaySleep);
                CFNumberRef count2 = CFDictionaryGetValue(assertionStates, kIOPMAssertionTypePreventUserIdleDisplaySleep);
                long longCount = 0, longCount2 = 0;

                if (count)
                    CFNumberGetValue(count, kCFNumberLongType, &longCount);
                if (count2)
                    CFNumberGetValue(count2, kCFNumberLongType, &longCount2);

                *(BOOL *)ptr_param = !longCount && !longCount2;
                CFRelease(assertionStates);
            }
            else
            {
                WARN("Could not determine screen saver state, error code %d\n", status);
                *(BOOL *)ptr_param = TRUE;
            }
            return TRUE;
        }
        break;

    case SPI_SETSCREENSAVEACTIVE:
        {
            static IOPMAssertionID powerAssertion = kIOPMNullAssertionID;
            if (int_param)
            {
                if (powerAssertion != kIOPMNullAssertionID)
                {
                    IOPMAssertionRelease(powerAssertion);
                    powerAssertion = kIOPMNullAssertionID;
                }
            }
            else if (powerAssertion == kIOPMNullAssertionID)
            {
                CFStringRef assertName;
                /*Are we running Lion or later?*/
                if (kCFCoreFoundationVersionNumber >= kCFCoreFoundationVersionNumber10_7)
                    assertName = kIOPMAssertionTypePreventUserIdleDisplaySleep;
                else
                    assertName = kIOPMAssertionTypeNoDisplaySleep;
                IOPMAssertionCreateWithName( assertName, kIOPMAssertionLevelOn,
                                             CFSTR("Wine Process requesting no screen saver"),
                                             &powerAssertion);
            }
        }
        break;
    }
    return FALSE;
}
