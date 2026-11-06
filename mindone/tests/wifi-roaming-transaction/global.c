// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#define CFG_SUPPORT_POWER_THROTTLING 1
#define CFG_SUPPORT_CNM_POWER_CTRL 1
#define CFG_SUPPORT_ROAMING 1
#define CFG_SUPPORT_SWCR 1
#define KAL_AIS_NUM 2
struct REG_INFO { int unused; };
static struct ADAPTER adapter;
static bool roles[2], scan_visible;
static int failed_role = -1;
static void cnmPowerControl(struct ADAPTER *a) { CHECK(a == &adapter); }
static void kalPwrLevelHdlrRegister(struct ADAPTER *a, void (*callback)(struct ADAPTER *)) { callback(a); }
static void cnmInit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void wmmInit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void rlmFsmEventInit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void scnInit(struct ADAPTER *a) { CHECK(a == &adapter); scan_visible = true; }
static void scnUninit(struct ADAPTER *a) { CHECK(a == &adapter); scan_visible = false; }
static void wmmUnInit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void rlmFsmEventUninit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void cnmUninit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void swCrDebugInit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void swCrDebugUninit(struct ADAPTER *a) { CHECK(a == &adapter); }
static void aisFsmInit(struct ADAPTER *a, unsigned index)
{
    CHECK(a == &adapter && index < 2 && a->rWifiVar.fgAisBlacklistInitialized);
    if ((int)index != failed_role) roles[index] = true;
}
static void aisFsmUninit(struct ADAPTER *a, unsigned index)
{
    CHECK(a == &adapter && index < 2);
    roles[index] = false;
}
static void aisInitializeConnectionSettings(struct ADAPTER *a, struct REG_INFO *reg, unsigned index)
{
    CHECK(a == &adapter && reg && index < 2);
}
static void roamingFsmInit(struct ADAPTER *a, unsigned index) { CHECK(a == &adapter && index < 2); }
static void roamingFsmUninit(struct ADAPTER *a, unsigned index) { CHECK(a == &adapter && index < 2); }
static void observed_free(void *pointer, unsigned kind, size_t size)
{
    CHECK(!scan_visible && !roles[0] && !roles[1]);
    kalMemFree(pointer, kind, size);
}
#define kalMemFree observed_free
#include "driver-mgmt.inc"
#undef kalMemFree
int main(void)
{
    (void)fake_time;
    struct REG_INFO reg = {0};
    memset(&adapter, 0, sizeof(adapter));
    nicUninitMGMT(&adapter);
    CHECK(!active_allocations && !adapter.rWifiVar.fgAisBlacklistInitialized);
    failed_role = 0;
    nicInitMGMT(&adapter, &reg);
    CHECK(!roles[0] && roles[1] && adapter.rWifiVar.fgAisBlacklistInitialized);
    struct AIS_BLACKLIST_ITEM *used = kalMemAlloc(sizeof(*used), VIR_MEM_TYPE);
    struct AIS_BLACKLIST_ITEM *spare = kalMemAlloc(sizeof(*spare), VIR_MEM_TYPE);
    CHECK(used && spare);
    memset(used, 0, sizeof(*used)); memset(spare, 0, sizeof(*spare));
    used->ucCount = 9; used->fgIsInFWKBlacklist = TRUE;
    LINK_INSERT_TAIL(&adapter.rWifiVar.rBlackList.rUsingLink, &used->rLinkEntry);
    LINK_INSERT_TAIL(&adapter.rWifiVar.rBlackList.rFreeLink, &spare->rLinkEntry);
    struct LINK_MGMT before = adapter.rWifiVar.rBlackList;
    failed_role = -1;
    nicInitMGMT(&adapter, &reg);
    CHECK(roles[0] && roles[1]);
    CHECK(!memcmp(&before, &adapter.rWifiVar.rBlackList, sizeof(before)) && active_allocations == 2);
    CHECK(used->ucCount == 9 && used->fgIsInFWKBlacklist);
    aisFsmUninit(&adapter, 0);
    CHECK(roles[1] && adapter.rWifiVar.fgAisBlacklistInitialized);
    CHECK(!memcmp(&before, &adapter.rWifiVar.rBlackList, sizeof(before)));
    aisFsmInit(&adapter, 0); aisFsmUninit(&adapter, 1); aisFsmInit(&adapter, 1);
    CHECK(!memcmp(&before, &adapter.rWifiVar.rBlackList, sizeof(before)));
    nicUninitMGMT(&adapter);
    CHECK(!active_allocations && !adapter.rWifiVar.fgAisBlacklistInitialized);
    nicUninitMGMT(&adapter);
    CHECK(!active_allocations && !adapter.rWifiVar.fgAisBlacklistInitialized);
    nicInitMGMT(&adapter, &reg);
    CHECK(roles[0] && roles[1] && adapter.rWifiVar.fgAisBlacklistInitialized);
    nicUninitMGMT(&adapter);
    CHECK(!active_allocations && !adapter.rWifiVar.fgAisBlacklistInitialized);
    printf("PASS dual-AIS partial-start, retained-reset and global cleanup MBO=%d\n", CFG_SUPPORT_MBO);
    return 0;
}
