// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

struct Packet {
    int sequence;
    int flags;
};
extern "C" int process(Packet*);
extern "C" int legacy_version();
// Link without the C runtime. This fixture is inspected, never executed.
extern "C" void _start() {
    Packet packet{1, 2};
    volatile int result = process(&packet) + legacy_version();
    (void)result;
}
