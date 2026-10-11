struct power_supply;
struct power_supply_desc {
    int (*set_property)(struct power_supply *, enum power_supply_property,
                        const union power_supply_propval *);
};
struct power_supply {
    atomic_t use_cnt;
    const struct power_supply_desc *desc;
    void *private;
    int changed;
};
struct mtk_charger {
    struct power_supply *psy1, *psy2, *psy_dvchg1, *psy_dvchg2, *chg_psy;
    struct charger_device *chg1_dev, *chg2_dev, *dvchg1_dev, *dvchg2_dev;
    struct charger_data chg_data[CHGS_SETTING_MAX];
    bool enable_hv_charging;
    int config, chr_type, usb_type, wakes;
};
static struct mtk_charger info;
static struct power_supply master, slave, unrelated;
static struct charger_device primary_device, secondary_device;
static struct charger_cooling_device cooler;
static struct thermal_cooling_device thermal;
static struct device device;
static int calls, failure_call, failure_errno;
static int call_property[8], call_value[8], call_owner[8];
static int wake_cc[8], wake_iin[8], wake_hv[8], wake_slave[8];
static int atomic_read(const atomic_t *value) { return *value; }
static void *power_supply_get_drvdata(struct power_supply *psy) { return psy->private; }
static void _wake_up_charger(struct mtk_charger *charger) {
    assert(charger->wakes < (int)ARRAY_SIZE(wake_cc));
    wake_cc[charger->wakes] = charger->chg_data[CHG1_SETTING].thermal_charging_current_limit;
    wake_iin[charger->wakes] = charger->chg_data[CHG1_SETTING].thermal_input_current_limit;
    wake_hv[charger->wakes] = charger->enable_hv_charging;
    wake_slave[charger->wakes] = charger->chg_data[CHG2_SETTING].thermal_charging_current_limit;
    charger->wakes++;
}
static void power_supply_changed(struct power_supply *psy) { psy->changed++; }
static int mtk_charger_enable_power_path(struct mtk_charger *charger, int idx, bool enable) {
    (void)charger; (void)idx; (void)enable; return 0;
}
static int mtk_charger_force_disable_power_path(struct mtk_charger *charger, int idx, bool enable) {
    (void)charger; (void)idx; (void)enable; return 0;
}
static int fixture_get_charging_current(struct charger_device *charger, u32 *value) {
    if (charger->cc_errno) return charger->cc_errno;
    *value = charger->cc; return 0;
}
static int fixture_get_input_current(struct charger_device *charger, u32 *value) {
    if (charger->iin_errno) return charger->iin_errno;
    *value = charger->iin; return 0;
}
static const struct charger_ops charger_ops = {
    .get_charging_current = fixture_get_charging_current,
    .get_input_current = fixture_get_input_current,
};
static struct chg_alg_device *get_chg_alg_by_name(const char *name) { (void)name; return NULL; }
static int chg_alg_is_algo_ready(struct chg_alg_device *alg) { return alg->state; }
static int power_supply_get_property(struct power_supply *psy, enum power_supply_property property,
                                     union power_supply_propval *value) {
    (void)psy; (void)property; (void)value; return -ENODEV;
}
static int charger_dev_is_charging_done(struct charger_device *charger, bool *value) {
    (void)charger; *value = false; return 0;
}
static int charger_dev_is_enabled(struct charger_device *charger, bool *value) {
    (void)charger; *value = true; return 0;
}
static int get_vbus(struct mtk_charger *charger) { (void)charger; return 5000; }
static int get_charger_zcv(struct mtk_charger *charger, struct charger_device *dev) {
    (void)charger; (void)dev; return -EOPNOTSUPP;
}
