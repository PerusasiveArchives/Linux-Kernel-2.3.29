
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/io.h>
#include "i8259.h"

unsigned char cached_8259[2] = { 0xff, 0xff };
#define cached_A1 (cached_8259[0])
#define cached_21 (cached_8259[1])

int i8259_irq(int cpu)
{
	int irq;
	
        /*
         * Perform an interrupt acknowledge cycle on controller 1
         */                                                             
        outb(0x0C, 0x20);
        irq = inb(0x20) & 7;                                   
        if (irq == 2)                                                     
        {                                                                   
                /*                                     
                 * Interrupt is cascaded so perform interrupt
                 * acknowledge on controller 2
                 */
                outb(0x0C, 0xA0);                      
                irq = (inb(0xA0) & 7) + 8;
        }
        else if (irq==7)                                
        {
                /*                               
                 * This may be a spurious interrupt
                 *                         
                 * Read the interrupt status register. If the most
                 * significant bit is not set then there is no valid
		 * interrupt
		 */
		outb(0x0b, 0x20);
		if(~inb(0x20)&0x80)
			return -1;
	}
	return irq;
}

static void i8259_mask_and_ack_irq(unsigned int irq_nr)
{
        if ( irq_nr >= i8259_pic.irq_offset )
                irq_nr -= i8259_pic.irq_offset;

        if (irq_nr > 7) {                                                   
                cached_A1 |= 1 << (irq_nr-8);                                   
                inb(0xA1);      /* DUMMY */                                     
                outb(cached_A1,0xA1);                                           
                outb(0x20,0xA0);        /* Non-specific EOI */             
                outb(0x20,0x20);        /* Non-specific EOI to cascade */
        } else {                                                            
                cached_21 |= 1 << irq_nr;                                   
                inb(0x21);      /* DUMMY */                                 
                outb(cached_21,0x21);
                outb(0x20,0x20);        /* Non-specific EOI */                 
        }                                                                
}

static void i8259_set_irq_mask(int irq_nr)
{
        outb(cached_A1,0xA1);
        outb(cached_21,0x21);
}

static void i8259_mask_irq(unsigned int irq_nr)
{
        if ( irq_nr >= i8259_pic.irq_offset )
                irq_nr -= i8259_pic.irq_offset;
        if ( irq_nr < 8 )
                cached_21 |= 1 << irq_nr;
        else
                cached_A1 |= 1 << (irq_nr-8);
        i8259_set_irq_mask(irq_nr);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{

        if ( irq_nr >= i8259_pic.irq_offset )
                irq_nr -= i8259_pic.irq_offset;
        if ( irq_nr < 8 )
                cached_21 &= ~(1 << irq_nr);
        else
                cached_A1 &= ~(1 << (irq_nr-8));
        i8259_set_irq_mask(irq_nr);
}

struct hw_interrupt_type i8259_pic = {
        " i8259    ",
        NULL,
        NULL,
        i8259_unmask_irq,
        i8259_mask_irq,
        i8259_mask_and_ack_irq,
        0
};

static void
no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}

void __init i8259_init(void)
{
        /* init master interrupt controller */
        outb(0x11, 0x20); /* Start init sequence */
        outb(0x00, 0x21); /* Vector base */
        outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
        outb(0x01, 0x21); /* Select 8086 mode */
        outb(0xFF, 0x21); /* Mask all */
        /* init slave interrupt controller */
        outb(0x11, 0xA0); /* Start init sequence */
        outb(0x08, 0xA1); /* Vector base */
        outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
        outb(0x01, 0xA1); /* Select 8086 mode */
        outb(0xFF, 0xA1); /* Mask all */
        outb(cached_A1, 0xA1);
        outb(cached_21, 0x21);
        request_irq( i8259_pic.irq_offset + 2, no_action, SA_INTERRUPT,
                     "82c59 secondary cascade", NULL );
        enable_irq(i8259_pic.irq_offset + 2);  /* Enable cascade interrupt */
}
