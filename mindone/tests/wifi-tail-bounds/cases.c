// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "tx")) {
        unsigned counts[] = {1, 2, 3, 14, 64, 255};
        for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++)
            tx(counts[i]);
        printf("6 channel table scenarios passed; tail offset=%zu\n", offsetof(struct TX_PWR_CTRL_ELEMENT, rChlSettingList));
        return 0;
    }
    uint32_t offset = offsetof(struct PARAM_BSSID_EX, aucIEs);
    uint32_t size = offset + 64;
    struct PARAM_BSSID_EX *entry = calloc(1, size);
    assert(entry);
    entry->u4Length = size;
    entry->u4IELength = 64;
    uint8_t *ies;
    assert(wext_get_variable_ies(entry, size, &ies) == 0);
    assert(ies == (uint8_t *)entry + offset + 12);
    entry->u4IELength = 12;
    assert(wext_get_variable_ies(entry, size, &ies) == 0 && ies);
    entry->u4IELength = 0;
    assert(wext_get_variable_ies(entry, size, &ies) == 0 && !ies);
    for (unsigned length = 1; length < 12; length++) {
        entry->u4IELength = length;
        assert(wext_get_variable_ies(entry, size, &ies) == -EINVAL && !ies);
    }
    entry->u4IELength = 64;
    assert(wext_get_variable_ies(entry, offset - 1, &ies) == -EINVAL && !ies);
    assert(wext_get_variable_ies(entry, size - 1, &ies) == -EINVAL && !ies);
    entry->u4IELength = 65;
    assert(wext_get_variable_ies(entry, size, &ies) == -EINVAL && !ies);
    entry->u4Length = offset - 1;
    assert(wext_get_variable_ies(entry, size, &ies) == -EINVAL && !ies);
    entry->u4Length = size;
    entry->u4IELength = 64;
    assert(wext_get_variable_ies((void *)((uint8_t *)entry + size), 0, &ies) == -EINVAL && !ies);
    free(entry);
    uint32_t list_size = offset * 2 + 16;
    uint8_t *list = calloc(1, list_size);
    assert(list);
    struct PARAM_BSSID_EX *first = (void *)list;
    struct PARAM_BSSID_EX *second = (void *)(list + offset);
    first->u4Length = offset;
    first->u4IELength = 0;
    second->u4Length = offset + 16;
    second->u4IELength = 16;
    unsigned exported = 0, searched = 0;
    for (uint8_t *cursor = list; cursor < list + list_size;) {
        struct PARAM_BSSID_EX *record = (void *)cursor;
        if (wext_get_variable_ies(record, list_size - (cursor - list), &ies))
            break;
        exported++;
        if (ies) {
            assert(record->u4IELength - sizeof(struct PARAM_FIXED_IEs) == 4);
            searched++;
        }
        cursor += record->u4Length;
    }
    assert(exported == 2 && searched == 1);
    free(list);
    printf("20 scan IE boundary scenarios passed; wire offset=%u\n", offset);
    return 0;
}
