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
 * instead.
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

#include <windows.h>

#include <utils.h>
#include <interception.h>

using namespace std;

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
    bool service;           /* run under the service control manager */
    bool list;
    bool probe;
    bool verbose;
    bool quiet;
};

Options default_options() {
    Options options;
    options.code = right_alt_scan_code;
    options.e0 = e0_only;
    options.device = 0;
    options.hardware_id.clear();
    options.release = true;
    options.service = false;
    options.list = false;
    options.probe = false;
    options.verbose = false;
    options.quiet = false;
    return options;
}

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

/* Key up strokes that undo the configured key, one per E0 variant. */
vector<InterceptionKeyStroke> release_strokes(const Options &options) {
    vector<InterceptionKeyStroke> strokes;

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
        << "  --probe               print every keystroke and swallow nothing\n"
        << "  --list                list the keyboard devices, then exit\n"
        << "  --service             run as a Windows service, started by the service\n"
        << "                        control manager, not from a command line\n"
        << "  --verbose             print every swallowed keystroke\n"
        << "  --quiet               print nothing but errors\n"
        << "  -h, --help            print this help\n"
        << "\n"
        << "Right Alt is scan code 0x38 with the E0 prefix, left Alt the same scan\n"
        << "code without it.  Quit with left Ctrl + left Shift + Q.\n";
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
        } else if (argument == "--probe") {
            options.probe = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "-h" || argument == "--help") {
            return 1;
        } else {
            cerr << "blockkey: unknown option " << argument << endl;
            return 2;
        }
    }

    return 0;
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

/* A service has no console, so its few messages go to this file instead. */
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

string number_text(unsigned long number) {
    ostringstream text;

    text << number;

    return text.str();
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
        if (options.service) {
            ofstream file(log_file_path().c_str(), ios::app);
            if (file) file << timestamp() << " " << line << endl;
        } else if (!options.quiet) {
            cout << line << endl;
        }
    }
};

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

    service_stop_event = CreateEventA(0, TRUE, FALSE, 0);
    if (!service_stop_event) {
        report_service_status(SERVICE_STOPPED, GetLastError());
        return;
    }

    service_session.log("service starting, swallowing " + describe_target(service_session.options));

    service_session.context = interception_create_context();
    if (!service_session.context) {
        service_session.log("cannot reach the Interception driver");
        report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        CloseHandle(service_stop_event);
        service_stop_event = 0;
        return;
    }

    raise_process_priority();

    interception_set_filter(service_session.context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_ALL);

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
}

int run_as_service(const Options &options) {
    SERVICE_TABLE_ENTRYA service_table[2];
    void *program_instance = 0;

    service_session.options = options;
    service_session.options.service = true;
    service_session.options.quiet = true;

    program_instance = try_open_single_program(single_program_name);
    if (!program_instance) {
        service_session.log("cannot start, another instance is already running");
        return 1;
    }

    service_table[0].lpServiceName = service_name;
    service_table[0].lpServiceProc = service_main;
    service_table[1].lpServiceName = 0;
    service_table[1].lpServiceProc = 0;

    if (!StartServiceCtrlDispatcherA(service_table)) {
        DWORD error = GetLastError();

        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            service_session.log("--service must be started by the service control manager");
        else
            service_session.log("cannot start as a service, error " + number_text(error));

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
        cerr << "blockkey: cannot reach the Interception driver; install it and run this"
                " program as administrator" << endl;
        close_single_program(program_instance);
        return 1;
    }

    raise_process_priority();

    interception_set_filter(session.context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_ALL);

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
