/*
	MAX7219 7-Segment LED Dsplay Driver for Raspberry Pi (Zero)
	  by Mark Street December 2017
	    Stanley, Falkland Islands.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/spi/spidev.h>

// The SPI bus parameters
static const char       *spiDev0  = "/dev/spidev0.0";
static const char       *spiDev1  = "/dev/spidev0.1";
static const uint8_t     spiBPW   = 8;
static const uint16_t    spiDelay = 0;

static uint32_t		spiSpeeds[2];
static int			spiFds[2];

static const int 	CHANNEL = 0;
static const int	GPIO_BUTTON1 = 17;
static const int	GPIO_BUTTON2 = 26;

static int 			intensity = 1;

// MAX 7219 Register Address Map
const unsigned char MSG_DECODE_MODE		= 0x09;
const unsigned char MSG_INTENSITY 		= 0x0A;
const unsigned char MSG_SCAN_LIMIT 		= 0x0B;
const unsigned char MSG_SHUTDOWN 		= 0x0C;
const unsigned char MSG_DISPLAY_TEST 	= 0x0F;

// I/O access
volatile unsigned *allof7e;

#define ACCESS(base) *(volatile int*)((int)allof7e+base-0x7e000000)
#define SETBIT(base, bit) ACCESS(base) |= 1<<bit
#define CLRBIT(base, bit) ACCESS(base) &= ~(1<<bit)

#define GPFSEL0 	(0x7E200000)
#define GPFSEL1 	(0x7E200004)
#define GPFSEL2 	(0x7E200008)
#define GPLEV0 		(0x7E200034)
#define GPLEV1 		(0x7E200038)
#define GPEDS0		(0x7E200040)
#define GPEDS1		(0x7E200044)
#define GPREN0		(0x7E20004C)
#define GPREN1		(0x7E200050)

void gpio_init(void)
{
	/* Open /dev/mem */
	int mem_fd;
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0)
	{
        printf("Failed to open /dev/mem\n");
        exit(-1);
    }
    
    allof7e = (unsigned *)mmap(
                  NULL,
                  0x01000000,  //len
                  PROT_READ|PROT_WRITE,
                  MAP_SHARED,
                  mem_fd,
                  0x20000000  //base
              );

    if ((int)allof7e == -1)
		exit(-1);

	close(mem_fd);

	// Buttons are connected to GPIO17 and GPIO26, make these inputs
	for (int i=21; i<=23; i++)
    	CLRBIT(GPFSEL1, i);
	for (int i=18; i<=20; i++)
    	CLRBIT(GPFSEL2, i);

	// Set GPIO Falling Edge detect enable register 
	// This will conflict with IRQ #73 20200000.gpio:bank0
	/*
	SETBIT(GPREN0, GPIO_BUTTON1);
	SETBIT(GPREN0, GPIO_BUTTON2);
	*/
}

int gpio_scan(void)
{
	// We can't use GPIO event detection without running into complicated
	// IRQ issues so we'll need to poll the pin level registers instead.
	int ret = 0;
	int reg = ACCESS(GPLEV0);

	static int debounce = 0;

	if (!(reg & (1 << GPIO_BUTTON1)))
		ret |= 1;
	if (!(reg & (1 << GPIO_BUTTON2)))
		ret |= 2;

	if (debounce)
	{
		debounce--;
		return 0;
	}

	if (ret)
		debounce = 1;

	return ret;
}

int SPISetupMode(int channel, int speed, int mode)
{
  int fd;

  mode    &= 3;		// Mode is 0, 1, 2 or 3
  channel &= 1;		// Channel is 0 or 1

  if ((fd = open (channel == 0 ? spiDev0 : spiDev1, O_RDWR)) < 0)
  {
	perror("Unable to open SPI device: ");
	return -1;
  }

  spiSpeeds [channel] = speed;
  spiFds    [channel] = fd;

  // Set SPI parameters.
  if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)
  {
	  perror("SPI Mode Change failure: ");
	  return -1;
  }
  
  if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &spiBPW) < 0)
  {
	  perror("SPI BPW Change failure: ");
	  return -1;
  }

  if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
  {
	  perror("SPI Speed Change failure: ");
	  return -1;
  }

  return fd;
}

int SPIDataRW (int channel, unsigned char *data, int len)
{
  struct spi_ioc_transfer spi;
  memset(&spi, 0, sizeof(spi));

  channel &= 1;

  spi.tx_buf        = (unsigned long)data;
  spi.rx_buf        = (unsigned long)data;
  spi.len           = len;
  spi.speed_hz      = spiSpeeds[channel];
  spi.delay_usecs   = spiDelay;
  spi.bits_per_word = spiBPW;

  return ioctl(spiFds [channel], SPI_IOC_MESSAGE(1), &spi);
}

void MaxSetRegister(unsigned char address, unsigned char data)
{
	// MAX7219 uses 16 bit word with MSB = Address and LSB = Data
	unsigned char buffer[2];

	buffer[0] = address;
	buffer[1] = data;
	SPIDataRW(CHANNEL, buffer, 2);
}

void signal_handler(int signo)
{
	fprintf(stderr, "%s() Received signal %d\n", __func__, signo);

	if (signo == SIGINT)
	{
		printf("SIGINT received, shutting down display\n");
		MaxSetRegister(MSG_DISPLAY_TEST, 0x00);	// Normal Operation
		MaxSetRegister(MSG_SHUTDOWN, 0x00);		// Shutdown Mode
		exit(0);
	}
}

void intensity_offset(int level)
{
	if (level > 0 && intensity < 15)
		MaxSetRegister(MSG_INTENSITY, ++intensity);
	if (level < 0 && intensity > 0)
		MaxSetRegister(MSG_INTENSITY, --intensity);
}

void mode_clock(int mode)
{
	// High resolution clock display
	struct tm *tm_now;
	struct timeval tv;

	unsigned char display[9];
	unsigned char backing[9];
	memset(display, 0x00, sizeof(display));
	memset(backing, 0x00, sizeof(backing));

	// Set Decode Mode
	if (mode == 0)
	{
		MaxSetRegister(MSG_DECODE_MODE, 0xFF);
	}
	else
	{
		MaxSetRegister(MSG_DECODE_MODE, 0x7E);
		MaxSetRegister(8, 0x00);	
		MaxSetRegister(1, 0x00);
	}

	while(1)
	{
		gettimeofday(&tv, NULL);
		tm_now = localtime(&tv.tv_sec);

		if (mode == 0)
		{
			// Hour
			display[8] = tm_now->tm_hour / 10;
			display[7] = (tm_now->tm_hour % 10) | 0x80;

			// Minute
			display[6] = tm_now->tm_min / 10;
			display[5] = (tm_now->tm_min % 10) | 0x80;

			// Second
			display[4] = tm_now->tm_sec / 10;
			display[3] = (tm_now->tm_sec % 10) | 0x80;

			// Milliseconds
			display[2] = (tv.tv_usec / 10000) / 10;
			display[1] = (tv.tv_usec / 10000) % 10;
		}
		else
		{
			// Hour
			display[7] = tm_now->tm_hour / 10;
			display[6] = (tm_now->tm_hour % 10) | 0x80;

			// Minute
			display[5] = tm_now->tm_min / 10;
			display[4] = (tm_now->tm_min % 10) | 0x80;

			// Second
			display[3] = tm_now->tm_sec / 10;
			display[2] = (tm_now->tm_sec % 10) | 0x00;
		}

		// Write changed registers to display driver:
		for (int i=1; i<=8; i++)
		{
			if (backing[i] != display[i])
			{
				MaxSetRegister(i, display[i]);	// Set digit val (BCD)
				backing[i] = display[i];
			}
		}

		// Handle button events:
		if ((tv.tv_usec / 10000) % 10 == 0)
		{
			int scan = gpio_scan();
			if (scan == 1)			// Left
				intensity_offset(-1);
			if (scan == 2)			// Right
				intensity_offset(1);
		}

		usleep(10000);
	}
}

void mode_date(int mode)
{
	// Date display
	struct tm *tm_now;
	struct timeval tv;

	unsigned char display[9];
	unsigned char backing[9];
	memset(display, 0xff, sizeof(backing));
	memset(backing, 0xff, sizeof(backing));

	while(1)
	{
		gettimeofday(&tv, NULL);
		tm_now = localtime(&tv.tv_sec);

		int year = tm_now->tm_year + 1900;

		if (mode == 0)
		{
			MaxSetRegister(MSG_DECODE_MODE, 0xFF);

			display[8] = year / ((int) pow(10, 3)) % 10;	// EU
			display[7] = year / ((int) pow(10, 2)) % 10;
			display[6] = year / ((int) pow(10, 1)) % 10;
			display[5] = (year / ((int) pow(10, 0)) % 10) | 0x80;

			display[4] = tm_now->tm_mon+1 / 10;
			display[3] = (tm_now->tm_mon+1 % 10) | 0x80;

			display[2] = tm_now->tm_mday / 10;
			display[1] = tm_now->tm_mday % 10;
		}
		else
		{
			MaxSetRegister(MSG_DECODE_MODE, 0xDB);

			display[8] = tm_now->tm_mday / 10;		// UK
			display[7] = tm_now->tm_mday % 10;
			display[6] = 0x01;
			display[5] = tm_now->tm_mon+1 / 10;
			display[4] = (tm_now->tm_mon+1 % 10) | 0x00;
			display[3] = 0x01;
			display[2] = year / ((int) pow(10, 1)) % 10;
			display[1] = (year / ((int) pow(10, 0)) % 10) | 0x00;
		}

		// Write changed registers to display driver:
		for (int i=1; i<=8; i++)
		{
			if (backing[i] != display[i])
			{
				MaxSetRegister(i, display[i]);	// Set digit val (BCD)
				backing[i] = display[i];
			}
		}

		// Handle button events:
		int scan = gpio_scan();
		if (scan == 1)			// Left
			intensity_offset(-1);
		if (scan == 2)			// Right
			intensity_offset(1);

		usleep(100000);
	}
}

int main(int argc, char **argv)
{
	int verbose 	= 0;
	int opt_clock	= 0;
	int opt_date	= -1;

	int args;
	while ((args = getopt(argc, argv, "vi:c:d:")) != EOF)
	{
		switch(args)
		{
			case 'i':
				intensity = atoi(optarg);
				if (intensity < 0 || intensity > 15)
				{
					fprintf(stderr, "Invalid intensity (0-15)\n");
					return 1;
				}
				break;
			case 'c':
				opt_clock = atoi(optarg);
				if (opt_clock < 0 || opt_clock > 1)
				{
					fprintf(stderr, "Invalid clock option (0-1)\n");
					return 1;
				}
				break;
			case 'd':
				opt_date = atoi(optarg);
				if (opt_date < 0 || opt_date > 1)
				{
					fprintf(stderr, "Invalid date option (0-1)\n");
					return 1;
				}
				break;
			case 'v':
				verbose = 1;
				break;
			default:
				fprintf(stderr, "Usage: 7segclk [options]\n" \
					"\t-i <0-15> Display intensity\n" \
					"\t-c <0|1> Select clock display option\n" \
					"\t-d <0|1> Select date display option\n" \
					"\t-v Verbose\n"
				);
				return 1;
		}
	}

	// Initialise GPIO for push buttons, do this early as needs root
	gpio_init();

	// Configure the SPI interface.
	if (SPISetupMode(CHANNEL, 10000000, 0) < 0)
		return 1;

	if (verbose)
	   printf("Initialised SPI ok\n");

	if (signal(SIGINT, signal_handler) == SIG_ERR)
		printf("Error! Unable to catch SIGINT\n");

	// Display Test
	MaxSetRegister(MSG_DISPLAY_TEST, 0x01);	// Display Test Mode
	sleep(3);

	// Clear digit registers so display always starts up blank
	MaxSetRegister(MSG_DECODE_MODE, 0x00);	// BCD off for all digits
	for (int i=0; i<8; i++)
		MaxSetRegister(i+1, 0x00);			// No segments lit

	// Normal Display
	MaxSetRegister(MSG_DISPLAY_TEST, 0x00);
	sleep(1);

	// Set Scan Limit to full 8 digits
	MaxSetRegister(MSG_SCAN_LIMIT, 0x07);

	// Set Intensity
	MaxSetRegister(MSG_INTENSITY, intensity);

	// Switch to Normal Operation
	MaxSetRegister(MSG_SHUTDOWN, 0x01);

	// Display is initialised and ready to go, yay!
	if (verbose)
		printf("Init complete, ready to display\n");

	// Call appropriate clock routine
	if (opt_date >= 0)
		mode_date(opt_date);
	else if (opt_clock >= 0)
		mode_clock(opt_clock);

	// Display routine has returned, shutdown nicely
	MaxSetRegister(MSG_SHUTDOWN, 0x00);		// Shutdown Mode
	return 0;
}

