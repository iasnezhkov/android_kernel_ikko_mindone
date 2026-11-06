/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_KBASE_MMU_FAULTS_DECODER_H_
#define _MALI_KBASE_MMU_FAULTS_DECODER_H_

#include <linux/types.h>
#include <mali_kbase.h>

#define SOURCE_ID_CORE_ID_SHIFT (9)
#define SOURCE_ID_CORE_ID_MASK (0x7F << SOURCE_ID_CORE_ID_SHIFT)
#define SOURCE_ID_UTLB_ID_SHIFT (8)
#define SOURCE_ID_UTLB_ID_MASK (0x01 << SOURCE_ID_UTLB_ID_SHIFT)
#define SOURCE_ID_CORE_TYPE_SHIFT (12)
#define SOURCE_ID_CORE_TYPE_MASK (0x0F << SOURCE_ID_CORE_TYPE_SHIFT)
#define SOURCE_ID_CORE_INDEX_SHIFT (6)
#define SOURCE_ID_CORE_INDEX_MASK (0x3F << SOURCE_ID_CORE_INDEX_SHIFT)

#define FAULT_SOURCE_ID_CORE_ID_GET(source_id) \
	((source_id & SOURCE_ID_CORE_ID_MASK) >> SOURCE_ID_CORE_ID_SHIFT)

#define FAULT_SOURCE_ID_UTLB_ID_GET(source_id) \
	((source_id & SOURCE_ID_UTLB_ID_MASK) >> SOURCE_ID_UTLB_ID_SHIFT)

#define FAULT_SOURCE_ID_CORE_TYPE_GET(source_id) \
	((source_id & SOURCE_ID_CORE_TYPE_MASK) >> SOURCE_ID_CORE_TYPE_SHIFT)

#define FAULT_SOURCE_ID_CORE_INDEX_GET(source_id) \
	((source_id & SOURCE_ID_CORE_INDEX_MASK) >> SOURCE_ID_CORE_INDEX_SHIFT)

unsigned int fault_source_id_internal_requester_get(struct kbase_device *kbdev,
						    unsigned int source_id);

const char *fault_source_id_internal_requester_get_str(struct kbase_device *kbdev,
						       unsigned int source_id,
						       unsigned int access_type);

const char *fault_source_id_core_type_description_get(struct kbase_device *kbdev,
						      unsigned int source_id);

#endif
