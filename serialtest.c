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
#include <sys/time.h>
#include <unistd.h>

#include "crc16.h"

#define SEND_BUFF_SIZE	1024
#define RECV_BUFF_SIZE	1024

static char *dev = "/dev/ttyACM1";
static int baud_rate = 115200;//B115200;
static int interval = 1000;//us
static pthread_t recv_thd;
static int terminated = 0;
static char *send_buff = NULL;
static char *recv_buff = NULL;
static unsigned char dump_on = 0;
static unsigned char show_prompt = 0;

struct timeval last_tv = {-1, 0};
static void print_timestamp(const char *msg)
{
	struct timeval tv;
	long tv_sec;
	long tv_usec;
	struct tm* local;
  
	gettimeofday(&tv, NULL);
	local = localtime(&tv.tv_sec);
	if (last_tv.tv_sec == -1) {
		printf("[%02d:%02d:%02d.%03ld]%s, start the timer\n",
			local->tm_hour,
			local->tm_min,
			local->tm_sec,
			tv.tv_usec / 1000,
			msg);
	} else {
		tv_sec = tv.tv_sec - last_tv.tv_sec;
		tv_usec = tv.tv_usec - last_tv.tv_usec;
		if (tv_usec < 0) {
			tv_sec--;
			tv_usec += 1000000;
		}
		printf("[%02d:%02d:%02d.%03ld]%s, it took %ld.%03lds\n",
			local->tm_hour,
			local->tm_min,
			local->tm_sec,
			tv.tv_usec / 1000,
			msg,
			tv_sec,
			tv_usec / 1000);
	}

	memcpy(&last_tv, &tv, sizeof(last_tv));

	//printf("seconds:%ld\n", tv.tv_sec);  //seconds
    //printf("millisecond:%ld\n",tv.tv_sec*1000 + tv.tv_usec/1000);  //milliseconds
    //printf("microsecond:%ld\n",tv.tv_sec*1000000 + tv.tv_usec);  //microseconds
}

static void usage(char *name)
{
	printf("%s [-d <device_path> | -b <baud_rate> | -i <interval> | -h]\n", name);
	printf("options\n");
	printf("  -d <device_path>\n    customize device path, default is:%s\n", dev);
	printf("  -b <baud_rate>\n    customize baud rate, default is %dHz\n", baud_rate);
	printf("  -i <interval>\n    customize interval of receive thread, default is %dus\n", interval);
	printf("  -p\n    show prompt\n"); 
	printf("  -o\n    dump data\n");
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
			if (dump_on) {
				printf("\nRecieve:%s\n", recv_buff);
				printf("\n***Dump Recieved Data***\n");
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
		}

		usleep(interval);
	}

	free(recv_buff);
	
	return NULL;
}

static void showmenu(void)
{
	printf("***PROXY FUNCTIONS***\n");
	printf("1. disable crc validation\n");
	printf("2. enable crc validation\n");
	printf("3. pull down gpio\n");
	printf("4. pull up gpio\n");
	printf("5. read 16bit address i2c data\n");
	printf("6. write 16bit address i2c data\n");
	printf("7. read 8bit address i2c data\n");
	printf("8. write 8bit address i2c data\n");
	printf("9. spi read\n");
	printf("10. spi write\n");
	printf("11. spi erase\n");
	printf("12. transfer file\n");
	printf("which one do you like:");
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

	while ((ch = getopt(argc,argv,"b:d:i:oph"))!= -1) {
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
			case 'o':
				dump_on = 1;
				break;
			case 'p':
				show_prompt = 1;
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

#if 0
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
#else
	{
		int choice, a, b, c, d, i;
		unsigned char buffer[8192];
		unsigned char input[80];
		unsigned char *p, *q;
		unsigned seq = 1;
		unsigned short t, crc;
		struct stat statbuf;
		int fw, rc;
				
		for (;;) {
			if (!show_prompt) {
				usleep(1000000);
				continue;
			}

			showmenu();
			scanf("%d", &choice);
			switch (choice) {
			case 1:
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0xFE;
				*p++ = 0;
				*p++ = 0;
				*p++ = 0;
				*p++ = 0;
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 2:
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0xFD;
				*p++ = 0;
				*p++ = 0;
				*p++ = 0;
				*p++ = 0;
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 3:
			case 4:
				printf("Input the gpio number:");
				scanf("%d", &a);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x0C;
				*p++ = 0x02;
				*p++ = 0;
				*p++ = a;
				*p++ = choice == 3 ? 0 : 1;
				crc = crc16(p - 2, 2);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Pulling gpio%d %s...\n", a, choice == 3 ? "down" : "up");
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 5:
				printf("Input hexadecimal slave address:");
				scanf("%x", &a);
				printf("Input hexadecimal data address:");
				scanf("%x", &b);
				printf("How many bytes do you want to read?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x04;
				*p++ = 0x04;
				*p++ = 0;
				*p++ = (a << 1) | 1;
				*p++ = b & 0xFF;
				*p++ = b >> 8;
				*p++ = c;
				crc = crc16(p - 4, 4);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Reading %d bytes at %04X@%02X\n", c, b, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 6:
				printf("Input hexadecimal slave address:");
				scanf("%x", &a);
				printf("Input hexadecimal data address:");
				scanf("%x", &b);
				printf("How many bytes do you want to write?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x05;
				*p++ = 0x04 + c;
				*p++ = 0;
				*p++ = a << 1;
				*p++ = b & 0xFF;
				*p++ = b >> 8;
				*p++ = c;
				printf("Generates %d random numbers:\n", c);
				for (i = 0; i < c; i++) {
					if (i % 16 == 0)
						printf("%08X:", i);
					d = rand() % 0xFF;
					printf(" %02X", d);
					*p++ = d;
					if ((i+1) % 16 == 0)
						printf("\n");
				}
				if (i % 16)
					printf("\n");
				crc = crc16(p - 4 - c , 4 + c);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Writing %d bytes at %04X@%02X\n", c, b, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 7:
				printf("Input hexadecimal slave address:");
				scanf("%x", &a);
				printf("Input hexadecimal data address:");
				scanf("%x", &b);
				printf("How many bytes do you want to read?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x06;
				*p++ = 0x03;
				*p++ = 0;
				*p++ = (a << 1) | 1;
				*p++ = b & 0xFF;
				*p++ = c;
				crc = crc16(p - 4, 4);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Reading %d bytes at %02X@%02X\n", c, b, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 8:
				printf("Input hexadecimal slave address:");
				scanf("%x", &a);
				printf("Input hexadecimal data address:");
				scanf("%x", &b);
				printf("How many bytes do you want to write?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x07;
				*p++ = 0x03 + c;
				*p++ = 0;
				*p++ = a << 1;
				*p++ = b & 0xFF;
				*p++ = c;
				printf("Generating %d random numbers:\n", c);
				for (i = 0; i < c; i++) {
					if (i % 16 == 0)
						printf("%08X:", i);
					d = rand() % 0xFF;
					printf(" %02X", d);
					*p++ = d;
					if ((i+1) % 16 == 0)
						printf("\n");
				}
				if (i % 16)
					printf("\n");
				crc = crc16(p - 4 - c , 4 + c);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Writing %d bytes at %02X@%02X\n", c, b, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 9:
				printf("Input SPI 16bit page address:");
				scanf("%x", &a);
				printf("How many bytes do you want to read?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x08;
				*p++ = 0x04;
				*p++ = 0;
				*p++ = a & 0xFF;
				*p++ = (a >> 8) & 0xFF;
				*p++ = c & 0xFF;
				*p++ = (c >> 8) & 0xFF;
				crc = crc16(p - 4, 4);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Reading %d bytes at %02X\n", c, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 10:
				printf("Input SPI 16bit page address:");
				scanf("%x", &a);
				printf("How many bytes do you want to write?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x09;
				*p++ = 0x04;
				*p++ = 0;
				*p++ = a & 0xFF;
				*p++ = (a >> 8) & 0xFF;
				*p++ = c & 0xFF;
				*p++ = (c >> 8) & 0xFF;
				printf("Generating %d random numbers:\n", c);
				for (i = 0; i < c; i++) {
					if (i % 16 == 0)
						printf("%08X:", i);
					d = rand() % 0xFF;
					printf(" %02X", d);
					*p++ = d;
					if ((i+1) % 16 == 0)
						printf("\n");
				}
				if (i % 16)
					printf("\n");
				crc = crc16(p - 4 - c, 4 + c);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Writing %d bytes at %02X\n", c, a);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}				
				break;
			case 11:
				printf("Which sector do you want to erase?");
				scanf("%d", &c);
				p = buffer;
				*(unsigned *)p = seq++;
				p += sizeof(unsigned);
				*p++ = 0x0A;
				*p++ = 0x01;
				*p++ = 0;
				*p++ = c & 0xFF;
				crc = crc16(p - 1, 1);
				*p++ = crc & 0xFF;
				*p++ = crc >> 8;
				printf("Erasing SPI sector %d...\n", c);
				rc = write(fd, buffer, p - buffer);
				if (rc < 0) {
					printf("rc = %d\n", rc);
				}
				break;
			case 12:
				printf("Input the file path:");
				scanf("%s", input);
				rc = stat(input, &statbuf);
				if (rc) {
					perror("Can't get file status\n");
					continue;
				}
				printf("loading file(%s), size %d\n", input, (int)statbuf.st_size);

				fw = open(input, O_RDONLY);
				if (fw < 0) {
					perror("Failed to open file\n");
					continue;
				}

				c = 0;
				print_timestamp("transfer file: begin");
				while (c < statbuf.st_size) {
					p = buffer;
					*(unsigned *)p = seq++;
					p += sizeof(unsigned);
					*p++ = 0xFC;
					q = p;
					p += 2;
					b = read(fw, p, 1000);
					if (b > 0) {
						*q++ = b & 0xFF;
						*q++ = (b >> 8) & 0xFF;
					} else {
						close(fd);
						break;
					}
					crc = crc16(p, b);
					p += b;
					*p++ = crc & 0xFF;
					*p++ = crc >> 8;
				retry:
					rc = write(fd, buffer, p - buffer);
					if (rc < 0) {
						//printf("rc = %d\n", rc);
						usleep(100);
						goto retry;
					}
					printf("Sent %ld bytes...\n", p - buffer);
					c += b;
				}
				close(fw);
				print_timestamp("transfer file: end");
				break;
			default:
				printf("error choice\n");
				break;
			}
		}
	}
#endif

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
