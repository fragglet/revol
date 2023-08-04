/*
    Driver for PsionW Sound output

    (c) 2002 Simon Howard

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* TODO:
 *
 *  - Microphone support
 *  - /dev/mixer support
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sound.h>
#include <linux/soundcard.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hardware/psionw.h>
#include <asm/arch/irqs.h>
#include <asm/arch/psionw-power.h>

#include "ulaw.h"

/* these ought to be in the hardware headers: */

#define CODOUTEN (1 << 0)
#define CODINEN  (1 << 1)

#define SAMPLE_RATE 8000 /* this cant be changed */

static int users;
static struct semaphore users_lock;

extern void enable_irq(unsigned int irq);
extern void disable_irq(unsigned int irq);

static int audio_dev;

/* buffer size */

#define BUFSIZE 128

/* lock for access to writebuf. the interrupt handler does not check
 * for this but interrupts are also disabled while we access writebuf
 * in write
 */

static struct semaphore writebuf_lock;

/* freelist for buffering data */

static unsigned char writebuf[BUFSIZE];
static int writebuf_head, writebuf_tail, writebuf_size;

/* interrupt routine */

void psionw_sound_isr(unsigned int irq)
{
	int i, count;

	/* write some more data to the speaker */

	count = writebuf_size < 8 ? writebuf_size : 8;

	for (i=0; i<count; ++i) {
		psionw_writeb(dsp_ulaw[writebuf[writebuf_head]], CODR);
		writebuf_head = (writebuf_head + 1) % BUFSIZE;
	}

	for (; i<8; ++i) {
		psionw_writeb(0, CODR);
	}

	writebuf_size -= count;

	/* if we had nothing left, stop interrupts until we 
	 * get more (set in the write function)
	 */

	if (count == 0) 
		disable_irq(IRQ_CSINT);

	/* clear interrupt */

	psionw_writel(1, COEOI);

}

static ssize_t psionw_sound_read(struct file *file, char *buffer, 
				 size_t count, loff_t *ppos)
{
	/* insert microphone code here */
	return count;
}

static ssize_t psionw_sound_write(struct file *file, const char *buffer, 
				  size_t count, loff_t *ppos)
{
	int i, n, ret = 0;

	while (count > 0) {

		/* check we can fit anything into the buffer */

		if (writebuf_size >= BUFSIZE) {
			if (file->f_flags & O_NONBLOCK)
				break;

			set_current_state(TASK_INTERRUPTIBLE);
                        schedule_timeout(1);
			continue;
		}

		down(&writebuf_lock);

		/* disable irq while we do this */
	
		disable_irq(IRQ_CSINT);
	
		/* fit in as much as we can to the write buffer */
	
		if (count > BUFSIZE - writebuf_size)
			n = BUFSIZE - writebuf_size;
		else
			n = count;
	
		/* this is complicated stuff to add to the buffer in blocks
		 * rather than byte-by-byte, for speed
		 * we dont want to disable the sound IRQ for too long or we
		 * get nasty breaks in the sound
		 */
	
		if (writebuf_tail >= writebuf_head) {
	
			/* fill in as much as possible at the end */
	
			i = BUFSIZE - writebuf_tail;
	
			if (i >= n) {
	
				/* we can fit them all at the end */
	
				memcpy(writebuf + writebuf_tail, buffer, n);
			} else {
	
				/* first part at the end of buffer */
	
				memcpy(writebuf + writebuf_tail, buffer, i);
	
				/* second part at the beginning */
	
				memcpy(writebuf, buffer+i, n-i);
			}
		} else {
	
			/* just fit in as much as we can */
	
			memcpy(writebuf + writebuf_tail, buffer, n);
		}
	
		writebuf_tail = (writebuf_tail + n) % BUFSIZE;
		writebuf_size += n;

		count -= n;
		buffer += n;
		ret += n;

		/* enable irq again */

		enable_irq(IRQ_CSINT);

		up(&writebuf_lock);
	}

	if (file->f_flags & O_NONBLOCK && ret == 0)
		return -EAGAIN;

	return ret;
}

static int psionw_sound_ioctl(struct inode *inode, struct file *file, 
	 		      unsigned int cmd, unsigned long arg)
{
	int val;

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
		return 0;

	case SNDCTL_DSP_SYNC:
		/* we cant really do this, but we'll pretend
		 * we can, to keep programs that use it happy
		 */
		return 0;

	case SNDCTL_DSP_SPEED:  /* set sample rate */
		/* we cant change it */

		if (get_user(val, (int *)arg))
			return -EFAULT;

		if (val >= 0)
			return put_user(SAMPLE_RATE, (int *)arg);
		else
			return -EFAULT;

	case SNDCTL_DSP_STEREO: /* set stereo or mono */

		if (get_user(val, (int *)arg))
			return -EFAULT;

		/* we are stuck in mono mode */

		if (val)
			printk("psionw_sound: attempt to put in stereo mode (unsupported)\n");
	
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		return put_user(8, (int *)arg);  /* 8 byte block size (?) */

	case SNDCTL_DSP_GETFMTS:
		return put_user(AFMT_U8, (int *)arg);

	case SNDCTL_DSP_SETFMT:
		return put_user(AFMT_U8, (int *)arg);

	case SNDCTL_DSP_CHANNELS: 	/* set channels */
		return put_user(1, (int *) arg);

		
	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;

	}

	return -EINVAL;
}

static int psionw_sound_open(struct inode *inode, struct file *file)
{
	down(&users_lock);

	if (users > 0) {
		up(&users_lock);
		return -EBUSY;
	}

	++users;

	/* empty list */

	writebuf_head = writebuf_tail = writebuf_size = 0;

	/* enable codec */

	psionw_writeb(psionw_readb(PDDR) | PDDR_AMPEN | PDDR_CDE, PDDR);
	psionw_writel(psionw_readl(CONFG) | CODINEN | CODOUTEN, CONFG);

	/* enable sound irq */

	irq_desc[IRQ_CSINT].mask_ack = psionw_sound_isr;

	enable_irq(IRQ_CSINT);

	up(&users_lock);

	return 0; 
}

static int psionw_sound_release(struct inode *inode, struct file *file)
{
	down(&users_lock);

	--users;

	/* disable codec */

	psionw_writeb(psionw_readb(PDDR) & ~(PDDR_AMPEN|PDDR_CDE), PDDR);
	psionw_writel(psionw_readl(CONFG) & ~(CODINEN|CODOUTEN), CONFG);

	/* disable sound irq */

	disable_irq(IRQ_CSINT);

	up(&users_lock);

	return 0;
}

static int psionw_sound_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENODEV;
}

static struct file_operations sound_ops = {
        owner:          THIS_MODULE,
	llseek: 	no_llseek,
	read: 		psionw_sound_read,
        write:          psionw_sound_write,
        ioctl:          psionw_sound_ioctl,
	mmap:		psionw_sound_mmap,
        open:           psionw_sound_open,
        release:        psionw_sound_release,
};

int __init init_psionw_sound(void)
{
	audio_dev = register_sound_dsp(&sound_ops, -1);

	if (audio_dev < 0) {
		printk(KERN_ERR "psionw_sound: cannot register sound device\n");
		return -ENODEV;
	}

	init_MUTEX(&writebuf_lock);
	init_MUTEX(&users_lock);

	printk("psionw_sound: initialised sound output\n");

	return 0;
}

void __exit cleanup_psionw_sound(void)
{
	unregister_sound_dsp(audio_dev);
}


MODULE_DESCRIPTION("psionw sound output driver");
MODULE_AUTHOR("Simon Howard");
MODULE_LICENSE("GPL");

module_init(init_psionw_sound);
module_exit(cleanup_psionw_sound);

