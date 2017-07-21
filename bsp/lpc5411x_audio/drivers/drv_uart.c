/*
 * File      : drv_uart.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2009-2014 RT-Thread Develop Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author       Notes
 * 2013-05-18     Bernard      The first version for LPC40xx
 * 2014-12-16     RT_learning  The first version for LPC5410x
 */


#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "chip.h"

static uint32_t _UART_DivClk(uint32_t pclk, uint32_t m);
static uint32_t _UART_GetHighDiv(uint32_t val, uint8_t strict);
static int32_t _CalcErr(uint32_t n, uint32_t d, uint32_t *prev);
static ErrorCode_t _UART_CalcDiv(UART_BAUD_T *ub);
static void _UART_CalcMul(UART_BAUD_T *ub);
 

struct lpc_uart
{
    LPC_USART_T *UART;
    IRQn_Type UART_IRQn;
};



typedef struct {
	uint32_t cfg;		/*!< UART Configuration value; OR'ed UART_CFG_XXXX values(example #UART_CFG_8BIT) */
	uint16_t div;		/*!< UART baudrate divider value; usually calculated by using UART_CalBaud() API */
	uint8_t ovr;		/*!< UART Over sampling value; usually calculated by using UART_CalBaud() API */
	uint8_t res;		/*!< Reserved for alignment; must be 0 */
} UART_CFG_T;

volatile LPC_USART_T* g_Uart;
static rt_err_t lpc_configure(struct rt_serial_device *serial, struct serial_configure *cfg)
{
    struct lpc_uart *uart;
		
		UART_BAUD_T baud;
		UART_CFG_T UART_cfg;

		int idx;

		
    RT_ASSERT(serial != RT_NULL);
    uart = (struct lpc_uart *)serial->parent.user_data;
		idx = Chip_FLEXCOMM_GetIndex( uart->UART );
		if (idx < 0)
			return idx;
    /* Initialize UART Configuration parameter structure to default state:
     * Baudrate = 115200 b
     * 8 data bit
     * 1 Stop bit
     * None parity
     */
		
		/* Set up baudrate parameters */
		//baud.clk = Chip_Clock_GetAsyncSyscon_ClockRate();	/* Clock frequency */
		if (Chip_Clock_GetFRGClockSource() == SYSCON_FRGCLKSRC_NONE) {
			Chip_Clock_SetFRGClockSource(SYSCON_FRGCLKSRC_FRO12MHZ);
		}
    baud.clk = Chip_Clock_GetFRGInClockRate();
		
		baud.baud = cfg->baud_rate;	/* Required baud rate */
		baud.ovr = 0;	/* Set the oversampling to the recommended rate */
		baud.mul = baud.div = 0;

		if(!baud.mul)
		{
			_UART_CalcMul(&baud);
		}
		_UART_CalcDiv(&baud);
		
//		/* Set fractional control register */
//		LPC_ASYNC_SYSCON->FRGCTRL = ((uint32_t) baud.mul << 8) | 0xFF;
//		
		/* Configure the UART */
		UART_cfg.cfg = UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1; //UART_CFG_8BIT;
		UART_cfg.div = baud.div;	/* Use the calculated div value */
		UART_cfg.ovr = baud.ovr;	/* Use oversampling rate from baud */
//		UART_cfg.res = UART_BIT_DLY(cfg->baud_rate);
		g_Uart = uart->UART;
		/* P254,255,246 */
	  uart->UART->OSR = (UART_cfg.ovr - 1) & 0x0F;
		uart->UART->BRG = (UART_cfg.div - 1) & 0xFFFF;
		uart->UART->CFG = UART_CFG_ENABLE | (UART_cfg.cfg & ~UART_CFG_RES);
		Chip_SYSCON_SetUSARTFRGCtrl(baud.mul, 255);
		Chip_Clock_SetFLEXCOMMClockSource(idx, SYSCON_FLEXCOMMCLKSELSRC_FRG);
		uart->UART->FIFOTRIG = (0<<16)|(1 << 1);
		uart->UART->FIFOINTENSET |= (1<<3);
		
    return RT_EOK;
}


static rt_err_t lpc_control(struct rt_serial_device *serial, int cmd, void *arg)
{
    struct lpc_uart *uart;

    RT_ASSERT(serial != RT_NULL);
    uart = (struct lpc_uart *)serial->parent.user_data;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CLR_INT:
        /* disable rx irq */
				uart->UART->INTENCLR &= ~0x01;
        break;
    case RT_DEVICE_CTRL_SET_INT:
        /* enable rx irq */
        uart->UART->INTENSET |= 0x01;
        break;
    }

    return RT_EOK;
}


static int lpc_putc(struct rt_serial_device *serial, char c)
{
    struct lpc_uart *uart;

    uart = (struct lpc_uart *)serial->parent.user_data;
		//while(!(uart->UART->STAT & (0x01<<2)));
		while (((Chip_UART_GetStatus(uart->UART) & UART_STAT_TXRDY) == 0));
		uart->UART->FIFOWR = c; //TXDAT = c ;  
	
    return 1;
}



static int lpc_getc(struct rt_serial_device *serial)
{
    struct lpc_uart *uart;
		int temp;
    uart = (struct lpc_uart *)serial->parent.user_data;

		if ((Chip_UART_GetFIFOStatus(uart->UART) & UART_FIFOSTAT_RXNOTEMPTY) != 0) {
				temp = uart->UART->FIFORD & 0x000001FF;
				//uart->UART->FIFOCFG |= 1<< 17; 
        return (temp);
		}
    else
        return -1;
}

static const struct rt_uart_ops lpc_uart_ops =
{
    lpc_configure,
    lpc_control,
    lpc_putc,
    lpc_getc,
};

#include "chip.h"
#include "board.h"

/* UART0 device driver structure */
#define USART0_FLEXCOMM 0
struct lpc_uart uart0 =
{
    LPC_USART0,
    USART0_IRQn, // For LPC54110  UART0_IRQn,
};

struct rt_serial_device serial0;

void USART0_IRQHandler(void) //UART0_IRQHandler(void)
{
    volatile  uint32_t  INTSTAT, tmp;
    /* enter interrupt */
    rt_interrupt_enter();
		INTSTAT = (LPC_USART0->FIFOINTSTAT & 0x1F);	

    switch (INTSTAT)
    {
    case 0x08:
        rt_hw_serial_isr(&serial0, RT_SERIAL_EVENT_RX_IND);
        break;
    default :
        tmp = LPC_USART0->INTSTAT;
        break;
    }
		
    /* leave interrupt */
    rt_interrupt_leave();
}


void rt_hw_uart_init(void)
{
    struct lpc_uart *uart;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
	
	
    uart = &uart0;

    serial0.ops    = &lpc_uart_ops;
    serial0.config = config;
    serial0.parent.user_data = uart;
		
		/* Enable IOCON clock  Then your cfg will effective P38 */
// Magicoe		LPC_SYSCON->AHBCLKCTRLSET[0] = (1UL << 13);
		/* Setup UART TX,RX Pin configuration  cfg Pin as Tx, Rx */
		/*	P63,P77
				Selects pin function 1		IOCON_FUNC1
				No addition pin function	IOCON_MODE_INACT
				Enables digital function by setting 1 to bit 7(default)	IOCON_DIGITAL_EN
		*/
// Magicoe		LPC_IOCON->PIO[0][0] = (0x1 | (0x0 << 3) | (0x1 << 7));
// Magicoe		LPC_IOCON->PIO[0][1] = (0x1 | (0x0 << 3) | (0x1 << 7));
	  Chip_IOCON_PinMuxSet(LPC_IOCON, 0, 0, IOCON_MODE_INACT | IOCON_FUNC1 | IOCON_DIGITAL_EN | IOCON_INPFILT_OFF);
	  Chip_IOCON_PinMuxSet(LPC_IOCON, 0, 1, IOCON_MODE_INACT | IOCON_FUNC1 | IOCON_DIGITAL_EN | IOCON_INPFILT_OFF);
	
		/* Enable asynchronous APB bridge and subsystem P30 */
// Magicoe		LPC_SYSCON->ASYNCAPBCTRL = 0x01;
		
		/* The UART clock rate is the main system clock divided by this value P59 */
// Magicoe		LPC_ASYNC_SYSCON->ASYNCCLKDIV = 1;										/* Set Async clock divider to 1 */
		
	  /* Enable peripheral clock(asynchronous APB) to UART0  P57*/
// Magicoe		LPC_ASYNC_SYSCON->ASYNCAPBCLKCTRLSET = (1 << 0x01);
		
		/* Controls the clock for the Fractional Rate Generator used with the USARTs P57*/
// Magicoe		LPC_ASYNC_SYSCON->ASYNCAPBCLKCTRLSET = (1 << 0x0F);		/* Enable clock to Fractional divider */
		
    /* Setup UART */
    LPC_ASSERT(Chip_UART_Init( uart->UART  ) == 0, __FILE__, __LINE__);		

    /* preemption = 1, sub-priority = 1 */
    NVIC_SetPriority(uart->UART_IRQn, ((0x01 << 3) | 0x01));

    /* Enable Interrupt for UART channel */
    NVIC_EnableIRQ(uart->UART_IRQn);

    /* register UART0 device */
    rt_hw_serial_register(&serial0, "uart0",
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_STREAM,
                          uart);
}


/* PRIVATE: Division logic to divide without integer overflow */
static uint32_t _UART_DivClk(uint32_t pclk, uint32_t m)
{
	uint32_t q, r, u = pclk >> 24, l = pclk << 8;
	m = m + 256;
	q = (1 << 24) / m;
	r = (1 << 24) - (q * m);
	return ((q * u) << 8) + (((r * u) << 8) + l) / m;
}

/* PRIVATE: Get highest Over sampling value */
static uint32_t _UART_GetHighDiv(uint32_t val, uint8_t strict)
{
	int32_t i, max = strict ? 16 : 5;
	for (i = 16; i >= max; i--) {
		if (!(val % i)) {
			return i;
		}
	}
	return 0;
}

/* Calculate error difference */
static int32_t _CalcErr(uint32_t n, uint32_t d, uint32_t *prev)
{
	uint32_t err = n - (n / d) * d;
	uint32_t herr = ((n / d) + 1) * d - n;
	if (herr < err) {
		err = herr;
	}

	if (*prev <= err) {
		return 0;
	}
	*prev = err;
	return (herr == err) + 1;
}

/* Calculate the base DIV value */
static ErrorCode_t _UART_CalcDiv(UART_BAUD_T *ub)
{
	int32_t i = 0;
	uint32_t perr = ~0UL;

	if (!ub->div) {
		i = ub->ovr ? ub->ovr : 16;
	}

	for (; i > 4; i--) {
		int32_t tmp = _CalcErr(ub->clk, ub->baud * i, &perr);

		/* Continue when no improvement seen in err value */
		if (!tmp) {
			continue;
		}

		ub->div = tmp - 1;
		if (ub->ovr == i) {
			break;
		}
		ub->ovr = i;
	}

	if (!ub->ovr) {
		return ERR_UART_BAUDRATE;
	}

	ub->div += ub->clk / (ub->baud * ub->ovr);
	if (!ub->div) {
		return ERR_UART_BAUDRATE;
	}

	ub->baud = ub->clk / (ub->div * ub->ovr);
	return LPC_OK;
}

/* Calculate the best MUL value */
static void _UART_CalcMul(UART_BAUD_T *ub)
{
	uint32_t m, perr = ~0UL, pclk = ub->clk, ovr = ub->ovr;

	/* If clock is UART's base clock calculate only the divider */
	for (m = 0; m < 256; m++) {
		uint32_t ov = ovr, x, v, tmp;

		/* Get clock and calculate error */
		x = _UART_DivClk(pclk, m);
		tmp = _CalcErr(x, ub->baud, &perr);
		v = (x / ub->baud) + tmp - 1;

		/* Update if new error is better than previous best */
		if (!tmp || (ovr && (v % ovr)) ||
			(!ovr && ((ov = _UART_GetHighDiv(v, ovr)) == 0))) {
			continue;
		}

		ub->ovr = ov;
		ub->mul = m;
		ub->clk = x;
		ub->div = tmp - 1;
	}
}
