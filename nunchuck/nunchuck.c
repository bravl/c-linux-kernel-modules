#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input-polldev.h>
#include <linux/platform_device.h>

#define RW_BUFSIZE 10

static int debug = 0;
module_param(debug,int,0);

struct nunchuck_data {
	struct i2c_client *c;
};

void nunchuck_print_registers(char *rw_buf)
{
	pr_info("X-Joystick: %x\n",rw_buf[0]);	
	pr_info("Y-Joystick: %x\n",rw_buf[1]);
	pr_info("X-Acceller: %x\n",rw_buf[2]);	
	pr_info("Y-Acceller: %x\n",rw_buf[3]);
	pr_info("Z-Acceller: %x\n",rw_buf[4]);
	pr_info("X-Acceller REL: %x\n",(rw_buf[5] & 0x0C) >> 2);	
	pr_info("Y-Acceller REL: %x\n",(rw_buf[5] & 0x30) >> 4);	
	pr_info("Y-Acceller REL: %x\n",(rw_buf[5] & 0xC0) >> 6);	
	
	pr_info("Z pressed: %s\n", (rw_buf[5] & 0x01) ? "false" : "true");
	pr_info("C pressed: %s\n", (rw_buf[5] & 0x02) ? "false" : "true");
}

int nunchuck_read_registers(struct i2c_client *client, char *rw_buf)
{
	int ret = 0;
	
	rw_buf[0] = 0x00;

	msleep(10);
	ret = i2c_master_send(client,rw_buf,1);
	if (ret < 0) {
		pr_err("Failed to send i2c %d\n",ret);
		return ret;
	}	

	msleep(10);
	ret = i2c_master_recv(client,rw_buf,6);
	if (ret < 0) {
		pr_err("Failed to read i2c %d\n",ret);
		return ret;
	}
	return ret;
}

void nunchuck_poll(struct input_polled_dev *dev)
{
	char rw_buf[RW_BUFSIZE];
	struct nunchuck_data *data;
	data = (struct nunchuck_data*)dev->private;
	nunchuck_read_registers(data->c,rw_buf);
	if (debug)
		nunchuck_print_registers(rw_buf);

	input_event(dev->input,EV_KEY,BTN_Z, !(rw_buf[5] & 0x01));
	input_event(dev->input,EV_KEY,BTN_C, !(rw_buf[5] & 0x02));	
	input_sync(dev->input);
}

int nunchuck_probe(struct i2c_client *c, const struct i2c_device_id *i)
{
	int retval = 0;
	static char first_init[2] = {0xf0,0x55};
	static char second_init[2] = {0xfb, 0x00};
	char rw_buf[RW_BUFSIZE];
	struct nunchuck_data *data;
	struct input_polled_dev *pdev;
	struct input_dev *idev;
	pdev = devm_input_allocate_polled_device(&c->dev);
	idev = pdev->input;

	idev->name = "Wii nunchuck";
	idev->id.bustype = BUS_I2C;

	set_bit(EV_KEY, idev->evbit);
	set_bit(BTN_C, idev->keybit);
	set_bit(BTN_Z, idev->keybit);

	data = devm_kzalloc(&c->dev, sizeof(struct nunchuck_data), GFP_KERNEL);
	data->c = c;
	pdev->private = data;

	printk("Probing i2c device\n");
	retval = i2c_master_send(c,first_init,2);
	if (retval < 0) {
		printk("Failed to send i2c %d\n",retval);
		return retval;
	}
	msleep(1);
	
	retval = i2c_master_send(c,second_init,2);
	if (retval < 0) {
		printk("Failed: %d\n",retval);
		return retval;
	}

	nunchuck_read_registers(c,rw_buf);
	nunchuck_read_registers(c,rw_buf);
	nunchuck_print_registers(rw_buf);

	pdev->poll = nunchuck_poll;

	retval = input_register_polled_device(pdev);
	if (retval < 0) return -1;
	return 0;
}

int nunchuck_remove(struct i2c_client *c)
{
	pr_info("Removing nunchuck driver\n");
	return 0;
}

static const struct i2c_device_id driver_id[] = {
	{"nunchuck",0},
	{}
};
MODULE_DEVICE_TABLE(i2c,driver_id);

static const struct of_device_id nunchuck_id[] = {
	{ .compatible = "olimex,nunchuck",},
	{}
};
MODULE_DEVICE_TABLE(of,nunchuck_id);

static struct i2c_driver nunchuck_driver = {
	.probe = nunchuck_probe,
	.remove = nunchuck_remove,
	.driver = {
		.name = "nunchuck",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nunchuck_id),
	}
};

module_i2c_driver(nunchuck_driver);
MODULE_LICENSE("GPL");

