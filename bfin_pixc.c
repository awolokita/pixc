#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/atomic.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
//#include "bfin_pixc.h"

#define DRIVER_NAME "bfin-pixc"
/*
 * Default values for testing this driver
 * Use SVGA input/output image: 800x600
 * Use 16x16 pixel image for overlay
 * Overlay top lefthand corner at (100,100)
 */
/* PIXC Disabled, Overlay A enabled, all formats RGB */
#define def_PIXC_CTL 		0x3A 
/* Use SVGA for now */
#define def_PIXC_PPL 		800
#define def_PIXC_LPF 		600
/* (A) Overlay coordinates*/
#define def_PIXC_AHSTART 	100
#define def_PIXC_AHEND		199
#define def_PIXC_AVSTART	100
#define def_PIXC_AVEND		199
/* No transparency */
#define def_PIXC_ATRANSP	15
#define def_PIXC_INTRSTAT	0

#define IMAGE_DMA_SIZE		def_PIXC_PPL * def_PIXC_LPF * 3
#define OVERLAY_DMA_SIZE	(def_PIXC_AHEND - def_PIXC_AHSTART + 1) * \
				(def_PIXC_AVEND - def_PIXC_AVSTART +1) * 3
#define OUTPUT_DMA_SIZE		IMAGE_DMA_SIZE

#define pixc_major		121
#define pixc_minor		1


#define PIXC_EN_CLEAR		0xFFFE

/*
struct bfin_pixc_regs {
	unsigned int bfin_pixc_ctl;
	unsigned int bfin_pixc_ppl;
	unsigned int bfin_pixc_lpf;
	unsigned int bfin_pixc_ahstart;
	unsigned int bfin_pixc_ahend;
	unsigned int bfin_pixc_avstart;
	unsigned int bfin_pixc_avend;
	unsigned int bfin_pixc_atransp;
	unsigned int bfin_pixc_bhstart;
	unsigned int bfin_pixc_bhend;
	unsigned int bfin_pixc_bvstart;
	unsigned int bfin_pixc_bvend;
	unsigned int bfin_pixc_btransp;
	unsigned int bfin_pixc_intrstat;
	unsigned int bfin_pixc_rycon;
	unsigned int bfin_pixc_gucon;
	unsigned int bfin_pixc_bvcon;
	unsigned int bfin_pixc_ccbias;
	unsigned int bfin_pixc_tc;	
};
*/

struct bfin_pixc_device {
        /* DMA buffers */
        char *image_dma_buf;
        char *overlay_dma_buf;
        char *output_dma_buf;
        /* Spin locks */
        spinlock_t image_lock;
        /* DMA IRQ */
        unsigned int image_irq;
        unsigned int overlay_irq;
        unsigned int output_irq;
        /* DMA Channels */
        unsigned int image_dma_channel;
        unsigned int overlay_dma_channel;
        unsigned int output_dma_channel;

	struct cdev cdev;

	unsigned int image_buf_count;
	unsigned int overlay_buf_count;

	bool output_ready;

	atomic_t pixc_available;
	
	wait_queue_head_t inq;
};

struct bfin_pixc_device *pixc = NULL;

static void bfin_pixc_initiate(struct bfin_pixc_device *dev)
{
	printk(KERN_INFO "bfin-pixc: PIXC overlaying initiated\n");
	/* Implement LOTS of error checking in here */

	/* Image DMA channel */
	set_dma_config(dev->image_dma_channel,
		set_bfin_dma_config(DIR_READ, DMA_FLOW_STOP,
			INTR_ON_BUF,
			DIMENSION_2D,
			DATA_SIZE_32,
			DMA_SYNC_RESTART));
	set_dma_start_addr(dev->image_dma_channel, dev->image_dma_buf);
	set_dma_x_count(dev->image_dma_channel, def_PIXC_PPL*3/4);
	set_dma_x_modify(dev->image_dma_channel,4);
	set_dma_y_count(dev->image_dma_channel, def_PIXC_LPF);
	set_dma_y_modify(dev->image_dma_channel, 4);
	SSYNC();
	enable_dma(dev->image_dma_channel);

	/* Overlay DMA channel */
	set_dma_config(dev->overlay_dma_channel,
		set_bfin_dma_config(DIR_READ, DMA_FLOW_STOP,
			INTR_ON_BUF,
			DIMENSION_2D,
			DATA_SIZE_32,
			DMA_SYNC_RESTART));
	set_dma_start_addr(dev->overlay_dma_channel, dev->overlay_dma_buf);
	set_dma_x_count(dev->overlay_dma_channel, 100*3/4);
	set_dma_x_modify(dev->overlay_dma_channel,4);
	set_dma_y_count(dev->overlay_dma_channel, 100);
	set_dma_y_modify(dev->overlay_dma_channel, 4);

	SSYNC();
	enable_dma(dev->overlay_dma_channel);

	/* Image DMA channel */
	set_dma_config(dev->output_dma_channel,
		set_bfin_dma_config(DIR_WRITE, DMA_FLOW_STOP,
			INTR_ON_BUF,
			DIMENSION_2D,
			DATA_SIZE_32,
			DMA_SYNC_RESTART));
	set_dma_start_addr(dev->output_dma_channel, dev->output_dma_buf);
	set_dma_x_count(dev->output_dma_channel, def_PIXC_PPL*3/4);
	set_dma_x_modify(dev->output_dma_channel,4);
	set_dma_y_count(dev->output_dma_channel, def_PIXC_LPF);
	set_dma_y_modify(dev->output_dma_channel, 4);
	SSYNC();
	enable_dma(dev->output_dma_channel);

	bfin_write_PIXC_CTL(bfin_read_PIXC_CTL()|1);	
}

static int bfin_pixc_open(struct inode *inode, struct file *filp)
{
	
	struct bfin_pixc_device *dev;

	dev = container_of(inode->i_cdev, struct bfin_pixc_device, cdev);
	/* Test  if PIXC is already open */
	if (! atomic_dec_and_test(&dev->pixc_available)) {
		atomic_inc(&dev->pixc_available);
		return -EBUSY;
	}

	filp->private_data = dev;

	//printk("bfin-pixc: Opened\n");
	return 0;
}

static int bfin_pixc_release(struct inode *inode, struct file *filp)
{
	//printk("bfin-pixc: Released\n");
	struct bfin_pixc_device *dev = filp->private_data;
	atomic_inc(&dev->pixc_available);
	return 0;
}

ssize_t bfin_pixc_write(struct file *filp, const char __user *buff,
				size_t count, loff_t *f_pos)
{
	//printk("bfin-pixc: Write\n");

	struct bfin_pixc_device *dev = filp->private_data;
	int pos, size, err;

	pos = *f_pos;
	/* We are currently writing to the IMAGE buffer */
	if (dev->image_buf_count < IMAGE_DMA_SIZE) {
		//printk("bfin-pixc: in IMAGE write:%d\n", IMAGE_DMA_SIZE);
		size = IMAGE_DMA_SIZE+1;

		if (pos >= size)
			pos = size;
		if ((pos + count) >= size)
			count = (size - pos) - 1;

		//printk("%s:count:%d\n",__func__,(int)count);

		if (copy_from_user(&dev->image_dma_buf[pos], buff, count))
			return -EFAULT;

		pos += count;
		*f_pos = pos;
		dev->image_buf_count += count;

		//printk("%s:bufcount:%d\n",__func__,dev->image_buf_count);
		/* TODO do this properly
		 */
		//if (dev->image_buf_count == IMAGE_DMA_SIZE)
		//	dev->write_stage = 1;
	} else {
		dev->output_ready = 0;
		//printk("bfin-pixc: in OVERLAY write:%d\n",OVERLAY_DMA_SIZE);
		size = OVERLAY_DMA_SIZE+1;

		if (pos >= size)
			pos = size;
		if ((pos + count) >= size)
			count = (size - pos) - 1;

		//printk("%s:count:%d\n",__func__,(int)count);

		if (err = copy_from_user(&dev->overlay_dma_buf[pos], buff, count))
		{
			printk(KERN_ERR "bfin-pixc: overlay copy error: %d\n",err);
			return -EFAULT;
		}

		pos += count;
		*f_pos = pos;
		dev->overlay_buf_count +=count;
		//printk("%s:bufcount:%d\n",__func__,dev->overlay_buf_count);
		if (dev->overlay_buf_count >= OVERLAY_DMA_SIZE) {
			dev->image_buf_count = 0;
			dev->overlay_buf_count = 0;
			/* Kick off the pixc - separate function */
			bfin_pixc_initiate(dev);
		}
	}
	return count; 
}

ssize_t bfin_pixc_read(struct file *filp, char __user *buf, size_t count,
			loff_t *f_pos)
{
	int pos;
	int size;
	struct bfin_pixc_device *dev = filp->private_data;
	
	/* Block on read if output data not ready */
	while (!dev->output_ready) {
		/* Cater for non-blocking read request */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_DEBUG "\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, dev->output_ready))
			return -ERESTARTSYS;
	}


	if (dev->output_ready) {
		pos = *f_pos;
		size = OUTPUT_DMA_SIZE+1;

		if (pos >=size)
			pos = size-1;
		if ((pos + count) >= size)
			count = (size - pos) - 1;
		if (copy_to_user(buf,&dev->output_dma_buf[pos],count))
			return -EFAULT;
		pos += count;
		*f_pos = pos;

		return count;
	} else {
		
		printk("bfin-pixc: Output not ready\n");
		return 0;
	}
}

struct file_operations pixc_fops = {
	.owner 		= THIS_MODULE,
	.read 		= bfin_pixc_read,
	.write 		= bfin_pixc_write,
	.open 		= bfin_pixc_open,
 	.release 	= bfin_pixc_release
};


static void bfin_pixc_setup_cdev(struct bfin_pixc_device *pixc)
{
	int err, devno = MKDEV(pixc_major, pixc_minor);
	cdev_init(&pixc->cdev, &pixc_fops);
	pixc->cdev.owner = THIS_MODULE;
	pixc->cdev.ops = &pixc_fops;
	err = cdev_add(&pixc->cdev, devno, 1);
	if (err)
		printk(KERN_NOTICE "Error %d adding pixc\n",err);
}

static int bfin_pixc_config(void)
{
	//pr_info("Configuring PIXC Registers");
	/* Let's just get things running with default parameters. */
	bfin_write_PIXC_INTRSTAT(def_PIXC_INTRSTAT);
	bfin_write_PIXC_ATRANSP(def_PIXC_ATRANSP);
	bfin_write_PIXC_AVSTART(def_PIXC_AVSTART);
	bfin_write_PIXC_AVEND(def_PIXC_AVEND);
	bfin_write_PIXC_AHSTART(def_PIXC_AHSTART);
	bfin_write_PIXC_AHEND(def_PIXC_AHEND);
	bfin_write_PIXC_LPF(def_PIXC_LPF);
	bfin_write_PIXC_PPL(def_PIXC_PPL);
	bfin_write_PIXC_CTL(def_PIXC_CTL);

	SSYNC();

	return 0;
}

/* PIXC_IMAGE DMA callback i
 * These callbacks might need some data passed in
 */
static irqreturn_t bfin_pixc_dma_image_int(int irq, void *dev_id)
{
	//int ret = 0;
	return IRQ_HANDLED;
}

/* PIXC_OVERLAY DMA callback */
static irqreturn_t bfin_pixc_dma_overlay_int(int irq, void *dev_id)
{
	//int ret = 0;
	return IRQ_HANDLED;
}

/* PIXC_OUTPUT DMA callback */
static irqreturn_t bfin_pixc_dma_output_int(int irq, void *dev_id)
{
	struct bfin_pixc_device *dev = dev_id;
	unsigned int irqstat;

	irqstat=get_dma_curr_irqstat(dev->output_dma_channel);
	clear_dma_irqstat(dev->output_dma_channel);
	
	if (irqstat == 1) {
		printk(KERN_INFO "bfin-pixc: Output ready\n");
 		dev->output_ready = 1;
		//printk("%d\n",bfin_read_PIXC_CTL());
		bfin_write_PIXC_CTL(bfin_read_PIXC_CTL()&PIXC_EN_CLEAR);
		//printk("%d\n",bfin_read_PIXC_CTL());
		disable_dma(dev->image_dma_channel);
		disable_dma(dev->overlay_dma_channel);
		disable_dma(dev->output_dma_channel);
	}
	
	return IRQ_HANDLED;
}

/* All of the initial DMA configuration happens in here */
static int bfin_pixc_startup(struct bfin_pixc_device *pixc)
{
        dma_addr_t dma_handle;

	/* Request the three DMA channels */
        if (request_dma(pixc->image_dma_channel, "BFIN_PIXC_IMAGE") < 0) {
                printk(KERN_NOTICE "Unable to attach Blackfin UART RX DMA channel\n");
                return -EBUSY;
        }
	disable_dma(pixc->image_dma_channel);

        if (request_dma(pixc->overlay_dma_channel, "BFIN_PIXC_OVERLAY") < 0) {
                printk(KERN_NOTICE "Unable to attach Blackfin UART RX DMA channel\n");
                return -EBUSY;
        }

        if (request_dma(pixc->output_dma_channel, "BFIN_PIXC_OUTPUT") < 0) {
                printk(KERN_NOTICE "Unable to attach Blackfin UART RX DMA channel\n");
                return -EBUSY;
        }
	
	/* Allocate callback functions to each DMA channel */
	set_dma_callback(pixc->output_dma_channel, bfin_pixc_dma_output_int, pixc);

	/* Start off by using separate buffers for IMAGE, OVERLAY and OUTPUT. Then explore the
	 * possibility of using the IMAGE buffer as the OUTPUT buffer, to reduce memory
	 * footprint.
	 */
	if (pixc->image_dma_buf = (unsigned char *)dma_zalloc_coherent(NULL, IMAGE_DMA_SIZE, 
								&dma_handle, GFP_DMA) == NULL)
		printk("IMAGE DMA buffer allocation failed - Uncached DMA region?\n");
	pixc->image_dma_buf = dma_handle; //The real allocated memory pointer?

	if (pixc->overlay_dma_buf = (unsigned char *)dma_zalloc_coherent(NULL, OVERLAY_DMA_SIZE, 
								&dma_handle, GFP_DMA) == NULL)
		printk("OVERLAY DMA buffer allocation failed - Uncached DMA region?\n");
	pixc->overlay_dma_buf = dma_handle;

	if (pixc->output_dma_buf = (unsigned char *)dma_zalloc_coherent(NULL, OUTPUT_DMA_SIZE,
								&dma_handle, GFP_DMA) == NULL)
		printk("OUTPUT DMA buffer allocation failed - Uncached DMA region?\n");
	pixc->output_dma_buf = dma_handle;

	return 0;
}

static int bfin_pixc_shutdown(struct bfin_pixc_device *dev)
{
	disable_dma(dev->image_dma_channel);
	free_dma(dev->image_dma_channel);
	disable_dma(dev->overlay_dma_channel);
	free_dma(dev->overlay_dma_channel);
	disable_dma(dev->output_dma_channel);
	free_dma(dev->output_dma_channel);
	dma_free_coherent(NULL, IMAGE_DMA_SIZE, dev->image_dma_buf, 0);
	dma_free_coherent(NULL, OVERLAY_DMA_SIZE, dev->overlay_dma_buf, 0);
	dma_free_coherent(NULL, OUTPUT_DMA_SIZE, dev->output_dma_buf, 0);

	cdev_del(&pixc->cdev);

	return 0;

}

static int bfin_pixc_probe(struct platform_device *pdev)
{	
	printk("bfin-pixc: Probe\n");

	struct resource *res;
	int ret = 0;

	pixc = kzalloc(sizeof(*pixc), GFP_KERNEL);
	if (!pixc) {
		dev_err(&pdev->dev,
				"fail to malloc bfin_pixc \n");
		return -ENOMEM;
	}

	if (bfin_pixc_config()) {
		pr_info("PIXC configuration failed");
		ret = -1;
	}
	// TODO: Remap PIXC MMR to something like pixc->membase.
	//	 See bfin_uart: probe () function for example.

	/* Get DMA IRQ info from ezkit.c */
	pixc->image_irq = platform_get_irq(pdev, 0);
	if (pixc->image_irq < 0) {
		dev_err(&pdev->dev, "No PIXC image IRQ specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}

	pixc->overlay_irq = platform_get_irq(pdev, 1);
	if (pixc->overlay_irq < 0) {
		dev_err(&pdev->dev, "No PIXC overlay IRQ specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}

	pixc->output_irq = platform_get_irq(pdev, 2);
	if (pixc->output_irq < 0) {
		dev_err(&pdev->dev, "No PIXC output IRQ specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}

	/* Get the dma information defined in ezkit.c */
	res = platform_get_resource(pdev,IORESOURCE_DMA, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No PIXC image channel specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}
	pixc->image_dma_channel = res->start;

	res = platform_get_resource(pdev,IORESOURCE_DMA, 1);
	if (res == NULL) {
		dev_err(&pdev->dev, "No PIXC overlay channel specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}
	pixc->overlay_dma_channel = res->start;

	res = platform_get_resource(pdev,IORESOURCE_DMA, 2);
	if (res == NULL) {
		dev_err(&pdev->dev, "No PIXC channel specified\n");
		ret = -ENOENT;
		goto out_no_dma;
	}
	pixc->output_dma_channel = res->start;

	/* Put bfin_pixc in pdev so it remains global */
	dev_set_drvdata(&pdev->dev, pixc);
	
	/* TODO Actually check that DMA is properly initialised, if 
	 * not, free stuff */
	if (bfin_pixc_startup(pixc)) {
		pr_info("DMA Failed\n");
	}

	/* Do cdev stuff here */
	bfin_pixc_setup_cdev(pixc);
	
	pixc->image_buf_count = 0;	//should this live in probe?
	pixc->overlay_buf_count = 0;
	pixc->output_ready = 0;	
	pixc->pixc_available = (atomic_t){1};

	init_waitqueue_head(&pixc->inq);
	
out_no_dma:
	kfree(pixc);
	return ret;
}

static int bfin_pixc_remove(struct platform_device *pdev) 
{
	//printk("bfin-pixc: Remove\n");

	struct bfin_pixc_device *dev = platform_get_drvdata(pdev);

	bfin_pixc_shutdown(dev);
	kfree(dev);	
	
	
	return 0;
};


static struct platform_driver bfin_pixc_driver = {
	.probe		= bfin_pixc_probe,
	.remove		= bfin_pixc_remove,
//	.suspend	= bfin_pixc_suspend,
//	.resume		= bfin_pixc_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};


static int __init bfin_pixc_init(void)
{
	int ret;
	pr_info("Blackfin PIXC driver");
	pr_info("PIXC driver: loaded\n");
	ret = platform_driver_register(&bfin_pixc_driver);
	if (ret) {
		printk("failed to register bfin pixc platform\n");
	}
	return 0;
}

static void __exit bfin_pixc_exit(void)
{
	platform_driver_unregister(&bfin_pixc_driver);
}

module_init(bfin_pixc_init);
module_exit(bfin_pixc_exit);

MODULE_AUTHOR("Andre Wolokita");
MODULE_DESCRIPTION("Blackfin Pixel Compositor subsystem driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bfin-pixc");
