#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#define SEND_BUFF_SIZE	1024
#define RECV_BUFF_SIZE	1024

static char *dev = "/dev/ttyUSB0";
static int baud_rate = 115200;//B115200;
static int interval = 100000;//us
static pthread_t recv_thd;
static int terminated = 0;
static char *send_buff = NULL;
static char *recv_buff = NULL;

static void usage(char *name)
{
	printf("%s [-d <device_path> | -b <baud_rate> | -i <interval> | -h]\n", name);
	printf("options\n");
	printf("  -d <device_path>\n    customize device path, default is:%s\n", dev);
	printf("  -b <baud_rate>\n    customize baud rate, default is %dHz\n", baud_rate);
	printf("  -i <interval>\n    customize interval of receive thread, default is %dus\n", interval);
	printf("  -h\n    show this usage\n");
}

//serial port set function
static void set_termios(struct termios *pNewtio, unsigned short uBaudRate)
{
	  bzero(pNewtio,sizeof(struct termios));
	  pNewtio->c_cflag = uBaudRate|CS8|CREAD|CLOCAL;
	  pNewtio->c_iflag = IGNPAR;
	  pNewtio->c_oflag = 0;
	  pNewtio->c_lflag = 0;
	  pNewtio->c_cc[VINTR] = 0;
	  pNewtio->c_cc[VQUIT] = 0;
	  pNewtio->c_cc[VERASE] = 0;
	  pNewtio->c_cc[VKILL] = 0;
	  pNewtio->c_cc[VEOF] = 4;
	  pNewtio->c_cc[VTIME] = 5;
	  pNewtio->c_cc[VMIN] = 0;
	  pNewtio->c_cc[VSWTC] = 0;
	  pNewtio->c_cc[VSTART] = 0;
	  pNewtio->c_cc[VSTOP] = 0;
	  pNewtio->c_cc[VSUSP] = 0;
	  pNewtio->c_cc[VEOL] = 0;
	  pNewtio->c_cc[VREPRINT] = 0;
	  pNewtio->c_cc[VDISCARD] = 0;
	  pNewtio->c_cc[VWERASE] = 0;
	  pNewtio->c_cc[VLNEXT] = 0;
	  pNewtio->c_cc[VEOL2] = 0;
}

static void *recv_thread(void *param)
{
	int fd;
	int ret;
	int i;

	fd = *(int *)param;
	if (fd <= 0)
		return NULL;

	while (!terminated) {
		ret = read(fd, recv_buff, RECV_BUFF_SIZE);
		if (ret > 0) {
			recv_buff[ret] = 0;
			printf("%s\nDUMP:\n", recv_buff);
			for (i = 0; i < ret; i ++) {
				if (i % 16 == 0)
					printf("%08X:", i);
				printf(" %02X", recv_buff[i] & 0xFF);
				if ((i + 1) % 16 == 0)
					printf("\n");
			}
			if (ret % 16)
				printf("\n");
		}

		usleep(interval);
	}

	free(recv_buff);
	
	return NULL;
}

int main(int argc,char **argv)
{
	int fd, ch, ret = 0;
	struct termios oldtio, newtio;

	send_buff = malloc(SEND_BUFF_SIZE);
	if (!send_buff) {
		perror("Failed to allocate send buffer!\n");
		ret = errno;
		goto out;
	}

	recv_buff = malloc(RECV_BUFF_SIZE);
	if (!recv_buff) {
		perror("Failed to allocate receive buffer!\n");
		ret = errno;
		goto out;
	}

	while ((ch = getopt(argc,argv,"b:d:i:h"))!= -1) {
		switch (ch) {
			case 'd':
				dev = optarg;
				break;
			case 'b':
				baud_rate = atoi(optarg);
				break;
			case 'i':
				interval = atoi(optarg);
				break;
			case 'h':
				usage(argv[0]);
				return 0;
			default:
				break;
		}
	}

	if ((fd = open(dev, O_RDWR|O_NOCTTY|O_NDELAY)) < 0) {
		printf("Can't open serial port %s!\n", dev);
		ret = errno;
		goto out;
	}
	printf("Open serial port %s with %d baud rate ok\n", dev, baud_rate);

	tcgetattr(fd, &oldtio);
	set_termios(&newtio, baud_rate);
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &newtio);

	pthread_create(&recv_thd, NULL, recv_thread, (void *)&fd);

	printf("Type something to send, \"quit\" to exit:\n");
	for (;;) {
		scanf("%s", send_buff);
		if (!strcmp(send_buff, "quit")) {
			terminated = 1;
			break;
		} else if (!strcmp(send_buff, "spiwrite")) {
			char *p = send_buff;
			int i;
			memcpy(p, "AT%SPI=", 7);
			p += 7;
			*p++ = 0; //write;
			*(unsigned short *)p = 0;
			p += 2;
			*(unsigned short *)p = 256;
			p += 2;
			for (i = 0; i < 256; i++)
				*p++ = i;
			write(fd, send_buff, p - send_buff);
			continue;
		} else if (!strcmp(send_buff, "spiwrite2")) {
			char *p = send_buff;
			int i;
			memcpy(p, "AT%SPI=", 7);
			p += 7;
			*p++ = 0; //write;
			*(unsigned short *)p = 0;
			p += 2;
			*(unsigned short *)p = 256;
			p += 2;
			for (i = 0; i < 256; i++)
				*p++ = 255 - i;
			write(fd, send_buff, p - send_buff);
			continue;
		} else if (!strcmp(send_buff, "spiread")) {
			char *p = send_buff;
			memcpy(p, "AT%SPI=", 7);
			p += 7;
			*p++ = 1; //read;
			*(unsigned short *)p = 0;
			p += 2;
			*(unsigned short *)p = 256;
			p += 2;
			write(fd, send_buff, p - send_buff);
			continue;
		}

		write(fd, send_buff, strlen(send_buff));
	}

	pthread_join(recv_thd, NULL);

	tcsetattr(fd,TCSANOW,&oldtio);
	close(fd);

out:
	if (send_buff)
		free(send_buff);
	if (recv_buff)
		free(recv_buff);

	return ret;
}
