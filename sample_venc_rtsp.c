/******************************************************************************
  A simple program of Hisilicon HI3531 video encode implementation.
  Copyright (C), 2010-2011, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2011-2 Created
******************************************************************************/
#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"
#include "rtsp_demo.h"
#include "comm.h"


VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_NTSC;

#ifdef hi3518ev201

HI_U32 g_u32BlkCnt = 4;
#endif

#ifdef hi3518ev200

HI_U32 g_u32BlkCnt = 4;

#endif

#ifdef hi3516cv200

HI_U32 g_u32BlkCnt = 10;

#endif

static pthread_t VencPid;

rtsp_demo_handle g_rtsplive = NULL;
rtsp_session_handle session= NULL;

/******************************************************************************
* function : to process abnormal case                                         
******************************************************************************/
void SAMPLE_VENC_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_ISP_Stop();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
	
	if(session)
		rtsp_del_session(session);
	if(g_rtsplive)
		rtsp_del_demo(g_rtsplive);
    exit(-1);
}

/******************************************************************************
* function : to process abnormal case - the case of stream venc
******************************************************************************/
void SAMPLE_VENC_StreamHandleSig(HI_S32 signo)
{

    if (SIGINT == signo || SIGTSTP == signo)
    {
        SAMPLE_COMM_ISP_Stop();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    exit(0);
}


/******************************************************************************
* funciton : get stream from each channels and save them
******************************************************************************/
HI_VOID* VENC_GetVencStreamProc(HI_VOID *p)
{  
	HI_S32 s32Ret = 0;
	static int s_LivevencChn = 0,s_LivevencFd=0; 
	static int s_maxFd = 0;

	fd_set read_fds;
	VENC_STREAM_S stVStream;
	VENC_CHN_STAT_S stStat;

	s_LivevencChn = 1;
	s_LivevencFd  = HI_MPI_VENC_GetFd(s_LivevencChn);	
	s_maxFd   = s_maxFd > s_LivevencFd ? s_maxFd:s_LivevencFd;	
	s_maxFd = s_maxFd+1;

	
	//struct sched_param param;
	struct timeval TimeoutVal;
	VENC_PACK_S *pstPack = NULL;	
	pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * 128);
	if (NULL == pstPack)
	{
		pstPack = NULL;
		return -1;
	}	
	int i;

	unsigned char* pStremData = NULL;
	int nSize = 0;

	while(1)	
	{
		FD_ZERO( &read_fds );
		FD_SET( s_LivevencFd, &read_fds);
		
		TimeoutVal.tv_sec  = 2;
		TimeoutVal.tv_usec = 0;
		s32Ret = select( s_maxFd, &read_fds, NULL, NULL, &TimeoutVal );
		if (s32Ret <= 0)
		{
			printf("%s select failed!\n",__FUNCTION__);
			sleep(1);
			continue;
		}	

		//Live stream
		if (FD_ISSET( s_LivevencFd, &read_fds ))
		{
			s32Ret = HI_MPI_VENC_Query( s_LivevencChn, &stStat );
			if (HI_SUCCESS != s32Ret)
			{
				printf("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", s_LivevencChn, s32Ret);
				continue;
			}
	
			stVStream.pstPack = pstPack;
			stVStream.u32PackCount = stStat.u32CurPacks;
			s32Ret = HI_MPI_VENC_GetStream( s_LivevencChn, &stVStream, HI_TRUE );
			if (HI_SUCCESS != s32Ret)
			{
				printf("HI_MPI_VENC_GetStream .. failed with %#x!\n", s32Ret);
				continue;
			}
	
	
			for (i = 0; i < stVStream.u32PackCount; i++)
			{
				//ÔÝÊ±È¥µôSEIÖ¡
				if(stVStream.pstPack[i].DataType.enH264EType == H264E_NALU_SEI) continue;				

				pStremData = (unsigned char*)stVStream.pstPack[i].pu8Addr+stVStream.pstPack[i].u32Offset;
				nSize = stVStream.pstPack[i].u32Len-stVStream.pstPack[i].u32Offset;

				if(g_rtsplive)
				{
					rtsp_sever_tx_video(g_rtsplive,session,pStremData,nSize,stVStream.pstPack[i].u64PTS);
				}
			}	
	
			s32Ret = HI_MPI_VENC_ReleaseStream(s_LivevencChn, &stVStream);
			if (HI_SUCCESS != s32Ret)
			{
				SAMPLE_PRT("HI_MPI_VENC_ReleaseStream chn[%d] .. failed with %#x!\n", s_LivevencChn, s32Ret);
				stVStream.pstPack = NULL;
				continue;
			}
		} 
	}
	
	if(pstPack) free(pstPack);
	return NULL;
}

/******************************************************************************
* function :  H.264@1080p@30fps+H.264@VGA@30fps
******************************************************************************/
HI_S32 SAMPLE_VENC_1080P_CLASSIC(HI_VOID)
{
    PAYLOAD_TYPE_E enPayLoad[3]= {PT_H264, PT_H264,PT_H264};
    PIC_SIZE_E enSize[3] = {PIC_HD1080, PIC_VGA,PIC_QVGA};
	HI_U32 u32Profile = 0;
	
    VB_CONF_S stVbConf;
    SAMPLE_VI_CONFIG_S stViConfig = {0};
    
    VPSS_GRP VpssGrp;
    VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_CHN_MODE_S stVpssChnMode;
    
    VENC_CHN VencChn;
    SAMPLE_RC_E enRcMode= SAMPLE_RC_CBR;
	
    HI_S32 s32ChnNum=0;
    
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;
    //char c;


    /******************************************
     step  1: init sys variable 
    ******************************************/
    memset(&stVbConf,0,sizeof(VB_CONF_S));
    
	SAMPLE_COMM_VI_GetSizeBySensor(&enSize[0]);
    if (PIC_HD1080 == enSize[0])
    {
        enSize[1] = PIC_VGA;
		s32ChnNum = 2;
    }
    else if (PIC_HD720 == enSize[0])
    {
        enSize[1] = PIC_VGA;			
		enSize[2] = PIC_QVGA;
		s32ChnNum = 3;
    }
    else
    {
        printf("not support this sensor\n");
        return HI_FAILURE;
    }
#ifdef hi3518ev201
	s32ChnNum = 1;
#endif
	printf("s32ChnNum = %d\n",s32ChnNum);

    stVbConf.u32MaxPoolCnt = 128;

    /*video buffer*/
	if(s32ChnNum >= 1)
    {
	    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
	                enSize[0], SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
	    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	    stVbConf.astCommPool[0].u32BlkCnt = g_u32BlkCnt;
	}
	if(s32ChnNum >= 2)
    {
	    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
	                enSize[1], SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
	    stVbConf.astCommPool[1].u32BlkSize = u32BlkSize;
	    stVbConf.astCommPool[1].u32BlkCnt =g_u32BlkCnt;
	}
	if(s32ChnNum >= 3)
    {
		u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                enSize[2], SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
		stVbConf.astCommPool[2].u32BlkSize = u32BlkSize;
		stVbConf.astCommPool[2].u32BlkCnt = g_u32BlkCnt;
    }

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_VENC_1080P_CLASSIC_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    stViConfig.enViMode   = SENSOR_TYPE;
    stViConfig.enRotate   = ROTATE_NONE;
    stViConfig.enNorm     = VIDEO_ENCODING_MODE_AUTO;
    stViConfig.enViChnSet = VI_CHN_SET_NORMAL;
    stViConfig.enWDRMode  = WDR_MODE_NONE;
    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_VENC_1080P_CLASSIC_1;
    }
    
    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enSize[0], &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_VENC_1080P_CLASSIC_1;
    }
	if(s32ChnNum >= 1)
	{
		VpssGrp = 0;
	    stVpssGrpAttr.u32MaxW = stSize.u32Width;
	    stVpssGrpAttr.u32MaxH = stSize.u32Height;
	    stVpssGrpAttr.bIeEn = HI_FALSE;
	    stVpssGrpAttr.bNrEn = HI_TRUE;
	    stVpssGrpAttr.bHistEn = HI_FALSE;
	    stVpssGrpAttr.bDciEn = HI_FALSE;
	    stVpssGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
	    stVpssGrpAttr.enPixFmt = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		
	    s32Ret = SAMPLE_COMM_VPSS_StartGroup(VpssGrp, &stVpssGrpAttr);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Vpss failed!\n");
	        goto END_VENC_1080P_CLASSIC_2;
	    }

	    s32Ret = SAMPLE_COMM_VI_BindVpss(stViConfig.enViMode);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Vi bind Vpss failed!\n");
	        goto END_VENC_1080P_CLASSIC_3;
	    }

		VpssChn = 0;
	    stVpssChnMode.enChnMode      = VPSS_CHN_MODE_USER;
	    stVpssChnMode.bDouble        = HI_FALSE;
	    stVpssChnMode.enPixelFormat  = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	    stVpssChnMode.u32Width       = stSize.u32Width;
	    stVpssChnMode.u32Height      = stSize.u32Height;
	    stVpssChnMode.enCompressMode = COMPRESS_MODE_SEG;
	    memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
	    stVpssChnAttr.s32SrcFrameRate = -1;
	    stVpssChnAttr.s32DstFrameRate = -1;
	    s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Enable vpss chn failed!\n");
	        goto END_VENC_1080P_CLASSIC_4;
	    }
	}

	if(s32ChnNum >= 2)
	{
		s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enSize[1], &stSize);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
	        goto END_VENC_1080P_CLASSIC_4;
	    }
	    VpssChn = 1;
	    stVpssChnMode.enChnMode       = VPSS_CHN_MODE_USER;
	    stVpssChnMode.bDouble         = HI_FALSE;
	    stVpssChnMode.enPixelFormat   = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
	    stVpssChnMode.u32Width        = stSize.u32Width;
	    stVpssChnMode.u32Height       = stSize.u32Height;
	    stVpssChnMode.enCompressMode  = COMPRESS_MODE_SEG;
	    stVpssChnAttr.s32SrcFrameRate = -1;
	    stVpssChnAttr.s32DstFrameRate = -1;
	    s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Enable vpss chn failed!\n");
	        goto END_VENC_1080P_CLASSIC_4;
	    }
	}
	

	if(s32ChnNum >= 3)
	{	
		s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enSize[2], &stSize);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
	        goto END_VENC_1080P_CLASSIC_4;
	    }
		VpssChn = 2;
		stVpssChnMode.enChnMode 	= VPSS_CHN_MODE_USER;
		stVpssChnMode.bDouble		= HI_FALSE;
		stVpssChnMode.enPixelFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
		stVpssChnMode.u32Width		= stSize.u32Width;
		stVpssChnMode.u32Height 	= stSize.u32Height;
		stVpssChnMode.enCompressMode = COMPRESS_MODE_NONE;
		
		stVpssChnAttr.s32SrcFrameRate = -1;
		stVpssChnAttr.s32DstFrameRate = -1;
		
		s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);
		if (HI_SUCCESS != s32Ret)
		{
			SAMPLE_PRT("Enable vpss chn failed!\n");
			goto END_VENC_1080P_CLASSIC_4;
		}
	}
    /******************************************
     step 5: start stream venc
    ******************************************/
	enRcMode = SAMPLE_RC_CBR;

	/*** enSize[0] **/
	if(s32ChnNum >= 1)
	{
		VpssGrp = 0;
	    VpssChn = 0;
	    VencChn = 0;
	    s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad[0],\
	                                   gs_enNorm, enSize[0], enRcMode,u32Profile);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }

	    s32Ret = SAMPLE_COMM_VENC_BindVpss(VencChn, VpssGrp, VpssChn);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }
	}

	/*** enSize[1] **/
	if(s32ChnNum >= 2)
	{
		VpssChn = 1;
	    VencChn = 1;
	    s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad[1], \
	                                    gs_enNorm, enSize[1], enRcMode,u32Profile);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }

	    s32Ret = SAMPLE_COMM_VENC_BindVpss(VencChn, VpssGrp, VpssChn);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }
	}
	/*** enSize[2] **/
	if(s32ChnNum >= 3)
	{
	    VpssChn = 2;
	    VencChn = 2;
	    s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad[2], \
	                                    gs_enNorm, enSize[2], enRcMode,u32Profile);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }

	    s32Ret = SAMPLE_COMM_VENC_BindVpss(VencChn, VpssGrp, VpssChn);
	    if (HI_SUCCESS != s32Ret)
	    {
	        SAMPLE_PRT("Start Venc failed!\n");
	        goto END_VENC_1080P_CLASSIC_5;
	    }
	}
    /******************************************
     step 6: stream venc process -- get stream, then save it to file. 
    ******************************************/

	pthread_create(&VencPid, 0, VENC_GetVencStreamProc, NULL);

#if 0
    s32Ret = SAMPLE_COMM_VENC_StartGetStream(s32ChnNum);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed!\n");
        goto END_VENC_1080P_CLASSIC_5;
    }
#endif
    printf("please press twice ENTER to exit this sample\n");

	return 0;

    /******************************************
     step 7: exit process
    ******************************************/
    SAMPLE_COMM_VENC_StopGetStream();
    
END_VENC_1080P_CLASSIC_5:
	
    VpssGrp = 0;
	switch(s32ChnNum)
	{
		case 3:
			VpssChn = 2;   
		    VencChn = 2;
		    SAMPLE_COMM_VENC_UnBindVpss(VencChn, VpssGrp, VpssChn);
		    SAMPLE_COMM_VENC_Stop(VencChn);
		case 2:
			VpssChn = 1;   
		    VencChn = 1;
		    SAMPLE_COMM_VENC_UnBindVpss(VencChn, VpssGrp, VpssChn);
		    SAMPLE_COMM_VENC_Stop(VencChn);
		case 1:
			VpssChn = 0;  
		    VencChn = 0;
		    SAMPLE_COMM_VENC_UnBindVpss(VencChn, VpssGrp, VpssChn);
		    SAMPLE_COMM_VENC_Stop(VencChn);
			break;
			
	}
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);
	
END_VENC_1080P_CLASSIC_4:	//vpss stop

    VpssGrp = 0;
	switch(s32ChnNum)
	{
		case 3:
			VpssChn = 2;
			SAMPLE_COMM_VPSS_DisableChn(VpssGrp, VpssChn);
		case 2:
			VpssChn = 1;
			SAMPLE_COMM_VPSS_DisableChn(VpssGrp, VpssChn);
		case 1:
			VpssChn = 0;
			SAMPLE_COMM_VPSS_DisableChn(VpssGrp, VpssChn);
		break;
	
	}

END_VENC_1080P_CLASSIC_3:    //vpss stop       
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);
END_VENC_1080P_CLASSIC_2:    //vpss stop   
    SAMPLE_COMM_VPSS_StopGroup(VpssGrp);
END_VENC_1080P_CLASSIC_1:	//vi stop
    SAMPLE_COMM_VI_StopVi(&stViConfig);
END_VENC_1080P_CLASSIC_0:	//system exit
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;    
}



/******************************************************************************
* function    : main()
* Description : video venc sample
******************************************************************************/
int main(int argc, char *argv[])
{
	HI_S32 s32Ret = 0;
	signal(SIGINT, SAMPLE_VENC_HandleSig);
	signal(SIGTERM, SAMPLE_VENC_HandleSig); 
	
	//
	
	g_rtsplive = create_rtsp_demo(554);
	session= create_rtsp_session(g_rtsplive,"/live.sdp");


	s32Ret = SAMPLE_VENC_1080P_CLASSIC();       
	while(1) sleep(1);	
	exit(s32Ret);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
