// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
struct snapshot {
    struct ADAPTER adapter;
    struct BSS_DESC scan[16];
    struct AIS_BLACKLIST_ITEM *entries[32];
    struct AIS_BLACKLIST_ITEM bytes[32];
    unsigned count;
    OS_SYSTIME time;
};
static struct ADAPTER adapter;
static struct AIS_BLACKLIST_ITEM *initial_using[4], *initial_free[4];
static void capture(struct snapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->adapter = adapter;
    memcpy(snapshot->scan, scan_bss, sizeof(scan_bss));
    snapshot->time = fake_time;
    struct LINK *lists[] = {&adapter.rWifiVar.rBlackList.rUsingLink, &adapter.rWifiVar.rBlackList.rFreeLink};
    for (unsigned n = 0; n < 2; n++) {
        struct LINK_ENTRY *link;
        unsigned count = 0;
        LINK_FOR_EACH(link, lists[n]) {
            CHECK(snapshot->count < 32 && count++ < 32);
            struct AIS_BLACKLIST_ITEM *entry = LINK_ENTRY(link, struct AIS_BLACKLIST_ITEM, rLinkEntry);
            snapshot->entries[snapshot->count] = entry;
            memcpy(&snapshot->bytes[snapshot->count++], entry, sizeof(*entry));
        }
        CHECK(count == lists[n]->u4NumElem);
    }
}
static void unchanged(const struct snapshot *before)
{
    struct snapshot after;
    capture(&after);
    CHECK(memcmp(before, &after, sizeof(after)) == 0);
}
static struct AIS_BLACKLIST_ITEM *node(unsigned key, bool in_use)
{
    struct AIS_BLACKLIST_ITEM *entry = kalMemAlloc(sizeof(*entry), VIR_MEM_TYPE);
    CHECK(entry != NULL);
    memset(entry, 0, sizeof(*entry));
    COPY_MAC_ADDR(entry->aucBSSID, scan_bss[key].aucBSSID);
    COPY_SSID(entry->aucSSID, entry->ucSSIDLen, scan_bss[key].aucSSID, scan_bss[key].ucSSIDLen);
    entry->ucCount = 9;
    entry->rAddTime = 70 + key;
    entry->u2DeauthReason = 100 + key;
    entry->u2AuthStatus = 200 + key;
    entry->fgDeauthLastTime = TRUE;
    entry->fgIsInFWKBlacklist = TRUE;
#if CFG_SUPPORT_MBO
    entry->fgDisallowed = TRUE;
    entry->u2DisallowSec = 300 + key;
    entry->i4RssiThreshold = -20 - key;
#endif
    struct LINK *list = in_use ? &adapter.rWifiVar.rBlackList.rUsingLink : &adapter.rWifiVar.rBlackList.rFreeLink;
    LINK_INSERT_TAIL(list, &entry->rLinkEntry);
    return entry;
}
static void setup(unsigned using_count, unsigned free_count)
{
    CHECK(active_allocations == 0);
    memset(&adapter, 0, sizeof(adapter));
    memset(scan_bss, 0, sizeof(scan_bss));
    memset(initial_using, 0, sizeof(initial_using));
    memset(initial_free, 0, sizeof(initial_free));
    forbid_allocations = false;
    failed_at = allocation_calls = 0;
    fake_time = 1000;
    LINK_MGMT_INIT(&adapter.rWifiVar.rBlackList);
    adapter.rWifiVar.fgAisBlacklistInitialized = TRUE;
    for (unsigned i = 0; i < 16; i++) {
        scan_bss[i].aucBSSID[0] = 2;
        scan_bss[i].aucBSSID[5] = i;
        scan_bss[i].ucSSIDLen = i % 3;
        memset(scan_bss[i].aucSSID, 'a' + i, scan_bss[i].ucSSIDLen);
    }
    for (unsigned i = 0; i < using_count; i++) {
        initial_using[i] = node(i, true);
        scan_bss[i].prBlack = initial_using[i];
    }
    for (unsigned i = 0; i < free_count; i++)
        initial_free[i] = node(12 + i, false);
    scan_bss[5].prBlack = (struct AIS_BLACKLIST_ITEM *)(uintptr_t)1;
    if (free_count)
        scan_bss[6].prBlack = initial_free[0];
    if (using_count)
        scan_bss[7].prBlack = initial_using[0];
    allocation_calls = 0;
}
static void cleanup(void)
{
    forbid_allocations = false;
    LINK_MGMT_UNINIT(&adapter.rWifiVar.rBlackList, struct AIS_BLACKLIST_ITEM, VIR_MEM_TYPE);
    CHECK(active_allocations == 0);
}
static void committed(unsigned using_count, unsigned free_count, const unsigned touches[16], const struct snapshot *before)
{
    unsigned new_count = 0;
    for (unsigned i = using_count; i < 16; i++)
        new_count += touches[i] != 0;
    CHECK(adapter.rWifiVar.rBlackList.rUsingLink.u4NumElem == using_count + new_count);
    CHECK(adapter.rWifiVar.rBlackList.rFreeLink.u4NumElem == (free_count > new_count ? free_count - new_count : 0));
    unsigned seen = 0;
    struct LINK_ENTRY *link;
    LINK_FOR_EACH(link, &adapter.rWifiVar.rBlackList.rUsingLink) {
        CHECK(seen++ < 32);
        struct AIS_BLACKLIST_ITEM *entry = LINK_ENTRY(link, struct AIS_BLACKLIST_ITEM, rLinkEntry);
        unsigned key = entry->aucBSSID[5];
        CHECK(key < 16 && EQUAL_MAC_ADDR(entry->aucBSSID, scan_bss[key].aucBSSID));
        CHECK(EQUAL_SSID(entry->aucSSID, entry->ucSSIDLen, scan_bss[key].aucSSID, scan_bss[key].ucSSIDLen));
        CHECK(entry->fgIsInFWKBlacklist == (touches[key] != 0));
        unsigned count = (key < using_count ? 9 : 0) + touches[key];
        CHECK(entry->ucCount == (count > 10 ? 10 : count));
        if (touches[key]) {
            CHECK(entry->rAddTime == 1001 && scan_bss[key].prBlack == entry);
        } else {
            CHECK(entry->rAddTime == 70 + key);
        }
        if (key < using_count) {
            CHECK(entry == initial_using[key]);
            CHECK(entry->u2DeauthReason == 100 + key && entry->u2AuthStatus == 200 + key && entry->fgDeauthLastTime);
#if CFG_SUPPORT_MBO
            CHECK(entry->fgDisallowed && entry->u2DisallowSec == 300 + key && entry->i4RssiThreshold == -20 - (int)key);
#endif
        } else {
            CHECK(!entry->u2DeauthReason && !entry->u2AuthStatus && !entry->fgDeauthLastTime);
#if CFG_SUPPORT_MBO
            CHECK(!entry->fgDisallowed && !entry->u2DisallowSec && !entry->i4RssiThreshold);
#endif
        }
    }
    CHECK(seen == using_count + new_count && fake_time == 1001);
    for (unsigned i = 0; i < 16; i++)
        if (!touches[i]) CHECK(scan_bss[i].prBlack == before->scan[i].prBlack);
}
static void exercise(unsigned using_count, unsigned free_count, unsigned count, unsigned failure, bool duplicate, bool abort_plan)
{
    setup(using_count, free_count);
    uint8_t bssid[CFG_FW_ROAMING_BLACKLIST_SIZE][MAC_ADDR_LEN];
    unsigned touches[16] = {0};
    for (unsigned i = 0; i < count; i++) {
        unsigned key = duplicate ? i % 4 : i;
        COPY_MAC_ADDR(bssid[i], scan_bss[key].aucBSSID);
        touches[key]++;
    }
    struct snapshot before;
    capture(&before);
    unsigned baseline_allocations = active_allocations;
    failed_at = failure;
    struct AIS_FWK_BLACKLIST_PLAN *plan = (void *)(uintptr_t)1;
    int result = aisPrepareFwkBlacklist(&adapter, count, bssid, &plan);
    unchanged(&before);
    if (result) {
        CHECK(result == -ENOMEM && plan == (void *)(uintptr_t)1);
        CHECK(active_allocations == baseline_allocations);
    } else if (abort_plan) {
        aisAbortFwkBlacklist(plan);
        unchanged(&before);
        CHECK(active_allocations == baseline_allocations);
    } else {
        forbid_allocations = true;
        aisCommitFwkBlacklist(plan);
        committed(using_count, free_count, touches, &before);
    }
    checked_cases++;
    cleanup();
}
int main(void)
{
    alarm(20);
    for (unsigned used = 0; used < 4; used++)
        for (unsigned spare = 0; spare < 4; spare++)
            for (unsigned count = 0; count <= 16; count++)
                for (unsigned fail = 0; fail <= 17; fail++) {
                    exercise(used, spare, count, fail, false, false);
                    exercise(used, spare, count, fail, true, false);
                    exercise(used, spare, count, fail, true, true);
                }
    setup(1, 1);
    uint8_t unknown[1][MAC_ADDR_LEN] = {{9, 9, 9, 9, 9, 9}};
    struct AIS_FWK_BLACKLIST_PLAN *plan = NULL;
    CHECK(aisPrepareFwkBlacklist(&adapter, 1, unknown, &plan) == 0);
    forbid_allocations = true;
    aisCommitFwkBlacklist(plan);
    CHECK(!initial_using[0]->fgIsInFWKBlacklist);
    cleanup();
    setup(1, 1);
    struct snapshot before;
    capture(&before);
    plan = (void *)(uintptr_t)1;
    CHECK(aisPrepareFwkBlacklist(&adapter, 17, unknown, &plan) == -EINVAL);
    CHECK(aisPrepareFwkBlacklist(NULL, 0, NULL, &plan) == -EINVAL);
    CHECK(aisPrepareFwkBlacklist(&adapter, 1, NULL, &plan) == -EINVAL);
    CHECK(aisPrepareFwkBlacklist(&adapter, 0, NULL, NULL) == -EINVAL);
    unchanged(&before);
    CHECK(plan == (void *)(uintptr_t)1 && allocation_calls == 0);
    adapter.rWifiVar.fgAisBlacklistInitialized = FALSE;
    CHECK(aisPrepareFwkBlacklist(&adapter, 0, NULL, &plan) == -ESHUTDOWN);
    adapter.rWifiVar.fgAisBlacklistInitialized = TRUE;
    unchanged(&before);
    scan_bss[0].ucSSIDLen = 33;
    capture(&before);
    uint8_t known[1][MAC_ADDR_LEN];
    COPY_MAC_ADDR(known[0], scan_bss[0].aucBSSID);
    CHECK(aisPrepareFwkBlacklist(&adapter, 1, known, &plan) == -EINVAL);
    unchanged(&before);
    CHECK(active_allocations == 2);
    cleanup();
    setup(1, 0);
    initial_using[0]->ucSSIDLen = 5;
    memcpy(initial_using[0]->aucSSID, "other", 5);
    capture(&before);
    COPY_MAC_ADDR(known[0], scan_bss[0].aucBSSID);
    CHECK(aisPrepareFwkBlacklist(&adapter, 1, known, &plan) == 0);
    unchanged(&before);
    forbid_allocations = true;
    aisCommitFwkBlacklist(plan);
    CHECK(adapter.rWifiVar.rBlackList.rUsingLink.u4NumElem == 2);
    CHECK(scan_bss[0].prBlack != initial_using[0] && scan_bss[0].prBlack->fgIsInFWKBlacklist);
    CHECK(!initial_using[0]->fgIsInFWKBlacklist && initial_using[0]->ucCount == 9);
    CHECK(initial_using[0]->ucSSIDLen == 5 && !memcmp(initial_using[0]->aucSSID, "other", 5));
    cleanup();
    setup(2, 0);
    COPY_MAC_ADDR(initial_using[1]->aucBSSID, scan_bss[0].aucBSSID);
    COPY_SSID(initial_using[1]->aucSSID, initial_using[1]->ucSSIDLen, scan_bss[0].aucSSID, scan_bss[0].ucSSIDLen);
    scan_bss[0].prBlack = initial_using[1];
    capture(&before);
    CHECK(aisPrepareFwkBlacklist(&adapter, 1, known, &plan) == 0);
    unchanged(&before);
    forbid_allocations = true;
    aisCommitFwkBlacklist(plan);
    CHECK(scan_bss[0].prBlack == initial_using[1]);
    CHECK(initial_using[1]->fgIsInFWKBlacklist && initial_using[1]->ucCount == 10);
    CHECK(!initial_using[0]->fgIsInFWKBlacklist && initial_using[0]->ucCount == 9);
    cleanup();
    printf("PASS %u exact AIS transaction cases MBO=%d\n", checked_cases + 10, CFG_SUPPORT_MBO);
    return 0;
}
