// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/thermal.h>
#include <linux/thermal.h>
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include "mali_kbase.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_ipa_simple.h"
#include "mali_kbase_ipa_debugfs.h"

#if MALI_USE_CSF

#define TOP_LEVEL_DYN_COEFF_SCALER (3)

#endif

#if MALI_UNIT_TEST

static int dummy_temp;

static int kbase_simple_power_model_get_dummy_temp(struct thermal_zone_device *tz, int *temp)
{
	CSTD_UNUSED(tz);

	*temp = READ_ONCE(dummy_temp);
	return 0;
}

#ifdef thermal_zone_get_temp
#undef thermal_zone_get_temp
#endif
#define thermal_zone_get_temp(tz, temp) kbase_simple_power_model_get_dummy_temp(tz, temp)

void kbase_simple_power_model_set_dummy_temp(int temp)
{
	WRITE_ONCE(dummy_temp, temp);
}
KBASE_EXPORT_TEST_API(kbase_simple_power_model_set_dummy_temp);

#endif

struct kbase_ipa_model_simple_data {
	u32 dynamic_coefficient;
	u32 static_coefficient;
	s32 ts[4];
	char tz_name[THERMAL_NAME_LENGTH];
	struct thermal_zone_device *gpu_tz;
	struct task_struct *poll_temperature_thread;
	int current_temperature;
	int temperature_poll_interval_ms;
};
#define FALLBACK_STATIC_TEMPERATURE 55000

static u32 calculate_temp_scaling_factor(s32 ts[4], s64 t)
{

	const s64 t2 = div_s64((t * t), 1000);

	const s64 t3 = div_s64((t * t2), 1000);

	const s64 res_big = ts[3] * t3
			    + ts[2] * t2
			    + ts[1] * t
			    + ts[0] * (s64)1000;

	s64 res_unclamped = div_s64(res_big, 1000);

	return clamp(res_unclamped, (s64)0, (s64)10000000);
}

static int poll_temperature(void *data)
{
	struct kbase_ipa_model_simple_data *model_data = (struct kbase_ipa_model_simple_data *)data;
	int temp;

	while (!kthread_should_stop()) {
		struct thermal_zone_device *tz = READ_ONCE(model_data->gpu_tz);

		if (tz) {
			int ret;

			ret = thermal_zone_get_temp(tz, &temp);
			if (ret) {
				pr_warn_ratelimited(
					"Error reading temperature for gpu thermal zone: %d\n",
					ret);
				temp = FALLBACK_STATIC_TEMPERATURE;
			}
		} else {
			temp = FALLBACK_STATIC_TEMPERATURE;
		}

		WRITE_ONCE(model_data->current_temperature, temp);

		msleep_interruptible(READ_ONCE(model_data->temperature_poll_interval_ms));
	}

	return 0;
}

static int model_static_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	u32 temp_scaling_factor;
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *)model->model_data;
	u64 coeff_big;
	int temp;

	temp = READ_ONCE(model_data->current_temperature);

	temp_scaling_factor = calculate_temp_scaling_factor(model_data->ts, temp);

	coeff_big = (u64)model_data->static_coefficient * (u64)temp_scaling_factor;
	*coeffp = div_u64(coeff_big, 1000000);

	return 0;
}

static int model_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *)model->model_data;

#if MALI_USE_CSF

	coeffp[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL] =
		model_data->dynamic_coefficient / TOP_LEVEL_DYN_COEFF_SCALER;
	coeffp[KBASE_IPA_BLOCK_TYPE_SHADER_CORES] = model_data->dynamic_coefficient;
#else
	*coeffp = model_data->dynamic_coefficient;
#endif

	return 0;
}

static int add_params(struct kbase_ipa_model *model)
{
	int err = 0;
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *)model->model_data;

	err = kbase_ipa_model_add_param_s32(model, "static-coefficient",
					    (s32 *)&model_data->static_coefficient, 1, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_s32(model, "dynamic-coefficient",
					    (s32 *)&model_data->dynamic_coefficient, 1, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_s32(model, "ts", model_data->ts, 4, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_string(model, "thermal-zone", model_data->tz_name,
					       sizeof(model_data->tz_name), true);
	if (err)
		goto end;

	model_data->temperature_poll_interval_ms = 200;
	err = kbase_ipa_model_add_param_s32(model, "temp-poll-interval-ms",
					    &model_data->temperature_poll_interval_ms, 1, false);

end:
	return err;
}

static int kbase_simple_power_model_init(struct kbase_ipa_model *model)
{
	int err;
	struct kbase_ipa_model_simple_data *model_data;

	model_data = kzalloc(sizeof(struct kbase_ipa_model_simple_data), GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model->model_data = (void *)model_data;

	model_data->current_temperature = FALLBACK_STATIC_TEMPERATURE;
	model_data->poll_temperature_thread = kthread_run(poll_temperature, (void *)model_data,
							  "mali-simple-power-model-temp-poll");
	if (IS_ERR(model_data->poll_temperature_thread)) {
		err = PTR_ERR(model_data->poll_temperature_thread);
		kfree(model_data);
		return err;
	}

	err = add_params(model);
	if (err) {
		kbase_ipa_model_param_free_all(model);
		kthread_stop(model_data->poll_temperature_thread);
		kfree(model_data);
	}

	return err;
}

static int kbase_simple_power_model_recalculate(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *)model->model_data;
	struct thermal_zone_device *tz;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	if (!strnlen(model_data->tz_name, sizeof(model_data->tz_name))) {
		model_data->gpu_tz = NULL;
		dev_warn(model->kbdev->dev,
			 "No thermal zone specified, will use the default temperature value of %u",
			 FALLBACK_STATIC_TEMPERATURE);
	} else {
		char tz_name[THERMAL_NAME_LENGTH];
		u32 string_len = strscpy(tz_name, model_data->tz_name, sizeof(tz_name));

		string_len += sizeof(char);

		KBASE_DEBUG_ASSERT(string_len <= sizeof(tz_name));
		CSTD_UNUSED(string_len);

		mutex_unlock(&model->kbdev->ipa.lock);
		tz = thermal_zone_get_zone_by_name(tz_name);
		mutex_lock(&model->kbdev->ipa.lock);

		if (IS_ERR_OR_NULL(tz)) {
			pr_warn_ratelimited(
				"Error %d getting thermal zone \'%s\', not yet ready?\n",
				PTR_ERR_OR_ZERO(tz), tz_name);
			return -EPROBE_DEFER;
		}

		if (strncmp(tz_name, model_data->tz_name, sizeof(tz_name)) == 0)
			model_data->gpu_tz = tz;
	}

	return 0;
}

static void kbase_simple_power_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *)model->model_data;

	kthread_stop(model_data->poll_temperature_thread);

	kfree(model_data);
}

struct kbase_ipa_model_ops kbase_simple_ipa_model_ops = {
	.name = "mali-simple-power-model",
	.init = &kbase_simple_power_model_init,
	.recalculate = &kbase_simple_power_model_recalculate,
	.term = &kbase_simple_power_model_term,
	.get_dynamic_coeff = &model_dynamic_coeff,
	.get_static_coeff = &model_static_coeff,
};
KBASE_EXPORT_TEST_API(kbase_simple_ipa_model_ops);
