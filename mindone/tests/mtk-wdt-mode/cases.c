// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct mtk_wdt_dev wdt;
static struct device_node node;
static struct device dev = { &node };
static void reset(u32 mode, bool disable, bool toprgu)
{
    memset(registers, 0, sizeof(registers));
    memset(&wdt, 0, sizeof(wdt));
    registers[0] = mode;
    wdt.wdt_base = registers;
    wdt.wdt_dev.data = &wdt;
    wdt.wdt_dev.timeout = 13;
    mode_writes = length_writes = kicks = parses = property_reads = 0;
    node = (struct device_node){disable, toprgu};
    read_properties(&wdt, &dev);
    CHECK(property_reads == 2);
    CHECK(wdt.disable_wdt_extrst == disable && wdt.reset_by_toprgu == toprgu);
}
int main(void)
{
    for (int disable = 0; disable < 2; disable++) {
        for (int toprgu = 0; toprgu < 2; toprgu++) {
            u32 expected = (disable ? 0x22000031u : 0x22000035u) | (toprgu ? 0x100u : 0);
            reset(0x7d, disable, toprgu);
            mtk_wdt_init(&node, &wdt.wdt_dev);
            CHECK(registers[0] == expected && mode_writes == 1);
            CHECK(wdt.wdt_dev.status == 1 && parses == 1);
            CHECK(length_writes == 1 && kicks == 1);
            CHECK(registers[1] == ((13u << 11) | 8) && registers[2] == 0x1971);
            reset(0x7c, disable, toprgu);
            mtk_wdt_init(NULL, &wdt.wdt_dev);
            CHECK(registers[0] == 0x7c && mode_writes == 0);
            CHECK(wdt.wdt_dev.status == 0 && length_writes == 0 && kicks == 0 && parses == 0);
            CHECK(mtk_wdt_start(&wdt.wdt_dev) == 0);
            CHECK(registers[0] == expected && mode_writes == 1);
            CHECK(wdt.wdt_dev.status == 1 && length_writes == 1 && kicks == 1);
            CHECK(mtk_wdt_stop(&wdt.wdt_dev) == 0);
            CHECK(registers[0] == (expected & ~1u) && mode_writes == 2);
            CHECK(wdt.wdt_dev.status == 0);
            reset(0x7d, disable, toprgu);
            wdt.wdt_dev.status = 1;
            CHECK(mtk_wdt_stop(&wdt.wdt_dev) == 0);
            CHECK(registers[0] == 0x2200007c && mode_writes == 1);
            CHECK(wdt.wdt_dev.status == 0 && kicks == 0 && length_writes == 0);
        }
    }
    puts("PASS adoption/start/stop and four DT property combinations");
    return 0;
}
