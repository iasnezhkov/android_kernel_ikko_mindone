// SPDX-License-Identifier: GPL-2.0

#include "../hyn_core.h"

int hyn_protocol_begin(struct hyn_ts_data *ts_data)
{
    if (READ_ONCE(ts_data->stopping) || !READ_ONCE(ts_data->irq_registered))
        return -ENODEV;
    mutex_lock(&ts_data->mutex_control);
    disable_irq(ts_data->gpio_irq);
    mutex_lock(&ts_data->mutex_protocol);
    if (READ_ONCE(ts_data->stopping)) {
        mutex_unlock(&ts_data->mutex_protocol);
        enable_irq(ts_data->gpio_irq);
        mutex_unlock(&ts_data->mutex_control);
        return -ENODEV;
    }
    return 0;
}

int hyn_protocol_try_begin(struct hyn_ts_data *ts_data)
{
    if (READ_ONCE(ts_data->stopping) || !READ_ONCE(ts_data->irq_registered))
        return -ENODEV;
    if (!mutex_trylock(&ts_data->mutex_control))
        return -EAGAIN;
    disable_irq(ts_data->gpio_irq);
    if (!mutex_trylock(&ts_data->mutex_protocol)) {
        enable_irq(ts_data->gpio_irq);
        mutex_unlock(&ts_data->mutex_control);
        return -EAGAIN;
    }
    if (READ_ONCE(ts_data->stopping)) {
        mutex_unlock(&ts_data->mutex_protocol);
        enable_irq(ts_data->gpio_irq);
        mutex_unlock(&ts_data->mutex_control);
        return -ENODEV;
    }
    return 0;
}

void hyn_protocol_end(struct hyn_ts_data *ts_data)
{
    mutex_unlock(&ts_data->mutex_protocol);
    enable_irq(ts_data->gpio_irq);
    mutex_unlock(&ts_data->mutex_control);
}

void hyn_irq_set(struct hyn_ts_data *ts_data, u8 value)
{
    if (READ_ONCE(ts_data->stopping) || !READ_ONCE(ts_data->irq_registered))
        return;
    if(atomic_read(&ts_data->irq_is_disable) != value){
        if(value ==0){
			disable_irq(ts_data->gpio_irq);
			msleep(1); //wait switch
		}
        else{
			enable_irq(ts_data->gpio_irq);
		}
        atomic_set(&ts_data->irq_is_disable,value);
		// HYN_INFO("IRQ %d",value);
    }
}

void hyn_set_i2c_addr(struct hyn_ts_data *ts_data,u8 addr)
{
#ifdef I2C_PORT  
	ts_data->client->addr = addr;
#endif
}

u16 hyn_sum16(int val, u8* buf,u16 len)
{
	u16 sum = val;
	while(len--) sum += *buf++;
	return sum;
}

u32 hyn_sum32(int val, u32* buf,u16 len)
{
	u32 sum = val;
	while(len--) sum += *buf++;
	return sum;
}

void hyn_esdcheck_switch(struct hyn_ts_data *ts_data, u8 enable)
{
#if ESD_CHECK_EN
	if(IS_ERR_OR_NULL(ts_data->hyn_workqueue) || IS_ERR_OR_NULL(&ts_data->esdcheck_work)) return;
	if(enable){
		if (READ_ONCE(ts_data->stopping))
			return;
		ts_data->esd_fail_cnt = 0;
		queue_delayed_work(ts_data->hyn_workqueue, &ts_data->esdcheck_work,
						msecs_to_jiffies(1000));
	}
	else{
		cancel_delayed_work_sync(&ts_data->esdcheck_work);
	}
#endif
}

int copy_for_updata(struct hyn_ts_data *ts_data,u8 *buf,u32 offset,u16 len)
{
	int ret = -1;
	return ret;
}
	
int hyn_wait_irq_timeout(struct hyn_ts_data *ts_data, int msec)
{
    long ret;

    lockdep_assert_held(&ts_data->mutex_control);
    lockdep_assert_held(&ts_data->mutex_protocol);
    if (msec <= 0 || READ_ONCE(ts_data->work_mode) < DIFF_MODE)
        return -EINVAL;
    if (READ_ONCE(ts_data->stopping))
        return -ENODEV;
    atomic_set(&ts_data->hyn_irq_flg, 0);
    enable_irq(ts_data->gpio_irq);
    ret = wait_event_timeout(ts_data->wait_irq,
                             atomic_read(&ts_data->hyn_irq_flg) ||
                             READ_ONCE(ts_data->stopping),
                             msecs_to_jiffies(msec));
    disable_irq(ts_data->gpio_irq);
    if (READ_ONCE(ts_data->stopping))
        return -ENODEV;
    if (!ret)
        return -ETIMEDOUT;
    atomic_set(&ts_data->hyn_irq_flg, 0);
    return 0;
}

#define MAX_WROD_LEN	(64)
int get_word(u8**sc_str, u8* ds_str)
{
	u8 ch,cnt = 0,nul_flg = 0; 
	while(1){
		ch = **sc_str;
		*sc_str += 1;
		if((ch==' '&& nul_flg) || ch=='\t' || ch=='\0' || (ch=='\r' && **sc_str == '\n') || ch=='\n'|| ch==','|| ch=='=' || cnt>=MAX_WROD_LEN){
			*ds_str++ = '\0';
			break;
		}
		if(ch >= 'A' && ch <= 'Z') ch = ch + 'a' - 'A';
		if(ch!=' '){
			*ds_str++ = ch;
			nul_flg = 1;
		}
		cnt++;
	}
	return cnt;
}

int str_2_num(char *str,u8 type)
{
	int ret = 0,step = 0,flg = 0,cnt = 15;
	char ch;
	while(*str != '\0' && --cnt){
		ch = *str++;
		if(ch==' ') continue;
		else if(ch=='-' && step==0){
			flg = 1;
			continue;
		}
		if(type == 10){
			if(ch <= '9' && ch >= '0'){
				step++;
				ret *= 10;
				ret += (ch - '0');
			}
			else{
				cnt = 0;
				break;
			}
		}
		else{
			if(ch <= '9' && ch >= '0'){
				step++;
				ret *= 16;
				ret += (ch - '0');
			}
			else if(ch <= 'f' && ch >= 'a'){
				step++;
				ret *= 16;
				ret += (10 + ch - 'a');
			}
			else{
				cnt = 0;
				break;
			}
		}
	}
	// if(cnt == 0 || step==0)HYN_ERROR("failed");
	return (cnt == 0 || step==0) ? 0xAC5AC5AC : (flg ? -ret:ret);
}

static int hyn_get_threshold(char *filename,char *match_string,s16 *pstore, u16 len)
{
	int ret = 0;
	HYN_ENTER();
	return ret;
}


int factory_multitest(struct hyn_ts_data *ts_data ,char *cfg_path, u8 *data,s16 *test_th,u8 test_item)
{
    int ret = 0;
    struct tp_info *ic = &ts_data->hw_info;
    u16 mt_len = ic->fw_sensor_rxnum*ic->fw_sensor_txnum,i=0;
    u16 st_len = ic->fw_sensor_txnum + ic->fw_sensor_rxnum;
    s16 *raw_h,*raw_l,*raw_s,*scap_p,tmp=0,scap_fix;
    raw_h = (s16*)data;
    raw_l = raw_h + mt_len;
    raw_s = raw_l + mt_len;
    scap_p = raw_s + st_len;

    if(test_item & MULTI_OPEN_TEST){
    //judge low TH
		HYN_INFO("check MULTI_OPEN_TEST OpenMin");
        if(hyn_get_threshold(cfg_path,"TX0OpenMin",test_th,mt_len)){
            HYN_ERROR("read threshold failed");
            return FAC_GET_CFG_FAIL;
        }
        for(i = mt_len; i< mt_len; i++){
            if(*(raw_h+i)-*(raw_l+i) < test_th[i]) break;
        }
        if(i == mt_len){
            HYN_INFO("open low pass");
        }
        else{
            HYN_ERROR("open low test failed row= %d clo= %d %d<(th)%d"
                    ,i/ic->fw_sensor_rxnum,i%ic->fw_sensor_rxnum,*(raw_h+i)-*(raw_l+i),test_th[i]);
            return FAC_TEST_OPENL_FAIL;
        }
    //judge high TH
		HYN_INFO("check MULTI_OPEN_TEST OpenMax");
        if(hyn_get_threshold(cfg_path,"TX0OpenMax",test_th,mt_len)){
            HYN_ERROR("read threshold failed");
            return FAC_GET_CFG_FAIL;
        }
        for(i = 0; i< mt_len; i++){
            if(*(raw_h+i)-*(raw_l+i) > test_th[i]) break;
        }
        if(i == mt_len){
            HYN_INFO("open high pass");
        }
        else{
            HYN_ERROR("open high test failed row= %d clo= %d %d>(th)%d"
                    ,i/ic->fw_sensor_rxnum,i%ic->fw_sensor_rxnum,*(raw_h+i)-*(raw_l+i),test_th[i]);
            return FAC_TEST_OPENH_FAIL;
        }
    }

    if(test_item & MULTI_SHORT_TEST){
    //judge short TH
        if(hyn_get_threshold(cfg_path,"FactoryTxShortTh",&tmp,1)){
            HYN_ERROR("read threshold failed");
            return FAC_GET_CFG_FAIL;
        }
        for(i = 0; i< st_len; i++){
            if(*(raw_s+i) < tmp) break;
        }
        HYN_INFO("%s,shortth = %d",i == st_len ? "short test pass":"short test failed",tmp);
        if(i != st_len){
            return FAC_TEST_SHORT_FAIL;
        }
    }

    if(test_item & MULTI_SCAP_TEST){
    // self captest
        ret = hyn_get_threshold(cfg_path,"RxSCapScanMin",test_th,ic->fw_sensor_rxnum);
        ret |= hyn_get_threshold(cfg_path,"TxSCapScanMin",test_th+ic->fw_sensor_rxnum,ic->fw_sensor_txnum);
        ret |= hyn_get_threshold(cfg_path,"RxSCapScanMax",test_th+st_len,ic->fw_sensor_rxnum);
        ret |= hyn_get_threshold(cfg_path,"TxSCapScanMax",test_th+st_len+ic->fw_sensor_rxnum,ic->fw_sensor_txnum);
        if(ret){
            HYN_ERROR("read threshold failed");
            return FAC_GET_CFG_FAIL;
        }
        else{
            scap_fix = 128*(test_th[st_len+2] - test_th[2])/(abs(*(scap_p+2))+1);
            for(i = 0; i< st_len; i++){
                tmp = *(scap_p+i)*scap_fix/128;
                if(tmp < test_th[i] || tmp > test_th[st_len+i]) break;
            }
            if(i == st_len){
                HYN_INFO("scap test pass");
            }
            else{
                HYN_ERROR("scap test failed[%d] %d<%d>%d",i,test_th[i],tmp,test_th[st_len+i]);
                return FAC_TEST_SCAP_FAIL;
            }
        }
    }
    return 0;
}


int fac_test_log_save(char *log_name,struct hyn_ts_data *ts_data,s16 *test_data, int test_ret, u8 test_item)
{
	HYN_ENTER();
	return 0;
}









