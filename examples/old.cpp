// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

struct Packet {
    int sequence;
    int flags;
};
extern "C" int process(Packet* packet) {
    return packet->sequence;
}
extern "C" int legacy_version() {
    return 1;
}
extern "C" int checksum(int value) {
    return value;
}
