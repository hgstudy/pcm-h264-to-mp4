#include "h264analysis.h"

//static bool flag = true;
static int info3=0, info4=0;

static int FindStartCode3(unsigned char *Buf)
{
 if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=1) return 0; //判断是否为0x000001,如果是返回1
 else return 1;
}

static int FindStartCode4(unsigned char *Buf)
{
 if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=0 || Buf[3] !=1) return 0;//判断是否为0x00000001,如果是返回1
 else return 1;
}
//这个函数输入为一个NAL结构体，主要功能为得到一个完整的NALU并保存在NALU_t的buf中，获取他的长度，填充F,IDC,TYPE位。
//并且返回两个开始字符之间间隔的字节数，即包含有前缀的NALU的长度
int GetAnnexbNALU (NALU_t *nalu, FILE *H264FileHandle)
{
	int pos = 0;
	int StartCodeFound,rewind;
	unsigned char *Buf =NULL;
	if ((Buf = (unsigned char*)calloc (nalu->max_size , sizeof(char))) == NULL)
		printf ("GetAnnexbNALU: Could not allocate Buf memory\n");
	
	nalu->startcodeprefix_len=3;//初始化码流序列的开始字符为3个字节
	
	if(3!= fread(Buf,1,3,H264FileHandle))   //从码流中读取3个字节
	{
		free(Buf);
		return 0;
	}
	info3 = FindStartCode3(Buf);   //判断是否为0x000001
	if(info3 != 1)
	{
		//如果不是，在读一个字节
		if(1 != fread(Buf+3,1,1,H264FileHandle))
		{
			free(Buf);
			return 0;
		}
		info4 = FindStartCode4(Buf);  //判断是否为0x00000001
		if(info4 != 1)
		{
			free(Buf);
			return 0;
		}
		else
		{
			//如果是 0x00000001,得到开始的前缀为四个字节
			pos = 4;
			nalu->startcodeprefix_len = 4;
		}
	}
	else
	{
		//如果是 0x000001 得到开始的前缀为三个字节
		pos = 3;
		nalu->startcodeprefix_len = 3;
	}
	
	//查找下一个字符开始的标志位
	StartCodeFound = 0;
	info3 = info4 = 0;
	while(!StartCodeFound)
	{
		if(feof(H264FileHandle)) //文件结束 返回非0
		{
		  nalu->len = (pos-1)-nalu->startcodeprefix_len;
		  memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);
		  nalu->forbidden_bit = nalu->buf[0] & 0x80; //1 bit
		  nalu->nal_reference_idc = nalu->buf[0] & 0x60; // 2 bit
		  nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;// 5 bit
		  free(Buf);
		  return pos-1;
		}
		Buf[pos++] = fgetc(H264FileHandle);  //读一个字节
	    info4 = FindStartCode4(&Buf[pos-4]);  //判断是否为0x00000001
		if(info4 != 1)
		{
			info3 = FindStartCode3(&Buf[pos-3]);
		}
		StartCodeFound = (info3==1 || info4==1);
	}
	
	 // Here, we have found another start code (and read length of startcode bytes more than we should
	// have.  Hence, go back in the file
	rewind = (info4 == 1)? -4 : -3;
	if(fseek(H264FileHandle,rewind,SEEK_CUR) != 0)  //把文件指针指向前一个NALU的末尾
	{
		free(Buf);
		printf("GetAnnexbNALU: Cannot fseek in the bit stream file.\n");
	}
	  // Here the Start code, the complete NALU, and the next start code is in the Buf.
   // The size of Buf is pos, pos+rewind are the number of bytes excluding the next
   // start code, and (pos+rewind)-startcodeprefix_len is the size of the NALU excluding the start code
	nalu->len = pos+rewind - nalu->startcodeprefix_len;
	memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);//拷贝一个完整NALU，不拷贝起始前缀0x000001或0x00000001
	nalu->forbidden_bit = nalu->buf[0] & 0x80; //1 bit
	nalu->nal_reference_idc = nalu->buf[0] & 0x60; // 2 bit
	nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;// 5 bit
	free(Buf);

   return (pos+rewind);//返回两个开始字符之间间隔的字节数，即包含有前缀的NALU的长度
}

FILE * OpenH264File(const char * filename)
{
	FILE * fp;
	if(NULL == (fp = fopen(filename,"rb")))
	{
		printf("open %s failed.\n",filename);
		exit(0);
	}
	
	return fp;
}

//为NALU_t结构体分配内存空间
NALU_t *AllocNALU(int buffersize)
{
	NALU_t *n;
	if(NULL == (n = (NALU_t *)calloc(1,sizeof(NALU_t))))
	{
		printf("calloc failed.\n");
		exit(0);
	}
	n->max_size = buffersize;
	if ((n->buf = (char*)calloc (buffersize, sizeof (char))) == NULL)
	{
		free (n);
		printf ("AllocNALU failed: n->buf");
		exit(0);
	}
	return n;
}

//释放
void FreeNALU(NALU_t *n)
{
  if (n)
  {
    if (n->buf)
    {
      free(n->buf);
      n->buf=NULL;
    }
    free(n);
  }
}

void simpleh264_parser(const char * filename)
{
	NALU_t * n;
	FILE * file;
	file = OpenH264File(filename);
	n = AllocNALU(1048576);
	              
	int nal_num =0;
	int data_offset=0;
	printf("-----+-------- NALU Table ------+---------+\n");
	printf(" NUM |    POS  |    IDC |  TYPE |   LEN   |\n");
	printf("-----+---------+--------+-------+---------+\n");
	while(!feof(file))
	{	
		char type_str[20]={0};
		int nalu_len = GetAnnexbNALU(n,file);
		switch(n->nal_unit_type){
			case NALU_TYPE_SLICE:sprintf(type_str,"SLICE");break;
			case NALU_TYPE_DPA:sprintf(type_str,"DPA");break;
			case NALU_TYPE_DPB:sprintf(type_str,"DPB");break;
			case NALU_TYPE_DPC:sprintf(type_str,"DPC");break;
			case NALU_TYPE_IDR:sprintf(type_str,"IDR");break;
			case NALU_TYPE_SEI:sprintf(type_str,"SEI");break;
			case NALU_TYPE_SPS:sprintf(type_str,"SPS");break;
			case NALU_TYPE_PPS:sprintf(type_str,"PPS");break;
			case NALU_TYPE_AUD:sprintf(type_str,"AUD");break;
			case NALU_TYPE_EOSEQ:sprintf(type_str,"EOSEQ");break;
			case NALU_TYPE_EOSTREAM:sprintf(type_str,"EOSTREAM");break;
			case NALU_TYPE_FILL:sprintf(type_str,"FILL");break;
		  }
		  char idc_str[20]={0};
		switch(n->nal_reference_idc>>5){
			case NALU_PRIORITY_DISPOSABLE:sprintf(idc_str,"DISPOS");break;
			case NALU_PRIRITY_LOW:sprintf(idc_str,"LOW");break;
			case NALU_PRIORITY_HIGH:sprintf(idc_str,"HIGH");break;
			case NALU_PRIORITY_HIGHEST:sprintf(idc_str,"HIGHEST");break;
		}
		printf("%5d| %8d| %7s| %6s| %8d|\n",nal_num,data_offset,idc_str,type_str,n->len);
		if(n->len >= 1048576 )
		{          
			printf("alloc is small,slice is large %d.\n",n->len);
			exit(0);
		}
		data_offset = data_offset+nalu_len;
		nal_num++;
	}
	FreeNALU(n);
}