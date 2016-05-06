#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <errno.h>
#include <string.h>
//#include <semaphore.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>


#include "MCTP.h"

/*------------------------ Global Definitions ------------------------*/

// Thread IDs
static pthread_t   pMCTPLANReceiver;
pthread_t   gthreadIDs[MAX_MCTP_THREADS] = {0};     // Stores all child PIDs for control


// Global Variables
int  gthreadIndex   = 0;            // Id each of the thread

#define UTIL_MAX(val1, val2) \
	((val1 > val2) ? val1 : val2)


SocketTbl_t SocketTable[MAX_USED_SESSION];
LANSocket_T LANConf;

LANIfc_t LANIfcConfig[4]=
{
	{"eth0", FALSE, 0, 0},
	{"eth1", FALSE, 1, 0},
	{"bond0", FALSE, 2, 0},
	{"", FALSE, 0, 0},


};

static void* MCTPLANReceiver(void* pArg);
static int InitializeMCTPLANSockets(void);
static int  ReadData (MsgPkt_t *pMsgPkt, int Socket);
static int AddSocket (int Socket,unsigned char IsLoopBackSocket, unsigned char IsFixedSocket);
static int RemoveSocket (int Socket);
static void SetSocketNonBlocking(int s);
static void SetFd (fd_set *fd);
static int ClearSocketTable (void);
static int InitializeSocket(unsigned int* n);
int InitTCPSocket(int EthIndex);
int get_network_interface_count(int *ifcount);
int get_up_network_interfaces(char *up_interfaces, int *ifupcount, int ifcount);
static int ConfigureSocket(int Ethindex);
static int DeConfigureSocket(int Ethindex);



/**
 *  * @fn  ReadData
 *   * @brief This function receives the LAN request packets
 *    *
 *     * @param pMsgPkt - pointer to message packet
 *      * @param Socket  - Socket handle
 *      **/
static int  ReadData (MsgPkt_t *pMsgPkt, int Socket)
{
	unsigned int SourceLen = 0;

	/*INT8U   *pData      = pMsgPkt->Data;  //need to be add later  */
	char pData[256];
	signed short  Len         = 0;
	unsigned short  RecvdLen    = 0;
	void *Source = NULL ;
	struct  sockaddr_in Sourcev4;
	int retry = 3;

	Source = &Sourcev4;
	SourceLen = sizeof(Sourcev4);


	//test by myself
	//read(Socket,pData,sizeof(pData));
	//printf("read data = %s\n",pData);
	/* Read  bytes */
#if 0
	while (RecvdLen < 256)
	{
		Len = recvfrom (Socket, pData, MAX_LAN_BUFFER_SIZE, 0, (struct sockaddr *)Source, &SourceLen);
		printf("%s: Len = %d\n",__func__,Len);
		if((Len >= -1) && (Len <= 0))
		{
			if(Len == -1)
			{
				if(errno == EAGAIN)
				{
					retry--;
				}
				else
					return -1;
			}
			else
				return -1;
		}
		if(retry == 0)
		{
			return -1;
		}
		RecvdLen += (unsigned short)Len;
	}
#endif
	//while(RecvdLen < 16)
	// {
	Len = recv(Socket,pData,MAX_LAN_BUFFER_SIZE,0);
	printf("%s: Len = %d\n",__func__,Len);
	if(Len < 0)
	{
		if(errno == EAGAIN)
		{
			retry--;
		}
		else
			return -1;
		if(retry == 0)
		{
			return -1;
		}
	}
	else if(Len == 0)
	{
		//printf("Nothing to do and will close socket\n");
		return 1;
	}
	else
		RecvdLen += (unsigned short)Len;
	// }

	printf("RecvdLen = %d\n",RecvdLen);
	printf("%s: pData[] = %s  \n",__func__,pData);
	if(RecvdLen != 0)
	{
		return 1;
	}

	/*  pMsgPkt->Size     = RecvdLen;
	 *      pMsgPkt->UDPPort  = ((struct sockaddr_in *)Source)->sin_port;
	 *          pMsgPkt->Socket   = Socket;
	 *          */

	//   printf("\n I received in Socket  and Channel for verification : %x\n",Socket);
	// if(SourceLen!=0) 
	// {
	//     memcpy (pMsgPkt->IPAddr, &((struct sockaddr_in *)Source)->sin_addr.s_addr, sizeof (struct in_addr));
	// }

	//printf("%s: pData[] = %s  \n",__func__,pData);
	return 0;
}


/**
 *  * @fn  AddSocket
 *   * @brief This function adds the new socket handle to socket table
 *    *
 *     * @param Socket - socket handle to be added.
 *       IsLoopBackSocket -If Socket is loopback. This will be true
 *       IsFixedSocket socket means, Socket which are created during InitSocket Process
 *
 ***/
static int AddSocket (int Socket,unsigned char IsLoopBackSocket, unsigned char IsFixedSocket)
{
	int i;
	for (i = 0; i < (MAX_USED_SESSION + 1); i++)
	{
		if (0 == SocketTable [i].Valid)
		{
			SocketTable [i].Socket = Socket;
			SocketTable [i].Valid  = 1;
			SocketTable [i].Time   = 0;
			SocketTable[i].IsLoopBackSocket= IsLoopBackSocket;
			SocketTable[i].IsFixedSocket=IsFixedSocket;
			return 0;
		}
	}

	printf("Error adding new socket to the table\n");
	return -1;
}

/**
 *  * @fn  RemoveSocket
 *   * @brief This function removes the socket from the socket table
 *    *
 *     * @param Socket - socket handle to be removed.
 *     **/
static int RemoveSocket (int Socket)
{
	int i;
	for (i = 0; i < (MAX_USED_SESSION + 1); i++)
	{
		if (Socket == SocketTable [i].Socket)
		{
			SocketTable [i].Socket = 0;
			SocketTable [i].Valid  = 0;
			return 0;
		}
	}

	printf("Error removing socket from the table\n");
	return -1;
}


static void SetSocketNonBlocking(int s)
{
	int flags;

	flags = fcntl(s, F_GETFL, 0);
	if(flags == -1)
	{
		flags = 0;
	}

	flags |= O_NONBLOCK;

	flags = fcntl(s, F_SETFL, flags);
	if(flags == -1)
	{
		printf("Could not set socket to non blocking!!\n");
	}

	return;
}


/**
 *  * @fn  SetFd
 *   * @brief This function sets the file descriptor.
 *    * @param fd
 *    **/
static void SetFd (fd_set *fd)
{
	int i;
	int x=0;

	FD_ZERO (fd);

	//printf("SetFd ......\n");
	for(x=0;x<MAX_MCTP_LAN_CHANNELS;x++)
	{
		if((LANIfcConfig[x].Enabled == TRUE)
				&& (LANIfcConfig[x].Up_Status == 1))
		{
			if(LANConf.TCPSocket[x] != -1)
				FD_SET(LANConf.TCPSocket[x], fd);
			// printf("LANConf.TCPSocket[%d] = %d is set\n",x,LANConf.TCPSocket[x]);
		}
	} 

	for (i = 0; i < (MAX_USED_SESSION + 1); i++)
	{
		if (1 == SocketTable[i].Valid)
		{
			FD_SET(SocketTable[i].Socket, fd);
			printf("SocketTable[%d].Socket = %d is valid\n",i,SocketTable[i].Socket);
		}
	}
	// return;
}


static int ClearSocketTable (void)
{
	int i;
	for (i = 0; i < (MAX_USED_SESSION + 1); i++)
	{
		//printf("debug 2...\n");
		SocketTable[i].Valid = 0;
	}
	return 0;
}


static int InitializeSocket(unsigned int* n)
{
	int x=0;
	*n=0;
	ClearSocketTable();
	for(x=0; x < MAX_MCTP_LAN_CHANNELS; x++)
	{
		if((LANIfcConfig[x].Enabled == TRUE)
				&& (LANIfcConfig[x].Up_Status == 1))
		{
#if 0
			if(LANConf.UDPSocket[x] != -1)
			{
				*n = (UTIL_MAX (*n, LANConf.UDPSocket[x])) ;
				/* Adding UDP Socket to the table */
				AddSocket (LANConf.UDPSocket[x], FALSE, TRUE);
				SetSocketNonBlocking(LANConf.UDPSocket[x]);
			}
#endif          

			if(LANConf.TCPSocket[x] != -1)
			{
				*n = (UTIL_MAX (*n, LANConf.TCPSocket[x]));
				// printf("*n = %d and LANConf.TCPSocket[%d] = %d \n",*n,x,LANConf.TCPSocket[x]);
			}

		}
	}
	return 0;
}


/*thread for receive and handle LAN packet*/
static void* MCTPLANReceiver(void* pArg)
{
	MsgPkt_t            MsgPkt; //received msg data and define MsgPkt_t struct in the future
	unsigned int        SourceLen, n=0, i, RetVal, NewTCPSocket;
	struct sockaddr_in  Sourcev4;
	fd_set              fdRead;
	struct timeval      Timeout;
	unsigned char IsloopBackStatus=FALSE;
	void *Source = NULL;

	prctl(PR_SET_NAME,__func__,0,0,0);
	InitializeSocket(&n);
	printf("InitializeSocket value: n = %d\n",n);
	/* Loop forever */
	while(1)
	{
		IsloopBackStatus = FALSE;
		SetFd(&fdRead);
		//printf("thread while\n");
		//   sleep(1);
		printf("%s : while get n = %d\n",__func__,n);
#if 1
		/* Set the timeout value for the select */
		Timeout.tv_sec = 2;
		/* Wait for an event on the socket*/
		RetVal = select((n+1), &fdRead, NULL, NULL, &Timeout);
		printf("Retval = %d\n",RetVal);
		if (-1 == RetVal)
		{
			printf("Error in select socket\n");
			continue;
		}

		if (0 == RetVal)
		{
			/* Its due to timeout - continue */
			continue;
		}

		/* Initialization of socket is done based on Network state change
		 *          or changes in NCML Configurations */
#if 0
		if(FD_ISSET(pBMCInfo->LANConfig.hLANMon_Q,&fdRead))
		{
			OS_GET_FROM_Q(&Buff,sizeof(int), pBMCInfo->LANConfig.hLANMon_Q,0,&Err);
			UpdateLANStateChange(BMCInst);
			InitializeSocket(&fdRead,&n,BMCInst);
			continue;
		}
#endif

		for(i=0;i<MAX_MCTP_LAN_CHANNELS;i++)
		{
			if((LANIfcConfig[i].Enabled == TRUE) 
					&& (LANIfcConfig[i].Up_Status == 1))
			{

				SourceLen = sizeof(Sourcev4);
				Source = &Sourcev4;

				if(LANConf.TCPSocket[i] != -1)
				{
					if (FD_ISSET (LANConf.TCPSocket[i], &fdRead))
					{
						/*Accept new TCP connections */
						NewTCPSocket = accept(LANConf.TCPSocket[i], (struct sockaddr *)Source, &SourceLen );    
						printf("accept NewTCPSocket value = %d\n",NewTCPSocket);
						if (-1 == NewTCPSocket)
						{
							printf("Error accepting connections \n");
							continue; //addednow
						} 
						/* Add the socket to the table */
						if(0 == AddSocket (NewTCPSocket, IsloopBackStatus, FALSE))
						{
							/* Add the new TCP client to set */
							FD_SET (NewTCPSocket, &fdRead);
							n = (NewTCPSocket >= n) ? NewTCPSocket + 1 : n;
							printf("reset n value = %d\n",n);
						}
						else
						{
							printf("Close NewTCP socket\n");
							close (NewTCPSocket);
						}
					}
				}

				/*Only IPv4 supported*/ 
				memcpy (&MsgPkt.IPAddr, &((struct sockaddr_in *)Source)->sin_addr.s_addr, sizeof (struct in_addr));
			}
		}

		for (i = 0; i < (MAX_USED_SESSION + 1); i++)
		{
			if ((FD_ISSET (SocketTable[i].Socket, &fdRead)) && (SocketTable[i].Valid))
			{
				printf("%s: ...start read SocketTable[%d].Socket = %d data\n",__func__,i,SocketTable[i].Socket);
				/* Receive IPMI LAN request packets */
				if(0 == ReadData (&MsgPkt, SocketTable[i].Socket))
				{
					/* Post the request packet to LAN Interface Queue */
#if 0
					if (0 != PostMsg (&MsgPkt, LAN_IFC_Q, BMCInst))
					{
						printf("Error posting message to LANIfc Q\n");
					}
#endif

					SocketTable[i].Time = 0;
				}
				else
				{
					if(!SocketTable[i].IsFixedSocket )
					{
						printf("Closing SocketTable [%d].Socket  socket\n",i);
						/* Remove the socket from the table and the set */
						FD_CLR (SocketTable [i].Socket, &fdRead);
						close (SocketTable [i].Socket);
						RemoveSocket (SocketTable [i].Socket);
					}
				}
			}/*if (FD_ISSET (Socket [i], &fdRead))*/
		}/*for (i = 0; i <= (MaxSession + 1); i++)*/
#endif
	} /* while(1) */
	/* never gets here */
	pthread_exit(NULL);
	//return;
}

/**
 * *@fn InitTCPSocket
 * *@brief This function is invoked to initialize LAN tcp sockets
 * *@return Returns 0 on success
 * */
int InitTCPSocket(int EthIndex)
{
	int     reuseaddr1 = 1;
	struct  sockaddr_in   Local;
	char ethname[MAX_ETHIFC_LEN];
	int ret = 0;
	printf("debug ......\n");

	if(LANConf.TCPSocket[EthIndex] != -1)
	{
		shutdown(LANConf.TCPSocket[EthIndex],SHUT_RDWR);
		close(LANConf.TCPSocket[EthIndex]);
		LANConf.TCPSocket[EthIndex] = -1;
	}
	/*IPv4*/
	Local.sin_family = AF_INET;
	Local.sin_addr.s_addr = htonl(INADDR_ANY);
	Local.sin_port = htons(MCTPMainPORT);
	LANConf.TCPSocket[EthIndex] = socket(AF_INET, SOCK_STREAM, 0);
	if ( LANConf.TCPSocket[EthIndex] == -1)
	{
		printf("LANIfc.c : Unable to create TCP socket\n");
		return -1;
	}
	memset(ethname, 0, sizeof(ethname));
	strcpy(ethname, LANIfcConfig[EthIndex].ifname);

	/* Initialize The Socket */

	if (0 != setsockopt (LANConf.TCPSocket[EthIndex], SOL_SOCKET, SO_BINDTODEVICE, ethname, sizeof (ethname)+1))
	{
		printf("SetSockOpt Failed for TCP Socket");
		return -1;
	}

	if (0 != setsockopt(LANConf.TCPSocket[EthIndex], SOL_SOCKET, SO_REUSEADDR, &reuseaddr1, sizeof(int)))
	{
		printf("Setsockopt(SO_REUSEADDR) Failed for TCP socket\n");
		return -1;
	} 

	ret = bind(LANConf.TCPSocket[EthIndex], (struct sockaddr *)&Local, sizeof(Local) );
	if (ret != 0)
	{
		printf("bind UDP socket error\n");
		return -1;
	}

	if (listen(LANConf.TCPSocket[EthIndex], MAX_USED_SESSION) == -1)
	{
		printf("Error listen\n");
		return -1;
	}
	printf("%s:  (TCP Sockets %d)  Successful on name (LANIfcConfig[%d].ifname = %s)\n",__func__,LANConf.TCPSocket[EthIndex],EthIndex,LANIfcConfig[EthIndex].ifname);

	return 0;
}


/*
 * *@fn get_network_interface_count
 * *@brief This function return the number of all network interfaces                       
 * *@param ifcount - The count of all interfaces
 * *@return Returns -1 on failure
 * *        Returns 0 on success
 * */
int get_network_interface_count(int *ifcount)
{   
#if 0
	int fd;
	int ret;

	fd = open("/dev/netmon", O_RDONLY);
	if (fd == -1)
	{
		printf("ERROR: open failed\n");
		return -1;
	}

	ret = ioctl(fd, NETMON_GET_INTERFACE_COUNT, ifcount);
	if (ret < 0)
	{
		printf("ERROR: Get Numnber of Interfaces failed\n");
		close(fd);
		return -1;
	}

	printf("Number of Network Interfaces = %d\n",*ifcount);

	close(fd);
#endif
	//add it for debug
	*ifcount = 1;
	return 0;
}

/*
 * *@fn get_up_network_interfaces
 * *@brief This function return the network interfaces with up status in the system at the time                       
 * *@param up_interfaces - An array of interface name strings that are currently enabled
 * *@param ifupcount - The count of the interfaces that are currently enabled
 * *@param ifcount - The count of all interfaces
 * *@return Returns -1 on failure
 * *        Returns 0 on success
 * */
int get_up_network_interfaces(char *up_interfaces, int *ifupcount, int ifcount)
{
#if 0
	int fd;
	int ret,i;
	INTERFACE_LIST *ilist;
	char *names;
	unsigned char *up_status;

	if (ifcount < 0)
	{
		printf("ERROR: The number of interface less then 0\n");
		return -1;
	}

	fd = open("/dev/netmon", O_RDONLY);
	if (fd == -1)
	{
		printf("ERROR: open failed\n");
		return -1;
	}

	/*
	 *      * Get all interface list
	 *           */ 
	ilist= (INTERFACE_LIST *)malloc(sizeof(INTERFACE_LIST));
	if (ilist == NULL)
	{
		printf("ERROR: Unable to allocate memory for interface list\n");
		close(fd);
		return -1;
	}

	names = malloc(ifcount* (IFNAMSIZ+1));
	if (names == NULL)
	{
		printf("ERROR: Unable to allocate memory for interface names list\n");
		close(fd);
		free(ilist);
		return -1;
	}
	ilist->ifname = names;

	up_status = malloc(ifcount* (sizeof(unsigned char)));
	if (up_status == NULL)
	{
		printf("ERROR: Unable to allocate memory for interface active status list\n");
		close(fd);
		free(names);
		free(ilist);
		return -1;
	}
	ilist->ifupstatus = up_status;

	ilist->count=0;
	ret = ioctl(fd, NETMON_GET_INTERFACE_LIST, ilist);
	if (ret < 0)
	{
		printf("ERROR: Get Iterfaces failed\n");
		close(fd);
		free(up_status); 
		free(names);
		free(ilist);
		return -1;
	}

	// Only copy the interface name with UP status
	(*ifupcount)=0;
	for(i=0;i<ilist->count;i++)
	{
		if(ilist->ifupstatus[i] == 1)
		{
			(*ifupcount)++;
			memcpy(&up_interfaces[i*(IFNAMSIZ+1)], &names[i*(IFNAMSIZ+1)], (IFNAMSIZ+1));
		}
	}

	close(fd);
	free(up_status); 
	free(names);
	free(ilist);

#endif
	//add it for debug
	char* up_interfaces_rao = "eth0";
	int count = 1;
	memcpy(up_interfaces,up_interfaces_rao,sizeof(up_interfaces_rao));
	memcpy(ifupcount,&count,sizeof(ifupcount));
	//printf("%s: up_interfaces = %s and ifupcount = %d\n",__func__,up_interfaces,*ifupcount);
	return 0;
}


/**
 *  * @fn ConfigureBONDSocket
 *   * @brief configures bond udp sockets
 *    * @return   1 if success, -1 if failed.
 *    **/
static int ConfigureSocket(int Ethindex)
{
#if 0
	if (InitUDPSocket( Ethindex) != 0)
	{
		printf("Error in creating UDP Sockets\n");
	}
#endif
	if (InitTCPSocket(Ethindex) != 0)
	{
		printf("Error in creating TCP Sockets\n");
	}

	//add for debug
	//return 0;
	return 1;
}


static int DeConfigureSocket(int Ethindex)
{
#if 0
	if(LANConf.UDPSocket[Ethindex] != -1)
	{
		close(LANConf.UDPSocket[Ethindex]);
		LANConf.UDPSocket[Ethindex] = -1;
	}
#endif
	if(LANConf.TCPSocket[Ethindex] != -1)
	{
		shutdown(LANConf.TCPSocket[Ethindex],SHUT_RDWR);
		close(LANConf.TCPSocket[Ethindex]);
		LANConf.TCPSocket[Ethindex] = -1;
	}
	return 0;
}



static int InitializeMCTPLANSockets(void)
{
	int up_count = 0,i,j,count=0,ifcupdated=0;
	char *EthIfcname=NULL,ifname[MAX_ETHIFC_LEN];

	if(get_network_interface_count(&count) < 0)
	{
		return -1;
	}
	//printf("%s:  MAX_ETHIFC_LEN = %d and count = %d\n",__func__,MAX_ETHIFC_LEN,count);
	EthIfcname = malloc(MAX_ETHIFC_LEN * count);
	if(EthIfcname == NULL)
	{
		printf("Error in allocating memory\n");
		return -1;
	}

	memset(EthIfcname,0,MAX_ETHIFC_LEN * count);
	if(get_up_network_interfaces(EthIfcname,&up_count,count) < 0)
	{
		return -1;
	}
	//printf("%s:  ....EthIfcname = %s and up_count = %d\n",__func__,EthIfcname,up_count);
	for(i=0;i<MAX_MCTP_LAN_CHANNELS;i++)
	{

		for(j=0;j<count;j++)
		{
			if((strcmp(LANIfcConfig[i].ifname,&EthIfcname[j*MAX_ETHIFC_LEN]) == 0)
					&& (strlen(LANIfcConfig[i].ifname) != 0))
			{
				LANIfcConfig[i].Up_Status = 1;
				LANIfcConfig[i].Enabled = TRUE;
				ifcupdated = 1;
				// printf("%s: ......LANIfcConfig[%d].ifname = %s  ........\n",__func__,i,LANIfcConfig[i].ifname);
			}
		}
		if (ifcupdated == 0)
		{
			LANIfcConfig[i].Up_Status = 0;
		}
		else
			ifcupdated = 0;
	}
	free(EthIfcname);

	for(j=0;j <MAX_MCTP_LAN_CHANNELS;j++)
	{
		if((LANIfcConfig[j].Enabled == TRUE) 
				&& (LANIfcConfig[j].Up_Status == 1))
		{
			if( LANConf.LANIFcheckFlag[j] == 0)
			{
				if(ConfigureSocket(j) != 1)
				{
					printf("%s: Error in creating Bond Socket\n",__func__);
				}
				/*Set the LANInterface Enabled flag*/
				LANConf.LANIFcheckFlag[j] = 1;
			}
		}

		if((LANIfcConfig[j].Enabled == TRUE) 
				&& (LANIfcConfig[j].Up_Status == 0))
		{
			if( LANConf.LANIFcheckFlag[j] != 0)
			{
				if(DeConfigureSocket(j) != 0)
				{
					printf("Error in deconfiguring bond socket\n");
				}
				LANConf.LANIFcheckFlag[j] = 0;
			}

		}
	}

	return 0;
}


int main(int argc,char *argv[])
{
	int retval=0;
	void *thread_result;
	// Initialize the MCTP lan Sockets
#if 1
	retval = InitializeMCTPLANSockets();
	if (retval < 0)
	{
		printf("MCTPFromLAN_ERROR: Unable to Establish a UDS for MCTP Connections.\n");
		return -1;
	} 
#endif

#if 1
	// This thread receives the Data from LAN
	if (0 != pthread_create(&pMCTPLANReceiver, NULL, (void *)MCTPLANReceiver, NULL))
	{
		printf("MCTPFromLAN_ERROR: Unable to create a thread to receive the Messages from LAN\n");
		return -1; 
	}
	if(0 != pthread_join(pMCTPLANReceiver,&thread_result))
	{
		printf("pthread join fail\n");
	}
	gthreadIDs[gthreadIndex++] = pMCTPLANReceiver;
	//   printf("pMCTPLANReceiver = %d\n",pMCTPLANReceiver);
#endif
	//MCTPLANReceiver();
	return 0;
}
