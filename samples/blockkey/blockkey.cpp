/* blockkey: swallow a single configurable key stroke, for instance the right
 * Alt key of a laptop keyboard whose switch is broken and keeps repeating.
 *
 * The default target is the right Alt key (AltGr): keyboards report it as scan
 * code 0x38 carrying the E0 prefix, while left Alt is the very same scan code
 * without that prefix, so the two are told apart exactly and only the broken
 * one is dropped.  Every other stroke is passed through untouched.
 *
 * As with the other samples, the Interception driver must be installed.  Check
 * the README of this directory for building and usage.
 *
 *     blockkey                          swallow right Alt on every keyboard
 *     blockkey --hardware-id Laptop     swallow it on one keyboard only
 *     blockkey --probe                  print every keystroke, drop nothing
 *     blockkey --list                   list the keyboards, then exit
 *     blockkey --service                run under the service control manager
 *     blockkey --help                   full option list
 *
 * If the key was already stuck when the program starts, Windows holds the
 * modifier down, so one key up for it is sent to every keyboard first.
 *
 * Quit at any time with left Ctrl + left Shift + Q.  A service, which has no
 * console and no keyboard of its own, is stopped with "sc stop blockkey"
 * instead, and writes what it does to the Windows event log rather than to a
 * file of its own.
 *
 * Whenever this program already has the rights to do so - it runs elevated, or
 * as the service, which the service control manager starts as SYSTEM - it also
 * restricts the Interception driver's control devices to SYSTEM and
 * Administrators, silently, so that no unelevated process can watch or inject
 * keystrokes any more.  Nothing has to be restarted for that: the devices are
 * restricted in place.  --unlock puts the permissive rights back, --no-lockdown
 * leaves them alone.  See the README for what that means for unelevated runs.
 */

#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

/* winnt.h defines a handful of STATUS_ codes that ntstatus.h defines again.
 * This keeps them out of the way, which is what the SDK documentation
 * prescribes for using NT status values in a user mode program. */
#ifndef WIN32_NO_STATUS
#define WIN32_NO_STATUS
#endif

#include <windows.h>

#ifdef WIN32_NO_STATUS
#undef WIN32_NO_STATUS
#endif

#include <ntstatus.h>
#include <winternl.h>
#include <aclapi.h>
#include <sddl.h>

#include <utils.h>
#include <interception.h>

using namespace std;

/* The native calls that fold the keyboard and mouse device names, and the flag
 * that makes those links outlive this program, are only declared by the
 * driver development headers, so they are spelled out here.  They are stable
 * and exported by ntdll.dll on every supported Windows. */
#ifndef OBJ_PERMANENT
#define OBJ_PERMANENT 0x00000010
#endif

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040
#endif

#ifndef SYMBOLIC_LINK_ALL_ACCESS
#define SYMBOLIC_LINK_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x0001)
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

extern "C" NTSTATUS WINAPI NtCreateSymbolicLinkObject(PHANDLE link_handle,
                                                      ACCESS_MASK desired_access,
                                                      POBJECT_ATTRIBUTES object_attributes,
                                                      PUNICODE_STRING target_name);
extern "C" NTSTATUS WINAPI NtOpenSymbolicLinkObject(PHANDLE link_handle,
                                                    ACCESS_MASK desired_access,
                                                    POBJECT_ATTRIBUTES object_attributes);
extern "C" NTSTATUS WINAPI NtMakeTemporaryObject(HANDLE handle);

namespace {

/* Scan codes of the strokes this program inspects itself to recognize the quit
 * chord.  E0 prefixed strokes reuse the same codes (right ctrl is E0 0x1D). */
namespace scancode {
    enum {
        q          = 0x10,
        leftctrl   = 0x1D,
        leftshift  = 0x2A,
        rightshift = 0x36
    };
}

/* Scan code swallowed by default: right Alt, which carries the E0 prefix. */
const unsigned short right_alt_scan_code = 0x38;

/* How the E0 prefix of a stroke must match the configured key. */
enum E0Mode {
    e0_any  = -1,
    e0_no   =  0,
    e0_only =  1
};

struct Options {
    unsigned short code;    /* scan code to swallow */
    int e0;                 /* E0Mode */
    int device;             /* 0: every keyboard, otherwise 1..INTERCEPTION_MAX_KEYBOARD */
    wstring hardware_id;    /* empty: every keyboard */
    bool release;           /* release the key once at start up */
    bool release_only;      /* send that release and exit, swallow nothing */
    bool service;           /* run under the service control manager */
    bool list;
    bool probe;
    bool verbose;
    bool quiet;
    bool no_lockdown;       /* leave the driver's device access rights alone */
    bool unlock_driver;     /* give every user access to the driver again */
    bool no_symlinks;       /* leave the driver's device names alone */
    bool clear_symlinks;    /* remove the folded device names */
};

Options default_options() {
    Options options;
    options.code = right_alt_scan_code;
    options.e0 = e0_only;
    options.device = 0;
    options.hardware_id.clear();
    options.release = true;
    options.release_only = false;
    options.service = false;
    options.list = false;
    options.probe = false;
    options.verbose = false;
    options.quiet = false;
    options.no_lockdown = false;
    options.unlock_driver = false;
    options.no_symlinks = false;
    options.clear_symlinks = false;
    return options;
}

/* Defined with the driver access helpers further down, and needed as soon as a
 * context cannot be created. */
string driver_access_hint();

bool is_key_up(const InterceptionKeyStroke &kstroke) {
    return (kstroke.state & INTERCEPTION_KEY_UP) != 0;
}

bool is_e0_stroke(const InterceptionKeyStroke &kstroke) {
    return (kstroke.state & INTERCEPTION_KEY_E0) != 0;
}

/* Device number shown to the user, the same one --list prints. */
int keyboard_number(InterceptionDevice device) {
    return device - INTERCEPTION_KEYBOARD(0) + 1;
}

bool is_ctrl(const InterceptionKeyStroke &kstroke) {
    return kstroke.code == scancode::leftctrl;
}

bool is_shift(const InterceptionKeyStroke &kstroke) {
    return kstroke.code == scancode::leftshift || kstroke.code == scancode::rightshift;
}

void update_modifiers(const InterceptionKeyStroke &kstroke,
                      bool &ctrl_down, bool &shift_down) {
    if (is_ctrl(kstroke)) ctrl_down = !is_key_up(kstroke);
    if (is_shift(kstroke)) shift_down = !is_key_up(kstroke);
}

bool is_quit_chord(const InterceptionKeyStroke &kstroke,
                   bool ctrl_down, bool shift_down) {
    return ctrl_down && shift_down && kstroke.code == scancode::q;
}

/* Does the stroke belong to the key the user asked to swallow? */
bool key_matches(const InterceptionKeyStroke &kstroke, const Options &options) {
    if (kstroke.code != options.code) return false;
    if (options.e0 == e0_only) return is_e0_stroke(kstroke);
    if (options.e0 == e0_no) return !is_e0_stroke(kstroke);
    return true;
}

wchar_t fold(wchar_t character) {
    return (wchar_t)towlower(character);
}

/* Case insensitive substring search, used on device hardware ids. */
bool contains_case_insensitive(const wchar_t *text, const wstring &needle) {
    if (needle.empty()) return true;
    if (text == 0) return false;

    for (const wchar_t *start = text; *start != L'\0'; ++start) {
        size_t matched = 0;

        while (matched < needle.size() && start[matched] != L'\0' &&
               fold(start[matched]) == fold(needle[matched]))
            ++matched;

        if (matched == needle.size()) return true;
    }

    return false;
}

bool device_matches(const Options &options, InterceptionDevice device,
                    const wstring &hardware_id) {
    if (options.device != 0 && device != INTERCEPTION_KEYBOARD(options.device - 1))
        return false;

    return contains_case_insensitive(hardware_id.c_str(), options.hardware_id);
}

string number_text(unsigned long number) {
    ostringstream text;

    text << number;

    return text.str();
}

/* Key up strokes that undo the configured key, one per E0 variant. */
vector<InterceptionKeyStroke> release_strokes(const Options &options) {    vector<InterceptionKeyStroke> strokes;

    if (options.e0 != e0_no) {
        InterceptionKeyStroke stroke = {options.code, INTERCEPTION_KEY_UP | INTERCEPTION_KEY_E0, 0};
        strokes.push_back(stroke);
    }

    if (options.e0 != e0_only) {
        InterceptionKeyStroke stroke = {options.code, INTERCEPTION_KEY_UP, 0};
        strokes.push_back(stroke);
    }

    return strokes;
}

/* A key that is already stuck when this program starts is also held down as far
 * as Windows is concerned, and swallowing its strokes from then on would keep
 * the modifier latched forever.  One key up per keyboard device releases it;
 * an unexpected key up for a key that is not held is simply ignored. */
int release_target_key(InterceptionContext context, const Options &options) {
    vector<InterceptionKeyStroke> strokes = release_strokes(options);
    int keyboards = 0;

    if (strokes.empty()) return 0;

    for (int number = 1; number <= INTERCEPTION_MAX_KEYBOARD; ++number)
        if (interception_send(context, INTERCEPTION_KEYBOARD(number - 1),
                              (InterceptionStroke *)&strokes[0],
                              (unsigned int)strokes.size()) > 0)
            ++keyboards;

    return keyboards;
}

/* Emergency stop for a modifier that a broken key left stuck: Windows then
 * believes Alt is still held, which turns Enter in a console window into
 * Alt+Enter and toggles fullscreen instead of running the command.  This sends
 * the key up once and exits, swallowing nothing, so it also works while the
 * service is running and needs no working keyboard of its own. */
int release_stuck_key(const Options &options) {
    Options release = options;
    InterceptionContext context;
    int keyboards;

    /* Both E0 variants, whichever variant was configured as the target. */
    release.e0 = e0_any;
    release.service = false;

    context = interception_create_context();
    if (!context) {
        cerr << "blockkey: cannot reach the Interception driver; " << driver_access_hint() << endl;
        return 1;
    }

    keyboards = release_target_key(context, release);

    interception_destroy_context(context);

    if (!options.quiet)
        cout << "blockkey: sent the key up to " << number_text((unsigned long)keyboards)
             << " keyboard device(s); a stuck key should be released now" << endl;

    return 0;
}

struct HardwareIdCache {
    bool fetched[INTERCEPTION_MAX_DEVICE + 1];
    wstring value[INTERCEPTION_MAX_DEVICE + 1];

    HardwareIdCache() {
        for (int i = 0; i <= INTERCEPTION_MAX_DEVICE; ++i) fetched[i] = false;
    }
};

/* Hardware id of a device, as reported by the driver, cached per device.  The
 * driver answers with the length in bytes of the wide string it wrote. */
wstring hardware_id_of(InterceptionContext context, InterceptionDevice device,
                       HardwareIdCache &cache) {
    const size_t capacity = 512;

    if (device < 1 || device > INTERCEPTION_MAX_DEVICE) return wstring();
    if (cache.fetched[device]) return cache.value[device];

    wchar_t buffer[capacity];
    buffer[0] = L'\0';

    unsigned int length = interception_get_hardware_id(
        context, device, buffer, (unsigned int)((capacity - 1) * sizeof(wchar_t)));

    size_t characters = length / sizeof(wchar_t);
    if (characters > capacity - 1) characters = capacity - 1;
    buffer[characters] = L'\0';

    cache.fetched[device] = true;
    cache.value[device] = buffer;

    return cache.value[device];
}

string narrow(const wstring &text) {
    string result;

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned int character = (unsigned int)text[i];
        result += (character >= 0x20 && character < 0x7F) ? (char)character : '?';
    }

    return result;
}

string describe_target(const Options &options) {
    ostringstream text;

    text << "scan code 0x" << hex << uppercase << setw(2) << setfill('0')
         << options.code << dec;

    if (options.e0 == e0_only)
        text << " with the E0 prefix";
    else if (options.e0 == e0_no)
        text << " without the E0 prefix";
    else
        text << " with or without the E0 prefix";

    if (options.device != 0)
        text << " on keyboard " << options.device;
    else
        text << " on every keyboard";

    if (!options.hardware_id.empty())
        text << " whose hardware id contains \"" << narrow(options.hardware_id) << "\"";

    return text.str();
}

string describe_stroke(const InterceptionKeyStroke &kstroke,
                       InterceptionDevice device, const wstring &hardware_id) {
    ostringstream text;

    text << "keyboard " << keyboard_number(device)
         << "  code 0x" << hex << uppercase << setw(2) << setfill('0') << kstroke.code
         << "  state 0x" << setw(4) << kstroke.state
         << (is_key_up(kstroke) ? "  up" : "  down")
         << (is_e0_stroke(kstroke) ? "  E0" : "");

    if (!hardware_id.empty()) text << "  " << narrow(hardware_id);

    return text.str();
}

void list_keyboards(InterceptionContext context, HardwareIdCache &cache) {
    int found = 0;

    cout << "keyboard devices known to the Interception driver:" << endl;

    for (int number = 1; number <= INTERCEPTION_MAX_KEYBOARD; ++number) {
        wstring hardware_id = hardware_id_of(context, INTERCEPTION_KEYBOARD(number - 1), cache);

        if (hardware_id.empty()) continue;

        ++found;
        cout << "  " << number << ": " << narrow(hardware_id) << endl;
    }

    if (found == 0)
        cout << "  (none; press a key on the keyboard, then run --list again)" << endl;

    cout << "Pass the part that identifies one keyboard to --hardware-id." << endl;
}

void print_usage(ostream &out) {
    out << "blockkey - swallow a single keyboard key\n"
        << "\n"
        << "usage: blockkey [options]\n"
        << "\n"
        << "options:\n"
        << "  --key <scan code>     scan code to swallow, such as 0x38 (default 0x38)\n"
        << "  --e0                  require the E0 prefix on the swallowed key (default)\n"
        << "  --no-e0               forbid the E0 prefix, which targets the left variant\n"
        << "  --any-e0              ignore the E0 prefix, which targets both variants\n"
        << "  --device <number>     act on that keyboard only (1 to "
        << INTERCEPTION_MAX_KEYBOARD << ")\n"
        << "  --hardware-id <text>  act only on keyboards whose hardware id contains it\n"
        << "  --release             release the key once at start up (default)\n"
        << "  --no-release          never send anything, only swallow\n"
        << "  --release-only        send the key up once and exit: clears a modifier\n"
        << "                        that a broken key left stuck down\n"
        << "  --probe               print every keystroke and swallow nothing\n"
        << "  --list                list the keyboard devices, then exit\n"
        << "  --service             run as a Windows service, started by the service\n"
        << "                        control manager, not from a command line\n"
        << "  --verbose             print every swallowed keystroke\n"
        << "  --quiet               print nothing but errors\n"
        << "  --no-lockdown         leave the driver's device access rights alone\n"
        << "  --unlock              give every user access to the driver again, then\n"
        << "                        exit instead of swallowing a key\n"
        << "  --no-symlinks         leave the driver's device names alone\n"
        << "  --clear-symlinks      remove the folded device names, then exit\n"
        << "  -h, --help            print this help\n"
        << "\n"
        << "Right Alt is scan code 0x38 with the E0 prefix, left Alt the same scan\n"
        << "code without it.  Quit with left Ctrl + left Shift + Q.\n"
        << "\n"
        << "Started with administrator rights, or as the service, this program also\n"
        << "keeps the driver in shape, silently and without a reboot:\n"
        << "  - its control devices are restricted to SYSTEM and Administrators, so\n"
        << "    no unelevated process can read or inject keystrokes any more\n"
        << "    (unelevated runs then need administrator rights themselves),\n"
        << "  - the keyboard and mouse device names are folded, so a device that is\n"
        << "    unplugged, replugged or resumed from sleep cannot end up outside the\n"
        << "    range the driver handles, which would leave it dead until a reboot.\n"
        << "--unlock, --no-lockdown, --clear-symlinks and --no-symlinks turn that off\n"
        << "or undo it, one half at a time.\n";
}

wstring widen(const char *text) {
    wstring wide;

    for (const char *letter = text; letter != 0 && *letter != '\0'; ++letter)
        wide += (wchar_t)(unsigned char)*letter;

    return wide;
}

bool parse_number(const char *text, unsigned long &value) {
    char *end = 0;

    value = strtoul(text, &end, 0);

    return end != 0 && end != text && *end == '\0';
}

/* Returns 0 to continue, 1 when help was asked for, 2 on a bad option. */
int parse_options(int argc, char *argv[], Options &options) {
    for (int i = 1; i < argc; ++i) {
        string argument = argv[i];

        if (argument == "--key") {
            unsigned long code;

            if (++i >= argc || !parse_number(argv[i], code) || code == 0 || code > 0xFFFF) {
                cerr << "blockkey: --key needs a scan code, such as 0x38" << endl;
                return 2;
            }

            options.code = (unsigned short)code;
        } else if (argument == "--e0") {
            options.e0 = e0_only;
        } else if (argument == "--no-e0") {
            options.e0 = e0_no;
        } else if (argument == "--any-e0") {
            options.e0 = e0_any;
        } else if (argument == "--device") {
            unsigned long number;

            if (++i >= argc || !parse_number(argv[i], number) ||
                number < 1 || number > INTERCEPTION_MAX_KEYBOARD) {
                cerr << "blockkey: --device needs a number between 1 and "
                     << INTERCEPTION_MAX_KEYBOARD << endl;
                return 2;
            }

            options.device = (int)number;
        } else if (argument == "--hardware-id" || argument == "--hwid") {
            if (++i >= argc) {
                cerr << "blockkey: --hardware-id needs the text to look for" << endl;
                return 2;
            }

            options.hardware_id = widen(argv[i]);
        } else if (argument == "--list") {
            options.list = true;
        } else if (argument == "--service") {
            options.service = true;
        } else if (argument == "--release") {
            options.release = true;
        } else if (argument == "--no-release") {
            options.release = false;
        } else if (argument == "--release-only") {
            options.release_only = true;
        } else if (argument == "--probe") {
            options.probe = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--no-lockdown") {
            options.no_lockdown = true;
        } else if (argument == "--unlock") {
            options.unlock_driver = true;
        } else if (argument == "--no-symlinks") {
            options.no_symlinks = true;
        } else if (argument == "--clear-symlinks") {
            options.clear_symlinks = true;
        } else if (argument == "-h" || argument == "--help") {
            return 1;
        } else {
            cerr << "blockkey: unknown option " << argument << endl;
            return 2;
        }
    }

    return 0;
}

/* --------------------------------------------------------- event log -- */

/* Message ids defined in blockkey.mc, which is compiled into the executable
 * and registered as the event source by install-service.cmd. */
enum EventId {
    event_information = 1,
    event_warning     = 2,
    event_error       = 3
};

const char *event_source_name = "blockkey";

/* Opened once per service run; 0 when the event log cannot be used. */
HANDLE event_source = 0;

bool open_event_source() {
    if (event_source) return true;

    event_source = RegisterEventSourceA(0, event_source_name);

    return event_source != 0;
}

void close_event_source() {
    if (event_source) DeregisterEventSource(event_source);

    event_source = 0;
}

bool report_event(WORD type, DWORD id, const string &text) {
    LPCSTR strings[1];

    if (!event_source) return false;

    strings[0] = text.c_str();

    return ReportEventA(event_source, type, 0, id, 0, 1, 0, strings, 0) != 0;
}

/* ------------------------------------------------- driver access rights -- */

/* The Interception driver creates its twenty control devices with a DACL that
 * grants Everyone generic read and write, so any local process - including one
 * running at low integrity, such as a sandboxed browser renderer - can watch
 * every keystroke and inject its own.  Changing that needs no driver change and
 * no reboot, only the right to write the DACL, which SYSTEM and Administrators
 * already have on these devices.
 *
 * So whenever a run of this program has those rights anyway, it restricts the
 * devices to SYSTEM and Administrators and says nothing while doing so: the
 * point is that this program keeps working exactly as before while everything
 * that is not elevated loses access.  --unlock puts the permissive DACL back,
 * --no-lockdown leaves the devices alone. */

const char *restricted_device_sddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)";
const char *permissive_device_sddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)";

enum DriverAccessAction {
    access_leave_alone = 0,
    access_restrict,
    access_open_up
};

/* What this run should do to the driver's access rights.  A service is started
 * by the service control manager as a SYSTEM account, which is not "elevated"
 * in the UAC sense of a filtered administrator token, yet may change the DACL
 * all the same. */
DriverAccessAction driver_access_action(const Options &options, bool privileged) {
    if (options.unlock_driver) return access_open_up;
    if (options.no_lockdown) return access_leave_alone;
    if (privileged) return access_restrict;

    return access_leave_alone;
}

/* True when the effective token is an administrator one: the membership check
 * looks at the filtered token, so an administrator who did not confirm the UAC
 * prompt is not counted. */
bool process_is_elevated() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = 0;
    BOOL member = FALSE;
    bool elevated = false;

    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators))
        return false;

    if (CheckTokenMembership(0, administrators, &member)) elevated = member != FALSE;

    FreeSid(administrators);

    return elevated;
}

/* The library opens "\\.\interception00" up to "...19"; the same names are
 * spelled out here by hand, so that nothing has to be formatted. */
string device_name_of(int index) {
    string name = "\\\\.\\interception";

    name += (char)('0' + index / 10);
    name += (char)('0' + index % 10);

    return name;
}

/* Applies one DACL to all twenty control devices.  Returns how many of them
 * were changed, and in failure what went wrong first. */
int set_device_access(const char *sddl, string &failure) {
    PSECURITY_DESCRIPTOR descriptor = 0;
    PACL dacl = 0;
    BOOL dacl_present = FALSE;
    BOOL dacl_defaulted = FALSE;
    int changed = 0;

    failure.clear();

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            sddl, SDDL_REVISION_1, &descriptor, 0)) {
        failure = "cannot build a security descriptor, error " +
                  number_text((unsigned long)GetLastError());

        return 0;
    }

    if (!GetSecurityDescriptorDacl(descriptor, &dacl_present, &dacl, &dacl_defaulted) ||
        !dacl_present) {
        LocalFree(descriptor);
        failure = "the security descriptor carries no DACL";

        return 0;
    }

    for (int index = 0; index < INTERCEPTION_MAX_DEVICE; ++index) {
        string name = device_name_of(index);
        HANDLE device = CreateFileA(name.c_str(), WRITE_DAC | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        DWORD result;

        if (device == INVALID_HANDLE_VALUE) {
            if (failure.empty())
                failure = "cannot open " + name + ", error " +
                          number_text((unsigned long)GetLastError());

            continue;
        }

        result = SetSecurityInfo(device, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, 0, 0, dacl, 0);
        CloseHandle(device);

        if (result == ERROR_SUCCESS)
            ++changed;
        else if (failure.empty())
            failure = "cannot set the access rights of " + name + ", error " +
                      number_text((unsigned long)result);
    }

    LocalFree(descriptor);

    return changed;
}

/* Why the Interception driver could not be used, in the words the user needs:
 * without this a driver restricted to administrators looks exactly like one
 * that is not installed at all. */
string driver_access_hint() {
    HANDLE probe = CreateFileA(device_name_of(0).c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);

    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);

        return "install it and run this program as administrator";
    }

    if (GetLastError() == ERROR_ACCESS_DENIED)
        return "its devices are restricted to administrators, so run this program as"
               " administrator, or open them up again with an elevated"
               " \"blockkey --unlock\"";

    return "install it and run this program as administrator";
}

/* ------------------------------------------------------------ logging -- */

string timestamp() {
    time_t now = time(0);
    struct tm *parts = localtime(&now);
    char text[32];

    if (!parts || strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", parts) == 0)
        return string("0000-00-00 00:00:00");

    return string(text);
}

/* Only used when a service cannot reach the event log, which is what Windows
 * normally journals for us, keeping the log size under system control. */
string log_file_path() {
    char program_data[MAX_PATH];
    DWORD length = GetEnvironmentVariableA("ProgramData", program_data, sizeof(program_data));
    string directory = (length > 0 && (size_t)length < sizeof(program_data))
                           ? string(program_data)
                           : string("C:\\ProgramData");

    directory += "\\blockkey";
    CreateDirectoryA(directory.c_str(), 0);

    return directory + "\\blockkey.log";
}

/* Everything the stroke handling needs, shared by the console loop and the
 * service loop. */
struct Session {
    Options options;
    HardwareIdCache cache;
    InterceptionContext context;
    bool ctrl_down;
    bool shift_down;
    unsigned long swallowed;

    Session()
        : context(0), ctrl_down(false), shift_down(false), swallowed(0) {
        options = default_options();
    }

    void log(const string &line) {
        write_line(EVENTLOG_INFORMATION_TYPE, event_information, line);
    }

    void log_warning(const string &line) {
        write_line(EVENTLOG_WARNING_TYPE, event_warning, line);
    }

    void log_error(const string &line) {
        write_line(EVENTLOG_ERROR_TYPE, event_error, line);
    }

    void write_line(WORD type, DWORD id, const string &line) {
        if (!options.service) {
            if (!options.quiet) cout << line << endl;

            return;
        }

        if (report_event(type, id, line)) return;

        /* The event log is normally there, this only keeps the messages when
         * it is not. */
        ofstream file(log_file_path().c_str(), ios::app);
        if (file) file << timestamp() << " " << line << endl;
    }
};

/* Restricts, or opens up, the driver's control devices when this run already
 * has the rights to do so.  Silent while it works; returns false when the
 * requested change could not be applied to all twenty devices. */
bool apply_driver_access_policy(Session &session, bool service_mode) {
    DriverAccessAction action =
        driver_access_action(session.options, service_mode || process_is_elevated());
    string failure;
    int changed;

    if (action == access_leave_alone) return true;

    changed = set_device_access(
        (action == access_restrict) ? restricted_device_sddl : permissive_device_sddl, failure);

    if (changed == INTERCEPTION_MAX_DEVICE) {
        /* Restricting is deliberately silent, only --unlock announces itself. */
        if (action == access_open_up)
            session.log("blockkey: the Interception driver is open to every user again");

        return true;
    }

    string detail = number_text((unsigned long)changed) + " of " +
                    number_text((unsigned long)INTERCEPTION_MAX_DEVICE) + " devices";

    if (!failure.empty()) detail += ": " + failure;

    if (action == access_restrict)
        session.log_warning("blockkey: could not restrict the Interception driver to"
                            " administrators, " + detail);
    else
        session.log_error("blockkey: could not open up the Interception driver, " + detail);

    return false;
}

/* -------------------------------------------------- device name folding -- */

/* The free build of the Interception driver only ever handles the first ten
 * KeyboardClass and PointerClass devices.  Windows numbers those names afresh
 * for every enumeration, so after a few unplugs, replugs or resumes from sleep
 * a device ends up outside that range: the filter has nothing to attach to, the
 * strokes it swallows never reach the class driver either, and that keyboard or
 * mouse stays dead until the next reboot - even with no interceptor running
 * (oblitum/Interception issues 25 and 93).
 *
 * The repair is to pre-create symbolic links that fold every higher name back
 * onto the first ten, so that wherever Windows counts up to, the name resolves
 * to a device the driver does handle:
 *
 *     \Device\KeyboardClass10..999  ->  \Device\KeyboardClass0..9
 *     \Device\PointerClass10..999   ->  \Device\PointerClass0..9
 *
 * The links are permanent kernel objects: they outlive this program and last
 * until the machine reboots, which is why the service recreates them at every
 * start.  The numbering also starts over with each boot, so folding the names
 * once per boot covers every enumeration of that session.
 *
 * This is the same repair the third party "interception-driver-fix" applies.
 * It needs no driver change and no reboot. */

const int class_device_count = 1000;

/* Links that could not be made permanent, kept open so that they stay alive
 * for as long as this program runs. */
vector<HANDLE> class_links_held_open;

string decimal_text(unsigned long value) {
    string text;

    if (value == 0) return string("0");

    while (value > 0) {
        text = string(1, (char)('0' + (int)(value % 10))) + text;
        value /= 10;
    }

    return text;
}

string hex_text(unsigned long value) {
    const char *digits = "0123456789ABCDEF";
    string text;
    bool started = false;

    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned long part = (value >> shift) & 0xF;

        if (part != 0 || started || shift == 0) {
            text += digits[part];
            started = true;
        }
    }

    return text;
}

/* The native calls take wide strings; device names are plain ASCII. */
wstring wide_text(const string &text) {
    wstring wide;

    for (size_t i = 0; i < text.size(); ++i) wide += (wchar_t)(unsigned char)text[i];

    return wide;
}

/* \Device\KeyboardClass10, \Device\PointerClass7, ... */
string class_device_name(const char *class_name, int index) {
    string name = "\\Device\\";

    name += class_name;

    return name + decimal_text((unsigned long)index);
}

/* How many names the folding covers, for one class. */
int folded_class_device_count(int count) {
    return (count / 10 - 1) * 10;
}

/* OBJ_PERMANENT needs SeCreatePermanentPrivilege: a service account has it
 * enabled already, an administrator token usually has it but disabled. */
bool enable_permanent_object_privilege() {
    HANDLE token = 0;
    TOKEN_PRIVILEGES privileges;
    LUID identifier;
    bool enabled = false;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    if (LookupPrivilegeValueA(0, SE_CREATE_PERMANENT_NAME, &identifier)) {
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = identifier;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (AdjustTokenPrivileges(token, FALSE, &privileges, 0, 0, 0) && GetLastError() == ERROR_SUCCESS)
            enabled = true;
    }

    CloseHandle(token);

    return enabled;
}

/* Returns 1 when the name resolves from now on, 0 when it does not (failure
 * then says why).  A link that could not be made permanent is handed back in
 * held_handle, which the caller has to keep open for as long as the name has to
 * resolve. */
int create_class_device_link(const string &link_name, const string &target_name, bool permanent,
                            HANDLE &held_handle, string &failure) {
    wstring wide_link = wide_text(link_name);
    wstring wide_target = wide_text(target_name);
    UNICODE_STRING link_string;
    UNICODE_STRING target_string;
    OBJECT_ATTRIBUTES attributes;
    HANDLE link_handle = 0;
    NTSTATUS status;

    held_handle = 0;

    RtlInitUnicodeString(&link_string, wide_link.c_str());
    RtlInitUnicodeString(&target_string, wide_target.c_str());

    InitializeObjectAttributes(&attributes, &link_string, permanent ? OBJ_PERMANENT : 0, 0, 0);
    status = NtCreateSymbolicLinkObject(&link_handle, SYMBOLIC_LINK_ALL_ACCESS, &attributes,
                                        &target_string);

    /* A name that is already a link of some kind is fine: an earlier run, or
     * another tool, has done this part already. */
    if (NT_SUCCESS(status) || status == STATUS_OBJECT_NAME_COLLISION ||
        status == STATUS_OBJECT_TYPE_MISMATCH) {
        if (link_handle) NtClose(link_handle);

        return 1;
    }

    if (permanent && (status == STATUS_PRIVILEGE_NOT_HELD || status == STATUS_ACCESS_DENIED)) {
        /* This token may not create a permanent object, but it may still create
         * one that lasts as long as this program keeps the handle open. */
        InitializeObjectAttributes(&attributes, &link_string, 0, 0, 0);
        status = NtCreateSymbolicLinkObject(&link_handle, SYMBOLIC_LINK_ALL_ACCESS, &attributes,
                                            &target_string);

        if (NT_SUCCESS(status)) {
            held_handle = link_handle;

            return 1;
        }

        if (status == STATUS_OBJECT_NAME_COLLISION || status == STATUS_OBJECT_TYPE_MISMATCH) {
            if (link_handle) NtClose(link_handle);

            return 1;
        }
    }

    if (link_handle) NtClose(link_handle);

    if (failure.empty())
        failure = "cannot create " + link_name + ", status 0x" + hex_text((unsigned long)status);

    return 0;
}

/* Folds every higher class device name back onto the first ten.  permanent is
 * an input and output: on entry it says whether permanent links are still
 * possible, and it is cleared once the links only last for this run. */
int fold_class_devices(const char *class_name, int count, bool &permanent, string &failure) {
    int folded = 0;

    for (int block = 10; block < count; block += 10) {
        for (int low = 0; low < 10; ++low) {
            string link_name = class_device_name(class_name, block + low);
            string target_name = class_device_name(class_name, low);
            HANDLE held = 0;

            if (create_class_device_link(link_name, target_name, permanent, held, failure) == 0)
                continue;

            ++folded;

            if (held) {
                permanent = false;
                class_links_held_open.push_back(held);
            }
        }
    }

    return folded;
}

/* Silently folds the keyboard and mouse device names when this run has the
 * rights to do so, on the same rule as the access rights above. */
bool apply_driver_name_repair(Session &session, bool service_mode) {
    bool privileged = service_mode || process_is_elevated();
    bool permanent = true;
    string failure;
    int wanted = folded_class_device_count(class_device_count) * 2;
    int folded;

    if (session.options.no_symlinks || !privileged) return true;

    folded = fold_class_devices("KeyboardClass", class_device_count, permanent, failure);
    folded += fold_class_devices("PointerClass", class_device_count, permanent, failure);

    if (folded == wanted && permanent) return true;

    if (folded == wanted) {
        /* It works, but only for as long as this program runs, which the user
         * cannot see from the outside. */
        session.log_warning("blockkey: the driver's device names are folded only until this"
                            " program exits, because this token may not create permanent"
                            " objects");

        return true;
    }

    string detail = number_text((unsigned long)folded) + " of " +
                    number_text((unsigned long)wanted) + " device names";

    if (!failure.empty()) detail += ": " + failure;

    session.log_warning("blockkey: could not fold the driver's device names, " + detail);

    return false;
}

int remove_class_device_links(const char *class_name, int count, string &failure) {
    int removed = 0;

    for (int block = 10; block < count; block += 10) {
        for (int low = 0; low < 10; ++low) {
            string link_name = class_device_name(class_name, block + low);
            wstring wide_link = wide_text(link_name);
            UNICODE_STRING link_string;
            OBJECT_ATTRIBUTES attributes;
            HANDLE link_handle = 0;
            NTSTATUS status;

            RtlInitUnicodeString(&link_string, wide_link.c_str());
            InitializeObjectAttributes(&attributes, &link_string, OBJ_CASE_INSENSITIVE, 0, 0);

            status = NtOpenSymbolicLinkObject(&link_handle, DELETE, &attributes);

            /* Nothing to remove is the normal case on a machine that never had
             * the names folded. */
            if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_TYPE_MISMATCH)
                continue;

            if (!NT_SUCCESS(status)) {
                if (failure.empty())
                    failure = "cannot open " + link_name + ", status 0x" +
                              hex_text((unsigned long)status);

                continue;
            }

            /* A permanent object goes away as soon as it is no longer permanent
             * and the last handle is closed. */
            status = NtMakeTemporaryObject(link_handle);

            if (NT_SUCCESS(status))
                ++removed;
            else if (failure.empty())
                failure = "cannot remove " + link_name + ", status 0x" +
                          hex_text((unsigned long)status);

            NtClose(link_handle);
        }
    }

    return removed;
}

/* --clear-symlinks: undo the folding and stop. */
bool clear_driver_name_repair(Session &session) {
    string failure;
    int wanted = folded_class_device_count(class_device_count) * 2;
    int removed = remove_class_device_links("KeyboardClass", class_device_count, failure);

    removed += remove_class_device_links("PointerClass", class_device_count, failure);

    if (removed == 0 && failure.empty()) {
        session.log("blockkey: no folded device names to remove");

        return true;
    }

    if (removed == wanted) {
        session.log("blockkey: removed " + number_text((unsigned long)removed) +
                    " folded device names, so the driver handles the first ten again");

        return true;
    }

    string detail = number_text((unsigned long)removed) + " of " +
                    number_text((unsigned long)wanted) + " device names";

    if (!failure.empty()) detail += ": " + failure;

    session.log_error("blockkey: could not remove all folded device names, " + detail);

    return false;
}

/* Would any keyboard at all be affected by the configured filter?  A hardware
 * id that matches nothing looks exactly like a working setup that swallows
 * nothing, so it is worth saying out loud. */
bool any_keyboard_matches(InterceptionContext context, const Options &options,
                          HardwareIdCache &cache) {
    for (int number = 1; number <= INTERCEPTION_MAX_KEYBOARD; ++number) {
        InterceptionDevice device = INTERCEPTION_KEYBOARD(number - 1);

        if (device_matches(options, device, hardware_id_of(context, device, cache)))
            return true;
    }

    return false;
}

void warn_about_unmatched_keyboard(Session &session) {
    if (session.options.probe) return;
    if (session.options.hardware_id.empty() && session.options.device == 0) return;
    if (any_keyboard_matches(session.context, session.options, session.cache)) return;

    session.log_warning("no keyboard matches the configured --hardware-id/--device,"
                        " so nothing will be swallowed");
}

/* ------------------------------------------------------------- stroke -- */

/* Handles one keyboard stroke.  Returns false when the user asked to quit. */
bool process_stroke(Session &session, InterceptionDevice device,
                    const InterceptionKeyStroke &kstroke) {
    update_modifiers(kstroke, session.ctrl_down, session.shift_down);

    /* A service has no keyboard of its own and is stopped by the service
     * control manager instead of by the quit chord. */
    if (!session.options.service &&
        is_quit_chord(kstroke, session.ctrl_down, session.shift_down))
        return false;

    wstring hardware_id = hardware_id_of(session.context, device, session.cache);

    if (session.options.probe) {
        session.log(describe_stroke(kstroke, device, hardware_id));
    } else if (key_matches(kstroke, session.options) &&
               device_matches(session.options, device, hardware_id)) {
        ++session.swallowed;

        /* Per stroke logging stays out of the service log on purpose: a stuck
         * key can repeat hundreds of times per second. */
        if (session.options.verbose && !session.options.service)
            session.log("swallowed " + describe_stroke(kstroke, device, hardware_id));

        /* Deliberately not sent back: the operating system never sees it. */
        return true;
    }

    interception_send(session.context, device, (InterceptionStroke *)&kstroke, 1);

    return true;
}

void run_console_interceptor(Session &session) {
    InterceptionDevice device;
    InterceptionKeyStroke kstroke;

    while (interception_receive(session.context, device = interception_wait(session.context),
                                (InterceptionStroke *)&kstroke, 1) > 0)
        if (!process_stroke(session, device, kstroke)) break;
}

/* A service has to answer stop requests, so it polls instead of waiting for a
 * stroke forever. */
const unsigned long service_poll_milliseconds = 250;

void run_service_interceptor(Session &session, HANDLE stop_event) {
    InterceptionKeyStroke kstroke;

    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
        InterceptionDevice device =
            interception_wait_with_timeout(session.context, service_poll_milliseconds);

        if (device == 0 || interception_is_invalid(device)) continue;
        if (interception_receive(session.context, device,
                                 (InterceptionStroke *)&kstroke, 1) <= 0) continue;

        if (!process_stroke(session, device, kstroke)) break;
    }
}

/* ------------------------------------------------------------ service -- */

/* Only one interceptor may run at a time, or the two would split the strokes
 * between them and neither would see the whole keyboard. */
const char *single_program_name = "1F0A5C7E-9B24-4D3A-8E61-0C7D2A4B6E19";

char service_name[] = "blockkey";

SERVICE_STATUS_HANDLE service_status_handle = 0;
SERVICE_STATUS service_status;
HANDLE service_stop_event = 0;
Session service_session;

void report_service_status(DWORD state, DWORD exit_code) {
    if (!service_status_handle) return;

    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwCurrentState = state;
    service_status.dwWin32ExitCode = exit_code;
    service_status.dwWaitHint = 0;
    service_status.dwCheckPoint = 0;
    service_status.dwControlsAccepted =
        (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;

    SetServiceStatus(service_status_handle, &service_status);
}

void WINAPI service_control_handler(DWORD control) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        report_service_status(SERVICE_STOP_PENDING, NO_ERROR);

        if (service_stop_event) SetEvent(service_stop_event);

        return;
    }

    report_service_status(service_status.dwCurrentState, NO_ERROR);
}

void WINAPI service_main(DWORD argc, char **argv) {
    /* The options were parsed from the process command line by main(). */
    (void)argc;
    (void)argv;

    service_status_handle = RegisterServiceCtrlHandlerA(service_name, service_control_handler);
    if (!service_status_handle) return;

    report_service_status(SERVICE_START_PENDING, NO_ERROR);

    /* From here on, everything worth knowing goes to the event log. */
    open_event_source();

    service_stop_event = CreateEventA(0, TRUE, FALSE, 0);
    if (!service_stop_event) {
        report_service_status(SERVICE_STOPPED, GetLastError());
        close_event_source();
        return;
    }

    service_session.log("service starting, swallowing " + describe_target(service_session.options));

    service_session.context = interception_create_context();
    if (!service_session.context) {
        service_session.log_error("cannot reach the Interception driver");
        report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        CloseHandle(service_stop_event);
        service_stop_event = 0;
        close_event_source();
        return;
    }

    raise_process_priority();

    interception_set_filter(service_session.context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_ALL);

    /* Started by the service control manager as SYSTEM, so this is the run that
     * keeps the driver restricted to administrators, and its device names
     * folded, across reboots. */
    apply_driver_access_policy(service_session, true);
    apply_driver_name_repair(service_session, true);

    warn_about_unmatched_keyboard(service_session);

    if (service_session.options.release)
        service_session.log(
            "released the key on " +
            number_text((unsigned long)release_target_key(service_session.context,
                                                          service_session.options)) +
            " keyboard device(s), in case it was stuck already");

    report_service_status(SERVICE_RUNNING, NO_ERROR);

    run_service_interceptor(service_session, service_stop_event);

    interception_destroy_context(service_session.context);
    service_session.context = 0;

    service_session.log("service stopped, swallowed " + number_text(service_session.swallowed) +
                        " keystrokes");

    CloseHandle(service_stop_event);
    service_stop_event = 0;

    report_service_status(SERVICE_STOPPED, NO_ERROR);

    close_event_source();
}

int run_as_service(const Options &options) {
    SERVICE_TABLE_ENTRYA service_table[2];
    void *program_instance = 0;

    service_session.options = options;
    service_session.options.service = true;
    service_session.options.quiet = true;

    /* Nothing is journalled from here: only service_main(), which the service
     * control manager calls, writes to the event log.  Started by hand from a
     * command line, this reports to stderr and touches no log at all. */
    program_instance = try_open_single_program(single_program_name);
    if (!program_instance) {
        cerr << "blockkey: another instance is already running" << endl;
        return 1;
    }

    service_table[0].lpServiceName = service_name;
    service_table[0].lpServiceProc = service_main;
    service_table[1].lpServiceName = 0;
    service_table[1].lpServiceProc = 0;

    if (!StartServiceCtrlDispatcherA(service_table)) {
        DWORD error = GetLastError();

        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            cerr << "blockkey: --service must be started by the service control manager"
                 << endl;
        else
            cerr << "blockkey: cannot start as a service, error " << error << endl;

        close_single_program(program_instance);

        return 1;
    }

    close_single_program(program_instance);

    return 0;
}

}

int main(int argc, char *argv[]) {
    Options options = default_options();

    int parsed = parse_options(argc, argv, options);
    if (parsed == 1) {
        print_usage(cout);
        return 0;
    }
    if (parsed != 0) {
        print_usage(cerr);
        return 2;
    }

    if (options.release_only) {
        Session release_session;
        release_session.options = options;

        apply_driver_access_policy(release_session, false);
        apply_driver_name_repair(release_session, false);

        return release_stuck_key(options);
    }

    /* Maintenance actions on their own: repair or undo the driver work and stop,
     * instead of going on to swallow a key.  They run before the context is
     * created, so that they also work while the driver is restricted. */
    if ((options.unlock_driver || options.clear_symlinks) && !options.service) {
        Session maintenance;
        bool ok = true;

        maintenance.options = options;

        if (options.unlock_driver) {
            bool step = apply_driver_access_policy(maintenance, false);

            ok = ok && step;
        }

        if (options.clear_symlinks) {
            bool step = clear_driver_name_repair(maintenance);

            ok = ok && step;
        }

        return ok ? 0 : 1;
    }

    if (options.service) return run_as_service(options);

    void *program_instance = try_open_single_program(single_program_name);
    if (!program_instance) {
        cerr << "blockkey: another instance is already running" << endl;
        return 1;
    }

    Session session;
    session.options = options;

    session.context = interception_create_context();
    if (!session.context) {
        cerr << "blockkey: cannot reach the Interception driver; " << driver_access_hint() << endl;
        close_single_program(program_instance);
        return 1;
    }

    raise_process_priority();

    interception_set_filter(session.context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_ALL);

    /* Silently, when this run has the rights: nothing unelevated keeps watching
     * the keyboard or injecting strokes through the driver, and a device that
     * is unplugged, replugged or resumed from sleep cannot go dead.  Runs after
     * the handles this program already holds are open, and leaves them alone. */
    apply_driver_access_policy(session, false);
    apply_driver_name_repair(session, false);

    if (options.list) {
        list_keyboards(session.context, session.cache);
        interception_destroy_context(session.context);
        close_single_program(program_instance);
        return 0;
    }

    if (options.probe)
        session.log("blockkey: probe mode, printing every keystroke and swallowing nothing");
    else
        session.log("blockkey: swallowing " + describe_target(options));

    session.log("blockkey: quit with left Ctrl + left Shift + Q");

    warn_about_unmatched_keyboard(session);

    if (options.release && !options.probe)
        session.log("blockkey: released the key on " +
                    number_text((unsigned long)release_target_key(session.context, options)) +
                    " keyboard device(s), in case it was stuck already");

    run_console_interceptor(session);

    interception_destroy_context(session.context);
    close_single_program(program_instance);

    session.log("blockkey: stopped, swallowed " + number_text(session.swallowed) + " keystrokes");

    return 0;
}
