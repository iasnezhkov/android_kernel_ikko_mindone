// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static uint8_t input[2048];
static unsigned used;
static struct wiphy wiphy = { &glue };
static struct wireless_dev wdev;
static void reset(void)
{
    memset(input, 0, sizeof(input));
    used = refreshes = scans = adds = collected = fail_add = missing_scan = 0;
    allow_mutation = 0;
    enabled_error = 0;
    glue.u4ReadyFlag = glue.u4FWRoamingEnable = 1;
    wiphy.glue = &glue;
    for (unsigned i = 0; i < 32; i++) {
        descriptors[i].id = i;
        items[i].fgIsInFWKBlacklist = 1;
    }
}
static void add_attr(unsigned type, const void *bytes, unsigned length)
{
    CHECK(used + NLA_ALIGN(length + NLA_HDRLEN) <= sizeof(input));
    struct nlattr *attr = (void *)(input + used);
    attr->nla_len = NLA_HDRLEN + length;
    attr->nla_type = type;
    if (length) memcpy(input + used + NLA_HDRLEN, bytes, length);
    used += NLA_ALIGN(attr->nla_len);
}
static void count_attr(unsigned count)
{
    add_attr(WIFI_ATTRIBUTE_ROAMING_BLACKLIST_NUM, &count, sizeof(count));
}
static void bssid_attr(unsigned id, unsigned length)
{
    uint8_t bytes[16] = {2, 4, 6, 8, 10, id};
    CHECK(length <= sizeof(bytes));
    add_attr(WIFI_ATTRIBUTE_ROAMING_BLACKLIST_BSSID, bytes, length);
}
static void whitelist(unsigned count)
{
    add_attr(WIFI_ATTRIBUTE_ROAMING_WHITELIST_NUM, &count, sizeof(count));
    for (unsigned i = 0; i < count; i++) {
        char ssid[33] = {0};
        memset(ssid, 'a' + i, 32);
        add_attr(WIFI_ATTRIBUTE_ROAMING_WHITELIST_SSID, ssid, 33);
    }
}
static int invoke(int length)
{
    input_start = input;
    input_length = length > 0 ? (unsigned)length : 0;
    return mtk_cfg80211_vendor_config_roaming(&wiphy, &wdev, input, length);
}
static void rejected(void)
{
    CHECK(invoke(used) == -EINVAL);
    CHECK(refreshes == 0 && scans == 0 && adds == 0);
    for (unsigned i = 0; i < 32; i++) CHECK(items[i].fgIsInFWKBlacklist);
    cases++;
}
static void valid(unsigned count, unsigned reverse, unsigned white)
{
    reset();
    if (!reverse) count_attr(count);
    for (unsigned i = 0; i < count; i++) bssid_attr(i, 6);
    whitelist(white);
    if (reverse) count_attr(count);
    allow_mutation = 1;
    CHECK(invoke(used) == 0);
    CHECK(refreshes == 1 && scans == count && adds == count);
    for (unsigned i = 0; i < count; i++) CHECK(order[i] == i && items[i].fgIsInFWKBlacklist);
    for (unsigned i = count; i < 32; i++) CHECK(!items[i].fgIsInFWKBlacklist);
    cases++;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    bssid_type = WIFI_ATTRIBUTE_ROAMING_BLACKLIST_BSSID;
    bssid_limit = MAX_FW_ROAMING_BLACKLIST_SIZE;
    if (!strcmp(argv[1], "missing-count")) {
        reset(); whitelist(0); rejected(); return 0;
    }
    if (!strcmp(argv[1], "wrong-count")) {
        reset(); count_attr(2); bssid_attr(0, 6); whitelist(0); rejected(); return 0;
    }
    if (!strcmp(argv[1], "duplicate-count")) {
        reset(); count_attr(0); count_attr(0); rejected(); return 0;
    }
    CHECK(!strcmp(argv[1], "all"));
    for (unsigned count = 0; count <= 16; count++)
        for (unsigned reverse = 0; reverse < 2; reverse++)
            for (unsigned white = 0; white <= 8; white++) valid(count, reverse, white);
    for (unsigned declared = 0; declared <= 17; declared++) {
        for (unsigned actual = 0; actual <= 17; actual++) {
            if (declared == actual && declared <= 16) continue;
            reset(); count_attr(declared);
            for (unsigned i = 0; i < actual; i++) bssid_attr(i, 6);
            whitelist(0); rejected();
        }
    }
    reset(); whitelist(0); rejected();
    reset(); bssid_attr(0, 6); rejected();
    reset(); count_attr(0); count_attr(0); rejected();
    reset(); count_attr(1); count_attr(0); bssid_attr(0, 6); rejected();
    for (unsigned length = 0; length <= 12; length++) {
        if (length == 6) continue;
        reset(); count_attr(1); bssid_attr(0, length); rejected();
    }
    for (unsigned length = 0; length <= 8; length++) {
        if (length == 4) continue;
        reset(); unsigned count[2] = {0};
        add_attr(WIFI_ATTRIBUTE_ROAMING_BLACKLIST_NUM, count, length); rejected();
    }
    for (unsigned bytes = 1; bytes <= 3; bytes++) {
        reset(); count_attr(0); used += bytes; rejected();
    }
    reset(); count_attr(0);
    ((struct nlattr *)input)->nla_len = used + 4; rejected();
    reset(); count_attr(0);
    ((struct nlattr *)input)->nla_len = 1; rejected();
    reset(); count_attr(0);
    ((struct nlattr *)input)->nla_type |= 0x8000; rejected();
    reset(); count_attr(0);
    ((struct nlattr *)input)->nla_type = WIFI_ATTRIBUTE_MAX + 1; rejected();
    for (unsigned length = 1; length <= 28; length++) {
        reset(); count_attr(1); bssid_attr(0, 6); whitelist(0);
        CHECK(used == 28);
        allow_mutation = length == 18 || length == 19 || length == 20 || length == 28;
        CHECK(invoke(length) == (allow_mutation ? 0 : -EINVAL));
        CHECK(refreshes == allow_mutation && scans == allow_mutation && adds == allow_mutation);
        cases++;
    }
    reset(); count_attr(2); bssid_attr(0, 6); bssid_attr(0, 6);
    allow_mutation = 1;
    CHECK(invoke(used) == 0 && refreshes == 1 && scans == 2 && adds == 2);
    CHECK(order[0] == 0 && order[1] == 0 && items[0].fgIsInFWKBlacklist);
    cases++;
    reset(); count_attr(0); allow_mutation = 1; missing_scan = 2;
    used = 0; count_attr(3); bssid_attr(0, 6); bssid_attr(1, 6); bssid_attr(2, 6);
    CHECK(invoke(used) == 0 && refreshes == 1 && scans == 3 && adds == 2);
    CHECK(items[0].fgIsInFWKBlacklist && !items[1].fgIsInFWKBlacklist && items[2].fgIsInFWKBlacklist);
    cases++;
    reset(); count_attr(3); bssid_attr(0, 6); bssid_attr(1, 6); bssid_attr(2, 6);
    allow_mutation = 1; fail_add = 2;
    CHECK(invoke(used) == -ENOMEM && refreshes == 0 && scans == 0 && adds == 0);
    for (unsigned i = 0; i < 32; i++) CHECK(items[i].fgIsInFWKBlacklist);
    cases++;
    reset(); whitelist(0); glue.u4FWRoamingEnable = 0;
    CHECK(invoke(used) == 0 && refreshes == 0 && scans == 0 && adds == 0); cases++;
    reset(); count_attr(0); enabled_error = -ESHUTDOWN;
    CHECK(invoke(used) == -ESHUTDOWN && refreshes == 0 && scans == 0 && adds == 0); cases++;
    reset(); count_attr(0); glue.u4ReadyFlag = 0;
    CHECK(invoke(used) == -EFAULT && refreshes == 0); cases++;
    reset(); count_attr(0); wiphy.glue = NULL;
    CHECK(invoke(used) == -EINVAL && refreshes == 0); cases++;
    reset(); count_attr(0);
    CHECK(invoke(0) == -EINVAL && invoke(-1) == -EINVAL);
    CHECK(mtk_cfg80211_vendor_config_roaming(&wiphy, &wdev, NULL, used) == -EINVAL);
    CHECK(refreshes == 0); cases += 3;
    printf("PASS %u exact roaming handler cases\n", cases);
    return 0;
}
