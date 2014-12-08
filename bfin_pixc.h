#include <linux/spinlock.h>

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
};
