/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "abi.h"

#define IN
#define ASSERT(x) assert(x)
#define current get_current()
#define DBGLOG(module,level,format,...) snprintf(log_buffer,sizeof(log_buffer),format,##__VA_ARGS__)
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define APF_ATTRIBUTE_PROGRAM 3
#define PKT_OFLD_TYPE_APF 0
#define PKT_OFLD_OP_INSTALL 2
#define PKT_OFLD_OP_ENABLE 1
#define PKT_OFLD_OP_QUERY 3
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_INVALID_LENGTH 0xc0010014U
#define WLAN_STATUS_INVALID_DATA 0xc0010015U
#define VIR_MEM_TYPE 0
#define FALSE 0
#define TRUE 1
#define unlikely(x) (x)
#define kalMemZero(p,n) memset(p,0,n)
#define kalMemCopy(p,q,n) memcpy(p,q,n)

struct wiphy { int unused; };
struct wireless_dev { int unused; };
struct GLUE_INFO { unsigned u4ReadyFlag; };
struct ADAPTER { struct GLUE_INFO *prGlueInfo; };
struct CMD_INFO { unsigned fgIsOid,u4InformationBufferLength; void *pvInformationBuffer; };
struct nlattr { uint16_t nla_len,nla_type; };
struct sk_buff { unsigned unused; };

static struct GLUE_INFO glue={1};
static struct ADAPTER adapter={&glue};
static unsigned calls,copied,program_len,query_len,allocations,alloc_calls,replies,put_calls;
static unsigned completed,completion_status,completion_len;
static const char *mode;
static uint8_t *expected;
static uint8_t returned[APF_MAX_PROGRAM_LEN];
static unsigned frag_lengths[256],frag_seq[256],frag_count[256],frag_op[256];
static char log_buffer[512];

static struct GLUE_INFO *wlanGetGlueInfo(void) { return &glue; }
static unsigned nla_type(struct nlattr *p) { return p->nla_type&0x3fff; }
static unsigned nla_len(struct nlattr *p) { return p->nla_len-sizeof(*p); }
static int nla_ok(struct nlattr *p,int remaining) { return remaining>=(int)sizeof(*p) && p->nla_len>=sizeof(*p) && p->nla_len<=remaining; }
static void *nla_data(struct nlattr *p) { return p+1; }
static int is_mode(const char *name) { return strcmp(mode,name)==0 || (strchr(mode,'+') && strstr(mode,name)); }
static int fail_alloc(void) { alloc_calls++; return (is_mode("alloc1") && alloc_calls==1) || (is_mode("alloc2") && alloc_calls==2); }
static void *kalMemAlloc(unsigned n,int type) { if(fail_alloc())return NULL; allocations++; return malloc(n); }
static void kalMemFree(void *p,int type,unsigned n) { allocations--; free(p); }
static struct sk_buff *cfg80211_vendor_cmd_alloc_reply_skb(struct wiphy *w,unsigned n) { if(fail_alloc())return NULL; allocations++; return malloc(sizeof(struct sk_buff)); }
static void kfree_skb(struct sk_buff *p) { allocations--; free(p); }
static int cfg80211_vendor_cmd_reply(struct sk_buff *p) { replies++; kfree_skb(p); return 0; }
static int nla_put(struct sk_buff *p,unsigned type,unsigned len,const void *data) { put_calls++; if(is_mode("putfail"))return -1; assert(len<=sizeof(returned)); memcpy(returned,data,len); query_len=len; return 0; }
static void kalOidComplete(struct GLUE_INFO *g,struct CMD_INFO *cmd,unsigned n,unsigned status) { completed++; completion_len=n; completion_status=status; }
static void nicCmdEventQueryOfldInfo(struct ADAPTER *,struct CMD_INFO *,uint8_t *);
static void wlanoidSetOffloadInfo(void) {}
static void wlanoidQueryOffloadInfo(void) {}

static unsigned kalIoctl(struct GLUE_INFO *g,void (*oid)(void),struct PARAM_OFLD_INFO *p,unsigned size,int read,int wait,int command,unsigned *used) {
    calls++;
    assert(calls<=2);
    *used=size;
    if ((is_mode("fail1") && calls==1) || (is_mode("fail2") && calls==2)) return 1;
    if (oid==wlanoidSetOffloadInfo) {
        frag_lengths[calls-1]=p->u4BufLen;
        frag_seq[calls-1]=p->ucFragSeq;
        frag_count[calls-1]=p->ucFragNum;
        frag_op[calls-1]=p->ucOp;
        assert(memcmp(p->aucBuf,expected+copied,p->u4BufLen)==0);
        copied+=p->u4BufLen;
        return 0;
    }
    assert(size==sizeof(*p) && read && wait && command);
    assert(p->ucType==PKT_OFLD_TYPE_APF && p->ucOp==PKT_OFLD_OP_QUERY);
    assert(p->ucFragSeq==calls-1 && p->u4BufLen==PKT_OFLD_BUF_SIZE);
    if(calls==1)assert(p->ucFragNum==0 && p->u4TotalLen==0);
    struct CMD_OFLD_INFO response={0};
    unsigned offset=(calls-1)*PKT_OFLD_BUF_SIZE;
    response.u4TotalLen=program_len;
    response.u4BufLen=program_len-offset>PKT_OFLD_BUF_SIZE?PKT_OFLD_BUF_SIZE:program_len-offset;
    response.ucFragNum=DIV_ROUND_UP(program_len,PKT_OFLD_BUF_SIZE);
    response.ucFragSeq=calls-1;
    for(unsigned i=0;i<sizeof(response.aucBuf);i++)response.aucBuf[i]=is_mode("allzero")?0:(uint8_t)(offset+i);
    if(is_mode("firstsize"))response.u4BufLen=PKT_OFLD_BUF_SIZE+1;
    if(is_mode("firstseq"))response.ucFragSeq=1;
    if(is_mode("truncated") && (program_len<=PKT_OFLD_BUF_SIZE || calls==2))response.u4BufLen--;
    if(is_mode("bigtotal")){response.u4TotalLen=3000;response.u4BufLen=PKT_OFLD_BUF_SIZE;response.ucFragNum=2;}
    if(is_mode("zerobuf"))response.u4BufLen=0;
    if(is_mode("manyfrags"))response.ucFragNum=255;
    if(is_mode("count0"))response.ucFragNum=0;
    if(is_mode("count1"))response.ucFragNum=1;
    if(calls==2 && is_mode("secondseq"))response.ucFragSeq=0;
    if(calls==2 && is_mode("secondtotal"))response.u4TotalLen++;
    if(calls==2 && is_mode("secondcount"))response.ucFragNum++;
    if(is_mode("consumerbound")) {
        struct CMD_INFO cmd={1,sizeof(*p),p};
        nicCmdEventQueryOfldInfo(&adapter,&cmd,(uint8_t *)&response);
        *used=completion_len;
        return completion_status;
    }
    p->ucFragNum=response.ucFragNum;p->ucFragSeq=response.ucFragSeq;
    p->u4TotalLen=response.u4TotalLen;p->u4BufLen=response.u4BufLen;
    memcpy(p->aucBuf,response.aucBuf,sizeof(p->aucBuf));
    if(is_mode("shortinfo"))*used=sizeof(*p)-1;
    return 0;
}
