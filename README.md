<p align="center">
  <img src="assets/icon.png" alt="CouchPlay" width="128"/>
</p>

<h1 align="center">CouchPlay</h1>

<p align="center">Run multiple gaming sessions on one Linux PC with Gamescope, separate player accounts, and dedicated input devices.</p>

This repository is an independently maintained fork of CouchPlay for continued development and CachyOS support.

## Screenshots

<p align="center">
  <img src="assets/couchplay-main-cropped.png" alt="CouchPlay home page" width="80%"/>
</p>

<p align="center">
  <img src="assets/couchplay-session-cropped.png" alt="CouchPlay session configuration" width="80%"/>
</p>

<p align="center">
  <img src="assets/couchplay-split-screen.png" alt="CouchPlay split-screen session" width="80%"/>
</p>

## Features

- Run multiple Gamescope instances on one desktop.
- Assign controllers, keyboards, and mice to individual players.
- Use separate Linux accounts for player-specific Steam data and settings.
- Configure side-by-side, top-and-bottom, grid, and multi-monitor layouts.
- Launch Steam, Steam Big Picture, and Heroic sessions.
- Save reusable session profiles.
- Route audio through PipeWire.

## Install on CachyOS

CouchPlay is developed and tested on CachyOS with KDE Plasma on Wayland.

Install the build and runtime dependencies:

```bash
sudo pacman -S --needed \
  base-devel cmake extra-cmake-modules git \
  qt6-base qt6-declarative \
  kirigami ki18n kcoreaddons kconfig kiconthemes \
  qqc2-desktop-style kglobalaccel polkit-qt6 \
  acl pipewire gamescope
```

Clone and build:

```bash
git clone https://github.com/louisemalvin/couchplay.git
cd couchplay
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel 1
```

Install the application and its system helper:

```bash
sudo cmake --install build
sudo ./scripts/install-helper.sh install
```

Launch CouchPlay from the application menu or run:

```bash
couchplay
```

## Usage

1. Open **Users** and create a dedicated account for each additional player.
2. Create a session and choose a layout.
3. Select a launcher and account for each player.
4. Assign input devices manually or use **Auto-Assign Controllers**.
5. Save the profile if you want to reuse it, then select **Start Session**.

Profiles are stored in `~/.local/share/couchplay/profiles/`.

## Upstream

Based on the original [CouchPlay](https://github.com/hikaps/couchplay) project.

## AI Disclosure

This project is developed with assistance from AI tools for code generation, documentation, and debugging.

## License

GPL-3.0-or-later
