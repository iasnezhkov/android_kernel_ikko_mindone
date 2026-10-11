int main(int argc,char **argv) {
    assert(argc==5);
    program_len=atoi(argv[2]);
    int expected_rc=atoi(argv[3]);
    mode=argv[4];
    struct wiphy w={0};struct wireless_dev d={0};
    if(!strcmp(argv[1],"set")) {
        unsigned capacity=sizeof(struct nlattr)+program_len;
        if(is_mode("shortheader"))capacity=1;
        struct nlattr *p=malloc(capacity);
        if(capacity>=sizeof(*p)) {
            p->nla_type=is_mode("wrongtype")?2:3;
            p->nla_len=sizeof(*p)+program_len;
            expected=(uint8_t *)(p+1);
            for(unsigned i=0;i<program_len;i++)expected[i]=(uint8_t)i;
            if(is_mode("truncatedattr"))p->nla_len++;
        }
        int rc=mtk_cfg80211_vendor_set_packet_filter(&w,&d,p,capacity);
        assert(rc==expected_rc);
        if(rc==0) {
            assert(copied==program_len);
            assert(calls==(program_len>PKT_OFLD_BUF_SIZE?2U:1U));
            assert(frag_op[calls-1]==PKT_OFLD_OP_ENABLE);
            assert(frag_seq[0]==0);
            assert(frag_count[0]==(program_len<PKT_OFLD_BUF_SIZE?0U:calls));
            if(calls==2)assert(frag_seq[1]==1 && frag_lengths[0]==PKT_OFLD_BUF_SIZE && frag_lengths[1]==program_len-PKT_OFLD_BUF_SIZE);
        } else if(expected_rc==-EINVAL)assert(calls==0);
        else assert(calls==(is_mode("fail2")?2U:1U));
        free(p);
    } else if(!strcmp(argv[1],"read")) {
        int rc=mtk_cfg80211_vendor_read_packet_filter(&w,&d,NULL,0);
        assert(rc==expected_rc && allocations==0);
        if(rc==0) {
            assert(replies==1 && query_len==program_len);
            for(unsigned i=0;i<program_len;i++)assert(returned[i]==(is_mode("allzero")?0:(uint8_t)i));
        } else assert(replies==0 && query_len==0);
    } else {
        struct PARAM_OFLD_INFO output,before;
        memset(&output,0xa5,sizeof(output));output.ucFragSeq=0;before=output;
        struct CMD_INFO cmd={1,sizeof(output),&output};
        struct CMD_OFLD_INFO event={0};
        event.ucFragNum=1;event.u4TotalLen=568;event.u4BufLen=568;
        memset(event.aucBuf,0x5a,sizeof(event.aucBuf));
        if(is_mode("shortout"))cmd.u4InformationBufferLength=1;
        if(is_mode("nullout"))cmd.pvInformationBuffer=NULL;
        if(is_mode("zerototal"))event.u4TotalLen=0;
        if(is_mode("zerobuf"))event.u4BufLen=0;
        if(is_mode("firstsize"))event.u4BufLen=PKT_OFLD_BUF_SIZE+1;
        if(is_mode("buftotal"))event.u4BufLen=569;
        if(is_mode("firstseq"))event.ucFragSeq=1;
        if(is_mode("nonoid"))cmd.fgIsOid=0;
        nicCmdEventQueryOfldInfo(&adapter,&cmd,(uint8_t *)&event);
        if(is_mode("nonoid"))assert(completed==0 && memcmp(&output,&before,sizeof(output))==0);
        else if(expected_rc==0) {
            assert(completed==1 && completion_status==0 && completion_len==sizeof(output));
            assert(output.u4BufLen==568 && output.aucBuf[567]==0x5a && output.aucBuf[568]==0xa5);
        } else {
            assert(completed==1 && completion_status==(unsigned)(expected_rc==1?WLAN_STATUS_INVALID_LENGTH:WLAN_STATUS_INVALID_DATA));
            assert(completion_len==0 && memcmp(&output,&before,sizeof(output))==0);
        }
    }
    puts("PASS");return 0;
}
