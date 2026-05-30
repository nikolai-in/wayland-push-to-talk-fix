#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <limits>
#include <vector>

namespace {

struct Config {
    std::string input_device;
    int input_keycode = 84;
    int output_keycode = 191;
    int interval_ms = 10;
    bool toggle_mode = false;
    bool grab_input = false;
};

volatile sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

bool parse_int(const std::string& s, int& out) {
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos, 10);
        if (pos != s.size()) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --input-device PATH [options]\n"
        << "Options:\n"
        << "  --input-device PATH  /dev/input/event* or /dev/input/by-id/* (required)\n"
        << "  --input-keycode N    linux keycode to watch on input device (default 84)\n"
        << "  --output-keycode N   linux keycode to inject via uinput (default 191)\n"
        << "  --interval-ms N      poll interval in ms (default 10)\n"
        << "  --toggle             toggle mode instead of hold-to-talk\n"
        << "  --grab-input         EVIOCGRAB the input device while running\n"
        << "  --help               show this help\n";
}

std::optional<Config> parse_args(int argc, char** argv) {
    Config cfg;
    const int max_u16_value = static_cast<int>(std::numeric_limits<__u16>::max());
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input-device" && i + 1 < argc) {
            cfg.input_device = argv[++i];
        } else if (arg == "--input-keycode" && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.input_keycode) || cfg.input_keycode < 0 ||
                cfg.input_keycode > KEY_MAX || cfg.input_keycode > max_u16_value) {
                std::cerr << "Invalid --input-keycode\n";
                return std::nullopt;
            }
        } else if (arg == "--output-keycode" && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.output_keycode) || cfg.output_keycode < 0 ||
                cfg.output_keycode > KEY_MAX || cfg.output_keycode > max_u16_value) {
                std::cerr << "Invalid --output-keycode\n";
                return std::nullopt;
            }
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.interval_ms) || cfg.interval_ms <= 0) {
                std::cerr << "Invalid --interval-ms\n";
                return std::nullopt;
            }
        } else if (arg == "--toggle") {
            cfg.toggle_mode = true;
        } else if (arg == "--grab-input") {
            cfg.grab_input = true;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown/invalid argument: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (cfg.input_device.empty()) {
        std::cerr << "--input-device is required\n";
        return std::nullopt;
    }

    return cfg;
}

bool write_event(int fd, __u16 type, __u16 code, __s32 value) {
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    ssize_t n = ::write(fd, &ev, sizeof(ev));
    return n == static_cast<ssize_t>(sizeof(ev));
}

int open_uinput_device(int output_keycode) {
    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fd = ::open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    }
    if (fd < 0) {
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, output_keycode) < 0) {
        ::close(fd);
        return -1;
    }

    uinput_user_dev uidev{};
    std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "wayland-push-to-talk");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1;
    uidev.id.product = 0x1;
    uidev.id.version = 1;

    if (::write(fd, &uidev, sizeof(uidev)) != static_cast<ssize_t>(sizeof(uidev))) {
        ::close(fd);
        return -1;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        ::close(fd);
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return fd;
}

bool send_key_state(int uinput_fd, int keycode, bool down) {
    return write_event(uinput_fd, EV_KEY, static_cast<__u16>(keycode), down ? 1 : 0) &&
           write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

void destroy_uinput_device(int fd) {
    if (fd >= 0) {
        (void)ioctl(fd, UI_DEV_DESTROY);
        ::close(fd);
    }
}

void release_if_needed(int uinput_fd, int output_keycode, bool sent_down) {
    if (sent_down) {
        (void)send_key_state(uinput_fd, output_keycode, false);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto cfg_opt = parse_args(argc, argv);
    if (!cfg_opt) {
        print_usage(argv[0]);
        return 2;
    }
    Config cfg = *cfg_opt;

    int input_fd = ::open(cfg.input_device.c_str(), O_RDONLY);
    if (input_fd < 0) {
        std::cerr << "Failed to open input device: " << cfg.input_device << " (" << std::strerror(errno) << ")\n";
        return 1;
    }

    if (cfg.grab_input) {
        if (ioctl(input_fd, EVIOCGRAB, 1) < 0) {
            std::cerr << "Failed to grab input device: " << std::strerror(errno) << "\n";
            ::close(input_fd);
            return 1;
        }
    }

    int uinput_fd = open_uinput_device(cfg.output_keycode);
    if (uinput_fd < 0) {
        std::cerr << "Failed to create uinput keyboard. Ensure uinput is loaded and "
                     "that your user can access /dev/uinput (or run with elevated privileges).\n";
        if (cfg.grab_input) {
            (void)ioctl(input_fd, EVIOCGRAB, 0);
        }
        ::close(input_fd);
        return 1;
    }

    bool last_sent_down = false;
    bool toggle_state = false;
    bool input_key_down = false;

    std::cout << "Monitoring " << cfg.input_device
              << " key " << cfg.input_keycode
              << " -> injecting key " << cfg.output_keycode
              << (cfg.toggle_mode ? " [toggle mode]" : " [hold mode]")
              << "\n";

    pollfd pfd{};
    pfd.fd = input_fd;
    pfd.events = POLLIN;

    std::vector<input_event> events(32);

    while (!g_stop) {
        int ready = ::poll(&pfd, 1, cfg.interval_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (ready == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        ssize_t n = ::read(input_fd, events.data(), events.size() * sizeof(input_event));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            std::cerr << "read failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (n == 0) {
            std::cerr << "input device closed\n";
            break;
        }
        if (n % static_cast<ssize_t>(sizeof(input_event)) != 0) {
            std::cerr << "Warning: partial input_event read (" << n << " bytes)\n";
            continue;
        }

        const size_t count = static_cast<size_t>(n / sizeof(input_event));
        for (size_t i = 0; i < count; ++i) {
            const input_event& ev = events[i];
            if (ev.type != EV_KEY || ev.code != static_cast<__u16>(cfg.input_keycode)) {
                continue;
            }
            if (ev.value == 2) {
                // Skip key repeat events.
                continue;
            }

            const bool current_down = (ev.value != 0);
            bool should_be_down = false;

            if (cfg.toggle_mode) {
                if (current_down && !input_key_down) {
                    toggle_state = !toggle_state;
                }
                should_be_down = toggle_state;
            } else {
                should_be_down = current_down;
            }

            if (should_be_down != last_sent_down) {
                if (!send_key_state(uinput_fd, cfg.output_keycode, should_be_down)) {
                    std::cerr << "Warning: failed to inject key "
                              << cfg.output_keycode << (should_be_down ? " down" : " up") << "\n";
                } else {
                    std::cout << "Injected key " << cfg.output_keycode
                              << (should_be_down ? " down\n" : " up\n");
                    last_sent_down = should_be_down;
                }
            }

            input_key_down = current_down;
        }
    }

    release_if_needed(uinput_fd, cfg.output_keycode, last_sent_down);
    destroy_uinput_device(uinput_fd);

    if (cfg.grab_input) {
        if (ioctl(input_fd, EVIOCGRAB, 0) < 0) {
            std::cerr << "Warning: failed to release input grab: " << std::strerror(errno) << "\n";
        }
    }

    ::close(input_fd);
    return g_stop ? 0 : 1;
}
