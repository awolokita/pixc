/*
 * Blackfin Pixel Compositor framebuffer device
 * 
 * Copyright (C) 2014 Andre Wolokita
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/fb.h>
#include <linux/init.h>

#define DRIVER_NAME "bfin_pixc-fb"

static struct platform_driver bfin_pixc_driver = {
	.probe		= bfin_pixc_fb_probe,
	.remove		= bfin_pixc_fb_remove,
	.driver		= {
		.name 	= DRIVER_NAME,
		.ownder	= THIS_MODULE,
	},
};

/*
 * Probing/initialisation
 */
static int bfin_pixc_fb_probe(struct platform_device *dev)
{
	
}

static int __init bfin_pixc_fb_init(void)
{
	int ret = 0;
	pr_info("Blackfin PIXC framebuffer\n");
	ret = platform_driver_register(&bfin_pixc_driver);
	// at this point vfb dynamically allocates and adds a
	// struct platform_device. I don't think that needs to happen here,
	// as the platform_device stuff lives in ezkit.c
	
	if (ret) {
		pr_info("BFIN PIXC: Failed to register platform driver");
	}
	return ret;
}

static void __exit bfin_pixc_fb_exit(void)
{
	platform_driver_unregister(&bfin_pixc_driver);
}

module_init(bfin_pixc_fb_init);
module_exit(bfin_pixc_fb_exit);

MODULE_AUTHOR("Andre Wolokita");
MODULE_DESCRIPTION("Blackfin Pixel Compositor framebuffer");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bfin_pixc-fb");
