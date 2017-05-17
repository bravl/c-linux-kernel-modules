/*
*   Dev:        Bram Vlerick
*   Date:       01/03/2014
*   Company:    On Semiconductors
*   Version:    V2.0
*   Updates:    V1.0: Initial version of the module
*   		V1.1: Implementing packet into write function
*		V1.2: Implementing SPI properties (LSBF, 16bit)
*		V1.3: Implementing SPI read function
*		V2.0: Redesign of driver structure in order to work with ASIC
*   Reason:     Timings of the /dev/spidev driver were to slow
*   Status:     Back to Alpha
*/

#ifndef SPI_DRIVER_H_
#define SPI_DRIVER_H_

#include <linux/init.h>
#include <linux/module.h>
#include <asm-generic/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/types.h>

//DEBUG DEFINES

#define DEBUG_INIT
#define DEBUG_PROBE
//#define DEBUG_OPEN
#define DEBUG_WRITE
//#define DEBUG_WRITE_MSG
//#define DEBUG_TRANSMIT
//#define DEBUG_PARSE
//#define DEBUG_REVERSE
//#define DEBUG_READ
//#define DEBUG_RELEASE
#define DEBUG_REMOVE
#define DEBUG_EXIT

//SPI MODULE DEFINES

#define MODULE_NAME 	"spidrv"

#define SPI_MAJOR		153
#define N_SPI_MINORS	32
#define BUFSIZE			4096

#define SPI_CPHA		0x01
#define SPI_CPOL		0x02

#define SPI_MODE_0		(0|0)
#define SPI_MODE_1		(0|SPI_CPHA)
#define SPI_MODE_2		(SPI_CPOL|0)
#define SPI_MODE_3		(SPI_CPOL|SPI_CPHA)

#define SPI_CS_HIGH		0x04
#define SPI_LSB_FIRST		0x08
#define SPI_3WIRE		0x10
#define SPI_LOOP		0x20
#define SPI_NO_CS		0x40
#define SPI_READY		0x80



//GLOBAL VARS
//static long backup_speed 	= 20000;		//default speed for AVR
//static int	backup_mode		= SPI_MODE_1;	//default mode for AVR

//static char buffer[BUFSIZE];
static u8 read_buffer[BUFSIZE];
static unsigned long minors[N_SPI_MINORS/ BITS_PER_LONG];
static struct mutex read_lock;
static unsigned int last_len = 0;

//STRUCTS

struct spidrv_data{
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;
	struct mutex		buf_lock;
	unsigned		users;
	u8			*buffer;
};

struct spi_parameters{
	unsigned int dummy_write;
	unsigned int control;
	unsigned int bpw;
	unsigned int freqm;
	unsigned int freq2k;
	unsigned int freqk;
	unsigned int cs;
	unsigned int mode;
	unsigned int lsbf;
	unsigned int csp;
	unsigned int ndb;
	unsigned long speed;
	unsigned int group_by;
};

//STRUCTS
static struct class spidrv_class = {
	.name = "spidrv",
	.owner = THIS_MODULE,
};

//INITS AND PROTOTYPES

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

//INIT AND EXIT FUNCTION
static int omap3_init_spidriver(void);
static void omap3_exit_spidriver(void);

//PROBE AND REMOVE FUNCTION
static int omap3_spi_probe(struct spi_device *spi);
static int omap3_spi_remove(struct spi_device *spi);

//OPENING AND CLOSING THE SPI DRIVER FILE
static int omap3_spi_open(struct inode *inode, struct file *filp);
static int omap3_spi_release(struct inode *inode, struct file *filp);

//SPI WRITE FUNCTION AND FUNCTIONS USED BY WRITE
static ssize_t omap3_spi_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
int omap3_spi_reverse_bits(int word);
int omap3_spi_lsb_first(u8 *buffer,int length,int bits);
static int omap3_spi_parse_msg(struct spi_device *spi, struct spi_parameters *params, const u8 *buf);
static int omap3_spi_write_msg(struct spi_device *spi, struct spi_parameters *params, u8 *buf);
static int omap3_spi_transmit_msg(struct spi_device *spi, struct spi_parameters *params, u8 *buf,u8 *return_buf,int len);

//SPI READ FUNCTION AND FUNCTIONS USED BY READ
static ssize_t omap3_spi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);

//DEBUG FUNCTIONS
//void omap3_print_spi_params(struct spi_parameters *params);
//void omap3_print_spi_struct(struct spi_device *spi);

//STRUCT CONTAINING THE FUNCTIONS THAT ARE LINKED TO FILE OPERATIONS
static struct file_operations spi_fops = {
	.owner  = THIS_MODULE,
	.read   = omap3_spi_read,
	.write  = omap3_spi_write,
	.open   = omap3_spi_open,
	.release = omap3_spi_release,
};

//STRUCT THAT IS USED TO REGISTER THE SPI DRIVER
static struct spi_driver spidrv = {
	.driver = {
		.name = "omap3_spi",
		.owner = THIS_MODULE,
	},
	.probe = omap3_spi_probe,
	.remove = omap3_spi_remove,
};


#endif /* SPI_DRIVER_H_ */
