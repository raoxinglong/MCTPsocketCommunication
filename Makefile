all: MCTPMain


MCTPMain: MCTP_Main.c MCTP.h
	gcc -o MCTPMain MCTP_Main.c -lpthread
