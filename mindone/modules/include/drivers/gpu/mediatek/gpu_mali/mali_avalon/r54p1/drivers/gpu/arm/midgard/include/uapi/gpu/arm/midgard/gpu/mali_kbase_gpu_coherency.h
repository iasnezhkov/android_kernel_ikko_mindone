/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UAPI_KBASE_GPU_COHERENCY_H_
#define _UAPI_KBASE_GPU_COHERENCY_H_

#if defined(__cplusplus)
extern "C" {
#endif

#define COHERENCY_ACE_LITE 0U
#define COHERENCY_ACE 1U
#define COHERENCY_NONE 31U
#define COHERENCY_FEATURE_BIT(x) (1U << (x))

#if defined(__cplusplus)
}
#endif

#endif
