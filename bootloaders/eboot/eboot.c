/* Copyright (c) 2015 Ivan Grokhotkov. All rights reserved. 
 * This file is part of eboot bootloader.
 *
 * Redistribution and use is permitted according to the conditions of the
 * 3-clause BSD license to be found in the LICENSE file.
 */


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "flash.h"
#include "eboot_command.h"

#define SWRST do { (*((volatile uint32_t*) 0x60000700)) |= 0x80000000; } while(0);


int load_app_from_flash_raw(const uint32_t flash_addr)
{
    image_header_t image_header;
    uint32_t pos = flash_addr + APP_START_OFFSET;

    if (SPIRead(pos, &image_header, sizeof(image_header))) {
        return 1;
    }
    pos += sizeof(image_header);
    

    for (uint32_t section_index = 0;
        section_index < image_header.num_segments; 
        ++section_index)
    {
        section_header_t section_header = {0};
        if (SPIRead(pos, &section_header, sizeof(section_header))) {
            return 2;
        }
        pos += sizeof(section_header);

        const uint32_t address = section_header.address;

        bool load = false;

        if (address < 0x40000000) {
            load = true;
        }

        if (address >= 0x40100000 && address < 0x40108000) {
            load = true;
        }

        if (address >= 0x60000000) {
            load = true;
        }

        if (!load) {
            pos += section_header.size;
            continue;
        }

        if (SPIRead(pos, (void*)address, section_header.size))
            return 3;

        pos += section_header.size;
    }

    register uint32_t sp asm("a1") = 0x3ffffff0;
    register uint32_t pc asm("a3") = image_header.entry;
    __asm__  __volatile__ ("jx a3");

    return 0;
}



int copy_raw(const uint32_t src_addr, 
             const uint32_t dst_addr, 
             const uint32_t size)
{
    ets_putc('\n');
    ets_putc('c');
    ets_putc('p');
    ets_putc('\n');
    // require regions to be aligned
    if (src_addr & 0xfff != 0 ||
        dst_addr & 0xfff != 0) {
        return 1;
    }

    if (SPIEraseAreaEx(dst_addr, size)) {
        return 2;
    }

    const uint32_t buffer_size = 4096;
    uint8_t buffer[buffer_size];

    const uint32_t end = src_addr + size;
    uint32_t saddr = src_addr;
    uint32_t daddr = dst_addr;
    uint32_t left = size;
    while (saddr < end) {
        uint32_t will_copy = (left < buffer_size) ? left : buffer_size;
        if (SPIRead(saddr, buffer, will_copy)) {
            return 3;
        }
        if (SPIWrite(daddr, buffer, will_copy)) {
            return 4;
        }
        saddr += will_copy;
        daddr += will_copy;
        left  -= will_copy;
    }

    return 0;
}



void main()
{
    int res = 9;
    struct eboot_command cmd;

    if (eboot_command_read(&cmd)) {
        cmd.action = ACTION_LOAD_APP;
        cmd.args[0] = 0;
        ets_putc('e');
    } else {
        ets_putc('@');
    }
    eboot_command_clear();

    if (cmd.action == ACTION_COPY_RAW) {
        res = copy_raw(cmd.args[0], cmd.args[1], cmd.args[2]);
        if (res == 0) {
            cmd.action = ACTION_LOAD_APP;
            cmd.args[0] = cmd.args[1];
        }
    }

    if (cmd.action == ACTION_LOAD_APP) {
        res = load_app_from_flash_raw(cmd.args[0]);
    }

    if (res) {
        ets_putc('\n');
        ets_putc('#');
        ets_putc('0' + res);
        ets_putc('\n');
        SWRST;
    }

    while(true){}
}
