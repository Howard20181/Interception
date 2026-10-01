/* Host side test for blockkey.cpp.
 *
 * The Interception API is replaced by stubs, so the whole program, argument
 * parsing included, runs without the driver: a scripted sequence of key
 * strokes is fed to interception_receive() and the strokes the program sends
 * back with interception_send() are checked.  Nothing is swallowed unless the
 * key under test matches.
 *
 * Build and run it with run-tests.cmd in this directory, no driver and no
 * administrative rights are needed.
 */

/* Every header blockkey.cpp includes is pulled in here first, before main is
 * renamed, so that the rename cannot possibly reach a system header. */
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#define INTERCEPTION_STATIC
#include <utils.h>
#include <interception.h>

#define main blockkey_main
#include "../blockkey.cpp"
#undef main

using namespace std;

/* ---------------------------------------------------------------- stubs -- */

struct ScriptedStroke {
    InterceptionDevice device;
    InterceptionKeyStroke stroke;
};

static vector<ScriptedStroke> script;
static size_t script_position = 0;
static vector<InterceptionKeyStroke> forwarded;
static vector<InterceptionDevice> forwarded_devices;
static map<InterceptionDevice, wstring> hardware_ids;

InterceptionContext interception_create_context(void) {
    return (InterceptionContext)1;
}

void interception_destroy_context(InterceptionContext context) {
    (void)context;
}

void interception_set_filter(InterceptionContext context,
                             InterceptionPredicate predicate, InterceptionFilter filter) {
    (void)context;
    (void)predicate;
    (void)filter;
}

InterceptionDevice interception_wait(InterceptionContext context) {
    (void)context;

    if (script_position >= script.size()) return 0;

    return script[script_position].device;
}

InterceptionDevice interception_wait_with_timeout(InterceptionContext context,
                                                 unsigned long milliseconds) {
    (void)milliseconds;

    return interception_wait(context);
}

int interception_receive(InterceptionContext context, InterceptionDevice device,
                         InterceptionStroke *stroke, unsigned int nstroke) {
    (void)context;
    (void)nstroke;

    if (script_position >= script.size()) return 0;
    if (script[script_position].device != device) return 0;

    *(InterceptionKeyStroke *)stroke = script[script_position].stroke;
    ++script_position;

    return 1;
}

int interception_send(InterceptionContext context, InterceptionDevice device,
                      const InterceptionStroke *stroke, unsigned int nstroke) {
    (void)context;

    const InterceptionKeyStroke *keystrokes = (const InterceptionKeyStroke *)stroke;

    for (unsigned int i = 0; i < nstroke; ++i) {
        forwarded.push_back(keystrokes[i]);
        forwarded_devices.push_back(device);
    }

    return (int)nstroke;
}

unsigned int interception_get_hardware_id(InterceptionContext context,
                                          InterceptionDevice device,
                                          void *hardware_id_buffer,
                                          unsigned int buffer_size) {
    (void)context;

    map<InterceptionDevice, wstring>::const_iterator found = hardware_ids.find(device);
    if (found == hardware_ids.end()) return 0;

    unsigned int size = (unsigned int)(found->second.size() * sizeof(wchar_t));
    if (size > buffer_size) return 0;

    memcpy(hardware_id_buffer, found->second.c_str(), size);

    return size;
}

int interception_is_keyboard(InterceptionDevice device) {
    return device >= INTERCEPTION_KEYBOARD(0) &&
           device <= INTERCEPTION_KEYBOARD(INTERCEPTION_MAX_KEYBOARD - 1);
}

int interception_is_mouse(InterceptionDevice device) {
    return device >= INTERCEPTION_MOUSE(0) &&
           device <= INTERCEPTION_MOUSE(INTERCEPTION_MAX_MOUSE - 1);
}

int interception_is_invalid(InterceptionDevice device) {
    return !interception_is_keyboard(device) && !interception_is_mouse(device);
}

void raise_process_priority(void) {}
void lower_process_priority(void) {}

void *try_open_single_program(const char *name) {
    (void)name;

    return (void *)1;
}

void close_single_program(void *program_instance) {
    (void)program_instance;
}

/* ------------------------------------------------------------- harness -- */

static int checks = 0;
static int failures = 0;

#define CHECK(condition)                                                  \
    do {                                                                  \
        ++checks;                                                         \
        if (!(condition)) {                                               \
            ++failures;                                                   \
            cout << "FAIL line " << __LINE__ << ": " << #condition << endl; \
        }                                                                 \
    } while (0)

/* Silences the output of the program under test while it runs. */
struct Silence {
    ostringstream buffer;
    streambuf *saved_cout;
    streambuf *saved_cerr;

    Silence() {
        saved_cout = cout.rdbuf(buffer.rdbuf());
        saved_cerr = cerr.rdbuf(buffer.rdbuf());
    }

    ~Silence() {
        cout.rdbuf(saved_cout);
        cerr.rdbuf(saved_cerr);
    }
};

struct Run {
    vector<InterceptionKeyStroke> forwarded;
    vector<InterceptionDevice> devices;
    int exit_code;
};

/* Arguments of the tests that only check what gets swallowed: silent, and
 * without the start up release, which would add strokes of its own. */
vector<string> blocking_args(const char *first = 0, const char *second = 0,
                             const char *third = 0, const char *fourth = 0) {
    vector<string> result;

    if (first) result.push_back(first);
    if (second) result.push_back(second);
    if (third) result.push_back(third);
    if (fourth) result.push_back(fourth);

    result.push_back("--quiet");
    result.push_back("--no-release");

    return result;
}

/* Arguments of the tests that check the start up release instead. */
vector<string> releasing_args(const char *first = 0, const char *second = 0) {
    vector<string> result;

    if (first) result.push_back(first);
    if (second) result.push_back(second);

    result.push_back("--quiet");

    return result;
}

InterceptionKeyStroke key(unsigned short code, unsigned short state) {
    InterceptionKeyStroke kstroke;

    kstroke.code = code;
    kstroke.state = state;
    kstroke.information = 0;

    return kstroke;
}

ScriptedStroke on(InterceptionDevice device, const InterceptionKeyStroke &stroke) {
    ScriptedStroke scripted;

    scripted.device = device;
    scripted.stroke = stroke;

    return scripted;
}

Run run_blockkey(const vector<string> &arguments,
                 const vector<ScriptedStroke> &strokes,
                 const map<InterceptionDevice, wstring> &ids = map<InterceptionDevice, wstring>()) {
    Silence silence;

    script = strokes;
    hardware_ids = ids;
    script_position = 0;
    forwarded.clear();
    forwarded_devices.clear();

    vector<string> all;
    all.push_back("blockkey");

    for (size_t i = 0; i < arguments.size(); ++i) all.push_back(arguments[i]);

    vector<char *> argv;
    for (size_t i = 0; i < all.size(); ++i) argv.push_back(const_cast<char *>(all[i].c_str()));

    Run result;
    result.exit_code = blockkey_main((int)argv.size(), &argv[0]);
    result.forwarded = forwarded;
    result.devices = forwarded_devices;

    return result;
}

/* --------------------------------------------------------------- cases -- */

const InterceptionDevice keyboard = INTERCEPTION_KEYBOARD(0);
const InterceptionDevice other_keyboard = INTERCEPTION_KEYBOARD(1);

const unsigned short down_state = INTERCEPTION_KEY_DOWN;
const unsigned short up_state = INTERCEPTION_KEY_UP;
const unsigned short e0_down_state = INTERCEPTION_KEY_E0;
const unsigned short e0_up_state = INTERCEPTION_KEY_E0 | INTERCEPTION_KEY_UP;

const unsigned short scan_right_alt = 0x38;
const unsigned short scan_left_ctrl = 0x1D;
const unsigned short scan_left_shift = 0x2A;
const unsigned short scan_q = 0x10;
const unsigned short scan_a = 0x1E;

static void test_right_alt_is_swallowed_and_the_rest_passes_through() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_up_state)));
    strokes.push_back(on(keyboard, key(scan_right_alt, down_state))); /* left Alt */
    strokes.push_back(on(keyboard, key(scan_right_alt, up_state)));
    strokes.push_back(on(keyboard, key(scan_a, down_state)));
    strokes.push_back(on(keyboard, key(scan_a, up_state)));

    Run run = run_blockkey(blocking_args("--quiet"), strokes);

    CHECK(run.exit_code == 0);
    CHECK(run.forwarded.size() == 4);

    if (run.forwarded.size() == 4) {
        CHECK(run.forwarded[0].code == scan_right_alt && run.forwarded[0].state == down_state);
        CHECK(run.forwarded[1].code == scan_right_alt && run.forwarded[1].state == up_state);
        CHECK(run.forwarded[2].code == scan_a && run.forwarded[2].state == down_state);
        CHECK(run.forwarded[3].code == scan_a && run.forwarded[3].state == up_state);
    }

    CHECK(run.devices.size() == 4 && run.devices[0] == keyboard && run.devices[3] == keyboard);
}

static void test_a_stuck_key_repeating_is_fully_swallowed() {
    vector<ScriptedStroke> strokes;
    for (int i = 0; i < 8; ++i) strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(keyboard, key(scan_a, up_state)));

    Run run = run_blockkey(blocking_args("--quiet"), strokes);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.forwarded[0].code == scan_a);
}

static void test_no_e0_targets_the_left_variant() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(keyboard, key(scan_right_alt, down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--no-e0"), strokes);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.forwarded[0].state == e0_down_state);
}

static void test_any_e0_targets_both_variants() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(keyboard, key(scan_right_alt, down_state)));
    strokes.push_back(on(keyboard, key(scan_a, down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--any-e0"), strokes);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.forwarded[0].code == scan_a);
}

static void test_another_key_can_be_chosen() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_left_ctrl, down_state)));
    strokes.push_back(on(keyboard, key(scan_left_ctrl, e0_down_state))); /* right ctrl */
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--key", "0x1D", "--any-e0"), strokes);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.forwarded[0].code == scan_right_alt);
}

static void test_device_filter_leaves_other_keyboards_alone() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(other_keyboard, key(scan_right_alt, e0_down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--device", "2"), strokes);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.devices[0] == keyboard);
}

static void test_hardware_id_filter_matches_case_insensitively() {
    map<InterceptionDevice, wstring> ids;
    ids[keyboard] = L"ACPI\\PNP0303\\4&1A2B3C&0&Laptop-KBD";
    ids[other_keyboard] = L"HID\\VID_046D&PID_C31C&MI_00\\External";

    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(other_keyboard, key(scan_right_alt, e0_down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--hwid", "laptop"), strokes, ids);

    CHECK(run.forwarded.size() == 1);
    if (run.forwarded.size() == 1) CHECK(run.devices[0] == other_keyboard);
}

static void test_quit_chord_stops_the_loop_and_is_not_forwarded() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_left_ctrl, down_state)));
    strokes.push_back(on(keyboard, key(scan_left_shift, down_state)));
    strokes.push_back(on(keyboard, key(scan_q, down_state)));
    strokes.push_back(on(keyboard, key(scan_a, down_state)));

    Run run = run_blockkey(blocking_args("--quiet"), strokes);

    CHECK(run.exit_code == 0);
    CHECK(run.forwarded.size() == 2);
    if (run.forwarded.size() == 2) {
        CHECK(run.forwarded[0].code == scan_left_ctrl);
        CHECK(run.forwarded[1].code == scan_left_shift);
    }
}

static void test_probe_swallows_nothing() {
    vector<ScriptedStroke> strokes;
    strokes.push_back(on(keyboard, key(scan_right_alt, e0_down_state)));
    strokes.push_back(on(keyboard, key(scan_a, down_state)));

    Run run = run_blockkey(blocking_args("--quiet", "--probe"), strokes);

    CHECK(run.exit_code == 0);
    CHECK(run.forwarded.size() == 2);
}

static void test_list_only_reports_devices() {
    map<InterceptionDevice, wstring> ids;
    ids[keyboard] = L"ACPI\\PNP0303";

    Run run = run_blockkey(blocking_args("--quiet", "--list"), vector<ScriptedStroke>(), ids);

    CHECK(run.exit_code == 0);
    CHECK(run.forwarded.empty());
}

static void test_start_up_release_undoes_an_already_stuck_key() {
    vector<ScriptedStroke> nothing;
    const size_t keyboards = (size_t)INTERCEPTION_MAX_KEYBOARD;

    Run released = run_blockkey(releasing_args(), nothing);

    CHECK(released.exit_code == 0);
    CHECK(released.forwarded.size() == keyboards);
    CHECK(released.devices.size() == keyboards);

    for (size_t i = 0; i < released.forwarded.size(); ++i) {
        CHECK(released.forwarded[i].code == scan_right_alt);
        CHECK(released.forwarded[i].state == e0_up_state);
    }

    if (released.devices.size() == keyboards) {
        CHECK(released.devices[0] == INTERCEPTION_KEYBOARD(0));
        CHECK(released.devices[keyboards - 1] ==
              INTERCEPTION_KEYBOARD(INTERCEPTION_MAX_KEYBOARD - 1));
    }

    /* Skipped by --no-release, and by the modes that must not inject input. */
    CHECK(run_blockkey(blocking_args(), nothing).forwarded.empty());
    CHECK(run_blockkey(releasing_args("--probe"), nothing).forwarded.empty());
    CHECK(run_blockkey(releasing_args("--list"), nothing).forwarded.empty());

    /* Both E0 variants are released when the target accepts either of them. */
    CHECK(run_blockkey(releasing_args("--any-e0"), nothing).forwarded.size() == keyboards * 2);
}

static void test_bad_arguments_are_rejected() {
    Run unknown = run_blockkey(blocking_args("--nonsense"), vector<ScriptedStroke>());
    CHECK(unknown.exit_code == 2);

    Run bad_device = run_blockkey(blocking_args("--device", "42"), vector<ScriptedStroke>());
    CHECK(bad_device.exit_code == 2);

    Run missing_key = run_blockkey(blocking_args("--key"), vector<ScriptedStroke>());
    CHECK(missing_key.exit_code == 2);

    Run help = run_blockkey(blocking_args("--help"), vector<ScriptedStroke>());
    CHECK(help.exit_code == 0);
}

int main() {
    test_right_alt_is_swallowed_and_the_rest_passes_through();
    test_a_stuck_key_repeating_is_fully_swallowed();
    test_no_e0_targets_the_left_variant();
    test_any_e0_targets_both_variants();
    test_another_key_can_be_chosen();
    test_device_filter_leaves_other_keyboards_alone();
    test_hardware_id_filter_matches_case_insensitively();
    test_quit_chord_stops_the_loop_and_is_not_forwarded();
    test_probe_swallows_nothing();
    test_list_only_reports_devices();
    test_start_up_release_undoes_an_already_stuck_key();
    test_bad_arguments_are_rejected();

    cout << (checks - failures) << "/" << checks << " checks passed" << endl;

    return failures == 0 ? 0 : 1;
}
