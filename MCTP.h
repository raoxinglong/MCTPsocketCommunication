#ifndef MCTP_MAIN_H
#define MCTP_MAIN_H

#include <linux/if.h>    /* Needed for IFNAMSIZ */

#define MAX_MCTP_THREADS        5
#define MAX_MCTP_LAN_CHANNELS 0x04
#define MAX_ETH_NAME 0x10
#define MAX_ETHIFC_LEN  (IFNAMSIZ+1)

#define MCTPMainPORT 9734
#define MAX_USED_SESSION 36
#define MAX_LAN_BUFFER_SIZE 1024*60
#define MSG_SIZE 256
#define TRUE 1
#define FALSE 0

typedef struct
{
	int     UDPSocket[MAX_MCTP_LAN_CHANNELS];
	int     TCPSocket[MAX_MCTP_LAN_CHANNELS];
	int     LANIFcheckFlag[MAX_MCTP_LAN_CHANNELS];
}LANSocket_T;

typedef struct
{
	char ifname[MAX_ETH_NAME];
	unsigned char Enabled;
	unsigned char Ethindex;
	unsigned char Up_Status;

}LANIfc_t;

typedef struct
{
	unsigned char   TimeToLive;
	unsigned char   IpHeaderFlags;
	unsigned char  TypeOfService;

}Ipv4HdrParam_t;



typedef struct
{
	int  Socket;
	unsigned char   Valid;
	time_t  Time;
	unsigned char  IsLoopBackSocket;
	unsigned char  IsFixedSocket;
}SocketTbl_t;


typedef struct
{
	unsigned char IPAddr[4];  //IPV4 address
	unsigned char Data[MSG_SIZE]; // Data

}MsgPkt_t;

#endif
