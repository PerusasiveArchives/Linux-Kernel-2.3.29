#ifndef __ASM_SH_DMA_H
#define __ASM_SH_DMA_H

/* Don't define MAX_DMA_ADDRESS; it's useless on the SuperH and any
   occurrence should be flagged as an error.  */

#define MAX_DMA_CHANNELS 8

/* The maximum address that we can perform a DMA transfer to on this platform */
/* XXX: This is not applicable to SuperH, just needed for alloc_bootmem */
#define MAX_DMA_ADDRESS      (PAGE_OFFSET+0x1000000)

extern int request_dma(unsigned int dmanr, const char * device_id);	/* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	/* release it again */

#endif /* __ASM_SH_DMA_H */
