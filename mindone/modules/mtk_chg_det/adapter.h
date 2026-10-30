/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef MINDONE_CHR_ADAPTER_H
#define MINDONE_CHR_ADAPTER_H

void *mindone_chr_probe(void *pdev);
void mindone_chr_remove(void *binding);
int mindone_chr_driver_register(void);
void mindone_chr_driver_unregister(void);
int mindone_chr_boot_tag(void *pdev, unsigned char *tag);
int mindone_chr_irq_active(void *pdev);
void *mindone_chr_iio_get(void *pdev);
int mindone_chr_iio_read(void *channel, int *value);
void *mindone_chr_regmap_get(void *pdev);
int mindone_chr_regmap_read(void *map, unsigned int address, unsigned int *value);
void mindone_chr_regmap_put(void *map);
void *mindone_chr_bc12_get(void *pdev);
int mindone_chr_bc12_set_online(void *supply, int online);
int mindone_chr_bc12_type(void *supply, int *value);
int mindone_chr_bc12_usb_type(void *supply, int *value);
int mindone_chr_bc12_changed(void *supply, int online);
void mindone_chr_bc12_put(void *supply);
void mindone_chr_settle(void);
void mindone_chr_power_off(void);
void *mindone_chr_irq_register(void *pdev, void (*callback)(void *), void *context);
void mindone_chr_irq_unregister(void *irq);

#endif
