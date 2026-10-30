// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_pbha.h"

#include <device/mali_kbase_device.h>
#include <mali_kbase.h>

#if MALI_USE_CSF
#define DTB_SET_SIZE 2
#endif

static bool read_setting_valid(unsigned int prod_model, unsigned int id, unsigned int read_setting)
{
	switch (id) {

	case SYSC_ALLOC_ID_R_OTHER:
	case SYSC_ALLOC_ID_R_CSF:
	case SYSC_ALLOC_ID_R_MMU:
	case SYSC_ALLOC_ID_R_TILER_VERT:
	case SYSC_ALLOC_ID_R_TILER_PTR:
	case SYSC_ALLOC_ID_R_TILER_INDEX:
	case SYSC_ALLOC_ID_R_TILER_OTHER:
	case SYSC_ALLOC_ID_R_IC:
	case SYSC_ALLOC_ID_R_ATTR:
	case SYSC_ALLOC_ID_R_SCM:
	case SYSC_ALLOC_ID_R_FSDC:
	case SYSC_ALLOC_ID_R_PLR:
	case SYSC_ALLOC_ID_R_TEX:
	case SYSC_ALLOC_ID_R_LSC:
		break;
	case SYSC_ALLOC_ID_R_VL:
		if (prod_model == GPU_ID_PRODUCT_TTIX)
			return false;
		break;
	default:
		return false;
	}

	switch (read_setting) {

	case SYSC_ALLOC_L2_ALLOC:
	case SYSC_ALLOC_NEVER_ALLOC:
	case SYSC_ALLOC_ALWAYS_ALLOC:
	case SYSC_ALLOC_PTL_ALLOC:
	case SYSC_ALLOC_L2_PTL_ALLOC:
		return true;
	default:
		return false;
	}

	return false;
}

static bool write_setting_valid(unsigned int prod_model, unsigned int id,
				unsigned int write_setting)
{
	CSTD_UNUSED(prod_model);
	switch (id) {

	case SYSC_ALLOC_ID_W_OTHER:
	case SYSC_ALLOC_ID_W_CSF:
	case SYSC_ALLOC_ID_W_PCB:
	case SYSC_ALLOC_ID_W_TILER_PTR:
	case SYSC_ALLOC_ID_W_TILER_VERT_PLIST:
	case SYSC_ALLOC_ID_W_TILER_OTHER:
	case SYSC_ALLOC_ID_W_L2_EVICT:
	case SYSC_ALLOC_ID_W_L2_FLUSH:
	case SYSC_ALLOC_ID_W_TIB_COLOR:
	case SYSC_ALLOC_ID_W_TIB_COLOR_AFBCH:
	case SYSC_ALLOC_ID_W_TIB_COLOR_AFBCB:
	case SYSC_ALLOC_ID_W_TIB_CRC:
	case SYSC_ALLOC_ID_W_TIB_DS:
	case SYSC_ALLOC_ID_W_TIB_DS_AFBCH:
	case SYSC_ALLOC_ID_W_TIB_DS_AFBCB:
	case SYSC_ALLOC_ID_W_LSC:
		break;
	default:
		return false;
	}

	switch (write_setting) {

	case SYSC_ALLOC_L2_ALLOC:
	case SYSC_ALLOC_NEVER_ALLOC:
	case SYSC_ALLOC_ALWAYS_ALLOC:
	case SYSC_ALLOC_PTL_ALLOC:
	case SYSC_ALLOC_L2_PTL_ALLOC:
		return true;
	default:
		return false;
	}

	return false;
}

struct settings_status {

	bool overall;

	bool read;

	bool write;
};

static struct settings_status settings_valid(unsigned int prod_model, unsigned int id,
					     unsigned int read_setting, unsigned int write_setting)
{
	struct settings_status valid = { .overall = (id < GPU_SYSC_ALLOC_COUNT * sizeof(u32)) };

	if (valid.overall) {
		valid.read = read_setting_valid(prod_model, id, read_setting);
		valid.write = write_setting_valid(prod_model, id, write_setting);
		valid.overall = valid.read || valid.write;
	}

	return valid;
}

bool kbasep_pbha_supported(struct kbase_device *kbdev)
{
	return kbdev->gpu_props.gpu_id.arch_id >= GPU_ID_ARCH_MAKE(11, 0, 3);
}

int kbase_pbha_record_settings(struct kbase_device *kbdev, bool runtime, unsigned int id,
			       unsigned int read_setting, unsigned int write_setting)
{
	struct settings_status const valid = settings_valid(kbdev->gpu_props.gpu_id.product_model,
							    id, read_setting, write_setting);

	if (valid.overall) {
		unsigned int const sysc_alloc_num = id / sizeof(u32);
		u32 modified_reg;

#if MALI_USE_CSF
		if (runtime) {
			uint i;

			kbase_pm_context_active(kbdev);

			for (i = 0; i < GPU_SYSC_ALLOC_COUNT; i++)
				kbdev->sysc_alloc[i] =
					kbase_reg_read32(kbdev, GPU_SYSC_ALLOC_OFFSET(i));
			kbase_pm_context_idle(kbdev);
		}
#else
		CSTD_UNUSED(runtime);
#endif

		modified_reg = kbdev->sysc_alloc[sysc_alloc_num];

		switch (id % sizeof(u32)) {
		case 0:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC0_SET(modified_reg,
										 read_setting) :
							  modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC0_SET(modified_reg,
										  write_setting) :
							   modified_reg;
			break;
		case 1:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC1_SET(modified_reg,
										 read_setting) :
							  modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC1_SET(modified_reg,
										  write_setting) :
							   modified_reg;
			break;
		case 2:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC2_SET(modified_reg,
										 read_setting) :
							  modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC2_SET(modified_reg,
										  write_setting) :
							   modified_reg;
			break;
		case 3:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC3_SET(modified_reg,
										 read_setting) :
							  modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC3_SET(modified_reg,
										  write_setting) :
							   modified_reg;
			break;
		}

		kbdev->sysc_alloc[sysc_alloc_num] = modified_reg;
	}

	return valid.overall ? 0 : -EINVAL;
}

void kbase_pbha_write_settings(struct kbase_device *kbdev)
{
#if MALI_USE_CSF
	if (kbasep_pbha_supported(kbdev)) {
		uint i;

		for (i = 0; i < GPU_SYSC_ALLOC_COUNT; ++i)
			kbase_reg_write32(kbdev, GPU_SYSC_ALLOC_OFFSET(i), kbdev->sysc_alloc[i]);
	}
#else
	CSTD_UNUSED(kbdev);
#endif
}

#if MALI_USE_CSF
static int kbase_pbha_read_int_id_override_property(struct kbase_device *kbdev,
						    const struct device_node *pbha_node)
{
	u32 dtb_data[GPU_SYSC_ALLOC_COUNT * sizeof(u32) * DTB_SET_SIZE];
	int sz, i;
	bool valid = true;

	sz = of_property_count_elems_of_size(pbha_node, "int-id-override", sizeof(u32));

	if (sz == -EINVAL) {

		sz = of_property_count_elems_of_size(pbha_node, "int_id_override", sizeof(u32));
	}
	if (sz == -EINVAL) {

		return 0;
	}
	if (sz <= 0 || (sz % DTB_SET_SIZE != 0)) {
		dev_err(kbdev->dev, "Bad DTB format: pbha.int_id_override\n");
		return -EINVAL;
	}
	if (of_property_read_u32_array(pbha_node, "int-id-override", dtb_data, (size_t)sz) != 0) {

		if (of_property_read_u32_array(pbha_node, "int_id_override", dtb_data,
					       (size_t)sz) != 0) {
			dev_err(kbdev->dev, "Failed to read DTB pbha.int_id_override\n");
			return -EINVAL;
		}
	}

	for (i = 0; valid && i < sz; i = i + DTB_SET_SIZE) {
		unsigned int rdset = SYSC_ALLOC_R_SYSC_ALLOC0_GET(dtb_data[i + 1]);
		unsigned int wrset = SYSC_ALLOC_W_SYSC_ALLOC0_GET(dtb_data[i + 1]);
		valid = valid &&
			(kbase_pbha_record_settings(kbdev, false, dtb_data[i], rdset, wrset) == 0);
		if (valid)
			dev_info(kbdev->dev, "pbha.int_id_override 0x%x r0x%x w0x%x\n", dtb_data[i],
				 rdset, wrset);
	}
	if (i != sz || (!valid)) {
		dev_err(kbdev->dev, "Failed recording DTB data (pbha.int_id_override)\n");
		return -EINVAL;
	}
	return 0;
}

static int kbase_pbha_read_propagate_bits_property(struct kbase_device *kbdev,
						   const struct device_node *pbha_node)
{
	u8 bits = 0;
	int err;

	if (!kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PBHA_HWU))
		return 0;

	err = of_property_read_u8(pbha_node, "propagate-bits", &bits);

	if (err == -EINVAL) {
		err = of_property_read_u8(pbha_node, "propagate_bits", &bits);
	}

	if (err < 0) {
		if (err != -EINVAL) {
			dev_err(kbdev->dev,
				"DTB value for propagate_bits is improperly formed (err=%d)\n",
				err);
			return err;
		} else {

			kbdev->pbha_propagate_bits = 0;
			return 0;
		}
	}

	if (bits > (L2_CONFIG_PBHA_HWU_MASK >> L2_CONFIG_PBHA_HWU_SHIFT)) {
		dev_err(kbdev->dev, "Bad DTB value for propagate_bits: 0x%x\n", bits);
		return -EINVAL;
	}

	kbdev->pbha_propagate_bits = bits;
	return 0;
}
#endif

int kbase_pbha_read_dtb(struct kbase_device *kbdev)
{
#if MALI_USE_CSF
	const struct device_node *pbha_node;
	int err;

	if (!kbasep_pbha_supported(kbdev))
		return 0;

	pbha_node = of_get_child_by_name(kbdev->dev->of_node, "pbha");
	if (!pbha_node)
		return 0;

	err = kbase_pbha_read_int_id_override_property(kbdev, pbha_node);

	if (err < 0)
		return err;

	err = kbase_pbha_read_propagate_bits_property(kbdev, pbha_node);
	return err;
#else
	return 0;
#endif
}
