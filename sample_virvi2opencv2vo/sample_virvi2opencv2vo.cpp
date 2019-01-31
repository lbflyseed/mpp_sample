/******************************************************************************
  Copyright (C),  Lindeni Tech. Co., Ltd.
******************************************************************************/
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgcodecs.hpp"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utils/plat_log.h>
#include "media/mpi_sys.h"
#include "media/mm_comm_vi.h"
#include "media/mpi_vi.h"
#include "media/mpi_isp.h"

#include "mpp_vo.h"

#include <confparser.h>
#include "sample_virvi2opencv2vo.h"
#include "sample_virvi2opencv2vo_config.h"

#define VO_HDMI_DISPLAY_WIDTH 1920
#define VO_HDMI_DISPLAY_HIGHT 1080

using namespace cv;

Mat imageA, imageB, image_diff;

VO_Params g_VOParams;

int g_PixelSize;
unsigned int g_u32PhyAddr;
void *g_pVoAddr;
int g_exit = 0;

VIDEO_FRAME_INFO_S g_VFrameInfo;

int AutoTestCount = 0,GetFrameCount = 0;
VirVi_Cap_S privCap[MAX_VIPP_DEV_NUM][MAX_VIR_CHN_NUM];

int hal_virvi_start(VI_DEV ViDev, VI_CHN ViCh, void *pAttr)
{
	int ret = -1;

	ret = AW_MPI_VI_CreateVirChn(ViDev, ViCh, pAttr);
	if(ret < 0) {
		aloge("Create VI Chn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
		return ret ;
	}
	ret = AW_MPI_VI_SetVirChnAttr(ViDev, ViCh, pAttr);
	if(ret < 0) {
		aloge("Set VI ChnAttr failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
		return ret ;
	}
	ret = AW_MPI_VI_EnableVirChn(ViDev, ViCh);
	if(ret < 0) {
		aloge("VI Enable VirChn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
		return ret ;
	}

	return 0;
}

int hal_virvi_end(VI_DEV ViDev, VI_CHN ViCh)
{
	int ret = -1;
	ret = AW_MPI_VI_DisableVirChn(ViDev, ViCh);
	if(ret < 0) {
		aloge("Disable VI Chn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
		return ret ;
	}
	ret = AW_MPI_VI_DestoryVirChn(ViDev, ViCh);
	if(ret < 0) {
		aloge("Destory VI Chn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
		return ret ;
	}
	return 0;
}

static int ParseCmdLine(int argc, char **argv, SampleVirVi2OpenCV2VOCmdLineParam *pCmdLinePara)
{
	alogd("sample virvi2opencv2vo path:[%s], arg number is [%d]", argv[0], argc);
	int ret = 0;
	int i=1;
	memset(pCmdLinePara, 0, sizeof(SampleVirVi2OpenCV2VOCmdLineParam));
	while(i < argc) {
		if(!strcmp(argv[i], "-path")) {
			if(++i >= argc) {
				aloge("fatal error! use -h to learn how to set parameter!!!");
				ret = -1;
				break;
			}
			if(strlen(argv[i]) >= MAX_FILE_PATH_SIZE) {
				aloge("fatal error! file path[%s] too long: [%d]>=[%d]!", argv[i], strlen(argv[i]), MAX_FILE_PATH_SIZE);
			}
			strncpy(pCmdLinePara->mConfigFilePath, argv[i], MAX_FILE_PATH_SIZE-1);
			pCmdLinePara->mConfigFilePath[MAX_FILE_PATH_SIZE-1] = '\0';
		} else if(!strcmp(argv[i], "-h")) {
			alogd("CmdLine param:\n"
				  "\t-path /home/sample_virvi2opencv2vo.conf\n");
			ret = 1;
			break;
		} else {
			alogd("ignore invalid CmdLine param:[%s], type -h to get how to set parameter!", argv[i]);
		}
		i++;
	}
	return ret;
}

static ERRORTYPE loadSampleVirVi2OpenCV2VOConfig(SampleVirVi2OpenCV2VOConfig *pConfig, const char *conf_path)
{
	int ret;
	char *ptr;
	CONFPARSER_S stConfParser;
	ret = createConfParser(conf_path, &stConfParser);
	if(ret < 0) {
		aloge("load conf fail");
		return FAILURE;
	}
	memset(pConfig, 0, sizeof(SampleVirVi2OpenCV2VOConfig));
	pConfig->AutoTestCount = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Auto_Test_Count, 0);
	pConfig->GetFrameCount = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Get_Frame_Count, 0);
	pConfig->DevNum = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Dev_Num, 0);
	pConfig->FrameRate = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Frame_Rate, 0);
	pConfig->PicWidth = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Pic_Width, 0);
	pConfig->PicHeight = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Pic_Height, 0);
	pConfig->displayColor = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Enable_Display_Color, 0);
	pConfig->movDetSen = GetConfParaInt(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Mov_Det_Sen, 5);
	char *pStrPixelFormat = (char*)GetConfParaString(&stConfParser, SAMPLE_VirVi2OpenCV2VO_Pic_Format, NULL);
	if(!strcmp(pStrPixelFormat, "nv21")) {
		printf("nv21!!!\n");
		pConfig->PicFormat = V4L2_PIX_FMT_NV21M;
	} else {
		aloge("fatal error! conf file pic_format must be yuv420sp");
		pConfig->PicFormat = V4L2_PIX_FMT_NV21M;
	}
	alogd("dev_num=%d, pic_width=%d, pic_height=%d, pic_frame_rate=%d",
		  pConfig->DevNum,pConfig->PicWidth,pConfig->PicHeight,pConfig->FrameRate);
	destroyConfParser(&stConfParser);
	return SUCCESS;
}

static void *GetCSIFrameThread(void *pArg)
{
	VI_DEV ViDev;
	VI_CHN ViCh;
	int ret = 0;
	int i = 0, j = 0, flag =0, mean_old = 0;
	Scalar Crmean;

	VirVi_Cap_S *pCap = (VirVi_Cap_S *)pArg;
	ViDev = pCap->Dev;
	ViCh = pCap->Chn;
	printf("loop Sample_virvi2opencv2vo, Cap threadid=0x%lx, ViDev = %d, ViCh = %d\n", pCap->thid, ViDev, ViCh);
	while (GetFrameCount != j) {
		if (g_exit) {
			printf("GetCSIFrameThread exit! \n");
			return NULL;
		}
		if ((ret = AW_MPI_VI_GetFrame(ViDev, ViCh, &pCap->pstFrameInfo, pCap->s32MilliSec)) != 0) {
			// printf("VI Get Frame failed!\n");
			continue ;
		}
#if 1
		if (flag == 0) {
			memcpy(imageA.data, pCap->pstFrameInfo.VFrame.mpVirAddr[0],g_PixelSize);
			flag = 1;
		} else {
			memcpy(imageB.data, pCap->pstFrameInfo.VFrame.mpVirAddr[0],g_PixelSize);
			flag = 0;
		}
		absdiff(imageA, imageB, image_diff);
		mean_old = (int) (Crmean[0] * 100);
		Crmean = mean(image_diff);
		int mean_new = (int) (Crmean[0] * 100);
		int mean_diff = mean_new - mean_old;
		if ( mean_diff >= pCap->movDetSen ) {
			printf("Something is moving! mean_diff:%d \n", mean_diff);
		}
#else
		memcpy(imageA.data, pCap->pstFrameInfo.VFrame.mpVirAddr[0], g_PixelSize);
		threshold(imageA, image_diff, 100, 255.0, 0);
#endif
		memcpy(g_VFrameInfo.VFrame.mpVirAddr[0], image_diff.data, g_PixelSize);
		if (pCap->displayColor) {
			memcpy(g_VFrameInfo.VFrame.mpVirAddr[1], pCap->pstFrameInfo.VFrame.mpVirAddr[1], g_PixelSize/2);
		}

		g_VFrameInfo.VFrame.mpts = pCap->pstFrameInfo.VFrame.mpts;
		AW_MPI_VO_SendFrame(g_VOParams.iVoLayer, g_VOParams.iVoChn, &g_VFrameInfo, 0);
#if 0
		i++;
		if (i % 25 == 0) {
			time_t now;
			struct tm *timenow;
			time(&now);
			timenow = localtime(&now);
			printf("Cap threadid=0x%lx, ViDev=%d,VirVi=%d,mpts=%lld; local time is %s\r\n", pCap->thid, ViDev, ViCh, pCap->pstFrameInfo.VFrame.mpts,asctime(timenow));


			FILE *fd;
			char filename[128];
			sprintf(filename, "%dx%d_%d.yuv",
					pCap->pstFrameInfo.VFrame.mWidth,
					pCap->pstFrameInfo.VFrame.mHeight,
					i);
			fd = fopen(filename, "wb+");
			fwrite(pCap->pstFrameInfo.VFrame.mpVirAddr[0],
				   pCap->pstFrameInfo.VFrame.mWidth * pCap->pstFrameInfo.VFrame.mHeight,
				   1, fd);
			fwrite(pCap->pstFrameInfo.VFrame.mpVirAddr[1],
				   pCap->pstFrameInfo.VFrame.mWidth * pCap->pstFrameInfo.VFrame.mHeight >> 1,
				   1, fd);
			fclose(fd);

		}
#endif
		AW_MPI_VI_ReleaseFrame(ViDev, ViCh, &pCap->pstFrameInfo);
		j++;
	}
	return NULL;
}

void VI_HELP()
{
	printf("Run CSI0/CSI1 command: ./sample_virvi2opencv2vo -path ./sample_virvi2opencv2vo.conf\r\n");
}

int vo_init(int src_w, int src_h)
{
  // Initial VO params
  g_VOParams.iVoDev	        = 0;
  g_VOParams.iVoChn	        = 0;
  g_VOParams.iVoLayer	    = 0;
  g_VOParams.iMiniGUILayer	= HLAY(2, 0);
#ifndef TRANS_VO_TO_LCD  //HDMI
  g_VOParams.iDispType		= VO_INTF_HDMI;
  g_VOParams.iDispSync		= VO_OUTPUT_1080P50;
  g_VOParams.iWidth 		= VO_HDMI_DISPLAY_WIDTH;
  g_VOParams.iHeight 		= VO_HDMI_DISPLAY_HIGHT;
#else  //LCD
  g_VOParams.iDispType		= VO_INTF_LCD;
  g_VOParams.iDispSync		= VO_OUTPUT_NTSC;
  g_VOParams.iWidth 		= VO_LCD_DISPLAY_WIDTH;
  g_VOParams.iHeight 		= VO_LCD_DISPLAY_HIGHT;
#endif
  //g_VOParams.iFrameNum 	    = g_GetFrameCount;

  create_vo(&g_VOParams);


  AW_MPI_SYS_MmzAlloc_Cached(&g_u32PhyAddr, &g_pVoAddr, g_PixelSize * 3 / 2);

  g_VFrameInfo.mId                  = 0;
  g_VFrameInfo.VFrame.mPhyAddr[0]   = g_u32PhyAddr;
  g_VFrameInfo.VFrame.mPhyAddr[1]   = g_u32PhyAddr + g_PixelSize;
  g_VFrameInfo.VFrame.mPhyAddr[2]   = g_u32PhyAddr + g_PixelSize + g_PixelSize / 4;
  g_VFrameInfo.VFrame.mpVirAddr[0]  = g_pVoAddr;
  g_VFrameInfo.VFrame.mpVirAddr[1]  = g_pVoAddr + g_PixelSize;
  g_VFrameInfo.VFrame.mpVirAddr[2]  = g_pVoAddr + g_PixelSize + g_PixelSize / 4;
  g_VFrameInfo.VFrame.mWidth        = src_w;
  g_VFrameInfo.VFrame.mHeight       = src_h;
  g_VFrameInfo.VFrame.mField        = VIDEO_FIELD_FRAME;
  g_VFrameInfo.VFrame.mPixelFormat  = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420; //TODO:д��nv21
  g_VFrameInfo.VFrame.mVideoFormat  = VIDEO_FORMAT_LINEAR;
  g_VFrameInfo.VFrame.mCompressMode = COMPRESS_MODE_NONE;
  g_VFrameInfo.VFrame.mOffsetTop    = 0;
  g_VFrameInfo.VFrame.mOffsetBottom = src_h;
  g_VFrameInfo.VFrame.mOffsetLeft   = 0;
  g_VFrameInfo.VFrame.mOffsetRight  = src_w;

  memset(g_VFrameInfo.VFrame.mpVirAddr[1], 0x80, g_PixelSize/2 );

  return 0;
}

int vo_exit()
{
  AW_MPI_SYS_MmzFree(g_u32PhyAddr, g_pVoAddr);
  destroy_vo(&g_VOParams);
  return 0;
}

void Stop(int signo)
{
	printf("stop!!!\n");

	g_exit = 1;
	usleep(200*1000);
	vo_exit();
	AW_MPI_SYS_Exit();
	_exit(0);
}

int main(int argc, char *argv[])
{
	int ret = 0,result = 0;
	int vipp_dev, virvi_chn;
	int count = 0;

	printf("Sample virvi2opencv2vo buile time = %s, %s.\r\n", __DATE__, __TIME__);
	if (argc != 3) {
		VI_HELP();
		exit(0);
	}
	signal(SIGINT, Stop);

	SampleVirVi2OpenCV2VOConfparser stContext;
	/* parse command line param,read sample_virvi2opencv2vo.conf */
	if(ParseCmdLine(argc, argv, &stContext.mCmdLinePara) != 0) {
		aloge("fatal error! command line param is wrong, exit!");
		result = -1;
		goto _exit;
	}

	char *pConfigFilePath;
	if(strlen(stContext.mCmdLinePara.mConfigFilePath) > 0) {
		pConfigFilePath = stContext.mCmdLinePara.mConfigFilePath;
	} else {
		pConfigFilePath = DEFAULT_SAMPLE_VIRVI2OPENCV2VO_CONF_PATH;
	}
	/* parse config file. */
	if(loadSampleVirVi2OpenCV2VOConfig(&stContext.mConfigPara, pConfigFilePath) != SUCCESS) {
		aloge("fatal error! no config file or parse conf file fail");
		result = -1;
		goto _exit;
	}
	AutoTestCount = stContext.mConfigPara.AutoTestCount;
	GetFrameCount = stContext.mConfigPara.GetFrameCount;
	vipp_dev = stContext.mConfigPara.DevNum;
	while (count != AutoTestCount) {
		if (g_exit) {
			printf("sample_virvi2opencv2vo exit!\n");
			return 0;
		}
		printf("======================================.\r\n");
		printf("Auto Test count : %d. (MaxCount==1000).\r\n", count);
		system("cat /proc/meminfo | grep Committed_AS");
		printf("======================================.\r\n");
		/* start mpp systerm */
		MPP_SYS_CONF_S mSysConf;
		memset(&mSysConf, 0, sizeof(MPP_SYS_CONF_S));
		mSysConf.nAlignWidth = 32;
		AW_MPI_SYS_SetConf(&mSysConf);
		ret = AW_MPI_SYS_Init();
		if (ret < 0) {
			aloge("sys Init failed!");
			return -1;
		}

		VI_ATTR_S stAttr;

		/* dev:0, chn:0,1,2,3 */
		/* dev:1, chn:0,1,2,3 */
		/* dev:2, chn:0,1,2,3 */
		/* dev:3, chn:0,1,2,3 */
		/*Set VI Channel Attribute*/
		memset(&stAttr, 0, sizeof(VI_ATTR_S));
		stAttr.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		stAttr.memtype = V4L2_MEMORY_MMAP;
		stAttr.format.pixelformat = stContext.mConfigPara.PicFormat; // V4L2_PIX_FMT_SBGGR12;
		stAttr.format.field = V4L2_FIELD_NONE;
		stAttr.format.width = stContext.mConfigPara.PicWidth;
		stAttr.format.height = stContext.mConfigPara.PicHeight;
		stAttr.nbufs = 5;
		stAttr.nplanes = 2;
		stAttr.fps = stContext.mConfigPara.FrameRate;
		/* update configuration anyway, do not use current configuration */
		stAttr.use_current_win = 0;
		stAttr.wdr_mode = 0;
		stAttr.capturemode = V4L2_MODE_VIDEO; /* V4L2_MODE_VIDEO; V4L2_MODE_IMAGE; V4L2_MODE_PREVIEW */
		AW_MPI_VI_CreateVipp(vipp_dev);
		AW_MPI_VI_SetVippAttr(vipp_dev, &stAttr);
		AW_MPI_VI_EnableVipp(vipp_dev);

		imageA = Mat(stAttr.format.height,stAttr.format.width,CV_8UC1,Scalar(0));
		imageB = Mat(stAttr.format.height,stAttr.format.width,CV_8UC1,Scalar(0));
		image_diff = Mat(stAttr.format.height,stAttr.format.width,CV_8UC1,Scalar(0));
		g_PixelSize = stAttr.format.width * stAttr.format.height;
		vo_init( stAttr.format.width, stAttr.format.height );

#define ISP_RUN 1
#if ISP_RUN
		int iIspDev;
		/* open isp */
		if (vipp_dev == 0 || vipp_dev == 2) {
			iIspDev = 1;
		} else if (vipp_dev == 1 || vipp_dev == 3) {
			iIspDev = 0;
		}
		AW_MPI_ISP_Init();
		AW_MPI_ISP_Run(iIspDev);
#endif
		// for (virvi_chn = 0; virvi_chn < MAX_VIR_CHN_NUM; virvi_chn++)
		for (virvi_chn = 0; virvi_chn < 1; virvi_chn++) {
			memset(&privCap[vipp_dev][virvi_chn], 0, sizeof(VirVi_Cap_S));
			privCap[vipp_dev][virvi_chn].Dev = stContext.mConfigPara.DevNum;
			privCap[vipp_dev][virvi_chn].Chn = virvi_chn;
			privCap[vipp_dev][virvi_chn].s32MilliSec = 5000;  // 2000;
			privCap[vipp_dev][virvi_chn].displayColor = stContext.mConfigPara.displayColor;
			privCap[vipp_dev][virvi_chn].movDetSen = stContext.mConfigPara.movDetSen;
			if (0 == virvi_chn) { /* H264, H265, MJPG, Preview(LCD or HDMI), VDA, ISE, AIE, CVBS */
				ret = hal_virvi_start(vipp_dev, virvi_chn, NULL); /* For H264 */
				if(ret != 0) {
					printf("virvi start failed!\n");
					result = -1;
					goto _exit;
				}
				privCap[vipp_dev][virvi_chn].thid = 0;
				ret = pthread_create(&privCap[vipp_dev][virvi_chn].thid, NULL, GetCSIFrameThread, (void *)&privCap[vipp_dev][virvi_chn]);
				if (ret < 0) {
					alogd("pthread_create failed, Dev[%d], Chn[%d].\n", privCap[vipp_dev][virvi_chn].Dev, privCap[vipp_dev][virvi_chn].Chn);
					continue;
				}
			}
		}
		for (virvi_chn = 0; virvi_chn < 1; virvi_chn++) {
			pthread_join(privCap[vipp_dev][virvi_chn].thid, NULL);
		}
		for (virvi_chn = 0; virvi_chn < 1; virvi_chn++) {
			ret = hal_virvi_end(vipp_dev, virvi_chn);
			if(ret != 0) {
				printf("virvi end failed!\n");
				result = -1;
				goto _exit;
			}
		}

		vo_exit();
#if ISP_RUN
		AW_MPI_ISP_Stop(iIspDev);
		AW_MPI_ISP_Exit();
#endif
		AW_MPI_VI_DisableVipp(vipp_dev);
		AW_MPI_VI_DestoryVipp(vipp_dev);
		/* exit mpp systerm */
		ret = AW_MPI_SYS_Exit();
		if (ret < 0) {
			aloge("sys exit failed!");
			return -1;
		}
		count++;
	}
	printf("sample_virvi2opencv2vo exit!\n");
	return 0;
_exit:
	return result;
}
