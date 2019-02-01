#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "mp4v2.h"  
#include "h264analysis.h"
#include "faac.h"
 

typedef unsigned int    uint32_t;
typedef unsigned char   uint8_t;

typedef struct Mp4_Config
{
	MP4TrackId videoId;
	MP4TrackId audioId;            //音频轨道标志符
	unsigned int timeScale;        //视频每秒的ticks数,如90000
	unsigned int fps;              //视频帧率
	unsigned short width;          //视频宽
	unsigned short height;         //视频高
}MP4_CONFIG;

typedef struct Mp4_aac_Config
{
	faacEncHandle hEncoder;        //音频文件描述符
	unsigned int    nPCMBitSize;        //音频采样精度
    unsigned long   nSampleRate;        // 采样率，单位是bps
    unsigned long   nChannels;          // 声道，1为单声道，2为双声道
    unsigned long   nInputSamples;     // 得到每次调用编码时所应接收的原始数据长度
    unsigned long   nMaxOutputBytes;    // 得到每次调用编码时生成的AAC数据的最大长度
	unsigned char* pbPCMBuffer;       //pcm数据
	unsigned char* pbAACBuffer;       //aac数据
	int 	      nPCMBufferSize;
}MP4_AAC_CONFIG;

MP4_CONFIG mp4_config = {MP4_INVALID_TRACK_ID,MP4_INVALID_TRACK_ID,90000,25,640,480};

/*********************************
* 初始化 AAC编码
**********************************/
MP4_AAC_CONFIG * InitAACEncoder(void)
{
	faacEncConfigurationPtr pConfiguration;
	MP4_AAC_CONFIG * aac_config = NULL;
	aac_config = (MP4_AAC_CONFIG *)malloc(sizeof(MP4_AAC_CONFIG));
	
	aac_config->nSampleRate = 8000;
	aac_config->nChannels = 1;
	aac_config->nPCMBitSize = 16;
	aac_config->nInputSamples = 0;
	aac_config->nMaxOutputBytes = 0;
	
	/*1 open FAAC engine */
	aac_config->hEncoder = faacEncOpen(aac_config->nSampleRate,aac_config->nChannels,&aac_config->nInputSamples,&aac_config->nMaxOutputBytes);
	if(aac_config->hEncoder == NULL)
	{
		printf("failed to call faacEncOpen()\n");
		return NULL;
	}
	aac_config->nPCMBufferSize = (aac_config->nInputSamples*(aac_config->nPCMBitSize/8));
	
	aac_config->pbPCMBuffer = (unsigned char *)malloc(aac_config->nPCMBufferSize*sizeof(unsigned char));
	memset(aac_config->pbPCMBuffer, 0, aac_config->nPCMBufferSize);
	
	aac_config->pbAACBuffer=(unsigned char*)malloc(aac_config->nMaxOutputBytes*sizeof(unsigned char));
	memset(aac_config->pbAACBuffer, 0, aac_config->nMaxOutputBytes);
	
	/*2  get set configuration */
	pConfiguration = faacEncGetCurrentConfiguration(aac_config->hEncoder);
#if 1
	pConfiguration->inputFormat = FAAC_INPUT_16BIT;
	pConfiguration->outputFormat = 0;
    pConfiguration->aacObjectType = LOW;
#else
	pConfiguration->inputFormat = FAAC_INPUT_16BIT;
		/*0 - raw; 1 - ADTS*/
	pConfiguration->outputFormat = 0;
    pConfiguration->useTns = 0;
	pConfiguration->allowMidside = 1;
	pConfiguration->shortctl = SHORTCTL_NORMAL;
    pConfiguration->aacObjectType = LOW;
    pConfiguration->mpegVersion = MPEG2;
#endif

	//set encoding configuretion
	int nRet = faacEncSetConfiguration(aac_config->hEncoder, pConfiguration);
	
	return aac_config;
}
/***************************************
* 打开pcm 文件
*************************************/
FILE * OpenPCMFile(const char * filename)
{
	FILE * fp;
	if(NULL == (fp = fopen(filename,"rb")))
	{
		printf("open %s failed.\n",filename);
		exit(0);
	}
	return fp;
}
/*****************************************
* 写AAC 数据
****************************************/
void WriteAACData(FILE *pcmfp, MP4FileHandle hMp4File, MP4_AAC_CONFIG * aac_config)
{
	int nRet;
	int nBytesRead;
	nBytesRead = fread(aac_config->pbPCMBuffer,1,aac_config->nPCMBufferSize,pcmfp);
	// 输入样本数，用实际读入字节数计算，一般只有读到文件尾时才不是nPCMBufferSize/(nPCMBitSize/8);
	aac_config->nInputSamples = nBytesRead/(aac_config->nPCMBitSize / 8);
	nRet = faacEncEncode(aac_config->hEncoder, (int*)(aac_config->pbPCMBuffer),aac_config->nInputSamples,aac_config->pbAACBuffer,aac_config->nMaxOutputBytes);
	MP4WriteSample(hMp4File, mp4_config.audioId, aac_config->pbAACBuffer, nRet , MP4_INVALID_DURATION, 0, 1);
}
void CloseAccEncoder(MP4_AAC_CONFIG* aac_config)
{
	if(aac_config->hEncoder)
	{  
		faacEncClose(aac_config->hEncoder);  
		aac_config->hEncoder = NULL;  
	}
}
//------------------------------------------------------------------------------------------------- Mp4Encode说明
// 【h264编码出的NALU规律】
// 第一帧 SPS【0 0 0 1 0x67】 PPS【0 0 0 1 0x68】 SEI【0 0 0 1 0x6】 IDR【0 0 0 1 0x65】
// p帧      P【0 0 0 1 0x61】
// I帧    SPS【0 0 0 1 0x67】 PPS【0 0 0 1 0x68】 IDR【0 0 0 1 0x65】
// 【mp4v2封装函数MP4WriteSample】
// 此函数接收I/P nalu,该nalu需要用4字节的数据大小头替换原有的起始头，并且数据大小为big-endian格式
//-------------------------------------------------------------------------------------------------
int WriteH264Data(MP4FileHandle hMp4File,NALU_t * n)
{
	int datalen = 0;
	if(hMp4File == NULL)
	{
		return -1;
	}
	switch(n->nal_unit_type)
	{
		case NALU_TYPE_SPS: printf("%s[%d]====NALU_SPS\n",__FUNCTION__,__LINE__);   
				MP4SetVideoProfileLevel(hMp4File, 1); //  Simple Profile @ Level 3 
				MP4AddH264SequenceParameterSet(hMp4File,mp4_config.videoId ,n->buf,n->len); 
			break;
		case NALU_TYPE_PPS:	 printf("%s[%d]====NALU_PPS\n",__FUNCTION__,__LINE__);
                MP4AddH264PictureParameterSet(hMp4File, mp4_config.videoId,n->buf, n->len);
			 break;
		case NALU_TYPE_SEI: printf("%s[%d]====NALU_SEI\n",__FUNCTION__,__LINE__);
			   // MP4AddH264PictureParameterSet(hMp4File, mp4_config.videoId,n->buf, n->len);
			 break;
		case NALU_TYPE_IDR:	printf("%s[%d]====I\n",__FUNCTION__,__LINE__);
			    datalen = n->len+4;
			   unsigned char *data = (unsigned char *)malloc(datalen);
			// MP4 Nalu前四个字节表示Nalu长度
				data[0] =  n->len>>24;
				data[1] =  n->len>>16;
				data[2] =  n->len>>8;
				data[3] =  n->len&0xff;
				memcpy(data+4, n->buf,  n->len);
				if(!MP4WriteSample(hMp4File, mp4_config.videoId, data, datalen, MP4_INVALID_DURATION, 0, 1))
				{
					free(data);
					return 0;
				}
				free(data);
				datalen = 0;
			break;
		case NALU_TYPE_SLICE:printf("%s[%d]====P\n",__FUNCTION__,__LINE__);
			 datalen = n->len+4;
			   unsigned char *pdata = (unsigned char *)malloc(datalen);
			  // MP4 Nalu前四个字节表示Nalu长度
               pdata[0] =  n->len>>24;
               pdata[1] =  n->len>>16;
               pdata[2] =  n->len>>8;
               pdata[3] =  n->len&0xff;
		       memcpy(pdata+4, n->buf,  n->len);
              if(!MP4WriteSample(hMp4File, mp4_config.videoId, pdata, datalen, MP4_INVALID_DURATION, 0, 1))
              {
                 free(pdata);
                 return 0;
              }
			  free(pdata);
			  datalen =0;
			 break;
	}
	return 1;
	
}
/**************************************
* 创建MP4文件
**/
MP4FileHandle CreateMP4File(const char *pFileName,MP4_AAC_CONFIG *aac_config)
{
	if(pFileName == NULL)    
    {    
        return false;    
    } 
	MP4FileHandle hMp4file = MP4Create(pFileName, 0);                 //1.创建输出的MP4文件
	if(hMp4file == MP4_INVALID_FILE_HANDLE)
	{
        printf("open file fialed.\n");
        return NULL;
    }
	MP4SetTimeScale(hMp4file, mp4_config.timeScale);    //2. 接下来设置timescale
	
	// 添加h264 track        
	mp4_config.videoId = MP4AddH264VideoTrack    
									(hMp4file,     
									 mp4_config.timeScale,                               //timeScale
									(mp4_config.timeScale / mp4_config.fps),           //sampleDuration    timeScale/fps
									 mp4_config.width,     								// width  
									 mp4_config.height,    								// height     
									 0x42,//n->buf[1], // sps[1] AVCProfileIndication    
									  0,// n->buf[2], // sps[2] profile_compat    
									 0x1f,//n->buf[3], // sps[3] AVCLevelIndication    
									 3);           // 4 bytes length before each NAL unit   
	if (mp4_config.videoId == MP4_INVALID_TRACK_ID)    
	{    
		printf("add video track failed.\n");    
		return 0;    
	} 
	
	//添加 aac track
	mp4_config.audioId = MP4AddAudioTrack(hMp4file,
										  aac_config->nSampleRate,
										  aac_config->nInputSamples,
										  MP4_MPEG4_AUDIO_TYPE
										  );
										  
	if (mp4_config.audioId == MP4_INVALID_TRACK_ID)    
	{    
		printf("add audio track failed.\n");    
		return 0;    
	} 
	
	MP4SetAudioProfileLevel(hMp4file, 0x2);            //  2    11    1  0
	unsigned char aacConfig[2] = {0x15, 0x88};        // 00010 1011 0001 000
	MP4SetTrackESConfiguration(hMp4file, mp4_config.audioId, aacConfig, 2);
	return hMp4file;
}
void CloseMP4File(MP4FileHandle hMp4File)    
{    
    if(hMp4File)    
    {    
        MP4Close(hMp4File,0);    
        hMp4File = NULL;    
    }    
}   
int WriteMP4File(const char * pFile264,const char * pcmfile,const char * pFileMp4)
{
	NALU_t * n;
	int writelen;
	n = AllocNALU(800000);
	MP4_AAC_CONFIG *aac_config = NULL;
	/* init AAC */
	if((aac_config = InitAACEncoder()) == NULL )
	{
		printf("init acc failed\n");
		return -1;
	}
	
	if(pFile264 == NULL || pFileMp4 == NULL)    
    {    
        return 0;    
    } 
	
	MP4FileHandle hMp4File = CreateMP4File(pFileMp4,aac_config); 
	if(hMp4File == NULL)    
    {    
        printf("ERROR:Create file failed!");    
        return 0;    
    }   
	
    FILE *fp = OpenH264File(pFile264);     
	FILE *fpcm = OpenPCMFile(pcmfile);	
    if(!fp)      
    {      
        printf("ERROR:open fp failed!");    
        return 0;    
    }   
	if(!fpcm)      
    {      
        printf("ERROR:open fpcm failed!");    
        return 0;    
    } 	
	
	while(!feof(fp))
	{
		int nalu_len = GetAnnexbNALU(n,fp);
		WriteAACData(fpcm,hMp4File,aac_config);
		writelen = WriteH264Data(hMp4File,n);   
		if(writelen <= 0)
		{
			break;
		}
	}
	FreeNALU(n);
	fclose(fp);
	CloseAccEncoder(aac_config);
	fclose(fpcm);
	free(aac_config->pbPCMBuffer);
	aac_config->pbPCMBuffer =NULL;
	free(aac_config->pbAACBuffer);
	aac_config->pbAACBuffer = NULL;
	free(aac_config);
	aac_config = NULL;
	CloseMP4File(hMp4File);
}

 
int main(void )
{
	 //simpleh264_parser("stream_chn1.h264");
	WriteMP4File("stream_chn0.h264","audio_chn0.pcm","test.mp4");
	return 0;
}
 
 
 
 #ifdef __cplusplus
}
#endif

