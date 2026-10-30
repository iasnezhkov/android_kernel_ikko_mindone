/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 */
#ifndef __GPUFREQ_MT6789_H__
#define __GPUFREQ_MT6789_H__

#define GPUFREQ_BRINGUP                 (0)

#define GPUFREQ_POWER_CTRL_ENABLE       (1)

#define GPUFREQ_DVFS_ENABLE             (1)
#define GPUFREQ_CUST_INIT_ENABLE        (0)
#define GPUFREQ_CUST_INIT_OPPIDX        (0)
#define GPUFREQ_SELF_CTRL_MTCMOS        (1)

#define POSDIV_2_MAX_FREQ               (1900000)
#define POSDIV_2_MIN_FREQ               (750000)
#define POSDIV_4_MAX_FREQ               (950000)
#define POSDIV_4_MIN_FREQ               (375000)
#define POSDIV_8_MAX_FREQ               (475000)
#define POSDIV_8_MIN_FREQ               (187500)
#define POSDIV_16_MAX_FREQ              (237500)
#define POSDIV_16_MIN_FREQ              (125000)
#define POSDIV_SHIFT                    (24)
#define DDS_SHIFT                       (14)
#define TO_MHZ_HEAD                     (100)
#define TO_MHZ_TAIL                     (10)
#define ROUNDING_VALUE                  (5)
#define MFGPLL_FIN                      (26)
#define MFGPLL_FH_PLL                   (6)
#define MFGPLL_CON0                     (g_apmixed_base + 0x268)
#define MFGPLL_CON1                     (g_apmixed_base + 0x26C)
#define MFGPLL_CON2                     (g_apmixed_base + 0x270)
#define MFGPLL_CON3                     (g_apmixed_base + 0x274)
#define CLK_MUX_OFS                     (0x1034)
#define CKMUX_SEL_REF_CORE              (4)
#define CKMUX_SEL_REF_PARK              (5)
#define CKMUX_SEL_REF_STACK             (7)
#define CKMUX_SEL_REF_STACK_PARK        (8)
#define GPUPLL_ID                       (0x0)
#define STACKPLL_ID                     (0x3)

#define GPUFREQ_FHCTL_ENABLE            (1)
#define MFG_PLL_NAME                    "mfgpll"

#define GPUFREQ_PDCv2_ENABLE            (0)
#define GPUFREQ_CHECK_MTCMOS_PWR_STATUS (0)
#define PWR_STATUS_OFS                  (0xF3C)
#define PWR_STATUS_2ND_OFS              (0xF40)
#define MFG_0_1_PWR_MASK                (0x6)
#define MFG_0_3_PWR_MASK                (0x1E)
#define MFG_0_4_PWR_MASK                (0x3E)
#define MFG_0_5_PWR_MASK                (0x7E)
#define MFG_1_5_PWR_MASK                (0x7C)
#define MFG_2_5_PWR_MASK                (0x78)

#define MFG2_SHADER_STACK0              (T0C0)
#define MFG3_SHADER_STACK2              (T2C0)

#define GPU_SHADER_PRESENT_1 \
	(MFG2_SHADER_STACK0)
#define GPU_SHADER_PRESENT_2 \
	(MFG2_SHADER_STACK0 | MFG3_SHADER_STACK2)

#define SHADER_CORE_NUM                 (2)
struct gpufreq_core_mask_info g_core_mask_table[] = {
	{2, GPU_SHADER_PRESENT_2},
	{1, GPU_SHADER_PRESENT_1},
};

#define GPU_ACT_REF_POWER               (977)
#define GPU_ACT_REF_FREQ                (1100000)
#define GPU_ACT_REF_VOLT                (85000)
#define GPU_LEAKAGE_POWER               (71)

#define VGPU_MAX_VOLT                   (129300)
#define VGPU_MIN_VOLT                   (50000)
#define VSRAM_MAX_VOLT                  (129300)
#define VSRAM_MIN_VOLT                  (50000)
#define PMIC_STEP                       (625)

#define MAX_BUCK_DIFF                   (20000)
#define MIN_BUCK_DIFF                   (0)

#define VSRAM_FIXED_THRESHOLD           (75000)
#define VSRAM_FIXED_VOLT                (75000)
#define VSRAM_FIXED_DIFF                (0)
#define VOLT_NORMALIZATION(volt) \
	((volt % 625) ? (volt - (volt % 625) + 625) : volt)

#define GPUFREQ_SAFE_VLOGIC             (60000)
#define VSRAM_LEVEL_0                   (75000)
#define VSRAM_LEVEL_1                   (80000)
#define SRAM_PARK_VOLT                  (75000)

#define GPUFREQ_AVS_ENABLE              (1)

#define GPUFREQ_ASENSOR_ENABLE          (0)
#define GPUFREQ_AGING_KEEP_FGPU         (670000)
#define GPUFREQ_AGING_KEEP_VGPU         (65000)
#define GPUFREQ_AGING_GAP_MIN           (-3)
#define GPUFREQ_AGING_GAP_1             (2)
#define GPUFREQ_AGING_GAP_2             (4)
#define GPUFREQ_AGING_GAP_3             (6)
#define GPUFREQ_AGING_MOST_AGRRESIVE    (0)

#define MFG1_PROT_STEP1_0_MASK          (BIT(21))
#define MFG1_PROT_STEP1_0_ACK_MASK      (BIT(21))
#define MFG1_PROT_STEP1_1_MASK          (BIT(5) | BIT(6))
#define MFG1_PROT_STEP1_1_ACK_MASK      (BIT(5) | BIT(6))
#define MFG1_PROT_STEP2_0_MASK          (BIT(21) | BIT(22))
#define MFG1_PROT_STEP2_0_ACK_MASK      (BIT(21) | BIT(22))
#define MFG1_PROT_STEP2_1_MASK          (BIT(7))
#define MFG1_PROT_STEP2_1_ACK_MASK      (BIT(7))

#define PWR_STATUS                      (g_sleep + 0x16C)
#define PWR_STATUS_2ND                  (g_sleep + 0x170)
#define MFG0_PWR_CON                    (g_sleep + 0x308)
#define MFG1_PWR_CON                    (g_sleep + 0x30C)
#define MFG2_PWR_CON                    (g_sleep + 0x310)
#define MFG3_PWR_CON                    (g_sleep + 0x314)

#define PWR_RST_B                       BIT(0)
#define PWR_ISO                         BIT(1)
#define PWR_ON                          BIT(2)
#define PWR_ON_2ND                      BIT(3)
#define PWR_CLK_DIS                     BIT(4)
#define SRAM_PDN                        BIT(8)
#define SRAM_PDN_ACK                    BIT(12)

#define MFG0_PWR_STA_MASK               BIT(2)
#define MFG1_PWR_STA_MASK               BIT(3)
#define MFG2_PWR_STA_MASK               BIT(4)
#define MFG3_PWR_STA_MASK               BIT(5)

#define INFRA_TOPAXI_PROTECTEN_SET      (g_infracfg_ao_base + 0x2A0)
#define INFRA_TOPAXI_PROTECTSTA1        (g_infracfg_ao_base + 0x228)
#define INFRA_TOPAXI_PROTECTEN_CLR      (g_infracfg_ao_base + 0x2A4)
#define INFRA_TOPAXI_PROTECTEN_1_SET    (g_infracfg_ao_base + 0x2A8)
#define INFRA_TOPAXI_PROTECTSTA1_1      (g_infracfg_ao_base + 0x258)
#define INFRA_TOPAXI_PROTECTEN_1_CLR    (g_infracfg_ao_base + 0x2AC)
#define INFRA_TOPAXI_PROTECTEN_2_SET    (g_infracfg_ao_base + 0x714)
#define INFRA_TOPAXI_PROTECTSTA1_2      (g_infracfg_ao_base + 0x724)
#define INFRA_TOPAXI_PROTECTEN_2_CLR    (g_infracfg_ao_base + 0x718)

enum gpufreq_segment {
	ENG_SEGMENT = 0,
	MT6789_SEGMENT = 1,
	MT6789T_SEGMENT = 2,
};

enum gpufreq_clk_src {
	CLOCK_MAIN = 0,
	CLOCK_SUB,
};

struct gpufreq_pmic_info {
	struct regulator *reg_vgpu;
	struct regulator *reg_vsram_gpu;
};

struct gpufreq_clk_info {
	struct clk *clk_mux;
	struct clk *clk_ref_mux;
	struct clk *clk_main_parent;
	struct clk *clk_sub_parent;
	struct clk *subsys_bg3d;
	struct clk *clk_fhctl;
};

struct gpufreq_mtcmos_info {
	struct device *mfg0_dev;
	struct device *mfg1_dev;
	struct device *mfg2_dev;
	struct device *mfg3_dev;
};

struct gpufreq_sb_info {
	int up;
	int down;
};

struct gpufreq_status {
	struct gpufreq_opp_info *signed_table;
	struct gpufreq_opp_info *working_table;
	struct gpufreq_sb_info *sb_table;
	int buck_count;
	int mtcmos_count;
	int cg_count;
	int power_count;
	unsigned int segment_id;
	int signed_opp_num;
	int segment_upbound;
	int segment_lowbound;
	int opp_num;
	int max_oppidx;
	int min_oppidx;
	int cur_oppidx;
	unsigned int cur_freq;
	unsigned int cur_volt;
	unsigned int cur_vsram;
};

#define SIGNED_OPP_GPU_NUM              ARRAY_SIZE(g_default_gpu)
struct gpufreq_opp_info g_default_gpu[] = {
	GPUOP(1100000, 90000, 90000, POSDIV_POWER_2, 1875, 0),
	GPUOP(1086000, 89375, 89375, POSDIV_POWER_2, 1875, 0),
	GPUOP(1072000, 88750, 88750, POSDIV_POWER_2, 1875, 0),
	GPUOP(1058000, 88125, 88125, POSDIV_POWER_2, 1875, 0),
	GPUOP(1045000, 87500, 87500, POSDIV_POWER_2, 1875, 0),
	GPUOP(1031000, 86875, 86875, POSDIV_POWER_2, 1875, 0),
	GPUOP(1017000, 86250, 86250, POSDIV_POWER_2, 1875, 0),
	GPUOP(1003000, 85625, 85625, POSDIV_POWER_2, 1875, 0),
	GPUOP(990000,  85000, 85000, POSDIV_POWER_2, 1875, 0),
	GPUOP(976000,  84375, 84375, POSDIV_POWER_2, 1875, 0),
	GPUOP(962000,  83125, 83125, POSDIV_POWER_2, 1875, 0),
	GPUOP(948000,  82500, 82500, POSDIV_POWER_4, 1875, 0),
	GPUOP(935000,  81875, 81875, POSDIV_POWER_4, 1875, 0),
	GPUOP(921000,  81250, 81250, POSDIV_POWER_4, 1875, 0),
	GPUOP(907000,  80625, 80625, POSDIV_POWER_4, 1875, 0),
	GPUOP(893000,  80000, 80000, POSDIV_POWER_4, 1875, 0),
	GPUOP(880000,  79375, 79375, POSDIV_POWER_4, 1875, 0),
	GPUOP(868000,  78750, 78750, POSDIV_POWER_4, 1875, 0),
	GPUOP(857000,  78125, 78125, POSDIV_POWER_4, 1875, 0),
	GPUOP(846000,  77500, 77500, POSDIV_POWER_4, 1875, 0),
	GPUOP(835000,  76875, 76875, POSDIV_POWER_4, 1250, 0),
	GPUOP(823000,  76250, 76250, POSDIV_POWER_4, 1250, 0),
	GPUOP(812000,  75625, 75625, POSDIV_POWER_4, 1250, 0),
	GPUOP(801000,  75625, 75625, POSDIV_POWER_4, 1250, 0),
	GPUOP(790000,  75000, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(778000,  74375, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(767000,  73750, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(756000,  73125, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(745000,  72500, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(733000,  71875, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(722000,  71250, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(711000,  70625, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(700000,  70000, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(674000,  70000, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(648000,  70000, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(622000,  69375, 75000, POSDIV_POWER_4, 1250, 0),
	GPUOP(596000,  69375, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(570000,  69375, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(545000,  68750, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(519000,  68750, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(493000,  68750, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(467000,  68125, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(441000,  68125, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(415000,  68125, 75000, POSDIV_POWER_4,  625, 0),
	GPUOP(390000,  67500, 75000, POSDIV_POWER_4,  625, 0),
};

#define SEGMENT_ADJ_NUM                 ARRAY_SIZE(g_segment_adj)
struct gpufreq_adj_info g_segment_adj[] = {
	ADJOP(25, 0, 65000, 0),
	ADJOP(26, 0, 64375, 0),
	ADJOP(27, 0, 64375, 0),
	ADJOP(28, 0, 63750, 0),
	ADJOP(29, 0, 63750, 0),
	ADJOP(30, 0, 63125, 0),
	ADJOP(31, 0, 63125, 0),
	ADJOP(32, 0, 62500, 0),
	ADJOP(33, 0, 62500, 0),
	ADJOP(34, 0, 61875, 0),
	ADJOP(35, 0, 61875, 0),
	ADJOP(36, 0, 61250, 0),
	ADJOP(37, 0, 61250, 0),
	ADJOP(38, 0, 60625, 0),
	ADJOP(39, 0, 60625, 0),
	ADJOP(40, 0, 60000, 0),
};

#define MCL50_ADJ_NUM                   ARRAY_SIZE(g_mcl50_adj)
struct gpufreq_adj_info g_mcl50_adj[] = {
	ADJOP(0, 0, 68750, VSRAM_LEVEL_0),
	ADJOP(1, 0, 68750, VSRAM_LEVEL_0),
	ADJOP(2, 0, 68125, VSRAM_LEVEL_0),
	ADJOP(3, 0, 67500, VSRAM_LEVEL_0),
	ADJOP(4, 0, 66875, VSRAM_LEVEL_0),
	ADJOP(5, 0, 66250, VSRAM_LEVEL_0),
	ADJOP(6, 0, 65625, VSRAM_LEVEL_0),
	ADJOP(7, 0, 65000, VSRAM_LEVEL_0),
	ADJOP(8, 0, 64375, 0),
	ADJOP(9, 0, 64375, 0),
	ADJOP(10, 0, 63750, 0),
	ADJOP(11, 0, 63125, 0),
	ADJOP(12, 0, 62500, 0),
	ADJOP(13, 0, 62500, 0),
	ADJOP(14, 0, 61875, 0),
	ADJOP(15, 0, 61250, 0),
	ADJOP(16, 0, 60625, 0),
	ADJOP(17, 0, 60625, 0),
	ADJOP(18, 0, 60000, 0),
	ADJOP(19, 0, 59375, 0),
	ADJOP(20, 0, 58750, 0),
	ADJOP(21, 0, 58750, 0),
	ADJOP(22, 0, 58125, 0),
	ADJOP(23, 0, 57500, 0),
	ADJOP(24, 0, 56875, 0),
	ADJOP(25, 0, 56875, 0),
	ADJOP(26, 0, 56875, 0),
	ADJOP(27, 0, 56875, 0),
	ADJOP(28, 0, 56250, 0),
	ADJOP(29, 0, 56250, 0),
	ADJOP(30, 0, 56250, 0),
	ADJOP(31, 0, 56250, 0),
	ADJOP(32, 0, 55625, 0),
	ADJOP(33, 0, 55625, 0),
	ADJOP(34, 0, 55625, 0),
	ADJOP(35, 0, 55625, 0),
	ADJOP(36, 0, 55000, 0),
	ADJOP(37, 0, 55000, 0),
	ADJOP(38, 0, 55000, 0),
	ADJOP(39, 0, 55000, 0),
	ADJOP(40, 0, 54375, 0),
};

#define AVS_ADJ_NUM                   ARRAY_SIZE(g_avs_adj)
struct gpufreq_adj_info g_avs_adj[] = {
	ADJOP(0,  0, 0, 0),
	ADJOP(32, 0, 0, 0),
	ADJOP(44, 0, 0, 0),
};

unsigned int g_aging_table[][SIGNED_OPP_GPU_NUM] = {
	{
		1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875,
		1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875, 1875,
		1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250,
		1250, 1250, 1250, 1250, 1250, 1250, 625,  625,  625,  625,
		625,  625,  625,  625,  625,
	},
	{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
	},
	{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
	},
	{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
	},
};

struct mt_gpufreq_power_table_info {
	unsigned int gpufreq_khz;
	unsigned int gpufreq_volt;
	unsigned int gpufreq_power;
};

#endif
