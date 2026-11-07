// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

struct Packet {
    int flags;
    int sequence;
};
extern "C" int process(Packet* packet) {
    return packet->sequence;
}
extern "C" long checksum(int value) {
    return value;
}
extern "C" int new_feature() {
    return 2;
}
