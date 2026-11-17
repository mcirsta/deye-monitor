# One C++20 / Qt Quick executable with the backend and UI linked together.
BUILD_DIR ?= build
.DEFAULT_GOAL := help
.PHONY: help configure gui gui-wayland run-gui run-gui-wayland install-desktop test check clean

help:
	@printf '%s\n' 'make gui             Build the C++ / Qt desktop application' 'make run-gui         Build and launch the desktop application' 'make run-gui-wayland Build and launch directly on Wayland' 'make test            Run backend, command-line and QML UI tests'

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

gui: configure
	cmake --build $(BUILD_DIR)

gui-wayland: gui

run-gui: gui
	./bin/deye-monitor-qt

run-gui-wayland: gui
	env -u DISPLAY QT_QPA_PLATFORM=wayland ./bin/deye-monitor-qt

install-desktop: configure
	cmake --build $(BUILD_DIR) --target install-desktop

test: gui
	ctest --test-dir $(BUILD_DIR) --output-on-failure

check: test

clean:
	rm -rf $(BUILD_DIR) bin
