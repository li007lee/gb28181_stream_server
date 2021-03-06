/*
 * rtp_pack.c
 *
 *  Created on: Jun 5, 2018
 *      Author: root
 */

#include "stream/rtp_pack.h"
#include "stream_hash.h"

extern STREAM_HASH_TABLE_HANDLE glStreamHashTable;

////////////////////////////////////////////////////////////////////////////////
// 函数名：rtsp_random
// 描  述：获取长整形的随机数
// 参  数：无
// 返回值：成功返回获取的随机数，出错返回0
// 说  明：
////////////////////////////////////////////////////////////////////////////////
HB_S32 rtsp_random(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);

	srandom((tv.tv_sec * 1000) + (tv.tv_usec / 1000));

	return random();
}

////////////////////////////////////////////////////////////////////////////////
// 函数名：rtsp_random32
// 描  述：获取无符号整形的随机数
// 参  数：无
// 返回值：成功返回获取的随机数，出错返回0
// 说  明：
////////////////////////////////////////////////////////////////////////////////
HB_U32 rtsp_random32(void)
{
  /* Return a 32-bit random number.
     Because "rtsp_random()" returns a 31-bit random number, we call it a second time,
     to generate the high bit. (Actually, to increase the likelhood of randomness,
     we take the middle 16 bits of two successive calls to "rtsp_random()")
  */
  HB_S32 random_1 = rtsp_random();
  HB_U32 random16_1 = (unsigned int)(random_1 & 0x00FFFF00);

  HB_S32 random_2 = rtsp_random();
  HB_U32 random16_2 = (unsigned int)(random_2 & 0x00FFFF00);

  return (random16_1 << 8) | (random16_2 >> 8);
}


HB_VOID rtp_info_init(rtp_info_t *rtp_info, HB_S32 payload, HB_U32 u32Ssrc)
{
	memset(rtp_info, 0x00, sizeof(rtp_info_t));

	//初始化RTP信息
	rtp_info->rtp_hdr.payload = payload;
//	rtp_info->rtp_hdr.ssrc = rtsp_random32();
	rtp_info->rtp_hdr.ssrc = u32Ssrc;
	rtp_info->rtp_hdr.version = RTP_VERSION;
	rtp_info->rtp_hdr.seq_no = 0;
	rtp_info->rtp_hdr.timestamp = 0;
	return;
}


////////////////////////////////////////////////////////////////////////////////
// 函数名：get_NAL_from_frame
// 描  述：从一个NALU数据中获取一包RTP数据
// 参  数：[in/out] rtp_info      RTP信息结构体
//         [out]    p_packet_size 获取到的RTP包的大小
// 返回值：获取到的RTP包的地址
// 说  明：
////////////////////////////////////////////////////////////////////////////////
static HB_CHAR *get_rtp_ps_pack(rtp_info_t *rtp_info, unsigned int *p_packet_size)
{
	HB_S32 bytes_left = 0, max_size = 0;
	HB_CHAR *cp = NULL, *cp2 = NULL;

	//判断是否到达结尾
	if(rtp_info->end == rtp_info->nal.end)
	{
		*p_packet_size = 0;
		return NULL;
	}

	//判断是否是一个NALU的开始
	if(rtp_info->beginNAL)
	{
		rtp_info->beginNAL = FALSE;
	}
	else
	{
		//设置上一个RTP包的结束位置为此RTP包的开始位置
		rtp_info->start = rtp_info->end;// continue with the next RTP-FU packet
	}

	//计算剩余的字节数
	bytes_left = rtp_info->nal.end - rtp_info->start;

	//设置RTP包数据最大的字节数
	max_size = MAX_RTP_PACKET_SIZE; // sizeof(basic rtp header) == 12 bytes
#if 0
	if(rtp_info->FU_flag)
	{
		max_size -= 2; //FU分包的情况，RTP Header为14字节，RTP包数据最大的字节数需要减2
	}
#endif
	//判断是否是最后一包数据
	if(bytes_left > max_size)
	{
		rtp_info->end = rtp_info->start + max_size; //limit RTP packet size to 1472 bytes
	}
	else
	{
		rtp_info->end = rtp_info->start + bytes_left;
	}

	//判断是否是FU分包
	if(rtp_info->FU_flag)
	{
		//判断是否到达结尾，设置FU Header的结束位
		if(rtp_info->end == rtp_info->nal.end)
		{
			rtp_info->e_bit = 1;
		}
	}

	//设置一帧数据最后一包数据的标识位
	rtp_info->rtp_hdr.marker = rtp_info->nal.eoFrame ? 1 : 0;
	if(rtp_info->FU_flag && !rtp_info->e_bit)
	{
		rtp_info->rtp_hdr.marker = 0;
	}

	//设置RTP Header以及RTP数据的指针
	cp = rtp_info->start;
	cp -= 12;
	rtp_info->pRTP = cp;

	cp2 = (HB_CHAR *)&rtp_info->rtp_hdr;
	cp[0] = cp2[0] ;
	cp[1] = cp2[1] ;

	//设置序列号
	cp[2] = (rtp_info->rtp_hdr.seq_no >> 8) & 0xff;
	cp[3] = rtp_info->rtp_hdr.seq_no & 0xff;

	//设置时间戳
	cp[4] = (rtp_info->rtp_hdr.timestamp >> 24) & 0xff;
	cp[5] = (rtp_info->rtp_hdr.timestamp >> 16) & 0xff;
	cp[6] = (rtp_info->rtp_hdr.timestamp >> 8) & 0xff;
	cp[7] = rtp_info->rtp_hdr.timestamp & 0xff;

	//设置源标识
	cp[8] =  (rtp_info->rtp_hdr.ssrc >> 24) & 0xff;
	cp[9] =  (rtp_info->rtp_hdr.ssrc >> 16) & 0xff;
	cp[10] = (rtp_info->rtp_hdr.ssrc >> 8) & 0xff;
	cp[11] = rtp_info->rtp_hdr.ssrc & 0xff;

	//序列号累加
	if(rtp_info->rtp_hdr.seq_no < 65535)
	{
		rtp_info->rtp_hdr.seq_no++;
	}
	else
	{
		rtp_info->rtp_hdr.seq_no = 0;
	}
#if 0
	/*!
	* \n The FU indicator octet has the following format:
	* \n
	* \n      +---------------+
	* \n MSB  |0|1|2|3|4|5|6|7|  LSB
	* \n      +-+-+-+-+-+-+-+-+
	* \n      |F|NRI|  Type   |
	* \n      +---------------+
	* \n
	* \n The FU header has the following format:
	* \n
	* \n      +---------------+
	* \n      |0|1|2|3|4|5|6|7|
	* \n      +-+-+-+-+-+-+-+-+
	* \n      |S|E|R|  Type   |
	* \n      +---------------+
	*/
	//设置FU Header
	if(rtp_info->FU_flag)
	{
		// FU indicator  F|NRI|Type
      cp[12] = cp[13] = 0;
		//设置RTP分包方式为FU_A，28
		cp[12] = (rtp_info->nal.type & 0xe0) | 28;//Type is 28 for FU_A
		//FU header        S|E|R|Type
		cp[13] = (rtp_info->s_bit << 7)
			| (rtp_info->e_bit << 6) | (rtp_info->nal.type & 0x1f);
		//R = 0, must be ignored by receiver
        //hb_fnc_log(HB_LOG_INFO, "PU flag %d -- %d" , cp[12] , cp[13]);
		rtp_info->s_bit = rtp_info->e_bit = 0;
		rtp_info->hdr_len = 14; //RTP Header的长度为14
	}
	else
	{
		rtp_info->hdr_len = 12; ////RTP Header的长度为12
	}
#endif
	rtp_info->hdr_len = 12; ////RTP Header的长度为12
	//设置RTP包数据的起始地址
	rtp_info->start = &cp[rtp_info->hdr_len];    // new start of payload

	//计算RTP包的长度
	*p_packet_size = rtp_info->hdr_len + (rtp_info->end - rtp_info->start);

	//返回RTP包的地址(Header + Payload)
	return rtp_info->pRTP;
}


static HB_S32 set_rtp_pack1(rtp_info_t *rtp_info, HB_CHAR *ps_data_buf,
 	HB_U32 ps_data_size, HB_U32 time_stamp, HB_U32 end_of_frame)
{
	//将一个NALU的信息填充进RTP信息结构体中
	rtp_info->nal.start = ps_data_buf;
	rtp_info->nal.size = ps_data_size;
	rtp_info->nal.eoFrame = end_of_frame;
	//rtp_info->nal.type = rtp_info->nal.start[3];
	rtp_info->nal.type = 0;
	rtp_info->nal.end = rtp_info->nal.start + rtp_info->nal.size;
	rtp_info->rtp_hdr.timestamp = time_stamp;
	//rtp_info->nal.start += 4; //跳过同步字符00000001
#if 1
	//判断是否需要进行RTP分包
	//if(rtp_info->nal.size > MAXRTPPACKSIZE)
	if(rtp_info->nal.size > MAX_RTP_PACKET_SIZE)
	{
		rtp_info->FU_flag = TRUE;
		rtp_info->s_bit = 1; //先将s_bit置1，get_rtp_pack后，会将s_bit置0
		rtp_info->e_bit = 0;

		//rtp_info->nal.start += 4; //跳过NALU Type字节
    }
	else
	{
		rtp_info->FU_flag = FALSE;
		rtp_info->s_bit = rtp_info->e_bit = 0;
	}
#endif
	rtp_info->start = rtp_info->end = rtp_info->nal.start;
	rtp_info->beginNAL = TRUE;

	return 0 ;
}

#if 0
HB_S32 pack_ps_rtp_and_add_node(STREAM_NODE_HANDLE pStreamNode, HB_CHAR *data_ptr, HB_U32 data_size, HB_U64 time_stamp, HB_U32 rtp_data_buf_pre_size, HB_S32 frame_type)
{
	HB_S32 addr_len = sizeof(struct sockaddr_in);
	HB_S32 hash_value = pStreamNode->iStreamNodeHashValue;
	HB_U32 left_bytes = 0;
	HB_U32 cur_data_size = 0;
	HB_U32 end_of_frame = 0;
	HB_U32 rtp_size = 0;
	HB_U32 nal_size = 0;
	HB_S32 nalu_type = 0;
	HB_CHAR *start_pos = NULL;
	HB_CHAR *end_pos = NULL;
	HB_CHAR *rtp_buf = NULL;
	HB_CHAR tmp_sps_pps[128] = { 0 };
	HB_CHAR sql[512] = { 0 };
	rtp_info_t *rtp_info = NULL;
	rtp_info = &(pStreamNode->stRtpSession.rtp_info_video);
	start_pos = data_ptr + rtp_data_buf_pre_size;
	left_bytes = data_size;
	RTP_CLIENT_TRANSPORT_HANDLE pClientNode = NULL;
	HB_S32 i = 0;
	HB_S32 client_list_size = 0;
	HB_S32 tmp_list_size = 0;

	while(left_bytes > 3)
	{
		if(1 == frame_type) //I帧
		{
			//获取一个NALU
			start_pos = get_NAL_from_frame(start_pos, left_bytes, &end_pos, &nalu_type);

			//printf("\nMMMMMMMMMMMMMm  nalu_type=%d\n", nalu_type);
#if 0
			if(nalu_type < 1 || nalu_type > 9)
			{
				left_bytes = left_bytes-1;
				start_pos = start_pos+1;
				return HB_FAILURE;
			}
#endif
			if (NULL == start_pos)
			{
				return HB_FAILURE;
			}

			left_bytes = data_size - (end_pos - frame);
			nal_size = end_pos - start_pos;

			if(7 == nalu_type) //SPS信息
			{
				nal_type=2;
				if(0==mov->sps_len)//记录一次SPS
				{
					mov->sps_len=nal_size-3;
					mov->sps=malloc(mov->sps_len);
					memcpy(mov->sps,start_pos+3,mov->sps_len);
				}
			}
			else if (8 == nalu_type)//PPS信息
			{
				nal_type=3;
				if(0==mov->pps_len){		//记录一次PPS
					mov->pps_len=nal_size-3;
					mov->pps=malloc(mov->pps_len);
					memcpy(mov->pps,start_pos+3,mov->pps_len);
				}
			}
			else if (5 == nalu_type)//IDR帧信息
			{
				nal_size +=1;
				start_pos = start_pos-1;
				printf("\nPPPPPPPPPPPPPPP   size=%d\n",nal_size);
				nal_type=1;
			}

			else if(6 == nalu_type)//SEI信息
			{
				nal_type=4;
			}
			else
			{
				nal_type=-1;
			}

		}
		else //B、P帧
		{
			nal_type=0;
			nal_size = frame_len;

		}

		one_node=(nal_list *)malloc(sizeof(struct nal_list));
		one_node->nal_type=nal_type;
		one_node->nal_offset=mov->mdat_size;
		one_node->nal_size=nal_size;
		one_node->next=NULL;

		//one_node->nal_type!=2&&one_node->nal_type!=3&&one_node->nal_type!=4){
		if(one_node->nal_type==1||one_node->nal_type==0)
		{
			nal_list_insert_end(one_node,mov);
			put_h264_buffer(mov->mp4_fp,start_pos,nal_size);		//把数据输入mp4文件
			mov->mdat_size+=nal_size;
		}
		if(0 == frame_type)
		{
			return HB_SUCCESS;
		}
		else
		{
			//更换起始地址，为下一次循环做准备
			start_pos = end_pos;
		}
	}
	return HB_SUCCESS;
}
#endif

#if 0
//返回当前节点中数据长度
HB_U32 pack_ps_rtp_and_add_node(STREAM_NODE_HANDLE pStreamNode, HB_CHAR *data_ptr, HB_U32 data_size, HB_U64 time_stamp, HB_U32 rtp_data_buf_pre_size, HB_S32 frame_type)
{
	HB_S32 addr_len = sizeof(struct sockaddr_in);
	HB_S32 u32HashValue = pStreamNode->uStreamNodeHashValue;
	HB_U32 u32LeftBytes = 0; //剩余需要发送的字节数
	HB_U32 cur_data_size = 0;
//	HB_U32 end_of_frame = 0;
	HB_U32 rtp_size = 0;
//	HB_U32 nal_size = 0;
//	HB_S32 nalu_type = 0;
	HB_CHAR *start_pos = NULL;
	HB_CHAR *end_pos = NULL;
	HB_CHAR *rtp_buf = NULL;
//	HB_CHAR tmp_sps_pps[128] = { 0 };
//	HB_CHAR sql[512] = { 0 };
	rtp_info_t *rtp_info = NULL;
	rtp_info = &(pStreamNode->stRtpSession.rtp_info_video);
	start_pos = data_ptr + rtp_data_buf_pre_size;
	u32LeftBytes = data_size;
	RTP_CLIENT_TRANSPORT_HANDLE pClientNode = NULL;
	HB_S32 i = 0;
	HB_S32 client_list_size = 0;
	HB_S32 tmp_list_size = 0;

	while (u32LeftBytes > 3)
	{
		//将NAL的信息填充进rtp信息的结构体中
		set_rtp_pack1(rtp_info, start_pos, u32LeftBytes, time_stamp, 1);

		client_list_size = list_size(&(pStreamNode->listClientNodeHead));
		//RTP分包，从一个NALU数据中获取一包RTP数据
		while ((rtp_buf = get_rtp_ps_pack(rtp_info, &rtp_size)) != NULL)
		{
//			printf("\n##@@@@@@  rtp_size=%d\n", rtp_size);
//			HB_U8 tcp_buf[4] = { 0 };
//			tcp_buf[0] = '$';
//			tcp_buf[1] = 0;
//			tcp_buf[2] = (HB_U8) ((rtp_size & 0xff00) >> 8);
//			tcp_buf[3] = (HB_U8) ((rtp_size & 0xff));

			i = 0;
			tmp_list_size = client_list_size;
			while (tmp_list_size)
			{
				pClientNode = (RTP_CLIENT_TRANSPORT_HANDLE)list_get_at(&(pStreamNode->listClientNodeHead), i);
				if (pClientNode->iDeleteFlag == 1)
				{
					del_one_client_node(&(pStreamNode->listClientNodeHead), pClientNode, u32HashValue);
					free(pClientNode);
					pClientNode = NULL;
					client_list_size--;
					--i;
				}
				else
				{
					if (pClientNode->iSendIframeFlag == 1)
					{
						sendto(pClientNode->stUdpVideoInfo.iUdpVideoFd, rtp_buf, rtp_size, 0, (struct sockaddr*)(&(pClientNode->stUdpVideoInfo.rtp_peer)), (socklen_t)(sizeof(struct sockaddr_in)));
					}
					else if (frame_type == 1)
					{
						pClientNode->iSendIframeFlag = 1;
					}
				}
				++i;
				tmp_list_size--;
			}

//			memmove(data_ptr + cur_data_size, tcp_buf, 4);
//			memmove(data_ptr + cur_data_size + 4, rtp_buf, rtp_size);
//			cur_data_size += (rtp_size + 4);
		}
		return cur_data_size;

//		if (0 == frame_type)
//		{
//			return cur_data_size;
//		}
//		else
//		{
//			//更换起始地址，为下一次循环做准备
//			start_pos = end_pos;
//		}
	}

	return cur_data_size;
}
#endif

#if 1
//返回当前节点中数据长度
HB_U32 pack_ps_rtp_and_add_node(STREAM_NODE_HANDLE pStreamNode, HB_CHAR *data_ptr, HB_U32 data_size, HB_U64 time_stamp, HB_U32 rtp_data_buf_pre_size, HB_S32 frame_type)
{
	HB_S32 addr_len = sizeof(struct sockaddr_in);
	HB_S32 u32HashValue = pStreamNode->uStreamNodeHashValue;
	HB_U32 u32LeftBytes = 0; //剩余需要发送的字节数
	HB_U32 cur_data_size = 0;
	HB_U32 rtp_size = 0;
	HB_CHAR *start_pos = NULL;
	HB_CHAR *end_pos = NULL;
	HB_CHAR *rtp_buf = NULL;
	rtp_info_t *rtp_info = NULL;
	rtp_info = &(pStreamNode->stRtpSession.rtp_info_video);
	start_pos = data_ptr + rtp_data_buf_pre_size;
	u32LeftBytes = data_size;
	RTP_CLIENT_TRANSPORT_HANDLE pClientNode = NULL;
	HB_S32 i = 0;
	HB_S32 client_list_size = 0;
	HB_S32 tmp_list_size = 0;

	while (u32LeftBytes > 3)
	{
		//将NAL的信息填充进rtp信息的结构体中
		set_rtp_pack1(rtp_info, start_pos, u32LeftBytes, time_stamp, 1);
		client_list_size = list_size(&(pStreamNode->listClientNodeHead));
		//RTP分包，从一个NALU数据中获取一包RTP数据
		while ((rtp_buf = get_rtp_ps_pack(rtp_info, &rtp_size)) != NULL)
		{
			i = 0;
			tmp_list_size = client_list_size;
			while (tmp_list_size)
			{
				pClientNode = (RTP_CLIENT_TRANSPORT_HANDLE)list_get_at(&(pStreamNode->listClientNodeHead), i);
				if (pClientNode->iDeleteFlag == 1)
				{
					del_one_client_node(&(pStreamNode->listClientNodeHead), pClientNode, u32HashValue);
					free(pClientNode);
					pClientNode = NULL;
					client_list_size--;
					--i;
				}
				else
				{
					if (pClientNode->iSendIframeFlag == 1)
					{
						sendto(pClientNode->stUdpVideoInfo.iUdpVideoFd, rtp_buf, rtp_size, 0, (struct sockaddr*)(&(pClientNode->stUdpVideoInfo.rtp_peer)), (socklen_t)(sizeof(struct sockaddr_in)));
					}
					else if (frame_type == 1)
					{
						pClientNode->iSendIframeFlag = 1;
					}
				}
				++i;
				tmp_list_size--;
			}
		}
		return cur_data_size;
	}

	return cur_data_size;
}
#endif
