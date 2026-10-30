MediaTek SCP VoW bridge, copyright 2020 MediaTek Inc., Michael Hsiao
<michael.hsiao@mediatek.com>, GPL-2.0; modified from the Samsung A075F 6.12
sound/soc/mediatek/vow/mtk-scp-vow.c. See ../LICENSE and ../mtk_vow/README.md.
Allocation validates the full requested size against SCP ID 5's 0x5a00 bytes.
The SCP getters use the LK-provided shared reservation (lead measurement M27);
do not add another DT reservation. Unrelated notifications return NOTIFY_DONE.

This module is non-removable: no module_exit. Its allocation notifier provider
uses RAW_NOTIFIER_HEAD and unregister only unlinks without draining callbacks
(mtk_afe_external/mtk_afe_external.c:9, :14-34). Init has one fallible
registration and returns its error directly; there is no partial-registration
unwind. AFE IPI send notification now belongs to the permanent mtk_vow core,
which already owns the IPI transport. Reboot is required to reclaim either module.

Remaining blockers: exact-set build/import/CRC gates, load-list integration, firmware hypothesis, Android access, device and power tests.
Safe removability needs provider changes outside the authorized write scope.
See ../mtk_vow/README.md for the full provider audit and init failure ordering.

Build stanzas and group membership are included in ../mtk_vow/port.patch.
Bridge deps are :mindone_headers, :mtk_afe_external and :scp; see
../mtk_vow/symbol-audit.tsv for source and Google KMI symbol evidence.
