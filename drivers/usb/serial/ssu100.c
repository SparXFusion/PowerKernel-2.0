/*
 * usb-serial driver for Quatech SSU-100
 *
 * based on ftdi_sio.c and the original serqt_usb.c from Quatech
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial_reg.h>
#include <linux/uaccess.h>

#define QT_OPEN_CLOSE_CHANNEL       0xca
#define QT_SET_GET_DEVICE           0xc2
#define QT_SET_GET_REGISTER         0xc0
#define QT_GET_SET_PREBUF_TRIG_LVL  0xcc
#define QT_SET_ATF                  0xcd
#define QT_GET_SET_UART             0xc1
#define QT_TRANSFER_IN              0xc0
#define QT_HW_FLOW_CONTROL_MASK     0xc5
#define QT_SW_FLOW_CONTROL_MASK     0xc6

#define  SERIAL_MSR_MASK            0xf0

#define  SERIAL_CRTSCTS ((UART_MCR_RTS << 8) | UART_MSR_CTS)

#define  SERIAL_EVEN_PARITY         (UART_LCR_PARITY | UART_LCR_EPAR)

#define  MAX_BAUD_RATE              460800

#define ATC_DISABLED                0x00
#define DUPMODE_BITS        0xc0
#define RR_BITS             0x03
#define LOOPMODE_BITS       0x41
#define RS232_MODE          0x00
#define RTSCTS_TO_CONNECTOR 0x40
#define CLKS_X4             0x02
#define FULLPWRBIT          0x00000080
#define NEXT_BOARD_POWER_BIT        0x00000004

static bool debug;

/* Version Information */
#define DRIVER_VERSION "v0.1"
#define DRIVER_DESC "Quatech SSU-100 USB to Serial Driver"

#define	USB_VENDOR_ID_QUATECH	0x061d	/* Quatech VID */
#define QUATECH_SSU100	0xC020	/* SSU100 */

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_SSU100)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);


static struct usb_driver ssu100_driver = {
	.name			       = "ssu100",
	.probe			       = usb_serial_probe,
	.disconnect		       = usb_serial_disconnect,
	.id_table		       = id_table,
	.suspend		       = usb_serial_suspend,
	.resume			       = usb_serial_resume,
	.no_dynamic_id		       = 1,
	.supports_autosuspend	       = 1,
};

struct ssu100_port_private {
	spinlock_t status_lock;
	u8 shadowLSR;
	u8 shadowMSR;
	struct async_icount icount;
};

static void ssu100_release(struct usb_serial *serial)
{
	struct ssu100_port_private *priv = usb_get_serial_port_data(*serial->port);

	dbg("%s", __func__);
	kfree(priv);
}

static inline int ssu100_control_msg(struct usb_device *dev,
				     u8 request, u16 data, u16 index)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       request, 0x40, data, index,
			       NULL, 0, 300);
}

static inline int ssu100_setdevice(struct usb_device *dev, u8 *data)
{
	u16 x = ((u16)(data[1] << 8) | (u16)(data[0]));

	return ssu100_control_msg(dev, QT_SET_GET_DEVICE, x, 0);
}


static inline int ssu100_getdevice(struct usb_device *dev, u8 *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       QT_SET_GET_DEVICE, 0xc0, 0, 0,
			       data, 3, 300);
}

static inline int ssu100_getregister(struct usb_device *dev,
				     unsigned short uart,
				     unsigned short reg,
				     u8 *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       QT_SET_GET_REGISTER, 0xc0, reg,
			       uart, data, sizeof(*data), 300);

}


static inline int ssu100_setregister(struct usb_device *dev,
				     unsigned short uart,
				     unsigned short reg,
				     u16 data)
{
	u16 value = (data << 8) | reg;

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       QT_SET_GET_REGISTER, 0x40, value, uart,
			       NULL, 0, 300);

}

#define set_mctrl(dev, set)		update_mctrl((dev), (set), 0)
#define clear_mctrl(dev, clear)	update_mctrl((dev), 0, (clear))

/* these do not deal with device that have more than 1 port */
static inline int update_mctrl(struct usb_device *dev, unsigned int set,
			       unsigned int clear)
{
	unsigned urb_value;
	int result;

	if (((set | clear) & (TIOCM_DTR | TIOCM_RTS)) == 0) {
		dbg("%s - DTR|RTS not being set|cleared", __func__);
		return 0;	/* no change */
	}

	clear &= ~set;	/* 'set' takes precedence over 'clear' */
	urb_value = 0;
	if (set & TIOCM_DTR)
		urb_value |= UART_MCR_DTR;
	if (set & TIOCM_RTS)
		urb_value |= UART_MCR_RTS;

	result = ssu100_setregister(dev, 0, UART_MCR, urb_value);
	if (result < 0)
		dbg("%s Error from MODEM_CTRL urb", __func__);

	return result;
}

static int ssu100_initdevice(struct usb_device *dev)
{
	u8 *data;
	int result = 0;

	dbg("%s", __func__);

	data = kzalloc(3, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	result = ssu100_getdevice(dev, data);
	if (result < 0) {
		dbg("%s - get_device failed %i", __func__, result);
		goto out;
	}

	data[1] &= ~FULLPWRBIT;

	result = ssu100_setdevice(dev, data);
	if (result < 0) {
		dbg("%s - setdevice failed %i", __func__, result);
		goto out;
	}

	result = ssu100_control_msg(dev, QT_GET_SET_PREBUF_TRIG_LVL, 128, 0);
	if (result < 0) {
		dbg("%s - set prebuffer level failed %i", __func__, result);
		goto out;
	}

	result = ssu100_control_msg(dev, QT_SET_ATF, ATC_DISABLED, 0);
	if (result < 0) {
		dbg("%s - set ATFprebuffer level failed %i", __func__, result);
		goto out;
	}

	result = ssu100_getdevice(dev, data);
	if (result < 0) {
		dbg("%s - get_device failed %i", __func__, result);
		goto out;
	}

	data[0] &= ~(RR_BITS | DUPMODE_BITS);
	data[0] |= CLKS_X4;
	data[1] &= ~(LOOPMODE_BITS);
	data[1] |= RS232_MODE;

	result = ssu100_setdevice(dev, data);
	if (result < 0) {
		dbg("%s - setdevice failed %i", __func__, result);
		goto out;
	}

out:	kfree(data);
	return result;

}


static void ssu100_set_termios(struct tty_struct *tty,
			       struct usb_serial_port *port,
			       struct ktermios *old_termios)
{
	struct usb_device *dev = port->serial->dev;
	struct ktermios *termios = tty->termios;
	u16 baud, divisor, remainder;
	unsigned int cflag = termios->c_cflag;
	u16 urb_value = 0; /* will hold the new flags */
	int result;

	dbg("%s", __func__);

	if (cflag & PARENB) {
		if (cflag & PARODD)
			urb_value |= UART_LCR_PARITY;
		else
			urb_value |= SERIAL_EVEN_PARITY;
	}

	switch (cflag & CSIZE) {
	case CS5:
		urb_value |= UART_LCR_WLEN5;
		break;
	case CS6:
		urb_value |= UART_LCR_WLEN6;
		break;
	case CS7:
		urb_value |= UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		urb_value |= UART_LCR_WLEN8;
		break;
	}

	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;

	dbg("%s - got baud = %d\n", __func__, baud);


	divisor = MAX_BAUD_RATE / baud;
	remainder = MAX_BAUD_RATE % baud;
	if (((remainder * 2) >= baud) && (baud != 110))
		divisor++;

	urb_value = urb_value << 8;

	result = ssu100_control_msg(dev, QT_GET_SET_UART, divisor, urb_value);
	if (result < 0)
		dbg("%s - set uart failed", __func__);

	if (cflag & CRTSCTS)
		result = ssu100_control_msg(dev, QT_HW_FLOW_CONTROL_MASK,
					    SERIAL_CRTSCTS, 0);
	else
		result = ssu100_control_msg(dev, QT_HW_FLOW_CONTROL_MASK,
					    0, 0);
	if (result < 0)
		dbg("%s - set HW flow control failed", __func__);

	if (I_IXOFF(tty) || I_IXON(tty)) {
		u16 x = ((u16)(START_CHAR(tty) << 8) | (u16)(STOP_CHAR(tty)));

		result = ssu100_control_msg(dev, QT_SW_FLOW_CONTROL_MASK,
					    x, 0);
	} else
		result = ssu100_control_msg(dev, QT_SW_FLOW_CONTROL_MASK,
					    0, 0);

	if (result < 0)
		dbg("%s - set SW flow control failed", __func__);

}


static int ssu100_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_device *dev = port->serial->dev;
	struct ssu100_port_private *priv = usb_get_serial_port_data(port);
	u8 *data;
	int result;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	data = kzalloc(2, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				 QT_OPEN_CLOSE_CHANNEL,
				 QT_TRANSFER_IN, 0x01,
				 0, data, 2, 300);
	if (result < 0) {
		dbg("%s - open failed %i", __func__, result);
		kfree(data);
		return result;
	}

	spin_lock_irqsave(&priv->status_lock, flags);
	priv->shadowLSR = data[0];
	priv->shadowMSR = data[1];
	spin_unlock_irqrestore(&priv->status_lock, flags);

	kfree(data);

/* set to 9600 */
	result = ssu100_control_msg(dev, QT_GET_SET_UART, 0x30, 0x0300);
	if (result < 0)
		dbg("%s - set uart failed", __func__);

	if (tty)
		ssu100_set_termios(tty, port, tty->termios);

	return usb_serial_generic_open(tty, port);
}

static void ssu100_close(struct usb_serial_port *port)
{
	dbg("%s", __func__);
	usb_serial_generic_close(port);
}

static int get_serial_info(struct usb_serial_port *port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));
	tmp.line		= port->serial->minor;
	tmp.port		= 0;
	tmp.irq			= 0;
	tmp.flags		= ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size	= port->bulk_out_size;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= 30*HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int wait_modem_info(struct usb_serial_port *port, unsigned int arg)
{
	struct ssu100_port_private *priv = usb_get_serial_port_data(port);
	struct async_icount prev, cur;
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	prev = priv->icount;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	while (1) {
		wait_event_interruptible(port->delta_msr_wait,
					 (port->serial->disconnected ||
					  (priv->icount.rng != prev.rng) ||
					  (priv->icount.dsr != prev.dsr) ||
					  (priv->icount.dcd != prev.dcd) ||
					  (priv->icount.cts != prev.cts)));

		if (signal_pending(current))
			return -ERESTARTSYS;

		if (port->serial->disconnected)
			return -EIO;

		spin_lock_irqsave(&priv->status_lock, flags);
		cur = priv->icount;
		spin_unlock_irqrestore(&priv->status_lock, flags);

		if ((prev.rng == cur.rng) &&
		    (prev.dsr == cur.dsr) &&
		    (prev.dcd == cur.dcd) &&
		    (prev.cts == cur.cts))
			return -EIO;

		if ((arg & TIOCM_RNG && (prev.rng != cur.rng)) ||
		    (arg & TIOCM_DSR && (prev.dsr != cur.dsr)) ||
		    (arg & TIOCM_CD  && (prev.dcd != cur.dcd)) ||
		    (arg & TIOCM_CTS && (prev.cts != cur.cts)))
			return 0;
	}
	return 0;
}

static int ssu100_get_icount(struct tty_struct *tty,
			struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ssu100_port_private *priv = usb_get_serial_port_data(port);
	struct async_icount cnow = priv->icount;

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}



static int ssu100_ioctl(struct tty_struct *tty,
		    unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s cmd 0x%04x", __func__, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		return get_serial_info(port,
				       (struct serial_struct __user *) arg);

	case TIOCMIWAIT:
		return wait_modem_info(port, arg);

	default:
		break;
	}

	dbg("%s arg not supported", __func__);

	return -ENOIOCTLCMD;
}

static int ssu100_attach(struct usb_serial *serial)
{
	struct ssu100_port_private *priv;
	struct usb_serial_port *port = *serial->port;

	dbg("%s", __func__);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&port->dev, "%s- kmalloc(%Zd) failed.\n", __func__,
			sizeof(*priv));
		return -ENOMEM;
	}

	spin_lock_init(&priv->status_lock);
	usb_set_serial_port_data(port, priv);

	return ssu100_initdevice(serial->dev);
}

static int ssu100_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_device *dev = port->serial->dev;
	u8 *d;
	int r;

	dbg("%s\n", __func__);

	d = kzalloc(2, GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	r = ssu100_getregister(dev, 0, UART_MCR, d);
	if (r < 0)
		goto mget_out;

	r = ssu100_getregister(dev, 0, UART_MSR, d+1);
	if (r < 0)
		goto mget_out;

	r = (d[0] & UART_MCR_DTR ? TIOCM_DTR : 0) |
		(d[0] & UART_MCR_RTS ? TIOCM_RTS : 0) |
		(d[1] & UART_MSR_CTS ? TIOCM_CTS : 0) |
		(d[1] & UART_MSR_DCD ? TIOCM_CAR : 0) |
		(d[1] & UART_MSR_RI ? TIOCM_RI : 0) |
		(d[1] & UART_MSR_DSR ? TIOCM_DSR : 0);

mget_out:
	kfree(d);
	return r;
}

static int ssu100_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_device *dev = port->serial->dev;

	dbg("%s\n", __func__);
	return update_mctrl(dev, set, clear);
}

static void ssu100_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_device *dev = port->serial->dev;

	dbg("%s\n", __func__);

	mutex_lock(&port->serial->disc_mutex);
	if (!port->serial->disconnected) {
		/* Disable flow control */
		if (!on &&
		    ssu100_setregister(dev, 0, UART_MCR, 0) < 0)
			dev_err(&port->dev, "error from flowcontrol urb\n");
		/* drop RTS and DTR */
		if (on)
			set_mctrl(dev, TIOCM_DTR | TIOCM_RTS);
		else
			clear_mctrl(dev, TIOCM_DTR | TIOCM_RTS);
	}
	mutex_unlock(&port->serial->disc_mutex);
}

static void ssu100_update_msr(struct usb_serial_port *port, u8 msr)
{
	struct ssu100_port_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	priv->shadowMSR = msr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (msr & UART_MSR_ANY_DELTA) {
		/* update input line counters */
		if (msr & UART_MSR_DCTS)
			priv->icount.cts++;
		if (msr & UART_MSR_DDSR)
			priv->icount.dsr++;
		if (msr & UART_MSR_DDCD)
			priv->icount.dcd++;
		if (msr & UART_MSR_TERI)
			priv->icount.rng++;
		wake_up_interruptible(&port->delta_msr_wait);
	}
}

static void ssu100_update_lsr(struct usb_serial_port *port, u8 lsr,
			      char *tty_flag)
{
	struct ssu100_port_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	priv->shadowLSR = lsr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	*tty_flag = TTY_NORMAL;
	if (lsr & UART_LSR_BRK_ERROR_BITS) {
		/* we always want to update icount, but we only want to
		 * update tty_flag for one case */
		if (lsr & UART_LSR_BI) {
			priv->icount.brk++;
			*tty_flag = TTY_BREAK;
			usb_serial_handle_break(port);
		}
		if (lsr & UART_LSR_PE) {
			priv->icount.parity++;
			if (*tty_flag == TTY_NORMAL)
				*tty_flag = TTY_PARITY;
		}
		if (lsr & UART_LSR_FE) {
			priv->icount.frame++;
			if (*tty_flag == TTY_NORMAL)
				*tty_flag = TTY_FRAME;
		}
		if (lsr & UART_LSR_OE) {
			priv->icount.overrun++;
			tty_insert_flip_char(tty_port_tty_get(&port->port),
					0, TTY_OVERRUN);
		}
	}

}

static int ssu100_process_packet(struct urb *urb,
				 struct tty_struct *tty)
{
	struct usb_serial_port *port = urb->context;
	char *packet = (char *)urb->transfer_buffer;
	char flag = TTY_NORMAL;
	u32 len = urb->actual_length;
	int i;
	char *ch;

	dbg("%s - port %d", __func__, port->number);

	if ((len >= 4) &&
	    (packet[0] == 0x1b) && (packet[1] == 0x1b) &&
	    ((packet[2] == 0x00) || (packet[2] == 0x01))) {
		if (packet[2] == 0x00)
			ssu100_update_lsr(port, packet[3], &flag);
		if (packet[2] == 0x01)
			ssu100_update_msr(port, packet[3]);

		len -= 4;
		ch = packet + 4;
	} else
		ch = packet;

	if (!len)
		return 0;	/* status only */

	if (port->port.console && port->sysrq) {
		for (i = 0; i < len; i++, ch++) {
			if (!usb_serial_handle_sysrq_char(port, *ch))
				tty_insert_flip_char(tty, *ch, flag);
		}
	} else
		tty_insert_flip_string_fixed_flag(tty, ch, flag, len);

	return len;
}

static void ssu100_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct tty_struct *tty;
	int count;

	dbg("%s", __func__);

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;

	count = ssu100_process_packet(urb, tty);

	if (count)
		tty_flip_buffer_push(tty);
	tty_kref_put(tty);
}

static struct usb_serial_driver ssu100_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ssu100",
	},
	.description	     = DRIVER_DESC,
	.id_table	     = id_table,
	.usb_driver	     = &ssu100_driver,
	.num_ports	     = 1,
	.open		     = ssu100_open,
	.close		     = ssu100_close,
	.attach              = ssu100_attach,
	.release             = ssu100_release,
	.dtr_rts             = ssu100_dtr_rts,
	.process_read_urb    = ssu100_process_read_urb,
	.tiocmget            = ssu100_tiocmget,
	.tiocmset            = ssu100_tiocmset,
	.get_icount	     = ssu100_get_icount,
	.ioctl               = ssu100_ioctl,
	.set_termios         = ssu100_set_termios,
	.disconnect          = usb_serial_generic_disconnect,
};

static int __init ssu100_init(void)
{
	int retval;

	dbg("%s", __func__);

	/* register with usb-serial */
	retval = usb_serial_register(&ssu100_device);

	if (retval)
		goto failed_usb_sio_register;

	retval = usb_register(&ssu100_driver);
	if (retval)
		goto failed_usb_register;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");

	return 0;

failed_usb_register:
	usb_serial_deregister(&ssu100_device);
failed_usb_sio_register:
	return retval;
}

static void __exit ssu100_exit(void)
{
	usb_deregister(&ssu100_driver);
	usb_serial_deregister(&ssu100_device);
}

module_init(ssu100_init);
module_exit(ssu100_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
