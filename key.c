#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "hi_common.h"
#include "hi_comm_video.h"
#include "hi_comm_sys.h"
#include "mpi_sys.h"
#include "hi_comm_vb.h"
#include "mpi_vb.h"
#include "hi_comm_vpss.h"
#include "mpi_vpss.h"
#include "mpi_vgs.h"



void CS(unsigned char flag)
{	
	if(flag == 0)
		//system("himm 0x2016040 0xFF");
		HI_MPI_SYS_SetReg(0x20160040,0x00);
	else
	//	system("himm 0x2016040 0x00");
		HI_MPI_SYS_SetReg(0x20160040,0xFF);
}

void DATA(unsigned char flag)
{
	//printf("%d \n",flag);
	
	if(flag == 0)
		//system("himm 0x2016080 0xFF");
		HI_MPI_SYS_SetReg(0x20160080,0x00);
	else
		//system("himm 0x2016080 0x00");
		HI_MPI_SYS_SetReg(0x20160080,0xFF);
}


int set_gpio_mode(unsigned int reg,int nIndex,int mode)
{
	//只能设定GPIO_DIR值.
	if((reg & 0x0400) != 0x0400)
		return -1;

	unsigned int nValue;
	if(HI_SUCCESS == HI_MPI_SYS_GetReg(reg,&nValue))
	{
		if(mode == 1)
		{
			nValue |= (0x01<<nIndex);
		}
		else
		{
			nValue &= ~(0x01<< nIndex);
		}
		HI_MPI_SYS_SetReg(reg,nValue);
	}
	return -1;	
}

//RST H2 GPIO0_2 muxctrl_reg30

static void key_Cfg(void)
{
	system("himm  0x200F0024  0x00");//set gpio mode
    system("himm  0x20140400  0x00");//set out mode
	usleep(100);
}


static unsigned int read_key_value()
{
    unsigned int uiRegValue = 0;    
    //HI_MPI_SYS_GetReg(0x20140100, &uiRegValue);    
    HI_MPI_SYS_GetReg(0x201403FC, &uiRegValue);    
	//printf("%s --- %d \n",__FUNCTION__,uiRegValue);
    return uiRegValue;
}

void sendcmd(unsigned char uCmd)
{
	printf("%s %d \n",__FUNCTION__,uCmd);

	int i=0;
	CS(0);
	for(i = 0 ;i< 8;i++)
	{
		DATA(uCmd & 0x80);
		usleep(12000);
		uCmd = uCmd << 1;
	}
	CS(1);
	sleep(1);
}
static void key_check_handle_thread()
{
    unsigned int uiRegValue = 0,uiRegValue1 = 0;
	static int flag = 0;
	
    while(1)
    {   
    	CS(1);
		DATA(1);
        uiRegValue = read_key_value();			
		usleep(10000);
		if(uiRegValue == 4)continue;
			
		uiRegValue1 = read_key_value();	//消抖	

        if(uiRegValue == uiRegValue1)
        {
			sendcmd(flag%3 + 1);
			flag ++;			
		}         
	}
}
static void key_check_thread(void) 
{ 	
	pthread_t tfid;
    int ret = 0;

	ret = pthread_create(&tfid, NULL, (void*)key_check_handle_thread, NULL);
    if (ret != 0)
    {
        //printf("pthread_create failed, %d, %s\n", errno, strerror(errno));
        return ;
    }
    pthread_detach(tfid);
}

/*
CS    SINGAL_IN4      B14  muxctrl_reg8   GPIO2_4
//GPIO
himm 0x200F0020 0x00
himm 0x20160400 0x10
himm 0x201603FC 0xFF
*/


/*

DATA    SINGAL_IN3    A13  muxctrl_reg9   GPIO2_5 

himm 0x200F0024 0x00
himm 0x20160400 0x20
himm 0x201603FC 0xFF

himm 0x201603FC 0x00

*/

//GPIO2_4


static void serio(void)
{
	//CS
	system("himm 0x200F0020 0x00");
	set_gpio_mode(0x20160400,4,1);
	//system("himm 0x20160400 0x10");
	system("himm 0x201603FC 0xFF");
	
	//Data
	system("himm 0x200F0024 0x00");
	//system("himm 0x20160400 0x20");
	set_gpio_mode(0x20160400,5,1);
	system("himm 0x201603FC 0xFF");
	usleep(100);
}



static void read_serial()
{
    while(1)
    {   
    	CS(1);
		DATA(1);

		switch(getchar())
		{
			case 0x31:
				sendcmd(0x01);
				printf("0x01 start rec\n");
			break;
			case 0x32:
				sendcmd(0x02);
				printf("0x02 stop rec\n");
				break;
			case 0x33:
				sendcmd(0x03);
				printf("0x03 capture\n");
			break;
		}
	}
}


int main()
{
	key_Cfg();
	serio();
	CS(1);
	DATA(1);
	//key_check_thread();
	//key_check_handle_thread();
	read_serial();
	return 0;
}
