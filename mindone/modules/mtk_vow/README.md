# VoW stock legacy ABI port

Source provenance: MediaTek Inc., copyright 2020, Michael Hsiao
<michael.hsiao@mediatek.com>, GPL-2.0. These sources are modified and reduced
from the Samsung A075F MediaTek 6.12 donor's drivers/misc/mediatek/vow/ver02
and sound/soc/mediatek/vow; see ../LICENSE for the GPL text. Source comments
were omitted at the executor request; this notice preserves attribution.

This is a review candidate, not a validated replacement. Model commands,
feature voting, keyword dispatch, version exchange and AFE allocation follow
the 6.12 donor. The local vow.h/vow_scp.h preserve the receiving tree's legacy
ver02 ABI and wire layout. Old-port facts retained: direct ioctl arguments,
control reset 6, seven-word model payloads, four-word start payloads,
32-bit Google version, zero transport param2, and bounds for signed handles.
The donor's 0xDEADBEEF ioctl wrapper, reset 7, nine-word payload array and
0x98 mailbox magic cannot be imposed on the stock HAL/stock wire contract.

The implementation is deliberately smaller than the donor: there is no
kernel filesystem dumping, sysfs debug control, 32-bit compat translation,
payload dump, stereo PCM transfer or donor PMIC-E1 efuse handling. Unsupported commands return errors.
The confirmed 64-bit stock HAL call sites are supported. Firmware protocol
compatibility, including keyword versus UUID recognition IDs and model
slot offsets, remains a hypothesis pending actual firmware testing.

SCP initialization happens in process context at first ioctl, never during
module insertion. Readiness waits at most 100 * 20 ms; IPI retries sleep and
are bounded at 50 * 2 ms, ACK waits at 100 ms, voice waits at 5 seconds.
SCP STOP invalidates the session, drains state through cancellable work and
requires RESET/INIT plus model reload. It never silently re-arms recognition.

DT source: mindone/dts/mt6789-tinysys.dtsi send_table channel 0/mailbox 0/9
words, recv_table channel 1/mailbox 0/2 words and channel 2/mailbox 0/26 words.
scp_rv.h names these IPI_OUT_AUDIO_VOW_1, IPI_IN_AUDIO_VOW_ACK_1 and
IPI_IN_AUDIO_VOW_1. No donor mailbox IDs are invented. scp_mem_tbl ID 1 is
0x4e300 bytes (VOW_MEM_ID); ID 5 is 0x5a00 (VOW_BARGEIN_MEM_ID). SCP getters
supply physical and mapped addresses. Initialization checks the entire
single-channel layout through custom-model end 0x451b8 fits ID 1.
M27 (measured by the lead): LK already supplies
/proc/device-tree/reserved-memory/mblock-22-reserve-memory-scp_share,
compatible mediatek,reserve-memory-scp_share, base 0x8f000000, size 0x7a0000.
This exceeds the table sum 0x519000. Do not add a DT reservation.
scp/scp_helper.c:1408-1429 finds scp_mem_key's compatible reserved node and
uses of_reserved_mem_lookup; :1508-1525 maps and partitions that region.
The getters at :1356-1384 return those partitions. vow.c uses all three
getters for VOW_MEM_ID; mtk_scp_vow.c uses them for VOW_BARGEIN_MEM_ID.
The device measurement is supplied evidence, not re-measured by this executor.

Both modules are deliberately non-removable: neither has module_exit.
Ordinary rmmod is rejected; reclaim requires reboot. Forced unload is unsupported.
This removes the unsafe concurrent-unload path without changing providers.
SCP's unregister can return while its notifier or pending registration still
references this module. Both AFE unregister APIs only unlink raw notifier nodes;
they do not wait for callbacks. A local callback counter or work flush cannot
protect code that the provider has fetched but not yet entered.

Provider audit (paths relative to mindone/modules):

| Registration | Provider evidence | Unregister safety | Final treatment |
| --- | --- | --- | --- |
| SCP READY/STOP | scp/scp_helper.c:465, :501, :1720 | STOP/READY uses blocking unregister; NOTIFYING queues removal at :508-513 and returns without waiting; registration also queues at :482-486 | Core is permanent; publish SCP notifier only after all fallible init steps |
| Audio receive and ACK handlers | mtk_tinysys_ipi/mtk_tinysys_ipi.c:261, :292; mtk_mbox/mtk_mbox.c:550, :591-600 | Generic unregister clears pin_buf without IRQ synchronization; ISR BUGs on a NULL buffer | Keep checked ISR-only mailbox-0 attachment; init rollback disables/drains IRQ, detaches both callbacks, retains SCP-device-managed receive sinks, then cancels work and frees wake source |
| AFE IPI send | mtk_afe_external/mtk_afe_external.c:58-78 | RAW chain; unregister at :67 only unlinks, no in-flight wait | Moved callback from bridge into permanent core; this is core's last fallible registration |
| AFE allocation | mtk_afe_external/mtk_afe_external.c:14-34 | RAW chain; unregister at :23 only unlinks, no in-flight wait | Permanent bridge has one registration, returns its error directly, no later failing operation |

Core init allocates the wake source, attaches mailbox callbacks, registers the
misc device, registers the AFE send notifier, then registers SCP notification.
Open returns -ENODEV until all registration finishes. Failures before notifier
publication deregister the misc device if present, detach/drain mailbox callbacks,
cancel work and release the wake source. No failed-init path calls unsafe notifier
unregister. No operation after successful AFE publication can return an init error;
SCP registration has a void API and its internal errors cannot be observed.
The bridge independently provides allocation; the core now owns PCM hw_free IPI.

The IRQ synchronization follows kernel/irq/manage.c:741-745: disable_irq calls
synchronize_irq. Process-context IPI callback paths test MBOX_CB_IN_PROCESS
(mtk_tinysys_ipi.c:428, :595), excluded by this port's checked ISR-only layout.
Provider registration writers must be serialized during module insertion: raw
notifier providers have no writer lock. The lead's serial module load order is
required; this patch does not repair provider-wide registration races.

Remaining blockers before integration/device acceptance:

- Exact Kleaf build, compiled imports/CRCs and complete module-set gate.
- Load-list/MODULES.tsv integration still needs application; BUILD targets and
  mindone_modules group membership are now included in port.patch.
- Firmware compatibility remains a hypothesis, including model offsets and IDs.
- Android permissions/SELinux, recognition/reset/process-death and power tests.
- Removable modules require provider-side synchronized unregister contracts;
  current candidate deliberately supports permanent residency only.

The node is mode 0600 until ueventd applies ownership. Android integration
must grant 0660 audioserver:media and SELinux access; media group alone with
the stock 0640 media:media mode cannot grant writes. The stock HAL itself
opens O_RDONLY, and ioctls must remain usable on that descriptor.

Earlier port verification (not repeated in this follow-up): stock vendor_a.img lib64 HAL extracted read-only with
debugfs and disassembled with llvm-objdump; all call sites named in the design
were checked. Checkpatch reported zero errors in the three C sources and
only the expected missing-SPDX-comment warnings. Source call tokens were
compared with gki/aarch64/symbols excluding mindone* and vendor exports.
Macro/inline expansion and actual undefined imports require the exact Kleaf
build, which was prohibited for this executor.

Build dependency follow-up against kernel HEAD e5d5a1f6cfe862377c2068a0e7c505595cbdb66b:
port.patch now includes the actual root mindone/modules/BUILD.bazel stanzas
and both entries in :mindone_modules. Core deps are :mindone_headers,
:mtk_afe_external, :mtk_tinysys_ipi and :scp; bridge deps are :mindone_headers,
:mtk_afe_external and :scp. The bridge needs no :mtk_vow dependency or local
VoW headers because it now only supplies allocation. No ALSA function is called.

Header ownership: :mindone_headers (BUILD.bazel:12-19) supplies include/**/*.h,
including the canonical AFE, SCP, IPI and mailbox headers. The symbol providers
are :mtk_afe_external (:3708), :mtk_tinysys_ipi (:5659) and :scp (:6490).
All include directories exist in the source tree (mtk_vow is added by this patch):
core uses mtk_vow plus include/sound/soc/mediatek/common,
include/drivers/misc/mediatek/scp/include and include/include; bridge uses only
the latter three. There are no donor include paths or guessed target names.

symbol-audit.tsv inventories every source call token, all eleven vendor symbol
references (including scp_ipidev), and kernel inline/macro leaf imports. Each
vendor reference has an EXPORT_SYMBOL[_GPL] in a source compiled by a direct
dependency. Kernel evidence is from gki/aarch64/symbols/* excluding mindone*;
this directory is common/gki/aarch64/symbols in the Kleaf workspace. Every
non-local call token is classified; pure macros/inlines are marked explicitly.
This is source evidence, not an nm/modpost result: compiler instrumentation,
configuration-specific helper imports and CRCs still need the exact Kleaf build.
In particular, of_node_put is inline with CONFIG_OF_DYNAMIC=n; enabling that
option would require a fresh KMI audit because that symbol is not listed.

Verification: port.patch applies cleanly with git apply --check to current HEAD.
BUILD stanzas match the local ddk_module form, all direct provider target names
and header directories were checked, and all classified external symbols have
provider/KMI evidence. No Bazel analysis, compilation or device test was run.
Prior checkpatch on three C sources had zero errors and three SPDX warnings.

Stock ELF evidence:
0xa26c 0x40045603 scalar 6 (RESET), x2 set at 0xa268.
0xaae0 0x40045604 pointer at x29-0x40; seven 8-byte fields, offsets
0 id, 8 keyword, 16 address, 24 size, 32 return_size_addr, 40 uuid, 48 data.
0x9f04 0x40045605 scalar model handle from w20.
0xd440 0x4004560c scalar 0.
0xd010 0x4004560f pointer sp+8; four 8-byte fields, handle, confidence,
dsp_inform_addr, dsp_inform_size_addr at offsets 0, 8, 16, 24.
0xd034 0x40045610 same 32-byte structure, final pointers zero.
0xa3f8 0x40045612 pointer sp+4, 4-byte version.
