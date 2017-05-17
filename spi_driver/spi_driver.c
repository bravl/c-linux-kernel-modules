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
*   Status:     Beta
*/

/***
 *
 * omap3_spi_open: Called when the .ko is modprobe'd
 * omap3_spi_probe: Called when a SPI device is connected
 * omap3_spi_remove: Called when the SPI device is removed
 * omap3_spi_release: Called when the .ko file is rmmod'ed
 */



#include "spi_driver.h"

#define SPI_MODE_MASK		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
				| SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP \
				| SPI_NO_CS | SPI_READY)


/* Function that is called when the driver is insmodded */
static int omap3_init_spidriver(void){
	int status = 0;
#ifdef DEBUG_INIT
	printk("#INIT: INITIALISING SPI DRIVER\n");
	printk("#INIT: REGISTERING CHARACTER DEVICE\n");
#endif
	/* Register a character device with a major number called spi with spi_fops as file functions (see header) */
	/* Meaning a device that we can easily write too */
	status = register_chrdev(SPI_MAJOR,"spi",&spi_fops);
	if(status < 0){
		printk("#INIT:FATAL ERROR! REGISTERING CHARACTER DEVICE FAILED\n");
		return status;
	}
#ifdef DEBUG_INIT
	printk("#INIT: REGISTERING CLASS\n");
#endif
	/* Register a class that will be used to create nodes of the driver */
	status = class_register(&spidrv_class);
	if(status < 0){
		printk("#INIT:FATAL ERROR! REGISTERING CLASS FAILED\n");
		unregister_chrdev(SPI_MAJOR,spidrv.driver.name);
		return status;
	}
#ifdef DEBUG_INIT
	printk("#INIT:FATAL REGISTERERING SPI DRIVER\n");
#endif
	/* Register the spi driver. This will call the probe function */
	status = spi_register_driver(&spidrv);
	if(status < 0){
		printk("#INIT:FATAL ERROR! REGISTERING SPI DRIVER FAILED\n");
		class_unregister(&spidrv_class);
		unregister_chrdev(SPI_MAJOR,spidrv.driver.name);
	}
#ifdef DEBUG_INIT
	printk("#INIT: INITIALISING SUCCESSFULL\n");
#endif
	return status;

}

/* Connect function to module init */
module_init(omap3_init_spidriver)

/* This function is called by the register spidriver function */
static int omap3_spi_probe(struct spi_device *spi){
	int status = 0;
	unsigned long minor;
	struct spidrv_data *spidrv;
	struct device *dev;

#ifdef DEBUG_PROBE
	printk("#PROBE: PROBING FOR SPI DEVICES\n");
#endif
	/* check if the device that is found exists */
	if(spi == NULL){
		printk("#PROBE: ERROR! SPI DEVICE CANNOT BE NULL\n");
		return -1;
	}
#ifdef DEBUG_PROBE
	printk("#PROBE: ALLOCATING MEMORY FOR SPI DRIVER\n");
#endif
	/* allocate memory for the spidrv struct (see header) */
	spidrv = kzalloc(sizeof(*spidrv),GFP_KERNEL);
	if(!spidrv){
		printk("#PROBE: FATAL ERROR! COULD NOT ALLOCATE MEMORY FOR DRIVER\n");
	}
	/* set spi device */
	spidrv->spi = spi;
	/* initialise the mutex's */
	mutex_init(&spidrv->buf_lock);
	mutex_init(&read_lock);

	/* init the device list */
	INIT_LIST_HEAD(&spidrv->device_entry);

#ifdef DEBUG_PROBE
	printk("#PROBE: ADDING DEVICE TO DEVICE LIST\n");
	printk("#PROBE: LOCKING DEVICE LIST\n");
#endif
	/* lock the device list */
	mutex_lock(&device_list_lock);
		/* select a minor number for the node */
		minor = find_first_zero_bit(minors, N_SPI_MINORS);
		if(minor < N_SPI_MINORS){
#ifdef DEBUG_PROBE
			printk("#PROBE: CREATING SYSFS ENTRY\n");
#endif
			/* Create a 32bit number that will identify the node */
			spidrv->devt = MKDEV(SPI_MAJOR,minor);
			/* create the spi device */
			dev = device_create(&spidrv_class,&spi->dev,
					    spidrv->devt,spidrv,"spidrv%d.%d",
					    spi->master->bus_num,
					    spi->chip_select);
			status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
		}
		else{
			printk("#PROBE: FATAL ERROR! NO SPI MINORS AVAILABLE\n");
			return -1;
		}
		if(status == 0){
			/* set minor bit as used */
			set_bit(minor,minors);
			/* add the device to the list */
			list_add(&spidrv->device_entry,&device_list);
		}
		/* unlock the device list */
		mutex_unlock(&device_list_lock);
#ifdef DEBUG_PROBE
	printk("#PROBE: DEVICE LIST UNLOCKED\n");
	printk("#PROBE: SETTING SPI DRIVER DATA\n");
#endif
	if(status == 0){
		/* set driver data into a struct */
		spi_set_drvdata(spi,spidrv);
	}
	else
		/* if something failed, free the struct */
		kfree(spidrv);
#ifdef DEBUG_PROBE
	printk("#PROBE: PROBE DEVICE SPIDRV%d.%d SUCCESFULL\n",spi->master->bus_num,spi->chip_select);
#endif
	return status;
}

/* function that is called when the device driver file is opened */
static int omap3_spi_open(struct inode *inode, struct file *filp){
	struct spidrv_data *spidrv;
	int status = -1;
#ifdef DEBUG_OPEN
	printk("#OPEN: OPENING DEVICE DRIVER FILE\n");
#endif
#ifdef DEBUG_OPEN
	printk("#OPEN: LOCKING DEVICE LIST\n");
#endif
	/* lock the device list and find the correct device */
	mutex_lock(&device_list_lock);
		list_for_each_entry(spidrv,&device_list,device_entry){
			if(spidrv->devt == inode->i_rdev){ /* inode holds the id of the owner of the device file */
				status = 0;
				break;
			}
		}
		if(status == 0){	/* device found */
#ifdef DEBUG_OPEN
			printk("#OPEN: SPI DEVICE FOUND IN DEVICE LIST\n");
#endif
			if(!spidrv->buffer){
#ifdef DEBUG_OPEN
				printk("#OPEN: SPI DEVICE NO BUFFER YET. CREATING BUFFER\n");
#endif				/* Allocate buffer space for the spi buffer */
				spidrv->buffer = kmalloc(BUFSIZE,GFP_KERNEL);
				if(!spidrv->buffer){
					printk("#OPEN: ERROR! FAILED TO ALLOCATE SPI BUFFER\n");
					status = -ENOMEM;
				}
			}
			/* if all goes well, add a user, open file, store driver data into file->private_buffer */
			if(status == 0){
				spidrv->users++;
				filp->private_data = spidrv;
				nonseekable_open(inode,filp);
			}
		}
		else{
			printk("#OPEN: ERROR! NO MATCHING SPI DEVICE FOUND\n");
			status = -1;
		}
	/* unlock the device list */
	mutex_unlock(&device_list_lock);
	return status;
}

/* this function is called when we write to the device file */
static ssize_t omap3_spi_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	struct spidrv_data *spidrv;
	struct spi_device *spi;
	struct spi_parameters params;
	int status = 0;
	int not_copied = 0;
#ifdef DEBUG_WRITE
	printk("#WRITE: WRITING DATA TO DRIVER FILE\n");
#endif
	if(BUFSIZE < count){
		printk("#WRITE: FATAL ERROR! WRITE BUFFER WILL NOT FIT IN SPI BUFFER\n");
		status = -EMSGSIZE;
	}
	if(status == 0){
#ifdef DEBUG_WRITE
		printk("#WRITE: SETTING SPI DRIVER DATA\n");
#endif
		spidrv = filp->private_data;
		spi = spidrv->spi;
#ifdef DEBUG_WRITE
		printk("#WRITE: LOCKING BUFFER MUTEX\n");
#endif		/* locking mutex */
		mutex_lock(&spidrv->buf_lock);
#ifdef DEBUG_WRITE
		printk("#WRITE: RETRIEVING BUFFER FROM USERSPACE\n");
#endif
		memset(spidrv->buffer,0,BUFSIZE);
			/* copy data from userspace to kernelspace */
			not_copied = copy_from_user(spidrv->buffer,buf,count);
			if(not_copied == 0){
				/* parse the buffer into spi parameter struct */
				status = omap3_spi_parse_msg(spi,&params,spidrv->buffer);
				if(status < 0){
					mutex_unlock(&spidrv->buf_lock); /* if there is an error don't forget to unlock mutex */
					return -1;
				}
				/* write the spi message */
				omap3_spi_write_msg(spi,&params,spidrv->buffer);
			}
			else{
				/* error, don't forget to unlock mutex */
				printk("#WRITE: COULD NOT RETREIVE FULL BUFFER FROM USERSPACE\n");
				mutex_unlock(&spidrv->buf_lock);
				status = -1;
			}
#ifdef DEBUG_WRITE
		printk("#WRITE: UNLOCKING BUFFER MUTEX\n");
#endif
		/* unlock mutex */
		mutex_unlock(&spidrv->buf_lock);
	}
	return count;
}

/* parsing message into spi parameters struct */
static int omap3_spi_parse_msg(struct spi_device *spi,struct spi_parameters *params ,const u8 *buf){
	int status = 0;
#ifdef DEBUG_PARSE
	printk("#PARSE: PARSING BUFFER INTO SPI PARAMS\n");
#endif
	params->control = 	(int)buf[1];
	params->bpw 	=	(int)buf[2];
	params->freqm	= 	(int)buf[3];
	params->freq2k 	= 	(int)buf[4];
	params->freqk 	=	(int)buf[5];
	params->ndb		=	(int)buf[7];
	params->cs 		=	((params->control & 0x30) >> 4);
	params->mode 	= 	((params->control & 0x0C) >> 2);
	params->lsbf 	=	((params->control & 0x02) >> 1);
	params->csp 	= 	((params->control & 0x01) >> 0);
	params->speed 	= 	((params->freqm * 1000000) + (params->freq2k * 100000) + (params->freqk * 1000));
	params->group_by	=	(int)buf[8];
#ifdef DEBUG_PARSE
	printk("#PARSE: DEBUG: BUF CHIPSELECT: %d\n",params->cs);
	printk("#PARSE: DEBUG: SPI CHIPSELECT: %d\n",spi->chip_select);
#endif
	if(params->cs != spi->chip_select){
		printk("#PARSE: ERROR! BUFFER CHIPSELECT DOES NOT MATCH DRIVER FILE CHIPSELECT\n");
		return -1;
	}
	else
		return status;
}

/* writing the message */
static int omap3_spi_write_msg(struct spi_device *spi,struct spi_parameters *params, u8 *buf){
	ssize_t status = 0;
	u8 *spi_buffer;
	u8 *temp_buf;
	int i;
	/* set chipselect polarity */
	if(params->csp){
		params->mode |= SPI_CS_HIGH;
	}
	/* set mode */
	spi->mode = params->mode;
#ifdef DEBUG_WRITE_MSG
	printk("#WRITE MSG: WRITING SPI MESSAGE\n");
#endif
	/* check chipselect */
	if(params->cs == spi->chip_select)
	{
#ifdef DEBUG_WRITE_MSG
		printk("#WRITE MSG: CHIPSELECT MATCHES\n");
#endif
		spi_buffer = &buf[9]; /* copy pointer to buffer */
		if(params->lsbf){
			/* switch from msb to lsb */
			omap3_spi_lsb_first(spi_buffer,params->ndb,params->bpw);
		}
		/*
		for(i = 0; i < 100; i++){
			printk("%x - ",spi_buffer[i]);
		}
		printk("\n");
		*/

		last_len = params->ndb;
		/* MORE DYNAMIC BUFFER ALLOCATING */
		temp_buf = kzalloc(params->group_by,GFP_KERNEL);
		/* SPLIT PACKAGES AND SEND THEM */
#ifdef DEBUG_WRITE_MSG
		printk("#WRITE MSG: PARAMS->GROUP BY = %d, PARAMS->NDB = %d\n",params->group_by,params->ndb);
#endif
		for(i = 0; i < params->ndb; i+=params->group_by){
			memcpy(temp_buf,&spi_buffer[i],params->group_by); 				//COPY INTO SMALLER TEMP BUFFERS
			omap3_spi_transmit_msg(spi,params,temp_buf,&read_buffer[i],params->group_by); 	//SEND THE SPI MESSAGE
		}
		kfree(temp_buf); /* free temp buffer */

	}
	else{
		status = -1;
		printk("#WRITE MSG: ERROR! UNRECOGNIZED CHIPSELECT\n");
	}
	return status;
}

/* reverse bits */
int omap3_spi_reverse_bits(int word)
{
        int temp = 0;
        int new_word = 0;
        int i;
        for(i = 0; i < 8;i++){
                temp = word;
                new_word |= ((temp & (1 << i)) >> i) * (1<<(8-i-1)); /* switch bits round */
        }
        return new_word;
}

/* set lsb first */
int omap3_spi_lsb_first(u8 *buffer,int length,int bits){
        int bytes = 0;
        int i;
        if(bits != 8 && bits != 16){	/* var bits wrong */
                printk("Error bits not right\n");
                return -1;
        }
        bytes = bits /8;
        if(bits == 16){	/* if 16 bits rotate bytes first */
                char temp;
                for(i = 0; i < length; i+=bytes){
                        temp = buffer[i];
                        buffer[i] = buffer[i+1];
                        buffer[i+1] = temp;
                }
        }
#ifdef DEBUG_REVERSE
        for(i = 0; i < length; i++){
                printk("%x - ",buffer[i]);
        }
        printk("\n");
#endif
	/* rotate bits */
        for(i = 0; i < length; i++){
                buffer[i] = omap3_spi_reverse_bits(buffer[i]);
        }
#ifdef DEBUG_REVERSE
        for(i = 0; i < length; i++){
                printk("%x - ",buffer[i]);
        }
        printk("\n");
#endif
	return 0;
}

/* send message over spi */
static int omap3_spi_transmit_msg(struct spi_device *spi, struct spi_parameters *params,u8 *buf,u8 *return_buffer,int len){
	struct spi_transfer xfer;
	struct spi_message sm;
	int ret;

#ifdef DEBUG_TRANSMIT
	printk("#TRANSMIT: TRANSMIT DATA STARTED\n");
#endif
	memset(return_buffer,0,len);
	memset(&xfer,0,sizeof(struct spi_transfer));
	/* set params */
	xfer.speed_hz = params->speed;
	xfer.bits_per_word = params->bpw;
	xfer.delay_usecs = 0;
	spi_message_init(&sm);
	xfer.tx_buf = buf;
	if(params->ndb < params->group_by)
		xfer.len = params->ndb;
	else
		xfer.len = len;
	xfer.rx_buf = return_buffer;
	//printk("%x - ", buf[0]);

#ifdef DEBUG_TRANSMIT
	printk("#TRANSMIT: LOCKING MUTEX READ BUFFER\n");
#endif
	mutex_lock(&read_lock);	/*lock buffer */
		spi_message_add_tail(&xfer,&sm);/* add message to queue */
		ret = spi_sync(spi,&sm);	/* sync spi message (= send/read) */
		if(ret < 0){
			printk("#TRANSMIT: COULD NOT SEND SPI MESSAGE\n");
		}

#ifdef DEBUG_TRANSMIT
		int i;
		printk("#TRANSMIT: READ BUFFER:\n");
		for(i = 0; i < len; i++){
			printk("%x - ",return_buffer[i]);
		}
		printk("\n");
#endif
	mutex_unlock(&read_lock); /*unlock mutex */
#ifdef DEBUG_TRANSMIT
	printk("#TRANSMIT: UNLOCKING MUTEX READ BUFFER\n");
#endif
	return ret;
}

/* read function */
static ssize_t omap3_spi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
	int not_copied = 0;
#ifdef DEBUG_READ
	printk("#READ: READING BUFFER\n");
	printk("#READ: READING FOR %d\n",last_len);
	printk("#READ: LOCKING BUFFER MUTEX\n");
	int i;
	printk("#READ: READ BUFFER:\n");
	for(i = 0; i < last_len; i++){
		printk("%x - ",read_buffer[i]);
	}
	printk("\n");
#endif
	/* mutex lock */
	mutex_lock(&read_lock);
		not_copied = copy_to_user(buf,read_buffer,last_len);	/* send to user */
		if(not_copied != 0){
			printk("#READ: ERROR: COULD NOT SEND BUFFER TO USERSPACE\n");
			mutex_unlock(&read_lock);
			return -1;
		}
	mutex_unlock(&read_lock);	/* unlock mutex */
#ifdef DEBUG_READ
	printk("#READ: UNLOCKING BUFFER MUTEX\n");
#endif
	return count;
}

/* function called when file is closed */
static int omap3_spi_release(struct inode *inode, struct file *filp){
	struct spidrv_data *spidrv;
	int dofree = 0;
	int status = 0;
#ifdef DEBUG_RELEASE
	printk("#RELEASE: CLOSING SPI DRIVER FILE\n");
	printk("#RELEASE: LOCKING DEVICE LIST\n");
#endif
	mutex_lock(&device_list_lock); /* lock the device list */
#ifdef DEBUG_RELEASE
	printk("#RELEASE: CLEARING DRIVER FILE DATA\n");
#endif
		spidrv = filp->private_data; 	/* get private data */
		filp->private_data = NULL;   	/* clear the pointer */
		spidrv->users--;		/* remove a user */
		if(!spidrv->users){		/* no users left */
#ifdef DEBUG_RELEASE
			printk("#RELEASE: NO MORE USERS, CLEARING EVERYTHING\n");
#endif
			kfree(spidrv->buffer);	/* free buffer */
			spidrv->buffer = NULL;	/* remove pointer */
			spin_lock_irq(&spidrv->spi_lock);	/* lock spilock */
				dofree = (spidrv->spi == NULL);	/* check if there is a device */
			spin_unlock_irq(&spidrv->spi_lock);	/* unlock spilock */
			if(dofree)
				kfree(spidrv);	/*free spidrv struct if still there */
		}
	mutex_unlock(&device_list_lock);	/* unlock device list */
#ifdef DEBUG_RELEASE
	printk("#RELEASE: UNLOCKING DEVICE LIST\n");
	printk("#RELEASE: CLOSING SPI DRIVER FILE SUCCESFULL\n");
#endif
	return status;
}

/* function called when remove driver */
static int omap3_spi_remove(struct spi_device *spi){
	struct spidrv_data *spidrv = spi_get_drvdata(spi);
#ifdef DEBUG_REMOVE
	printk("#REMOVE: REMOVING SPI DEVICE\n");
	printk("#REMOVE: GETTING SPI DATA\n");
#endif
	if(spidrv == NULL){
		printk("#REMOVE: FATAL ERROR! SPI DRIVER DATA CANNOT BE NULL\n");
		return -1;
	}
#ifdef DEBUG_REMOVE
	printk("#REMOVE: CLEARING SPI DRIVER POINTERS\n");
	printk("#REMOVE: SPINLOCK SET\n");
#endif
	spin_lock_irq(&spidrv->spi_lock);	/* lock spi lock */
		spidrv->spi = NULL;	/* clear pointer */
		spi_set_drvdata(spi,NULL);	/* set driver data to NULL */
	spin_unlock_irq(&spidrv->spi_lock);	/* unlock spi */
#ifdef DEBUG_REMOVE
	printk("#REMOVE: SPINLOCK CLEARED\n");
	printk("#REMOVE: LOCKING DEVICE LIST\n");
#endif
	mutex_lock(&device_list_lock);	/* lock device list */
		list_del(&spidrv->device_entry);	/* remove device entry */
		device_destroy(&spidrv_class, spidrv->devt);	/* remove class */
		clear_bit(MINOR(spidrv->devt),minors);	/* release a minor number */
		if(spidrv->users == 0)	/* no users == clear driverdata */
			kfree(spidrv);
	mutex_unlock(&device_list_lock); /* unlock device list */
#ifdef DEBUG_REMOVE
	printk("#REMOVE: DEVICE LIST UNLOCKED\n");
	printk("#REMOVE: REMOVE SUCCESFULL");
#endif
	return 0;
}

/* function called when removing driver */
static void omap3_exit_spidriver(){
#ifdef DEBUG_EXIT
	printk("#EXIT: REMOVING SPI DRIVER\n");
	printk("#EXIT: UNREGISTERING SPI DRIVER\n");
#endif
	spi_unregister_driver(&spidrv);	/* unregister driver, will call spi remove */
#ifdef DEBUG_EXIT
	printk("#EXIT: DESTROYING SPI DRIVER CLASS\n");
#endif
	class_destroy(&spidrv_class);	/* remove the class */
#ifdef DEBUG_EXIT
	printk("#EXIT: UNREGISTERING CHARACTER DEVICE\n");
#endif
	unregister_chrdev(SPI_MAJOR,spidrv.driver.name); /* unregister the character device */
#ifdef DEBUG_EXIT
		printk("#EXIT: EXIT SPI DRIVER SUCCESFULL\n");
#endif
}
module_exit(omap3_exit_spidriver); /* link module exit to the exit function */

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_VERSION("1.2");
MODULE_AUTHOR("Bram Vlerick");
