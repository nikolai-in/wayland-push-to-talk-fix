# wayland-push-to-talk

Push-to-talk bridge for Linux Wayland that works with the native Discord Wayland client (not Xwayland).

The app reads a key from a real input device (`/dev/input/event*`) and injects a synthetic key through a virtual keyboard created with `uinput`. Discord can bind to the injected key like any regular keyboard key.

## Features

- Native Wayland-compatible key injection via Linux `uinput`
- Reads global key events directly from an input device
- Hold-to-talk and toggle modes
- Configurable input/output keycodes and polling interval
- Optional exclusive input grab mode

## Dependencies

- Linux with `uinput`
- A user allowed to read target `/dev/input/event*` device and write to `/dev/uinput`
- `g++`
- `make`

## Build

```sh
make
```

Produces `./push-to-talk`.

## Usage

```sh
./push-to-talk --input-device /dev/input/by-id/<your-keyboard> [options]
```

Options:

- `--input-device PATH` (required) – input device to monitor
- `--input-keycode N` (default: `84`) – keycode read from input device
- `--output-keycode N` (default: `191`) – keycode injected via virtual keyboard
- `--interval-ms N` (default: `10`) – poll interval
- `--toggle` – toggles on each press (instead of hold-to-talk)
- `--grab-input` – grabs the source device with `EVIOCGRAB` while running

Example:

```sh
./push-to-talk --input-device /dev/input/by-id/usb-My_Keyboard-event-kbd --input-keycode 84 --output-keycode 191
```

## Discord setup

1. Start `push-to-talk`.
2. In Discord (Wayland native client), set Push-to-Talk to the same `--output-keycode` key.
3. Test in a voice channel.

Use `wev`, `evtest`, or `libinput debug-events` to discover keycodes for your setup.

## Nix flake

This repository now includes `flake.nix` with:

- `packages.<system>.default` for the binary package
- `apps.<system>.default` runnable app
- `devShells.<system>.default` development shell

Example:

```sh
nix run .# -- --input-device /dev/input/by-id/usb-My_Keyboard-event-kbd
```

## Autostart

A sample desktop entry is provided at `push-to-talk.desktop`.

- Edit `Exec=` with your input device path and keycodes
- Copy to `~/.config/autostart/`

## License

MIT (see `LICENSE`).
