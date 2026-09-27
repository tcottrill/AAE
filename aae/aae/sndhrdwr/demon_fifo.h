#pragma once

#include <cstdint>

// Demon sound-board FIFO protocol, following Aaron Giles' MAME cinemat.c.
// Hardware has wrapping 4-bit pointers, no separate full flag.
class DemonSoundFifo {
public:
    void reset() { *this = DemonSoundFifo{}; }

    void write_command(uint8_t value, uint8_t changed) {
        if ((changed & 0x10) && !(value & 0x10)) {
            data[in] = (~value) & 0x0f;
            in = (in + 1) & 15;
        }
    }

    uint8_t read_port_a() const { return data[out] | ((in != out) ? 0x10 : 0); }
    uint8_t read_port_b() const { return port_b; }

    void write_port_b(uint8_t value) {
        const uint8_t rising = value & ~port_b;
        if (rising & 1) out = (out + 1) & 15;
        if (rising & 2) in = out = 0;
        port_b = value;
    }

private:
    uint8_t data[16] = {};
    uint8_t in = 0, out = 0, port_b = 0xff;
};
