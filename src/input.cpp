#include "frontend.h"
#include "interface.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

#ifndef RETROK_PAUSE
#define RETROK_PAUSE       19
#endif
#ifndef RETROK_NUMLOCK
#define RETROK_NUMLOCK     300
#endif
#ifndef RETROK_CAPSLOCK
#define RETROK_CAPSLOCK    301
#endif
#ifndef RETROK_SCROLLOCK
#define RETROK_SCROLLOCK   302
#endif
#ifndef RETROK_PRINT
#define RETROK_PRINT       316
#endif

#ifndef RETRO_DEVICE_ID_MOUSE_X
#define RETRO_DEVICE_ID_MOUSE_X         0
#define RETRO_DEVICE_ID_MOUSE_Y         1
#define RETRO_DEVICE_ID_MOUSE_LEFT      2
#define RETRO_DEVICE_ID_MOUSE_RIGHT     3
#define RETRO_DEVICE_ID_MOUSE_WHEELUP   4
#define RETRO_DEVICE_ID_MOUSE_WHEELDOWN 5
#define RETRO_DEVICE_ID_MOUSE_MIDDLE    6
#endif

#ifndef RETRO_DEVICE_ID_POINTER_X
#define RETRO_DEVICE_ID_POINTER_X       0
#define RETRO_DEVICE_ID_POINTER_Y       1
#define RETRO_DEVICE_ID_POINTER_PRESSED 2
#endif

struct InputDriverState {
    float    virtual_ptr_x = 0.0f;
    float    virtual_ptr_y = 0.0f;
    bool     first_check = true;
    int      prev_kbd_count = -1;
    int      prev_mouse_count = -1;
    bool     prev_gamepad_connected[4] = {false, false, false, false};
    uint16_t rumble_strong[4] = {};
    uint16_t rumble_weak[4]   = {};
};

static InputDriverState s_drv;

namespace {
    struct KeyPair {
        uint16_t rk;
        uint16_t key;
    };
}

static constexpr KeyPair kSpecialKeys[] = {
    { RETROK_SPACE,       KEY_SPACE },
    { RETROK_ESCAPE,      KEY_ESC },
    { RETROK_RETURN,      KEY_ENTER },
    { RETROK_BACKSPACE,   KEY_BACKSPACE },
    { RETROK_TAB,         KEY_TAB },
    { RETROK_DELETE,      KEY_DELETE },
    { RETROK_UP,          KEY_UP },
    { RETROK_DOWN,        KEY_DOWN },
    { RETROK_LEFT,        KEY_LEFT },
    { RETROK_RIGHT,       KEY_RIGHT },
    { RETROK_HOME,        KEY_HOME },
    { RETROK_END,         KEY_END },
    { RETROK_PAGEUP,      KEY_PGUP },
    { RETROK_PAGEDOWN,    KEY_PGDN },
    { RETROK_INSERT,      KEY_INSERT },
    { RETROK_LSHIFT,      KEY_LSHIFT },
    { RETROK_RSHIFT,      KEY_RSHIFT },
    { RETROK_LCTRL,       KEY_LCONTROL },
    { RETROK_RCTRL,       KEY_RCONTROL },
    { RETROK_LALT,        KEY_LALT },
    { RETROK_RALT,        KEY_RALT },
    { RETROK_CAPSLOCK,    KEY_CAPSLOCK },
    { RETROK_NUMLOCK,     KEY_NUMLOCK },
    { RETROK_SCROLLOCK,   KEY_SCROLLOCK },
    { RETROK_PRINT,       KEY_PRINT },
    { RETROK_PAUSE,       KEY_PAUSE },
    { RETROK_KP_PERIOD,   KEY_KP_PERIOD },
    { RETROK_KP_DIVIDE,   KEY_KP_DIVIDE },
    { RETROK_KP_MULTIPLY, KEY_KP_MULTIPLY },
    { RETROK_KP_MINUS,    KEY_KP_MINUS },
    { RETROK_KP_PLUS,     KEY_KP_PLUS },
    { RETROK_KP_ENTER,    KEY_KP_ENTER },
    { RETROK_KP_EQUALS,   KEY_KP_EQUALS },
};

__attribute__((noinline))
static uint16_t map_key_table(uint16_t val, bool to_retro) {
    const KeyPair* p = kSpecialKeys;
    const KeyPair* end = p + (sizeof(kSpecialKeys) / sizeof(kSpecialKeys[0]));
    while (p < end) {
        if (to_retro ? (p->key == val) : (p->rk == val)) {
            return to_retro ? p->rk : p->key;
        }
        ++p;
    }
    return to_retro ? RETROK_UNKNOWN : 0;
}

__attribute__((noinline))
static bool retroKeyIsDown(unsigned rk) {
    const InputManager& inp = Frontend::get().input;
    if (rk >= 'a' && rk <= 'z') return inp.isKeyDown(static_cast<int>(rk - 32));
    if (rk >  ' ' && rk <  127) return inp.isKeyDown(static_cast<int>(rk));
    if (rk >= RETROK_KP0 && rk <= RETROK_KP9) return inp.isKeyDown(KEY_KP0 + (rk - RETROK_KP0));
    if (rk >= RETROK_F1  && rk <= RETROK_F12) return inp.isKeyDown(KEY_F1  + (rk - RETROK_F1));

    if (rk == RETROK_LSHIFT || rk == RETROK_RSHIFT) return inp.isKeyDown(KEY_SHIFT)   || inp.isKeyDown(rk == RETROK_LSHIFT ? KEY_LSHIFT : KEY_RSHIFT);
    if (rk == RETROK_LCTRL  || rk == RETROK_RCTRL)  return inp.isKeyDown(KEY_CONTROL) || inp.isKeyDown(rk == RETROK_LCTRL  ? KEY_LCONTROL : KEY_RCONTROL);
    if (rk == RETROK_LALT   || rk == RETROK_RALT)   return inp.isKeyDown(KEY_ALT)     || inp.isKeyDown(rk == RETROK_LALT   ? KEY_LALT : KEY_RALT);

    uint16_t k = map_key_table(static_cast<uint16_t>(rk), false);
    return k && inp.isKeyDown(k);
}

__attribute__((noinline))
unsigned inputKeyToRetroK(int key) {
    if (key >= 'A' && key <= 'Z') return 'a' + (key - 'A');
    if (key >= 32 && key < 127)   return static_cast<unsigned>(key);
    if (key >= KEY_KP0 && key <= KEY_KP9) return RETROK_KP0 + (key - KEY_KP0);
    if (key >= KEY_F1  && key <= KEY_F12) return RETROK_F1  + (key - KEY_F1);

    if (key == KEY_SHIFT)   return RETROK_LSHIFT;
    if (key == KEY_CONTROL) return RETROK_LCTRL;
    if (key == KEY_ALT)     return RETROK_LALT;

    return map_key_table(static_cast<uint16_t>(key), true);
}

static inline float clamp_ptr(float v) {
    return (v < -32767.0f) ? -32767.0f : ((v > 32767.0f) ? 32767.0f : v);
}

__attribute__((noinline))
static float map_viewport_coord(float rel, int fit) {
    if (fit <= 0) return 0.0f;
    float t = rel / static_cast<float>(fit);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    return (t * 2.0f - 1.0f) * 32767.0f;
}

static void detectDevices(int& kbd_count, int& mouse_count) {
    kbd_count = 0;
    mouse_count = 0;
#ifdef _WIN32
    RAWINPUTDEVICELIST devList[16];
    UINT numDevices = 16;
    UINT ret = GetRawInputDeviceList(devList, &numDevices, sizeof(RAWINPUTDEVICELIST));
    if (ret != static_cast<UINT>(-1)) {
        for (UINT i = 0; i < ret; i++) {
            if (devList[i].dwType == RIM_TYPEKEYBOARD) kbd_count++;
            else if (devList[i].dwType == RIM_TYPEMOUSE) mouse_count++;
        }
        return;
    }
#elif defined(__linux__)
    FILE* f = fopen("/proc/bus/input/devices", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == 'H' && line[1] == ':') {
                if (strstr(line, "kbd"))   kbd_count++;
                if (strstr(line, "mouse")) mouse_count++;
            }
        }
        fclose(f);
        return;
    }
#endif
    kbd_count = 1;
    mouse_count = 1;
}

__attribute__((noinline))
static void checkDeviceHotplug() {
    int kbd_count = 0, mouse_count = 0;
    detectDevices(kbd_count, mouse_count);

    if (s_drv.first_check) {
        s_drv.prev_kbd_count = kbd_count;
        s_drv.prev_mouse_count = mouse_count;
        for (int i = 0; i < 4; i++) {
            s_drv.prev_gamepad_connected[i] = globalGamepad().state(i).connected;
        }
        s_drv.first_check = false;
        return;
    }

    auto log_change = [](const char* name, bool conn) {
        Frontend::get().logMessage("[inf] %s %s", name, conn ? "Connected" : "Disconnected");
    };

    if (kbd_count != s_drv.prev_kbd_count) {
        log_change("Keyboard", kbd_count > s_drv.prev_kbd_count);
        s_drv.prev_kbd_count = kbd_count;
    }
    if (mouse_count != s_drv.prev_mouse_count) {
        log_change("Mouse", mouse_count > s_drv.prev_mouse_count);
        s_drv.prev_mouse_count = mouse_count;
    }
    for (int i = 0; i < 4; i++) {
        bool gp_connected = globalGamepad().state(i).connected;
        if (gp_connected != s_drv.prev_gamepad_connected[i]) {
            Frontend::get().logMessage("[inf] Controller %d %s", i + 1, gp_connected ? "Connected" : "Disconnected");
            s_drv.prev_gamepad_connected[i] = gp_connected;
        }
    }
}

__attribute__((noinline))
static void update_pointer(InputManager& inp) {
    if (inp.isMouseButtonPressed(MOUSE_MIDDLE)) {
        inp.setMouseLocked(!inp.isMouseLocked());
    }

    if (!inp.isMouseLocked()) {
        auto m = inp.getMousePosition();
        float mx = m.x, my = m.y;

#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            mx -= static_cast<float>(csbi.srWindow.Left);
            my -= static_cast<float>(csbi.srWindow.Top);
        }
#endif

        if (g_cfg.pixel_mode) my *= 2.0f;

        const Viewport& vp = Frontend::get().viewport;
        if (vp.fit_w > 0) s_drv.virtual_ptr_x = map_viewport_coord(mx - static_cast<float>(vp.off_x), vp.fit_w);
        if (vp.fit_h > 0) s_drv.virtual_ptr_y = map_viewport_coord(my - static_cast<float>(vp.off_y), vp.fit_h);
    } else {
        auto d = inp.getMouseDelta();
        s_drv.virtual_ptr_x = (s_drv.virtual_ptr_x + d.x * 40.0f) * 0.85f;
        s_drv.virtual_ptr_y = (s_drv.virtual_ptr_y + d.y * 40.0f) * 0.85f;
    }

    const GamepadState& gp = globalGamepad().state(g_cfg.p1_gamepad_index);
    if (gp.connected) {
        float speed = 5.0f * static_cast<float>(g_cfg.mouse_sensitivity);
        s_drv.virtual_ptr_x += gp.rightX * speed;
        s_drv.virtual_ptr_y += gp.rightY * speed;
    }

    s_drv.virtual_ptr_x = clamp_ptr(s_drv.virtual_ptr_x);
    s_drv.virtual_ptr_y = clamp_ptr(s_drv.virtual_ptr_y);
}

void input_frame_update() {
    globalGamepad().update();
    update_pointer(Frontend::get().input);

    static int s_check_timer = 0;
    if (++s_check_timer >= 60 || s_drv.first_check) {
        s_check_timer = 0;
        checkDeviceHotplug();
    }
}

void input_poll_cb(void) {
}

static inline bool is_gp_input_down(const GamepadState& gp, int m) {
    if (!gp.connected || m < 0) return false;
    if (m < 16) return gp.buttons[m];
    if (m >= 26) return false;
    float ax[6];
    padAxes(gp, ax);
    const float v = ax[kAxisOfBind[m - 16]];
    return gpBindPositive(m) ? v > 0.5f : v < -0.5f;
}

static inline bool is_action_down(const Frontend& f, const GamepadState& gp, int bind_idx) {
    if (bind_idx < 0 || bind_idx >= BIND_COUNT) return false;
    if (gp.connected) {
        int m = g_cfg.gamepad_mappings[bind_idx];
        if (is_gp_input_down(gp, m)) return true;
    }
    if (!f.keyboard_cb.callback) {
        int k = g_cfg.key_mappings[bind_idx];
        if (k >= 0 && f.input.isKeyDown(k)) return true;
    }
    return false;
}

static int16_t get_joypad_mask(const Frontend& f, const GamepadState& gp) {
    int16_t mask = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (is_action_down(f, gp, i)) mask |= (1 << i);
    }
    return mask;
}

int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port > 1) return 0;

    const Frontend& f = Frontend::get();
    int gp_idx = (port == 0) ? g_cfg.p1_gamepad_index : g_cfg.p2_gamepad_index;
    const GamepadState& gp = globalGamepad().state(gp_idx);

    switch (device) {
    case RETRO_DEVICE_JOYPAD: {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return get_joypad_mask(f, gp);
        if (id >= 16) return 0;
        return is_action_down(f, gp, id) ? 1 : 0;
    }

    case RETRO_DEVICE_ANALOG: {
        bool is_x = (id == RETRO_DEVICE_ID_ANALOG_X);
        float gp_val = 0.0f;
        if (gp.connected) {
            gp_val = (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
                ? (is_x ? gp.leftX : gp.leftY)
                : (is_x ? gp.rightX : gp.rightY);
        }
        if (gp_val < -0.15f || gp_val > 0.15f) {
            return static_cast<int16_t>(gp_val * 32767.0f);
        }

        int neg_bind = -1, pos_bind = -1;
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            neg_bind = is_x ? BIND_ANALOG_LEFT_LEFT : BIND_ANALOG_LEFT_UP;
            pos_bind = is_x ? BIND_ANALOG_LEFT_RIGHT : BIND_ANALOG_LEFT_DOWN;
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            neg_bind = is_x ? BIND_ANALOG_RIGHT_LEFT : BIND_ANALOG_RIGHT_UP;
            pos_bind = is_x ? BIND_ANALOG_RIGHT_RIGHT : BIND_ANALOG_RIGHT_DOWN;
        }

        bool neg_down = (neg_bind >= 0) && is_action_down(f, gp, neg_bind);
        bool pos_down = (pos_bind >= 0) && is_action_down(f, gp, pos_bind);

        if (neg_down && !pos_down) return -32767;
        if (pos_down && !neg_down) return  32767;
        return 0;
    }
    case RETRO_DEVICE_KEYBOARD:
        return retroKeyIsDown(id) ? 1 : 0;

    case RETRO_DEVICE_MOUSE: {
        float sens = static_cast<float>(g_cfg.mouse_sensitivity) * 0.01f;

        if (id == RETRO_DEVICE_ID_MOUSE_LEFT) {
            return f.input.isMouseButtonDown(MOUSE_LEFT) ? 1 : 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_RIGHT) {
            return f.input.isMouseButtonDown(MOUSE_RIGHT) ? 1 : 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_MIDDLE) {
            return f.input.isMouseButtonDown(MOUSE_MIDDLE) ? 1 : 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_WHEELUP) {
            float w = f.input.getMouseWheelDelta();
            return (w > 0.0f) ? 1 : 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_WHEELDOWN) {
            float w = f.input.getMouseWheelDelta();
            return (w < 0.0f) ? 1 : 0;
        }

        bool is_x = (id == RETRO_DEVICE_ID_MOUSE_X);
        float d = is_x ? f.input.getMouseDelta().x : f.input.getMouseDelta().y;
        return static_cast<int16_t>(d * sens);
    }

    case RETRO_DEVICE_POINTER:
        if (id == RETRO_DEVICE_ID_POINTER_X) return static_cast<int16_t>(s_drv.virtual_ptr_x);
        if (id == RETRO_DEVICE_ID_POINTER_Y) return static_cast<int16_t>(s_drv.virtual_ptr_y);
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) {
            return f.input.isMouseButtonDown(MOUSE_LEFT) ? 1 : 0;
        }
        return 0;

    default:
        return 0;
    }
}

void keyboard_callback_handler(bool down, int key_code, uint32_t character) {
    Frontend& f = Frontend::get();
    if (!f.keyboard_cb.callback) return;

    unsigned rk = inputKeyToRetroK(key_code);
    if (rk == RETROK_UNKNOWN) return;

    uint16_t mods = 0;
    const InputManager& inp = f.input;
    if (inp.isKeyDown(KEY_SHIFT))     mods |= 0x0001; 
    if (inp.isKeyDown(KEY_CONTROL))   mods |= 0x0002; 
    if (inp.isKeyDown(KEY_ALT))       mods |= 0x0004; 
    if (inp.isKeyDown(KEY_NUMLOCK))   mods |= 0x0010; 
    if (inp.isKeyDown(KEY_CAPSLOCK))  mods |= 0x0020; 
    if (inp.isKeyDown(KEY_SCROLLOCK)) mods |= 0x0040; 

    f.keyboard_cb.callback(down, rk, character, mods);
}

bool set_gamepad_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
    if (port >= 4) return false;
    int gp_idx = (port == 0) ? g_cfg.p1_gamepad_index : (port == 1 ? g_cfg.p2_gamepad_index : static_cast<int>(port));
    if (effect == RETRO_RUMBLE_STRONG) {
        s_drv.rumble_strong[port] = strength;
    } else if (effect == RETRO_RUMBLE_WEAK) {
        s_drv.rumble_weak[port] = strength;
    } else {
        return false;
    }
    return globalGamepad().setVibration(gp_idx, s_drv.rumble_strong[port], s_drv.rumble_weak[port]);
}