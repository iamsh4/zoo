/* SEGA Dreamcast Japanese boot-ROM v1.004
 *
 * Hacked by Lars Olsson
 *
 * Questions, corrections etc:
 * jlo@ludd.luth.se
 *
 * Notes:
 * ~~~~~~
 *
 * reg[REG] = access to register REG (including both normal
 *            CPU registers aswell as CPU-related memory-mapped
 *            registers, but NOT other hardware registers)
 *
 *
 * Most names have been made up by me and can be very misleading,
 * even to the point of being downright incorrect with regards to
 * their actual functions
 *
 * Beware: this source code is only meant to illustrate the funtion
 * of the BootROM. It is not a 100% translation of the actual BootROM
 * but a number of short cuts and simplifications have been made in order
 * to clarify the operation, which was the purpose of this whole exercise.
 *
 * Compiling this source (in so far it is possible at all) will NOT
 * produce a proper BootROM!
 *
 */

#include "types.h"
#include "bootROM.h"

void
boot()	/* 0xa0000000 - start of bootROM */
{

	uint32_t exception, exception_mask;
	uint16_t *src, *dst;

	exception = reg[EXPEVT];
	exception_mask = exception ^ 0x00000fff;

	if (exception * exception_mask == 0) {
		reg[MMUCR] = 0x00000000;
		reg[CCR]  = 0x00000929;
		reg[WCR1] = (uint16_t)0x0001;
		reg[WCR2] = 0x618066d8;
		reg[BCR2] = 0xa3020008;
		reg[WCR1] = 0x01110111;
		reg[MCR]  = 0x800a0e24;
		*(uint8_t *)0xff940190 = 0x90;	/* write to SDMR */
		reg[RFCR] = (uint16_t)0xa400;
		reg[RTCOR] = (uint16_t)0xa504;
		reg[RTCSR] = (uint16_t)0xa510;

		while((volatile uint16_t)reg[RFCR] <= 0x0010);

		reg[RTCOR] = (uint16_t)0xa55e;
		reg[MCR] = 0xc00a0e24;
		*(uint8_t *)0xff940190 = 0x90;	/* write to SDMR */

		*HW16(0xa05f7480) = 0x0400;

		/* copy small routine to RAM */
		src = (uint16_t *)romcopy;
		dst = (uint16_t *)0x8c0000e0;
		for (i = 0; i < sizeof(romcopy); i++)
			*(dst++) = *(src++);

		/* copy BootROM to RAM and continue executing at boot2(0) */
		(*(void (*)(void *, void *))0x8c0000e0)((void *)0x80000100, (void *)0x8c000100);

	}

	if (exception_mask == 0x0fdf) 	/* soft-reset */
		_8c000018(_ac004000);	/* = return (_ac004000) */

	system_reset();

}

void
exception_handler_100()		/* _8c000100 */
{
	irq_handler2(reg[EXPEVT]);
}

void
system_reset()	/* _a0000116 */
{
	/* Reset the system */
	*HW32(0xa05f6890) = 0x00007611;
	while(1);
}

void
boot2(debug_handler)		/* _8c000120 */
	uint32_t debug_handler;
{

	uint32_t i;

	reg[DBR] = debug_handler;

	init_machine(0);

	/* clear irq callbacks */
	for (i = 0; i < sizeof(irq_callback); i++)
		irq_callback[i] = 0x00000000;

	reg[SR] &= 0xdfffffff;

	/* setup a default debug handler if one isn't already installed */
	if (reg[DBR] >= 0) {
		reg[DBR] = &sysvars->rte_code[0];

		if (*(volatile uint32_t)0xff000030 == 0x00000080) {
			reg[WTCSR] = (uint16_t)0xa500;
			reg[WTCSR] = (uint16_t)0xa507;
			reg[WTCNT] = (uint16_t)0x5a00;
			reg[FRQCR] = (uint16_t)0x0000;
		}
	}

	__asm__("rte\n\t");	/* on power on reset, execution jumps to */
   				/* sys_do_bioscall(-3) = boot3() */
}

void
RTE()	/* 8c000170 */
{
	_8c000010();		/* rte */
}

void
sys_do_bioscall2(func)		/* _ac000178 */
	sint32_t func;
{

	func += 3;
	if (func > 7)
		func = 7;

	(*sys_callback[func])(func - 3);	

}

void
syBtExit(mode)		/* _8c0002c8 */
	sint32_t command;
{
   
	uint32_t i;
	uint64_t *src, *dst;

	/* turn off cache if it is enabled; details omitted */

	reg[SR]  = 0x700000f0;
	reg[GBR] = 0x8c000000;
	sysvars->select_menu = command;
	reg[R15] = 0x8d000000;
	reg[VBR] = _8c000000;

	flush_cache();

	reg[MMUCR] = 0x00000000;
	reg[CCR] = 0x00000929;

	*HW32(0xa05f6938) = 0x00000000;
	*HW32(0xa05f6934) = 0x00000000;
	*HW32(0xa05f6930) = 0x00000000;
	reg[IPRC] = (uint16_t)0x0000;
	*HW32(0xa05f6928) = 0x00000000;
	*HW32(0xa05f6924) = 0x00000000;
	*HW32(0xa05f6920) = 0x00000000;
	reg[IPRB] = (uint16_t)0x0000;
	*HW32(0xa05f6918) = 0x00000000;
	*HW32(0xa05f6914) = 0x00000000;
	*HW32(0xa05f6910) = 0x00000000;
	reg[IPRA] = (uint16_t)0x0000;
	reg[ICR] = (uint16_t)0x0000;

	(void)*HW32(0xa05f6908);
	(void)*HW32(0xa05f6900);

	/* disable display */
	*HW32(0xa05f8044) &= 0xfffffffe;

	/* reset rendering and registration */
	*HW32(0xa05f8008) = 0x00000003;
   
	/* disable PVR DMA */
	*HW32(0xa05f6808) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*(volatile uint32_t *)0xa05f6808 == 0x01)
			break;

	/* ??? (other direction perhaps?) */
	*HW32(0xa05f6820) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f6820) == 0x01)
			break;

	/* disable Maple DMA */
	*HW32(0xa05f6c14) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f6c14) == 0x01)
			break;

	/* disable GDROM DMA */
	*HW32(0xa05f7414) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7414) == 0x01)
			break;

	/* disable SPU DMA */
	*HW32(0xa05f7814) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7814) == 0x01)
			break;

	/* disable expansion port 1 DMA */
	*HW32(0xa05f7834) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7834) == 0x01)
			break;

	/* disable expansion port 2 DMA */
	*HW32(0xa05f7854) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7854) == 0x01)
			break;

	/* disable expansion port 3 DMA */
	*HW32(0xa05f7874) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7874) == 0x01)
			break;

	/* ??? */
	*HW32(0xa05f7c14) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f7c14) == 0x01)
			break;

	/* ??? */
	*HW32(0xa05f001c) &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (*HW32(0xa05f001c) == 0x01)
			break;

	reg[CHCR1] &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (reg[CHCR1] == 0x01)
			break;

	reg[CHCR2] &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (reg[CHCR2] == 0x01)
			break;

	reg[CHCR3] &= 0xfffffffe;
	for (i = 0; i < 0x7f; i++)
		if (reg[CHCR3] == 0x01)
			break;

	/* initialize ROM checksum */
	*HW32(0xa05f74e4) = 0x001fffff;

	/* switch to double precision (for 64bit copying below) */
	reg[FPSCR] = 0x00140001;

	/* Copy various parts from ROM to RAM */
	/* This is done for two reasons:
	 * First, code and data must be available to be executed from RAM
	 * secondly, the data read from ROM are passed through some type
	 * of "checksum". Unless a specific value is computed, a flag is
	 * set to disable the GDROM drive. Sega must have put this in there
	 * to "discourage" people from replacing the BootROM with their custom
	 * versions!
	 */
	src = (uint64_t *)0xa0000000;
	dst = (uint64_t *)0x8c000000;
	for (i = 0; i < 32; i++) {	/* skip the system variables */
		(void)*src++;		
		dst++;
	}
	for (i = 0; i < 2016; i++) 	/* copy a0000100-a0004000 to 8c000100 */
		*dst++ = *src++;

	for (i = 0; i < 2048; i++) {
		(void)*(src++);
		dst++;
	}
	for (i = 0; i < 258048; i++) 	/* copy a0008000-a0200000 to 8c008000 */
		*(dst++) = *(src++);

	/* back to single precision */
	reg[FPSCR] = 0x00040001;

	boot2(reg[DBR]);

}

void
exception_handler_400()	/* _8c000400 */
{
	irq_handler2(reg[EXPEVT]);
}

void
_8c000408()
{
	sw = sysvars->debug_switches.u0.switches.unknown0;
	if (sw != -1 && sw != '1')
		reg[PR] = _8c000000;	/* go to sleep */
}

void
boot3()		/* _8c000420 */
{

	reg[GBR] = (uint32_t *)0x8c000000;

	boot4();
	wait_timer();
	wait_timer();

	reg[SR] |= 0x000000f0;

	flush_cache();

	reg[MMUCR] = 0x00000000;
	reg[CCR] = 0x00000800;

	/* correct code in IP.BIN */
	for (i = 0; i < sizeof(patch_data); i++) {
		patch_data[i]->addr = patch_data[i].opcode;
	}

	/* start executing IP.BIN */
	_ac008300();

}

void
copy_security_stuff()	/* _8c000462 */
{

	unsigned int *src, *dst;
	unsigned int old_SR, i;

	src = _a0004300;
	dst = _8ce00000;
	for (i = 0; i < 0x0400; i++) {
		*(dst++) = *(src++);
	}

	flush_cache();

}

void
flush_cache()	/* _8c000472 */
{

	/* turn off cache; details omitted */

	old_SR = reg[SR];
	reg[SR] = reg[SR]|0x00f0;

	if (reg[CCR] & 0x20)
		i = 0x1000;
	else
		i = 0x2000;

	while(i) {
		i -= 0x20;
		*(_f4002000 + (i & 0xfffffffd)) = 0;
		*(_f4000000 + (i & 0xfffffffd)) = 0;
	}

	__asm__(
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t");

		reg[SR] = old_SR;

}

void
clear_sector_buf(buf)		/* _8c0004d8 */
	unsigned int *buf;
{

	unsigned int i;

	for (i = 0; i < 0x80; i++) {
		*(buf++) = 0x00000000;
		*(buf++) = 0x00000000;
		*(buf++) = 0x00000000;
		*(buf++) = 0x00000000;
	}
}

int
process_IP(ip_buf)	/* _8c0004f4 */
	ip_t *ip_buf;
{
	if (memcmp(ip_buf->hardware_ID, "SEGA SEGAKATANA ", 16) != 0)
		return (-1);

	if (memcmp(ip_buf->code, (uint8_t *)0xa0008000, 0x3400) != 0)
		return (-1);

	if (memcmp(ip_buf->area, (uint8_t *)0xa000b400, 0x20) != 0)
		return (-1);

	if (ip_buf->country_codes[0] != 'J')
		return (-1);

	sysvars->boot_file = ip_buf->boot_file;
	sysvars->OS_type = convert_WinCE(ip_buf->WinCE);
	sysvars->display_cable &= (convert_VGA(ip_buf->VGA) - 2);

	return (0);
}

int
memcmp(s1, s2, n)	/* _8c000548 */
	uint8_t *s1, *s2;
	int n;
{
	while(*(s1++) == *(s2++) && --n);
	return (n);
}

char
ascii2char(digit)	/* _8c00055e */
	uint8_t digit;
{
	if (digit =< 0x39)
		return (digit - 0x30);
	return (digit - 0x37);
}

char
convert_VGA(digit)	/* _8c00056c */
	uint8_t digit;
{
	if (digit == 0)
		return (1);
	if (ascii2char(digit) & 0x01 == 0)
		return (0);
	return (1);
}

char
convert_WinCE(digit)	/* _8c000570 */
	uint8_t digit;
{
	if (digit == 0)
		return (0);
	if (ascii2char(digit) & 0x01 == 0)
		return (0);
	return (1);
}

int
check_ISO_PVD(type, id)	/* _8c000590 */
	uint8_t type;
	uint8_t *id;
{
	if (type != 1)
		return (6);
	return (memcmp(id, "CD001", 5));
}

void
check_filename(s1, s2)	/* _8c000598 */
	uint8_t *s1, *s2;
{
	if (memcmp(s1, s2, 12) == 0)
		return (0);

	if (s2[0] != 0x20)
		return (-1);

	if (s1[0] != 0x3b)
		return (-1);

	return (0);

}

void
enter_gd_params(src)		/* _8c0005b8 */
	uint32_t *src;
{
	uint32_t *dst = &sysvars->gd_param4;

	/* fill in gd params and command in reverse order */
	for (i = 0; i < 5; i++)
		*(dst--) = *(src++);
}

void
irq_handler()		/* _8c000600 */
{
	irq_handler2(reg[INTEVT]);
}   

void
irq_handler2(interrupt)		/* _8c000606 */
	uint32_t interrupt;
{
	uint32_t irq;

	irq = (interrupt>>5)-2;
	if (irq >= 0x41)
		RTE();

	if (irq_callback[irq] == NULL)
		RTE();

	/* save various system registers; details omitted */
	reg[SR] = reg[SR] & 0xdfffffff;
	/* save registers details omitted */
	reg[FPSCR] = 0x00140001;
	/* save floating-point registers; details ommitted */

	(*irq_callback[irq])();

	reg[FPSCR] = 0x00140001;
	/* restore all registers; details omitted */

	__asm__("rte\n\t");

}

void
clear_IrqCallbacks()	/* _8c0006aa */
{
	sysvars->irq_sem0 = 0;
	sysvars->irq_sem1 = 0;
	sysvars->irq_sem2 = 0;
	sysvars->irq_sem3 = 0;
	sysvars->irq_callback0 = NULL;
	sysvars->irq_callback0 = NULL;
	sysvars->irq_callback0 = NULL;
	sysvars->irq_callback0 = NULL;
}

void
do_IrqCallbacks()	/* _8c0006c0 */
{
	uint32_t old_SR = reg[SR];
	uint8_t *sem = &sysvars->irq_sem0;
	uint32_t *cb = sysvars->irq_callback0;

	reg[GBR] = _8c000000;

	/* atomic test and set */
	if (*sem != 0) {
		*sem = 0x80;
		return;
	}
	*sem = 0x80;

	reg[SR] = reg[SR]&0xefffff0f;

	for (i = 0; i < 4; i++) {
		if (cb != NULL)
			(*cb)();
		sem++;
		cb++;
		/* atomic test and set */
		if (*sem != 0) {
			*sem = 0x80;
			break;
		}
		*sem = 0x80;
	}
	*(--sem) = 0;

	reg[SR] = old_SR;

}

void
_8c000728()
{
	_8c00073e(0x10);
}

void
_8c000730()
{
	_8c00073e(0x20);
}

void
_8c000738()
{
	_8c00073e(0x30);
}

void
_8c00073e(irq)
	uint32_t irq;
{
	*HW32(0xa05f6900) &= *HW32(0xa05f6900 + irq);
	*HW32(0xa05f6908) &= *HW32(0xa05f6908 + irq);

	(void)*HW32(0xa05f6908);

	do_IrqCallbacks();
}

void
set_IrqCallback1()		/* _8c000768 */
{
	sysvars->timer_count = 0;
	sysvars->irq_callback0 = (uint32_t *)increase_TimerCount;
}

void
wait_timer()	/* _8c000772 */
{
	uint32_t old_timer = sysvars->timer_count;

	while (sysvars->timer_count == old_timer);
}

void
increase_TimerCount()	/* _8c000780 */
{
	sysvars->timer_count++;
}

void
sys_do_bioscall(func)	/* _ac000800 */
{
	sys_do_bioscall2(func);
}

void
_8c000804()
{
	wait_timer();
}

uint32_t
toggle_endian(data)		/* _8c00080a */
	uint8_t *data;
{
	uint32_t i;
	uint8_t buf[4];

	for (i = 0; i < 4; i++)
		buf[i] = data[3-i];
	return (*(uint32_t *)buf);
}

void
boot5()		/* _8c000820 */
{

	if (sysvars->gd_param3 == 0) {
		clear_IrqCallback3();
		if (sysvars->OS_type == 2) {
			security_stuff(2);
		} else {
			security_stuff(0);
		}
	} else {
		reg[SR] = reg[SR] & 0xefffff0f;
		sysvars->gd_unknown1 = 0x000a;
		sysvars->gd_unknown0 = 0x0258;

		if (exec_GdCmd(1) == 1) {
			clear_IrqCallback3();
			sys_do_bioscall(1);	/* syBtExit(1) */
		}
		clear_IrqCallback3();
	}

	sysvars->gdhn = 0;
	*_a05f8040 = 0x00c0bebc;	/* set border color to light grey */
	sysvars->current_color = 0x00c0bebc;

}

void
_8c00087a()
{

	int i, result = 0, dummy;
	unsigned int *hwreg = _a05f6904;
	unsigned int old_SR = reg[SR];

	reg[SR] = reg[SR] | 0x00f0;

	*OldGdVector2 = *GdVector2;
	*GdVector2 = _8c001168;

	for (i = 0; i < 3; i++) {
		hwreg += 0x0c;
		result = result | ((*hwreg & 0x00004000)<<i);
		*hwreg = *hwreg & 0x0000bfff;
		hwreg++;
		result = result | ((*hwreg & 0x00000001)<<i);
		*hwreg = *hwreg & 0xfffffffe;
	}
	dummy = *hwreg;

	*_8c000098 = result;

	gdGdcInitSystem(0, 0);  /* gd_do_bioscall(0, 3, 0, 0) */;

	reg[SR] = old_SR;

}

void
_8c0008e0()
{

	unsigned int *irqreg;
	unsigned int old_SR = reg[SR];

	reg[SR] = reg[SR] & 0x000000f0;

	*GdVector2 = *OldGdVector2;		/* why mess with this here? */

	irqreg = _a05f6910;
	for (i = 0; i < 3; i++) {
		*irqreg = *irqreg | ((*_8c000098 >> (i+1)) & 0x00004000);
		irqreg += 4;
		*irqreg = *irqreg | ((*_8c000098 >> (i+1)) & 0x00000001);
		irqreg += 0x0c;
	}

	dummy = *_a05f6934;

	*_a05f6900 = *_a05f6900 & 0x00004000;

	reg[SR] = old_SR;

}

int
check_GdDrvStatus()		/* _8c00095c */
{

	switch (sysvars->gd_drv.stat) {
		case STAT_OPEN:
		case STAT_NODISK:
		case STAT_ERROR:
			return (-1);
			break;
		default:
			return (0);
	}

}

int
check_ipvector_media()		/* _8c000978 */
{
	if (sysvars->IP_vector != (uint32_t *)0x8c008000) {
		return (0);
	}

	if (sysvars->gd_drv.media == TYPE_XA) {
		return (0);
	}

	return (1);
}

int
syBtCheckDisc()	/* _8c000900 */
{

   int disc;		/* R14 */
   unsigned int old_GBR = reg[GBR];

   reg[GBR] = _8c000000;

   if (*DiscType < 0) {
      gdGdcExecServer();
   }
   /* _8c0009ae */

   switch (*DiscType) {
      case -8:
	 /* _8c000a98 */
	 if (process_IP(*IPVector) == 0) {
	    disc = 0;
	 } else {
	    disc = 126;
	 }
	 break;

      case -7:
      case -4:
	 /* _8c000a14 */
	 result = exec_GdCmd3(1);
	 /* this is fucked...rewrite this later */
	 if (result == -2 || result != 0) {
	    disc -= 2;
	    if (result == 1) {
	       if (check_GdDrvStatus() == -1) {
		  disc = -1;
	       } else {
		  switch (*GdStatus) {
		     case 2:
			/* _8c000a72 */
			disc += 2;
			break;

		     case 6:
			/* _8c000a6e */
			disc = -2;
			break;

		     case 16:
			/* _8c000a76 */
			if (check_ipvector_media() != 0) {
			   disc = 0;
			}
			break;

		     default:
			/* _8c000a82 */
			disc = -1;
			break;
		  }
	       }
	    }
	 }
	 break;

      case -6:
      case -3:
	 /* _8c0009fe */
	 result = _8c00ec8(0);
	 if (result == 1) {
	    disc = -1;
	 } else {
	    if (result == 0) {
	       *_8c000044 = 0x0258;
	       disc--;
	    }
	 }
	 break;

      case -5:
	 /* _8c000a86 */
	 enter_gd_params(_8c000fc4);	/* GdParam4 = 0x00000000
					   GdParam3 = 0x8c008000
					   GdParam2 = 0x00000007
					   GdParam1 = 0x0000b05e
					   GdCmd = CMD_DMAREAD
					*/
	 *GdParam3 = *IPVector;
	 *_8c000044 = 0x0268;
	 disc = -6;
	 /* _8c0009fe */
	 result = exec_GdCmd3(0);
	 if (result == 1) {
	    disc = -1;
	 } else {
	    if (result == 0) {
	       *_8c000044 = 0x0258;
	       disc--;
	    }
	 }
	 break;

      case -2:
	 /* _8c0009f6 */
	 *GdCmd = 24;
	 *_8c000044 = 0x0258;
	 disc = -3;
	 result = exec_GdCmd3(0);
	 if (result == 1) {
	    disc = -1;
	 } else {
	    if (result == 0) {
	       *_8c000044 = 0x0258;
	       disc--;
	    }
	 }
	 break;

      case -1:
	 /* _8c0009e4 */
	 if (*GdStatus > 0) {
	    disc = *GdStatus;
	 } else {
	    disc = 127;
	 }
	 break;

      default:
	 _8c00087a();
	 *DisplayCable |= 0x01;
	 *_8c000046 = 0x000a;
	 disc = -1;
	 break;
   }

   /* _8c000aaa */

   if (disc < *OldDiscType)
      disc = 0;

   if (disc < 0)
      _8c0008e0();

   *DiscType = disc;

   reg[GBR] = old_GBR;

   return (disc);

}





int
syBtCheckDisc()	/* _8c000990 */
{

   short disc;
   int result;
   unsigned int old_GBR = reg[GBR];

   reg[GBR] = _8c000000;

   disc = (short)*DiscType;
   if (disc < 0) {
      _8c000c86();
   }

   switch (disc) {
      case -8:
	 /* _8c000a98 */
	 if (process_IP(*IPVector) == 0) {
	    disc = 0;
	 }
	 else {
	    disc = 0x7e;
	 }
	 break;

      case -7:
      case -4:
	 /* _8c000a14 */
	 result = exec_GdCmd3(1);
	 if (result != -2) {
	    /* _8c000a2c */
	    if (result == 0) {
	       if (*GdMedia != TYPE_GDROM) {
		  if (check_ipvector_media() == 0) {
		     /* _8c000a48 */
		     result = 1;
		  }
		  else {
		     disc = 0;
		  }
		  /* _8c000a4a */
	       }
	       else {
		  disc--;
	       }
	    }
	 }
	 /* _8c000a4a */
         if (result == 1) {
	    if (check_GdDrvStatus() == -1) {
	       disc = -1;
	    }
	    else {
	       switch (*GdStatus) {
		  case 2:
		     disc += 2;
		  break;
		  case 6:
		     disc = -1;
		     break;
		  case 16:
		     if (check_ipvector_media() != 0) {
		        disc = 0;
		     }
		     break;
		  default:
		     disc = -1;
		     break;
	       }
	    }
	 }
	 break;

      case -6:
      case -3:
	 /* _8c0009fe */
	 result = exec_GdCmd3(0);
	 if (result == 1) {
	    disc = -1;
	 }
	 else {
	    if (result == 0) {
	       *_8c000044 = (short)0x0258;
	       disc--;
	    }
	 }
	 break;

      case -5:
	 /* _8c000a86 */
	 enter_gd_params(_8c000fc4);	/* GdParam4 = 0x00000000
					   GdParam3 = 0x8c008000
					   GdParam2 = 0x00000007
					   GdParam1 = 0x0000b05e
					   GdCmd = CMD_DMAREAD
					 */
	 *GdParam3 = *IPVector;
	 *_8c000044 = 0x258;
	 disc = -6;
	 result = exec_GdCmd3(0);
	 if (result == 1) {
	    disc = -1;
	 }
	 else {
	    if (result == 0) {
	       *_8c000044 = 0x258;
	       disc--;
	    }
	 }
	 break;

      case -2:
         /* _8c0009f6 */
	 *GdCmd = 24;
	 *_8c000044 = 0x258;
	 disc = -3;
	 result = exec_GdCmd3(0);
	 if (result = 1) {
	    disc = -1;
	 }
	 else {
	    if (result == 0) {
	       *_8c000044 = 0x258;
	       disc--;
	    }
	 }
	 break;

      case -1:
	 /* _8c0009e4 */
	 if (*GdStatus > 0) {
	    disc = *GdStatus;
	 }
	 else {
	    disc = 0x7f;
	 }
	 break;

      default:
	 /* _8c0009d6 */
	 _8c00087a();
	 *DisplayCable |= 0x01;
	 *_8c000046 = 0x000a;
	 disc = -1;
	 break;
   }
   /* _8c000aaa */

   if (disc < *OldDiscType) {
      disc = 0;
   }
   if (disc > 0) {
      _8c0008e0();
   }

   *DiscType = disc;

   reg[GBR] = old_GBR;

   return (disc);

}

void
boot4()		/* _8c000ae4 */
{

	uint32_t delayed_cmd;	/* R13 */
	uint32_t CD_boot;	/* R11 */
	uint32_t sct;	/* R10 */

	uint32_t sector_of_PVD;

	/* disable interrupts */
	reg[SR] = (reg[SR] & 0xefffff0f) | 0x00f0;

	gdGdcInitSystem(0, 0);

	sysvars->old_disc_type = -128;

	delayed_cmd = INIT;		/* R13 */

	if (sysvars->select_menu == OPENMENU) {
		sysvars->menu_param = 1;
		sysvars->display_cable |= 1;
		/* _8c000b9c */
		sysvars->IP_vector = (uint32_t *)0x8c008000;
		sysvars->old_disc_type = -128;
		_8c00c000(OPENMENU);
		flush_cache();
		if (sysvars->gd_drv.media == TYPE_XA
				|| sysvars->gd_stat.stat0 == 0x10)
			sys_do_bioscall(4);	/* no_return() */
		sys_do_bioscall(3);		/* syBtExit(3) */
	}
	else if (sysvars->select_menu != OPENCDMENU) {
		sysvars->menu_param = 0;
		_8c00c000(INIT);
		_8c00c000(UNKNOWN1);
		delayed_cmd = UNKNOWN2;
		sysvars->IP_vector = (uint32_t *)0x8c008000;
	}

	/* _8c000b36 */

	set_interrupts();
	wait_timer();
	CD_boot = -1;
	copy_security_stuff();
	CheckDisc(-4);

	if (sysvars->gd_drv.media == TYPE_XA) {
		sysvars->irq_callback2 = NULL;
		sct = security_stuff(1);
		if (sct != 0) {
			CD_boot = 0;
			if (process_IP(sysvars->IP_vector) == 0) {
				sysvars->OS_type = 0x02;
				set_IrqCallback3();
				goto check_passed;
			}
		}
	}
	/* _8c000b74 */

	set_IrqCallback3();
	sysvars->old_disc_type = -128;
	syBtCheckDisc();
	sysvars->disc_type = -5;
	newdisc = CheckDisc(-128);
	set_IrqCallback3();
	if (newdisc == 0)
		goto check_passed;

check_failed:
	clear_IrqCallback3();

	reg[SR] = (reg[SR] & 0xefffff0f) | 0x00000f0;

	sysvars->IP_vector = (uint32_t *)0x8c008000;

	if (delayed_cmd >= 0) {
		_8c00c000(delayed_cmd);
		_8c00c000(SETDATE);
	}
	delayed_cmd = INIT;
	sysvars->old_disc_type = -128;
	_8c00c000(OPENMENU);
	flush_cache();

	if (sysvars->gd_drv.media == TYPE_XA
			|| sysvars->gd_stat.stat0 == 0x10)
		sys_do_bioscall(4);		/* no_return() */
	/* _8c000bcc */
	sys_do_bioscall(3);		/* syBtExit(3) */

check_passed:
	if (sysvars->display_cable == 0)
		goto check_failed;

	if (delayed_cmd >= 0) {
		_8c00c000(delayed_cmd);
		_8c00c000(SETDATE);
	}
	/* 8c000bea */
	flush_cache();

	enter_gd_params((uint32_t *)0x8c000fd8); /* GdParam4 = 0x00000000
					   GdParam3 = 0x8c00b800
					   GdParam2 = 0x00000009
					   GdParam1 = 0x0000b065
					   GdCmd = CMD_DMAREAD
					*/

	if (CD_boot == 0) {
		sysvars->gd_param1 = sct + 7;
	}

	if (exec_GdCmd2() != 0) {
		sys_do_bioscall(1);		/* syBtExit(1) */
	}

	if (CD_boot == 0) {
		PVD_sector = sct + 16;
	} else {
		PVD_sector = 45166;
	}

	if (load_BootFile((uint32_t *)0x8c010000, PVD_sector) == 0)
		sys_do_bioscall(1);		/* syBtExit(1) */

	sysvars->old_disc_type = -128;
	sysvars->IP_vector = (uint32_t *)0x8c008100;

}

void
set_interrupts()		/* _8c000c3e */
{
	reg[SR] = reg[SR] | 0x100000f0;
	reg[VBR] = _8c000000;
   
	clear_IrqCallbacks();		/* clear _8c000080 to _8c00008c */

	/* set interrupt vector */
	*(uint32_t *)0x8c000234 = (uint32_t *)0x8c000728;
	*HW32(0xa05f6910) = 0x00000008;
	(void)*HW32(0xa05f6910);

	reg[SR] = reg[SR] & 0xefffff0f;

	set_IrqCallback1();

}

uint32_t
clear_IrqCallback3()			/* _8c000c6c */
{
	reg[SR] |= 0x100000f0;
	sysvars->irq_callback3 = NULL;
	*HW32(0xa05f6910) = 0x00000000;
	return (*HW32(a05f6910));
}

void
set_IrqCallback3()		/* _8c000c80 */
{
	sysvars->irq_callback3 = (uint32_t *)irq_ExecServer;
}

void
irq_ExecServer()		/* _8c000c86 */
{
	gd_do_bioscall(0, 2, 0, 0);	/* gdGdcExecServer(0, 0) */
}

void
irq_InitSystem()		/* _8c000c92 */
{
	gd_do_bioscall(0, 3, 0, 0);	/* gdGdcInitSystem(0, 0) */
}

void
_8c000cfc()
{
	sysvars->gd_unknown0 = 0x0258;
}

int
CheckDisc(olddisc)	/* _8c000d02 */
	sint32_t old_disc;
{
	sint32_t new_disk;

	sysvars->old_disc_type = old_disc;
	while (1) {
		wait_timer();
		new_disk = syBtCheckDisc();
		if (new_disk >= 0)
			break;
	}
	return (new_disk);
}

void
load_BootFile(buf, sct)		/* _8c000d1c */
	uint8_t *buf;
	uint32_t sct;
{

	struct primary_iso_descripter *vol_desc;
	struct iso_directory_record *dir_record;
	uint32_t extent, size;
	uint8_t old_char;

	clear_sector_buf(buf);

	/* GD params to read PVD */
	sysvars->gd_cmd = CMD_DMAREAD;
	sysvars->gd_param1 = sct;
	sysvars->gd_param2 = 1;
	sysvars->gd_param3 = buf;
	sysvars->gd_param4 = 0;

	if (exec_GdCmd2() != 0) 	/* read PVD */
		return (0);

	vol_desc = (struct primary_iso_descriptor *)buf;

	if (check_ISO_PVD(vol_desc->type, vol_desc->id) != 0) {
		return (0);
	}

	extent = toggle_endian(&vol_desc->root_directory_record.extent2);

	/* GD params to read root directory */
	sysvars->gd_param1 = extent + 150;	/* lead-in added */
	sysvars->gd_param2 = 1;
	sysvars->gd_param3 = buf;
	sysvars->gd_param4 = 0;
	sysvars->gd_cmd = CMD_DMAREAD;

	exec_GdCmd2();			/* read root directory */

	dir_record = (struct iso_directory_record *)buf;

	while (dir_record->length != 0 && dir_record < buf+2048) {
		if (dir_record->flags & 0x02 == 0) {
			old_char = dir_record->name[dir_record->name_len];
			dir_record->name[dir_record->name_len] = ';';
			if (check_filename(dir_record->name, *BootFile) == 0) {
				size = toggle_endian(dir_record->size2);
				extent = toggle_endian(dir_record->extent2)
					+ 150;
				if (extent < 450000)	/* 100 min check */
					/* check for backdoor */
					if (sysvars->OS_type != 0x02)
						sys_do_bioscall(1); /*syBtExit*/

				sysvars->gd_param1 = extent;
				sysvars->gd_param2 = size/2048;
				if (OS_type == 0) {
					sysvars->gd_param3 = buf;
					sysvars->gd_param4 = 0;
				} else {
					sysvars->gd_param3 = 0;
					sysvars->gd_param4 = size;
				}
				sysvars->gd_unknown1 = 0x000a;
				sysvars->gd_unknown2 = 0x0258;

				if (sysvars->OS_type == 0) {
					sysvars->gd_cmd = CMD_DMAREAD;
				} else {
					sysvars->gd_cmd = CMD_PIOREAD;
				}
				exec_GdCmd(0);
				_8c000804();
				return (size);
			}
			dir_record->name[dir_record->name_len] = old_char;
		}
		dir_record = dir_record + dir_record->length;
	}
	return (0);

}

uint32_t
exec_GdCmd(mode)		/* _8c000e7c */
	uint32_t mode;
{

	uint32_t result;

	do {
		wait_timer();
	} while ((result = exec_GdCmd3(mode)) < 0);

	return (result);

}

uint32_t
exec_GdCmd2()		/* _8c000e98 */
{

	uint32_t result;

	sysvars->gd_unknown1 = 0x000a;

	while(1) {
		sysvars->gd_unknown0 = 0x0258;
		result = exec_GdCmd(0);
		if (result != 0)
			return (result);

		while(1) {
			sysvars->gd_unknown0 = 0x0258;
			wait_timer();		/* _8c000804() */
			result = exec_GdCmd3(1);
			if (result == -2)
				break;		/* break inner loop */
			if (result >= 0)
				return (result);
		}
	}

}

uint32_t
exec_GdCmd3(mode)	/* _8c000ec8 */
	uint32_t mode;
{

	uint32_t result = 1;

	switch (mode) {
		case 0:
			sysvars->gdhn = gdGdcReqCmd(*GdCmd, GdParam1);
			if (sysvars->gdhn != 0) {
				result = 0;
			}
			break;

		default:
			sysvars->gd_cmd_stat = gdGdcGetCmdStat(sysvars->gdhn,
					sysvars->gd_stat);
			switch (sysvars->gd_cmd_stat) {
				case 0:
				case 2:
					result = 0;
					break;
				case 1:
				case 3:
					result = -1;
					break;
				case -1:
					sysvars->gd_unknown0 = 0x0000;
					switch (sysvars->gd_stat.stat0) {
						case 0:
						case 1:
						case 2:
						case 5:
						case 6:
						case 11:
						case 16:
							/* _8c0000f3c */
							sysvars->gd_unknown1 = 0x0000;
							break;
						default:
							break;
					}
					break;

				default:
					break;

			}
			/* _8c000f58 */
			if (result != -1) {
				if (gdGdcGetDrvStat(sysvars->gd_drv, 0) != 0) {
					if (check_GdDrvStatus() == -1) {
						sysvars->gd_unknown0 = 0x0000;
						sysvars->gd_unknown1 = 0x0000;
					}
				}
			}
			break;

	}
	/* _8c000f80 */

	if (result == -1)
		return (-1);

	if (sysvars->gd_unknown0 != 0) {
		sysvars->gd_unknown0--;
		return (-1);
	}

	if (sysvars->gd_unknown1 != 0) {
		sysvars->gd_unknown1--;
		sysvars->gd_unknown0 = 0x0258;
		return (-1);
	}
	return (result);

}

int
_8c001000(arg1, arg2, func1, func2)
{

   if (func1 == -1) {
      return (init_bioscall_vectors(arg1, arg2, func1, func2));
   }

   if (func1 >= 8) {
      return (-1);
   }

   return ((*_8c0000c0[func1])(arg1, arg2, func1, func2));	/* func1 = 0 gives gd_do_bioscall */

}

int
init_bioscall_vectors(arg1, arg2, value, cmd)	/* _8c001020 */
   unsigned int arg1, arg2, value, cmd;
{

   unsigned int *dst = _8c0000c0;

   switch (cmd) {
      case 0:			/* gdBtGdc(Re)InitEntry */
	 for (i = 0; i < 7; i++)
	    *(dst++) = 0x00000000;	/* clear _8c0000c0 to _8c0000dc */

	 *GdVector2  = gd_do_bioscall;
	 *GdVector   = _8c001000;
	 *FlVector   = _8c003d00;
	 *FntVector  = _8c003b80;
	 *KcfgVector = _8c003c00;

	 *GdBaseReg = _a05f7000;
	 *_8c0000a8 = _a0200000;
	 *_8c0000a4 = _a0100000;
	 *_8c0000a0 = 0x00000000;

	 *_8c00002e = 0x0000;
	 *_8c00002d = 0x00;
	 *SystemVector = sys_do_bioscall;

	 return (0);
	 break;

      case 1:		/* gdBtGdcAddDesc */
	 if (arg1 > 7) {
	    return (-1);
	 }
	 
	 if (arg2 == 0) {
	    _8c0000c0[arg1] = 0;
	    return (0);
	 }

	 if (_8c0000c0[arg1] == 0) {
	    _8c0000c0[arg1] = arg2;
	    return (0);
	 }
	 return (-1);
	 break;

      default:
         return (-1);
	 break;
   }

}

int *
_8c0010b0(init)
   int init;
{

   if (init != 0)
   {
      return (*_8c000050);
   }

   *_8c00005c = _8c3002e8;
   *_8c000058 = _8c010b42;
   *_8c000054 = _8c010b6e;
   *_8c000050 = _8c010b58;

}

void
gd_do_bioscall(func1, func2, arg1, arg2)	/* _8c0010f0 */
   int func1, func2, arg1, arg2;
{

   if (func2 > 16)
   {
      return;
   }

   (*_8c001180[func2])(arg1, arg2);

}

int
get_GdBaseReg()	/* _8c001108 */
{
   return (*GdBaseReg);
}

int
_8c001118(mode)
	int mode;
{

	if (mode != 0) {
		*_8c00002d = 0x01;
		if (*_8c00002e > 0x0000) {
			*_8c00002d = 0x00;
			return (-1);
		} else {
			return (0);
		}
	}
	/* _8c001142 */
	*_8c00002d = 0x00;

}

void
gd_do_cmd(param, my_gds, cmd)	/* _8c0011ec */
   int *param;
   GDS *my_gds;
   int cmd;
{

   if (cmd > 48) {
      return;
   }

   (*gd_cmd[cmd])(param, my_gds);

}

GDS *
get_GDS()		/* _8c0012de */
{
   return (gd_gds);
}

int
gdGdcInitSystem()		/* _8c001890 */
{

   *_8c001994 = 1;

   *reg[--R15] = reg[PR];
   *reg[--R15] = reg[MACH];
   *reg[--R15] = reg[MACL];
   *reg[--R15] = reg[R14];
   *reg[--R15] = reg[R13];
   *reg[--R15] = reg[R12];
   *reg[--R15] = reg[R11];
   *reg[--R15] = reg[R10];
   *reg[--R15] = reg[R9];
   *reg[--R15] = reg[R8];

   *_8c00198c = reg[R15];
   *_8c001990 = _8c001acc;

   return (_8c003570());

}

void
_8c0018c0()
{

   int i;
   int *ptr;

   i = (*_8c00198c - reg[R15])>>2;
   ptr = *_8c001990;

   *(--ptr) = reg[PR];
   *(--ptr) = reg[MACH];
   *(--ptr) = reg[MACL];
   *(--ptr) = reg[R14];
   *(--ptr) = reg[R13];
   *(--ptr) = reg[R12];
   *(--ptr) = reg[R11];
   *(--ptr) = reg[R10];
   *(--ptr) = reg[R9];
   *(--ptr) = reg[R8];

   for (j = 0; j != i; j++) {
      *(--ptr) = *reg[R15+];
   }

   *(--ptr) = i;

   *_8c001990 = ptr;

   reg[R8] = *reg[R15++];
   reg[R9] = *reg[R15++];
   reg[R10] = *reg[R15++];
   reg[R11] = *reg[R15++];
   reg[R12] = *reg[R15++];
   reg[R13] = *reg[R15++];
   reg[R14] = *reg[R15++];
   reg[MACL] = *reg[R15++];
   reg[MACH] = *reg[R15++];
   reg[PR] = *reg[R15++];

   *_8c001994 = 0;

}

int
gdGdcExecServer()		/* _8c001918 */
{

   int size;
   int *addr;

   /* the following is atomic */
   if (*_8c001994 != 0) {
      *_8c001994 = 0x80;
      return (1);
   }

   *(--reg[R15]) = reg[PR];
   *(--reg[R15]) = reg[MACH];
   *(--reg[R15]) = reg[MACL];
   *(--reg[R15]) = reg[R14];
   *(--reg[R15]) = reg[R13];
   *(--reg[R15]) = reg[R12];
   *(--reg[R15]) = reg[R11];
   *(--reg[R15]) = reg[R10];
   *(--reg[R15]) = reg[R9];
   *(--reg[R15]) = reg[R8];

   *_8c00198c = reg[R15];

   size = **_8c001990;
   addr = *_8c001990 + 4;
   while (size-- != 0)
      *(--reg[R15]) = *(addr++);
   }

   reg[R8] = *(addr++);
   reg[R9] = *(addr++);
   reg[R10] = *(addr++);
   reg[R11] = *(addr++);
   reg[R12] = *(addr++);
   reg[R13] = *(addr++);
   reg[R14] = *(addr++);
   reg[MACL] = *(addr++);
   reg[MACH] = *(addr++);
   reg[PR] = *(addr++);

   *_8c001990 = addr;

}

int
allocate_GD()	/* _8c001970 */
{
   /* the following is atomic */
   if (*_8c001994 = 0) {
      *_8c001994 = 0x80;
      return (0);
   }
   else {
      *_8c001994 = 0x80;
      return (1);
   }
}

void
release_GD()	/* _8c00197e */
{
   *_8c001994 = 0;
}

void
gd_dmaread(param, my_gds)	/* _8c001b2c */
	int *param;
	GDS *my_gds;
{

	int result;

	_8c0026fe(my_gds);
	result = _8c001118(1);
	if (result == 0) {
		/* _8c001b4e */
		_8c00266c(my_gds, param[1], param[0], param[3]);
		*_a05f74b8 = 0x8843307f;
		*_a05f7404 = param[2];
		*_a05f7408 = param[1] * my_gds->sector_size;
		*_a05f740c = 1;
		result = _8c002c44(my_gds);
		_8c001118(0);
		return (_8c002948(result, my_gds));
	} 
	/* _8c001bae */
	my_gds->_0018 = 0x20;
	return (result);

}

void
gd_gettoc(param, my_gds)	/* _8c001c34 */
	int *params;
	GDS *my_gds;
{

	if (my_gds->drvmedia != TYPE_GDROM && param[0] != 1) {
		my_gds->_0018 = 0x00000005;
		return;
	}
	/* _8c001c50 */
	param[1][0] = (*((int *)(my_gds->TOCS[param[0]].toc_buf + 0x0190)) & 0x00ff0000)>>16;
	/* _8c001c74 */
	i = 1;
	do {
		param[1][i] = ((int *)(my_gds->TOCS[param[0]].toc_buf)[i-1] & 0xff000000)>>24
	} while (i++ < param[1]);

}

void
gd_gettoc2(param, my_gds)	/* _8c001ca8 */
	int *param;
	GDS *my_gds;
{

	if (my_gds->drvmedia == TYPE_GDROM || param[0] != 1)
		for (i = 0; i < 408; i++)
			param[1][i] = my_gds->TOCS[param[0]].toc_buf[i];
	else
		my_gds->gd_cmd_stat = 5;

}

int
_8c00223e(size, my_gds)
   int *size;
   GDS *my_gds;
{

   if (*_a05f7418 == 1) {
      *size = *_a05f74f8;
      return (1);
   }

   *size = my_gds->size;
   return (0);

}

/* This initiates a DMA transfer prolly */
/* arg1[0] is destination area
 * arg1[1] is size
 *
int
_8c002266(arg1, my_gds)
   int *arg1;
   GDS *my_gds;
{

   if (arg1[1] != my_gds->size) {
      return (-1);
   }

   *_a05f74b8 = 0x8843407f;
   *_a05f7404 = arg1[0];
   *_a05f7408 = arg1[1];
   *_a05f740c = 0x00000001;

   my_gds->size -= arg1[1];

   *_a05f7414 = 0x00000001;
   *_a05f7418 = 0x00000001;

   return (0);

}

int
_8c002362(arg1, my_gds)
   int *arg1;
   GDS *my_gds;
{

   if (my_gds->_00e4 != 0) {
      arg1[0] = my_gds->_00a4;
      return (1);
   }

   arg1[0] = my_gds->size;

   return (0);

}

int
_8c002380(arg1, my_gds)
   int *arg1;
   GDS *my_gds;
{

   if (arg1[1] > my_gds->size) {
      return (-1);
   }

   my_gds->size -= arg1[1];
   my_gds->_00e0 = arg1[0];
   my_gds->_00e4 = arg1[1];
   return (0);

}

void
_8c0023a4(param, my_gds)
   int *param;
   GDS *my_gds;
{

   int result;

   my_gds->_0000 = (short)(my_gds->_0054 + (param[0]>>8));
   my_gds->_0002 = (short)(param[1] & 0xff00);
   my_gds->_0004 = (short)(param[1] & 0x00ff);
   my_gds->_0006 = (short)0x0000;
   my_gds->_0008 = (short)0x0000;
   my_gds->_000a = (short)0x0000;

   result = _8c002b4c(param[2], param[1], 0, my_gds);
   _8c002948(result);

}

void
_8c0025bc(param, my_gds)
   int *param;
   GDS *my_gds;
{

   my_gds->_0000 = my_gds->_000e;
   my_gds->_0002 = param[0];
   my_gds->_0004 = 0x0006;
   my_gds->_0006 = 0x0000;
   my_gds->_0008 = 0x0000;
   my_gds->_000a = 0x0000;

   result = _8c002b4c(&my_gds->_00e8, 6, 0, my_gds);
   if (result & 0x01 == 0) {
      param[1] = (char)(my_gds->_00ea & 0x00ff);
      param[2] = ((my_gds->_00ea & 0xff00)<<8) +
	         ((my_gds->_00ec & 0x00ff)<<8) +
		 ((my_gds->_00ec & 0xff00)>>8);
   }
   _8c002948(result);

}

void
_8c00262c(param, my_gds)
   int *param;
   GDS *my_gds;
{

   if (_8c0037b2(1) == 0) {
      return;
   }

   if (my_gds->_0018 != 0) {
      return;
   }

   my_gds->_0018 = 2;

}

void
_8c00266c(my_gds, param1, param2, param3)
	GDS *my_gds;
	int param1, param2, param3;
{

	int i;		/* R1 */

	i = ((param2 & 0x00ff0000)>>16) | (param2 & 0x0000ff00);

	if (param3 == 0) {
		my_gds->_0000 = my_gds->_004c + my_gds->sector_mode;
		my_gds->_0002 = ((param2 & 0x00ff0000)>>16) | (param2 & 0x0000ff00);
		my_gds->_0004 = (param2 & 0x000000ff);
		my_gds->_0006 = 0x0000;
		my_gds->_0008 = ((param1 & 0x00ff0000)>>16) | (param1 & 0x0000ff00);
		my_gds->_000a = (param1 & 0x000000ff);
	}
	/* _8c0026c4 */
	my_gds->_0000 = my_gds->_0050 + my_gds->sector_mode;
	my_gds->_0002 = ((param2 & 0x00ff0000)>>16) | (param2 & 0x0000ff00);
	my_gds->_0004 = (param2 & 0x000000ff);
	my_gds->_0006 = (param1>>8) | (param1<<8);
	my_gds->_0008 = ((param3 & 0x00ff0000)>>16) | (param3 & 0x0000ff00);
	my_gds->_000a = (param7 & 0x000000ff);

}

int
_8c0026fe(my_gds)
	GDS *my_gds;
{

	if (*_a05f7418 == 0x00000001) {
		*_a05f7414 = 0x00000000;
		while (*_a05f7418 & 0x00000001 != 0);
	}
	/* _8c002722 */
	my_gds->_00c4 = 0x00000000;
	return (_8c001118(0));

}

void
_8c002774(my_gds)
	GDS *my_gds;
{

	while (1) {
		if ((*_a05f7018 & 0x88) == 0) {
			my_gds->_00a8 = 0;
			return;
		} else {
			/* _8c0027a2 */
			my_gds->_00a8 = 2;
			_8c0018c0();
		}
	}
}

/* this is called after gdrom-controller has given result methinks */
void
_8c002948(result, my_gds)
   int result;
   GDS *my_gds;
{

   if (result == 0xdeaddead) {
      my_gds->_0018 = 0x00000002;
      my_gds->_001c = 0x0000003a;
      return;
   }

   if (result & 0x01 == 0) {
      my_gds->_0018 = 0x00000000;
      my_gds->_001c = 0x00000000;
      return;
   }

   if (my_gds->cmdabort == 0x02) {
      my_gds->_0018 = 0x00000000;
      my_gds->_001c = 0x00000000;
      return;
   }

   _8c002126(my_gds->_00e8, my_gds);

   my_gds->_0018 = (int)(my_gds->_00e8->_0002 & 0x000f);
   my_gds->_001c = (int)(my_gds->_00e8->_0008);

   if (my_gds->_0018 != 11) {
      return;
   }

   if (my_gds->_001c == 0)
      my_gds->_0018 = 0x00000000;

}

int
_8c0029a8(param1, param2, my_gds)
   int param1, param2;
   GDS *my_gds;
{

   int i;	/* R8 */
   int j;	/* 1, R15 */
   int temp;	/* R13 */

   _8c002818(0, my_gds);

major_loop:
   if (my_gds->cmdabort == 0) {
	   /* _8c0029e2 */
	   i = (char )*_a05f709c;
   } else {
      my_gds->cmdabort = 2;
      i = _8c00377c(0, my_gds);
   }
   /* _8c0029f0 */
   j = i & 0x08;
   if (j == 0x08) {
	   temp = ((char)(*_a05f7094)<<8) | (*_a05f7090);
	   if (param2 != 0x04) {
		   do {
			   if (my_gds->_00e4 > 0x00000001) {
				   *my_gds->_00e0 = (short)*_a05f7080;
				   my_gds->_00e0 += 2;
				   temp -= 2;
				   my_gds->_00a4 += 2;
				   my_gds->_00e4 -= 2;
			   } else {
			   	/* _8c002a78 */
				   if (my_gds->_00d8 == 0) {
					   if (my_gds->cmdabort != 0)
						   goto major_loop;
					   _8c0018c0();
				   } else {
				   	/* _8c002a92 */
					   (*my_gds->_00d8)(my_gds->_00dc);
				   }
				   /* _8c002aaa */
				   do {
					   /* _8c002a9e */
					   if (my_gds->cmdabort != 0)
						   goto major_loop;
					   _8c0018c0();
				   } while (my_gds->_00e4 == 0);
			   }
		   } while (temp > 0x01);
	   } else {
	   	/* _8c002aba */
	   	if ((param2 & 0x02) != 0) {
			APAAA
		}
	   }
   }
   /* _8c002b12 */

}

int
_8c002b4c(param1, param2, param3, my_gds)
   int param1, param2;
   int param3;
   GDS *my_gds;
{

   char *ptr;

   _8c002774(my_gds);

   *_a05f7090 = (char)param2 & 0x000000ff;
   *_a05f7094 = (char)((param2 & 0x0000ff00)>>8);
   *_a05f7084 = (char)0x00;

   if (_8c002880(my_gds) != 0) {
      return (0xdeaddead);
   }
   /* _8c002ba0 */

   return (_8c0029a8(param1, param3, my_gds));

}

int
_8c002c44(my_gds)
	GDS *my_gds;
{

	int i;		/* R13 */

	_8c002774(my_gds);
	*_a05f7084 = 0x01;
	_8c002774(my_gds);
	if (_8c002880(my_gds) != 0) {
		return (0xdeaddead);
	}

	my_gds->_00c4 = 0x0000001;
	*_a05f7414 = 0x00000001;
	*_a05f7418 = 0x00000001;

	_8c002818(1, my_gds);

	if (my_gds->cmdabort == 0) {
		/* _8c002c94 */
		if (my_gds->sector_size + my_gds->_00a4 > *_a05f7408) {
			_8c0027ba(my_gds);
		} else {
		/* _8c002cb4 */
			my_gfs->_00b8 = 0x00000002;
			i = _8c00377c(0, my_gds);
			if (my_gds->_00d0 == 0)
				return (i);
			if ((i & 0x00000001) == 1) {
				if ((*_a05f7084 & 0x04) == 0x04)
					return (i);
			}
			/* _8c002ce6 */
			_8c0027ba(my_gds);
			i = (char)*_a05f709c;
			_8c002774(my_gds);
			return (i);
		}
		/* _8c002cf8 */
	}
	/* _8c002cf8 */
	i = (char)*_a05f709c;
	_8c0026fe(my_gds);
	_8c002774(my_gds);
	return (i);

}

int
gdGdcReqCmd(cmd, param)		/* _8c002ff4 */
   int cmd;
   int *param;
{

   GDS *my_gds;
   int gd_chn;

   if (allocate_GD() != 0) {
      return (0);
   }

   my_gds = get_GDS();

   gd_chn = 0;
   if (my_gds->gd_cmd_stat == 0) {
      my_gds->gd_cmd = cmd;
      for (i = 0; i < my_gds->_04e8[cmd] ; i++) {
	 my_gds->_0060[i] = *(param++);
      }
      my_gds->gd_cmd_stat = 2;
      if ((my_gds->gd_chn++) == 0) {
	 my_gds->gd_chn++;
      }
      gd_chn = my_gds->gd_chn;
   }
   release_GD();

   return (gd_chn);

}

int
gdGdcGetCmdStat(gd_chn, status)	/* _8c003072 */
   int gd_chn;
   int *status;
{

   GDS *my_gds;

   if (allocate_GD() != 0) {
      return (4);
   }

   my_gds = get_GDS();

   status[0] = 0;
   status[1] = 0;
   status[2] = 0;
   status[3] = 0;

   if (gd_chn == 0) {
      if (my_gds->gd_cmd_stat == 0) {
	 release_GD();
	 return (0);
      }
      release_GD();
      return (1);
   }

   if (my_gds->gd_chn != gd_chn) {
      status[0] = 5;
      release_GD();
      return (-1);
   }

   switch (my_gds->gd_cmd_stat) {
      case 0:
	 release_GD();
	 return (0);
	 break;

      case 1:
      case 2:
	 status[2] = my_gds->_00a4;
	 status[3] = my_gds->_00a8;
	 release_GD();
	 return (1);
	 break;

      case 3:
	 if (my_gds->_0018 != 0) {
	    status[2] = my_gds->_00a4;
	    status[0] = my_gds->_0018;
	    status[1] = my_gds->_001c;
	    status[3] = my_gds->_00a8;
	    my_gds->gd_cmd_stat = 0;
	    release_GD();
	    return (-1);
	 }
	 status[2] = my_gds->_00a4;
	 status[3] = my_gds->_00a8;
	 my_gds->gd_cmd_stat = 0;
	 release_GD();
	 return (2);
	 break;

      case 4:
	 if (my_gds->_0018 != 0) {
	    status[2] = my_gds->_00a4;
	    status[0] = my_gds->_0018;
	    status[1] = my_gds->_001c;
	    status[3] = my_gds->_00a8;
	    release_GD();
	    return (-1);
	 }
	 status[2] = my_gds->_00a4;
	 status[3] = my_gds->_00a8;
	 release_GD();
	 return (3);
	 break;

      default:
	 release_GD();
	 return (0);
	 break;
   }

}

int
gdGdcGetDrvStat(status)	/* _8c003174 */
   int *status;
{

   GDS *my_gds;
   char stat1, stat2, stat3;

   if (allocate_GD() != 0) {
      return (4);
   }

   my_gds = get_GDS();

   if (my_gds->_00c4 == 0) {
      if (*_a05f7018 & 0x80 == 0) {
	 stat1 = *_a05f708c;
	 stat2 = *_a05f708c;
	 stat3 = *_a05f708c;
	 if (*_a05f7018 & 0x80 != 0) {
	    release_GD();
	    return (1);
	 }

	 if (stat1 != stat2) {
	    stat1 = stat3;
	 }
	 my_gds->_00ac = stat1 & 0x000f;
	 status[0] = stat1 & 0x000f;
	 my_gds->drvmedia = stat1 & stat2;
	 status[1] = stat1 & stat2;
	 release_GD();
	 return (0);
      }
   }
   /* _8c00320c */

   if (my_gds->_00c4 == 0) {
      release_GD();
      return (1);
   }

   my_gds->_00ac = GDD_DRVSTAT_PLAY;
   status[0] = GDD_DRVSTAT_PLAY;
   status[1] = my_gds->drvmedia;
   release_GD();
   return (0);

}

void
gdGdcG1DmaEnd(func, param)		/* _8c003238 */
   int *func, *param;
{

   GDS *my_gds;

   my_gds = get_GDS();

   *_a0606900 = 0x00004000;

   if (func == NULL) {
      return;
   }

   (*func)(param);

}

int
gdGdcCheckDmaTrans(gd_chn, arg2)	/* _8c00326a */
   int gd_chn;
   int arg2;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (gd_chn != my_gds->gd_chn) {
      return (-1);
   }

   if (my_gds->gd_cmd_stat != 4) {
      return (-1);
   }

   return (_8c00223e(arg2, my_gds));

}

int
gdGdcReqDmaTrans(gd_chn, arg2)	/* _8c0032a2 */
   int gd_chn;
   int *arg2;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (gd_chn != my_gds->gd_chn) {
      return (-1);
   }

   if (my_gds->gd_cmd_stat != 4) {
      return (-1);
   }

   return (_8c002266(arg2, my_gds));

}

/* This is probably for some type of callback stuff */
void
_8c0032da(arg1, arg2)
   int arg1;
   int arg2;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (arg1 != 0) {
      my_gds->_00d8 = arg1;
      my_gds->_00dc = arg2;
   }
   else {
      my_gds->_00d8 = 0;
      my_gds->_00dc = 0;
   }

}

int
_8c00333c(gd_chn, arg2)
   int gd_chn;
   int *arg2;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (gd_chn != my_gds->gd_chn) {
      return (-1);
   }

   if (my_gds->gd_cmd_stat != 4) {
      return (-1);
   }

   return (_8c002362(arg2, my_gds));

}

int
_8c003374(arg1, arg2)
   int arg1;
   int *arg2;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (arg1 != my_gds->gd_chn) {
      return (-1);
   }

   if (my_gds->gd_cmd_stat != 4) {
      return (-1);
   }

   return (_8c002380(arg2, my_gds));

}

int
gdGdcReadAbort(gd_chn)	/* _8c0033c0 */
   Sint32 gd_chn;
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (gd_chn != my_gds->gd_chn) {
      return (-1);
   }

   if (my_gds->cmdabort != 0) {
      return (-1);
   }

   switch (my_gds->gd_cmd) {
      case 16:
      case 17:
      case 20:
      case 21:
      case 22:
      case 27:
      case 28:
      case 29:
      case 32:
      case 33:
      case 34:
      case 37:
      case 38:
      case 39:
	 switch (my_gds->gd_cmd_stat) {
	    case 1:
	    case 2:
	    case 4:
	       my_gds->cmdabort = 1;
	       return (0);
	       break;
	    default:
	       return (0);
	       break;
	 }
	 break;

      default:
	 return (-1);
   }

}

void
gdGdcReset()		/* _8c003450 */
{

   GDS *my_gds;

   my_gds = get_GDS();

   if (*_a05f7418 == 0x00000001) {
      *_a05f7414 = 0x00000000;
      while (*_a05f7418 & 0x00000001);
      my_gds->_00c4 = 0;
   }

   *_a05f709c = (char)0x08;
   while (*_a05f7018 & 0x80);

}

int
gdGdcChangeDataType(arg1)	/* _8c0034a6 */
   int *arg1;
{

   GDS *my_gds;

   if (allocate_GD() != 0) {
      return (4);
   }

   my_gds = get_GDS();

   switch (arg1[0]) {
      case 0:
	 if (arg1[1] & 0x1000 == 0) {
	    switch (arg1[2]) {
	       case 0x0200:
	       case 0x0400:
	       case 0x0600:
	       case 0x0800:
	       case 0x0a00:
	       case 0x0c00:
		  break;
	       default:
		  release_GD();
		  return (-1);
		  break;
	    }
	 }
	 my_gds->sector_mode = arg1[1] | arg1[2];
	 my_gds->sector_size = arg1[3];
	 release_GD();
	 return (0);
	 break;
	 
      case 1:
	 arg1[1] = my_gds->sector_mode & 0xf000;
	 arg1[2] = my_gds->sector_mode & 0x0e00;
	 arg1[3] = my_gds->sector_size;
	 release_GD();
	 return (0);
	 break;

      default:
	 release_GD();
	 return (-1);
	 break;
   }

}

void
_8c003570()
{

   GDS *my_gds;

   my_gds = get_GDS();

   my_gds->gd_cmd = 0;
   my_gds->_0018 = 0;
   my_gds->_001c = 0;

   my_gds->_00a0 = get_GdBaseReg();
   my_gds->_00a4 = 0;
   my_gds->_00a8 = 0;
   my_gds->_00ac = 0;
   my_gds->drvmedia = 0;
   my_gds->_00b4 = 1;
   my_gds->cmdabort = 0;
   my_gds->size = 0;
   my_gds->gd_chn = 1;
   my_gds->_00c4 = 0;
   my_gds->sector_mode = 0x00002400;
   my_gds->sector_size = 0x00000800;
   my_gds->_00d0 = 2;
   my_gds->_00d4 = 0;
   my_gds->_00d8 = 0;
   my_gds->_00dc = 0;
   my_gds->_00e0 = 0;
   my_gds->_00e4 = 0;

   ptr = _8c0010b0(1);

   if (ptr[14] != 0) {
      for (i = 0; i < 16; i++) {
	 my_gds->_0020[i] = ptr[i];
      }
   }
   else {
      my_gds->_0020 = 0;
      my_gds->_0024 = 0x00000010;
      my_gds->_0028 = 0x00000011;
      my_gds->_002c = 0x00000012;
      my_gds->_0030 = 0x00000013;
      my_gds->_0034 = 0x00000014;
      my_gds->_0038 = 0x00000015;
      my_gds->_003c = 0x00000016;
      my_gds->_0040 = 0x00000020;
      my_gds->_0044 = 0x00000021;
      my_gds->_0048 = 0x00000022;
      my_gds->_004c = 0x00000030;
      my_gds->_0050 = 0x00000031;
      my_gds->_0054 = 0x00000040;
   }
   /* _8c003660 */
   for (i = 0; i < 16; i++) {
      my_gds->_0060[i] = 0;
   }

   for (i = 0; i < 48; i++) {
      my_gds->_00e8[i] = 0;
   }

   for (i = 0; i < 104; i++) {
      my_gds->_01a8[i] = 0xffffffff;
   }

   for (i = 0; i < 104; i++) {
      my_gds->_0348[i] = 0xffffffff;
   }

   for (i = 0; i < 48; i++) {
      my_gds->_04e8[i] = 0;
   }

   my_gds->_0528 = 4;
   my_gds->_052c = 4;
   my_gds->_0530 = 2;
   my_gds->_0534 = 2;
   my_gds->_0538 = 3;
   my_gds->_053c = 3;
   my_gds->_0554 = 1;
   my_gds->_0558 = 2;
   my_gds->_0570 = 3;
   my_gds->_055c = 4;
   my_gds->_0564 = 4;
   my_gds->_0560 = 1;
   my_gds->_0568 = 2;
   my_gds->_0574 = 3;
   my_gds->_0578 = 4;
   my_gds->_057c = 2;
   my_gds->_0580 = 3;
   my_gds->_0584 = 3;
   my_gds->_0588 = 1;
   
   my_gds->gd_cmd_stat = 0;

   while(1) {
      if (my_gds->gd_cmd_stat == 2) {
	 my_gds->gd_cmd_stat = 1;
	 my_gds->_0018 = 0;
	 my_gds->_001c = 0;
	 my_gds->_00a4 = 0;
	 if (my_gds->_00d4 != 1) {
	    gd_do_cmd(my_gds->_0060, my_gds, my_gds->gd_cmd);
	 }
	 else {
	    if (my_gds->gd_cmd != 24) {
	    gd_do_cmd(my_gds->_0060, my_gds, my_gds->gd_cmd);
	    }
	    else {
	       my_gds->_0018 = 6;
	    }
	 }
	 my_gds->gd_cmd_stat = 3;
	 my_gds->cmdabort = 0;

	 if (my_gds->_0018 == 6) {
	    my_gds->_00d4 = 1;
	 }
      }
      _8c0018c0();
   }

}

void
_8c003774(param, my_gds)
   int *param;
   GDS *my_gds;
{
   my_gds->_0018 = 5;
}

int
_8c0037b2(arg1, my_gds)
   int arg1;
   GDS *my_gds;
{

   char i;

   if (my_gds->_00d0 == 1) {
      _8c001148();
   }

   if (_8c002f7e(my_gds) & 0x0081 != 0) {
      my_gds->_0018 = 2;
      return (1);
   }

   i = 0;

   if (arg1 != 0) {
      i = i | 0x80;
   }

   i = i | 0x01;
   i = i | 0x02;
   i = i | 0x04;
   i = i | 0x08;
   i = i | 0x10;

   if (_8c003944(i, my_gds) != 0) {
      return (2);
   }

   /* _8c003828 */

   if (_8c002f7e(my_gds) & 0x0081 != 0) {
      my_gds->_0018 = 2;
      return (3);
   }

   if (_8c00399c(&i, my_gds) != 0) {
      return (4);
   }

   if (i & 0x10 == 0x10) {
      return (0);
   }

   return (-1);

}

int
_8c003944(arg1, my_gds)
   unsigned char arg1;
   GDS *my_gds;
{

   int result;

   my_gds->_0000 = (short)((arg1 & 0x9f)<<8 + 0x70);
   my_gds->_0002 = (short)0x0000;
   my_gds->_0004 = (short)0x0000;
   my_gds->_0006 = (short)0x0000;
   my_gds->_0008 = (short)0x0000;
   my_gds->_000a = (short)0x0000;

   result = _8c002bb6(my_gds);
   _8c002948(result);

   if (result & 0x81 != 0) {
      return (-1);
   }

   return (0);

}

int
_8c00399c(arg1, my_gds)
   char *arg1;
   GDS *my_gds;
{

   int result, i, j;

   my_gds->_0000 = (short)(((*arg1 & 0x1f)<<8) + 0x71);
   my_gds->_0002 = (short)0x0000;
   my_gds->_0004 = (short)0x0000;
   my_gds->_0006 = (short)0x0000;
   my_gds->_0008 = (short)0x0000;
   my_gds->_000a = (short)0x0000;
   my_gds->_00a4 = 0;

   result = _8c002b4c(my_gds);
   i = my_gds->_00a4;
   _8c002948(result, my_gds);

   if (result & 0x81 != 0) {
      return (-1);
   }
   j = 1;
   *arg1 = (char)0;

   if (_8c003b04(i, &j) != 0) {
      return (-1);
   }

   return (0);

}

void
_8c003c00(arg1, arg2, arg3, func)
	int arg1, arg2, arg3;	/* arg3 is discarded */
	int func;
{

	if (func >= 4)
		return (-1);

	switch (func) {
		case 0:
			/* 8c003c38 */
			_8c003ca8(0x0001a056, _8c000068, 8);
			_8c003ca8(0x0001a000, _8c000070, 5);
			_8c000075 = 0;
			_8c000076 = 0;
			_8c000077 = 0;
			_8c000078 = 0;
			_8c000079 = 0;
			_8c00007a = 0;
			_8c00007b = 0;
			_8c00007c = 0;
			_8c00007e = 0;
			_8c00007f = 0;
			__asm__("ocbwb	0x8c000060");
			return (0);
			break;
		case 1:
			/* 8c003c20 */
			/* not valid */
			break;
		case 2:
			/* 8c003c78 */
			if (arg1 >= 10)
				return (-1);
			return (_8c003ca8(0x0001a480 + arg1 * 0x2c0, arg2, 0x2c0));
			break;
		case 3:
			/* 8c003ca0 */
			return (0x8c010068);
			break;
	}

}

int
_8c003ca8(arg)
	int arg;
{
	(*_8c0000b8)();
}

void
_8c008300()
{
	reg[CCR] = 0x0000092b;
	reg[R15] = 0x7e001000;		/* use cache as memory for stack */
	_8c0083a8();

	__asm__("jmp 0xac00b700\n\t");
}

void
_8c0083a8()
{
	_8c0083c0();
	_8ced3d9c = 0;		/* hmm :/ */
	_8c0083f8();
}

void
_8c0083c0()
{
	char *dst = *_8c009d38;

	while (*_8c009d3c < dst)
		*(dst++) = 0;
}

void
_8c0083f8()
{

	int old_SR;
	int result;
	int i;		/* 8, R15 */
	int j;		/* 7, R15 */
	int k;		/* 6, R15 */
	int l;		/* 5, R15 */
	int m;		/* 4, R15 */
	int n;		/* 3, R15 */
	int o;		/* 2, R15 */
	int p;		/* 1, R15 */

	_8c009dec();

	old_SR = (reg[SR]>>4) & 0x000f;
	reg[SR] = (reg[SR] & 0xff0f) | 0x00f0;

	result = _8c009858();

	reg[SR] = ((old_SR & 0x000f)<<4) | (reg[SR] & 0xff0f);

	switch (result) {
		case 1:
		case 3:
			i = 8;
			break;
		case 4:
			i = 9;
			break;
		default:
			i = 6;
			break;
	}
	/* _8c008460 */
	old_SR = (reg[SR]>>4) & 0x000f;
	reg[SR] = (reg[SR] & 0xff0f) | 0x00f0;

	_8c009074(i);

	reg[SR] = ((old_SR & 0x000f)<<4) | (reg[SR] & 0xff0f);

	_8c00853c(i);
	_8c00908c(1);
	j = _8c009e12();
	p = 0;
	n = 0;
	while (1) {
		k = _8c009e12();
		l = _8c009e1c(j, k);
		m = _8c009e24(l);
		o = 0;

		while (o++ < 1000);
		if (p++ >= 4000)
			n = 1;
		if (m >= 6000000)
			break;
		if (n != 0)
			break;
	}
	/* 8c00851e */

	sys_do_bioscall(0);	/* 8c000820() */

}

void
_8c009074(arg)
	int arg;
{
	_8c009830(arg);
}

int
_8c0090f8(arg)
	int arg;
{
	int result, i, fog;

	result = *_a05f8000;
	*_a05f8008 = 0x00000000;
	*_8ced3d18 = (*_ac00002c<<16) | 0x08;
	*_8ced3d24 = 0x0000000c;

	if (arg == 9)
		*_8ced3d24 |= 0x00800000;

	*_a05f8040 = 0x00c0c0c0;	/* border color to light white */

	_8c00908c(0);

	*_a05f8030 = 0x00000101;
	*_a05f80b0 = 0x007f7f7f;
	*_a05f80b4 = 0x007f7f7f;
	*_a05f80b8 = 0x0000ff07;

	fog = 0xfffe;
	for (i = 0; i < 128; i++) {
		_a05f8200[i] = fog;
		fog = fog - 0x0101;
	}

	*_a05f8008 = 0x00000001;
	*_a05f8008 = 0x00000000;
	*_a05f6884 = 0x00000000;
	*_a05f6888 = 0x00000000;

	return (result);

}

void
_8c009214(arg)
	int arg;
{

	*_a05f8124 = 0x000c2680;
	*_a05f812c = 0x0009e800;
	*_a05f8128 = 0x00000000;
	*_a05f8130 = 0x0009e740;
	*_a05f813c = 0x000e0013;
	*_a05f8140 = 0x00100203;
	*_a05f8164 = 0x000c2680;
	*_a05f8144 = 0x80000000;
	*_a05f8068 = ((*_8ced3d00-1)<<16) & 0x07ff0000;
	*_a05f806c = ((*_8ced3d04-1)<<16) & 0x07ff0000;
	*_a05f8110 = 0x00093f39;
	*_a05f80d0 = *_8ced3d44;
	*_a05f80d4 = *_8ced3d4c;
	*_a05f80dc = *_8ced3d50;
	*_a05f80d8 = *_8ced3d54;
	*_a05f80e0 = *_8ced3d58;
	*_a05f8060 = *_8ced3d38;

	*_a05f8064 = *_8ced3d40;
	*_a05f8048 = *_8ced3d28;
	*_a05f804c = *_8ced3d2c;
	*_a05f8050 = *_8ced3d34;
	*_a05f8054 = *_8ced3d3c;
	*_a05f805c = *_8ced3d30;

	*_a05f80c8 = *_8ced3d48;
	*_a05f8074 = 0x00000001;
	*_a05f807c = 0x0027df77;
	*_a05f8080 = 0x00000007;
	*_a05f8118 = 0x00008040;

	*_a05f8078 = 0x3f800000;
	*_a05f8084 = 0x00000000;
	*_a05f8088 = (float)*_8c009448;		/* 0x38d1b717 */

	*_a05f808c = 0x01000000;
	*_a05f80bc = 0xffffffff;
	*_a05f80c0 = 0xff000000;
	*_a05f80e4 = *_8ced3d10;
	*_a05f8040 = *_8ced3d14;
	*_a05f80f4 = *_8ced3d5c;

}

void
_8c00940a(arg)
	int arg;
{

	if (arg == 9) {
		*_a0702c00 = *_a0702c00 & 0x01;
	} else {

	}

}

void
_8c0097b4(arg)
	int arg;
{

	_8c00940a(arg);

	switch (arg) {
		case 6:
			_8c009488(0x00008212);
			break;
		case 8:
			_8c009488(0x00008214);
			break;
		case 9:
			_8c009488(0x00008111);
			break;
		default:
			_8c009488(0x00008212);
			break;
	}

}

void
_8c009830(arg)
	int arg;
{
	_8c0090f8(arg);
	_8c0097b4(arg);
	_8c009214(arg);
}

int
_8c009858()
{

	int result;
	char flash_stuff1;

	result = _8c008380();

	flash_stuff1 = *_ac000074;

	if (result == 0)
		return (4);

	switch (flash_stuff1) {
		case 0x30:
			return (0);
			break;
		case 0x31:
			return (1);
			break;
		case 0x32:
			return (2);
			break;
		case 0x33:
			return (3);
			break;
		default:
			return (0);
			break;
	}
}

void
_8c009dec()
{

	reg[TOCR] = 0x00;
	reg[TSTR] = reg[TSTR] & 0xfe;
	reg[TCR0] = 0x0002;
	reg[TCOR0] = 0xffffffff;
	reg[TCNT0] = 0xffffffff;
	reg[TSTR] = reg[TSTR] | 0x01;

}

void
init_machine(mode)		/* _8c00b500 */
   int mode;
{

   unsigned time, gd_status;

   reg[R15] = 0x8c00b700;	/* default floating point registers here */
   reg[SR] = 0x500000f0;
   reg[FPSCR] = 0x00140001;

   /* reset all floating point registers; details omitted */

   reg[FPUL] = 0x00000000;
   reg[FPSCR] = 0x00140001;

   reg[R0] = 0x00000000;
   reg[R1] = 0x00000000;
   reg[R2] = 0x00000000;
   reg[R3] = 0x00000000;
   reg[R4] = 0xfffffffd;
   reg[R5] = 0x00000000;
   reg[R6] = 0x00000000;
   reg[R7] = 0x00000000;
   reg[SR] = 0x700000f0;	/* does this change banks? */
   reg[R2] = 0x8cfffff8;
   reg[R3] = 0x00000000;
   reg[R4] = 0x00000000;
   reg[R5] = 0x00000000;
   reg[R6] = 0x00000000;
   reg[R7] = 0x00000000;
   reg[R8] = 0x00000000;
   reg[R9] = 0x00000000;
   reg[R10] = 0x00000000;
   reg[R11] = 0x00000000;
   reg[R12] = 0x00000000;
   reg[R13] = 0x00000000;
   reg[R14] = 0x00000000;
   reg[MACH] = 0x00000000;
   reg[MACL] = 0x00000000;
   reg[PR] = 0x8c000128;
   reg[GBR] = 0x8c000000;
   reg[VBR] = 0x8c000000;
   reg[SSR] = 0x500000f0;
   reg[SPC] = (uint32_t *)sys_do_bioscall;
   reg[R15] = 0x8d000000;
   _8cfffffc = 0x00000000;
   _8cfffff8 = 0x8c000128;
   reg[R14] = 0x00000000;

   lmemset(0, (uint64_t *)0x8cffffff8, 0x001bffff);	/* clear memory above bootROM in RAM */

   /* check for initial boot ( = no debug handler installed) */
   if (reg[DBR] >= 0) {
      lmemset(0, (uint64_t *)0x8c000100, 0x20);	/* clear system variables */
      _8c00b800(0, mode);
      _8c00b800(4, mode);
   }
   else {
      _8c00b800(2, mode);
      wait_for_vsync();
   }
   
   _8c00b800(6, mode);

   if (mode != 0) {
      i = *_a05f74b0;
      *_8c000076 = (char)i;
      *_8c000072 = (char)(((i & 0x0c)>>2)|0x30);
      *_8c000074 = (char)((i & 0x03) | 0x30);
   }

   _8c00b800(8, mode);

   if (mode == 0 || reg[DBR] >= 0) {
      lmemset(0, _ac008000, 0x0800);	/* clear mem _ac004000 to _ac008000 */
      /* check for initial boot */
      if (reg[DBR] >= 0) {
	 *SelectMenu = -3;
      }
   }
   /* _8c00b5da */

   /* Save R3-R8, R12,R13 on stack; details omitted */

   init_bioscall_vectors(0, 0, mode);

   if (reg[DBR] >= 0) {
      _8c0010b0(0);
   }

   gdGdcInitSystem(0);		/* gd_do_bioscall(0, 3, 0) */
   *GdCmd = CMD_INIT;
   *GdHn = gdGdcReqCmd(CMD_INIT, NULL);	/* gd_do_bioscall(0, 0, CMD_INIT, 0) */
   gdGdcExecServer();		/* gd_do_bioscall(0, 2) */
   *_8c000020 = reg[TCNT0] + 0xff88ca6c;

   /* _8c00b61c - warning: messy code */

   gdGdcExecServer();		/* gd_do_bioscall(0, 2) */
   *GdCmdStat = gdGdcCheckDmaTrans(*GdHn, GdStatus);	/* gd_do_bioscall(0, 7, *_8c000040, _8c000030) */

   gd_status = 0;
   while (*GdCmdStat == 1) {
      /* _8c00b620 */

      time = reg[TCNT0];
      if (time > 0) {
	 /* _8c00b634 */
	 if (*_8c000020 > 0) {
	    if (time < *_8c000020) {
	       /* _8c00b694 */
	       *TimerCount = reg[TCNT0];
	       *GdDrvStatus = -1;
	       *GdMedia = -1;
	       *GdHn = -1;
	       return;
	    }
	 }
	 else if (time < *_8c000020) {
	    /* _8c00b694 */
	    *TimerCount = reg[TCNT0];
	    *GdDrvStatus = -1;
	    *GdMedia = -1;
	    *GdHn = -1;
	    return;
	 }
      }

      else if (*_8c000020 > 0) {
	 /* _8c00b638 */
	 if (time < *_8c000020) {
	    /* _8c00b694 */
	    *TimerCount = reg[TCNT0];
	    *GdDrvStatus = -1;
	    *GdMedia = -1;
	    *GdHn = -1;
	    return;
	 }
      }

      else if (time < *_8c000020) {
	 /* _8c00b694 */
	 *TimerCount = reg[TCNT0];
	 *GdDrvStatus = -1;
	 *GdMedia = -1;
	 *GdHn = -1;
	 return;
      }

      /* _8c00b63c */

      time = reg[TCNT0];
      if (time > 0) {
	 /* _8c00b650 */
	 if (*TimerCount > 0) {
	    /* _8c00b64a */
	    if (time > *TimerCount) {
	       /* _8c00b658 */
	       gd_status |= 0x02;
	       if (gd_status == 0x03) {
		  break;	/* goto _8c00b684 */
	       }
	    }
	 }
	 else if (time < *TimerCount) {
	    /* _8c00b658 */
	    gd_status |= 0x02;
	    if (gd_status == 0x03) {
	       break;		/* goto _8c00b684 */
	    }
	 }
      }

      else if (*TimerCount > 0) {
	 /* _8c00b654 */
	 if (time < *TimerCount) {
	    /* _8c00b658 */
	    gd_status |= 0x02;
	    if (gd_status == 0x03) {
	       break;		/* goto _8c00b684 */
	    }
	 }
      }

      else if (time > *TimerCount) {
	 /* _8c00b658 */
	 gd_status |= 0x02;
	 if (gd_status == 0x03) {
	    break;		/* goto _8c00b684 */
	 }
      }

      /* _8c00b662 */

      gdGdcExecServer();	/* gd_do_bioscall(0, 2) */
      *GdCmdStat = gdGdcGetCmdStat(*GdHn, GdStatus);	/* gd_do_bioscall(0, 1, *_8c000040, _8c000030) */

      if (*GdCmdStat != 1) {
	 gd_status |= 0x01;
	 if (gd_status != 0x03) {
	    *GdCmdStat = 1;
	 }
      }
   }

   /* _8c00b684 */

   *TimerCount = reg[TCNT0];

   GdDrvStatus = gdGdcGetDrvStat();	/* gd_do_bioscall(0, 4) */
   *GdHn = 0;

}

int
lmemset(value, start, n)	/* _8c00b6b8 */
   long value, long *start;
   int n;
{
   while(n--)
      *(--start) = value;
}

void
wait_for_vsync()	/* 8c00b6c2 */
{

   while (*_a05f810c & 0x2000);

}

/* Bah! this is a messy function; see below for rewritten version of this
void
_8c00b800(function)
   short function;
{

   int *dst;
   short *src, *func;
   short arg1, arg2;

   src = _8c00bb9c + _8c00bb9c[function];

   for (;;) {
      src += 3;
      src = (src>>2)<<2;
      arg1 = *(src++);
      if (arg1 == 0) {
	 arg1 = *(src++);
	 if (arg1 != 0) {
	    src += arg1;
	 }
	 else {
	    return;
	 }
      }
      else {
	 func = *(src++);
	 if ((func & 0x8000) != 0) {
	    func = func ^ (func & 0x8000);
	 }
	 else {
	    dst = *(src++);
	 }

	 if (func & 0x6000 != 0) {
	    func = func ^ (func & 0x6000);
	    func += 0x0c;
	    arg2 = src;
	    for (src += *src + 1; arg1--; dummy += 4) {
	       (*func)(arg1, arg2);
	    }
	    src = arg2;
	 }
	 else {
	    for (; arg1--; dst += 4) {
	       (*func)(arg1, dst);
	    }
	 }

      }
   }

}
*/

void
_8c00b800(function, mode)
   short function;
   int mode;
{

   unsigned int *dst;

   switch (function) {
      case 0:
	 reg[WCR3] = 0x07777777;
	 reg[PCR] = 0x0000;
	 reg[PDTRA] = 0x0000;
	 reg[PCTRA] = 0x000a03f0;
	 reg[PCTRB] = 0x00000000;
	 reg[PDTRB] = 0x0000;
	 reg[GPIOIC] = 0x0000;
	 reg[PTEH] = 0x00000000;
	 reg[PTEL] = 0x00000000;
	 reg[TTB] = 0x00000000;
	 reg[TEA] = 0x00000000;
	 reg[TRA] = 0x00000000;
	 reg[EXPEVT] = 0x00000000;
	 reg[INTEVT] = 0x00000000;
	 reg[PTEA] = 0x00000000;
	 reg[QACR0] = 0x00000000;
	 reg[QACR1] = 0x00000000;
	 reg[RMONAR] = 0x00;
	 reg[RCR1] = 0x00;
	 reg[STBCR] = 0x03;
	 reg[WTCNT] = 0x5a00;
	 reg[WTCSR] = 0x5a00;
	 reg[STBCR2] = 0x00;
	 reg[TOCR] = 0x00;
	 reg[TSTR] = 0x00;
	 reg[TCOR0] = 0xffffffff;
	 reg[TCNT0] = 0xffffffff;
	 reg[TCR0] = 0x0002;
	 reg[TCOR1] = 0xffffffff;
	 reg[TCNT1] = 0xffffffff;
	 reg[TCR1] = 0x0000;
	 reg[TCOR2] = 0xffffffff;
	 reg[TCNT2] = 0xffffffff;
	 reg[TCR2] = 0x0000;
	 reg[TSTR] = 0x01;
	 reg[SAR1] = 0x00000000;
	 reg[DAR1] = 0x00000000;
	 reg[DMATCR1] = 0x00000000;
	 reg[CHCR1] = 0x00005440;
	 reg[SAR2] = 0x00000000;
	 reg[DAR2] = 0x00000000;
	 reg[DMATCR2] = 0x00000000;
	 reg[CHCR2] = 0x000052c0;
	 reg[SAR3] = 0x00000000;
	 reg[DAR3] = 0x00000000;
	 reg[DMATCR3] = 0x00000000;
	 reg[CHCR3] = 0x00005440;
	 reg[DMAOR] = 0x00008201;
	 reg[SCSMR2] = 0x0000;
	 reg[SCBRR2] = 0xff;
	 reg[SCSCR2] = 0x0000;
	 reg[SCFCR2] = 0x0000;
	 reg[SCSPTR2] = 0x0000;
	 reg[ICR] = 0x0000;
	 reg[IPRA] = 0x0000;
	 reg[IPRB] = 0x0000;
	 reg[IPRC] = 0x0000;
	 reg[BBRA] = 0x0000;
	 reg[BBRB] = 0x0000;
	 reg[BRCR] = 0x0000;
	 *_a05f6800 = 0x11ff0000;
	 *_a05f6804 = 0x00000020;
	 *_a05f6808 = 0x00000000;
	 *_a05f6810 = 0x0cff0000;
	 *_a05f6814 = 0x0cff0000;
	 *_a05f6818 = 0x00000000;
	 *_a05f681c = 0x00000000;
	 *_a05f6820 = 0x00000000;
	 *_a05f6840 = 0x00000000;
	 *_a05f6844 = 0x00000000;
	 *_a05f6848 = 0x00000000;
	 *_a05f684c = 0x00000000;
	 *_a05f6884 = 0x00000000;
	 *_a05f6888 = 0x00000000;
	 *_a05f68a0 = 0x80000000;
	 *_a05f68a4 = 0x00000000;
	 *_a05f68ac = 0x00000000;
	 *_a05f6910 = 0x00000000;
	 *_a05f6914 = 0x00000000;
	 *_a05f6918 = 0x00000000;
	 *_a05f6920 = 0x00000000;
	 *_a05f6924 = 0x00000000;
	 *_a05f6928 = 0x00000000;
	 *_a05f6930 = 0x00000000;
	 *_a05f6934 = 0x00000000;
	 *_a05f6938 = 0x00000000;
	 *_a05f6940 = 0x00000000;
	 *_a05f6944 = 0x00000000;
	 *_a05f6950 = 0x00000000;
	 *_a05f6954 = 0x00000000;
	 *_a05f6c04 = 0x0cff0000;
	 *_a05f6c10 = 0x00000000;
	 *_a05f6c14 = 0x00000000;
	 *_a05f6c18 = 0x00000000;
	 *_a05f6c80 = 0xc3500000;
	 *_a05f6c8c = 0x61557f00;
	 *_a05f6ce8 = 0x00000001;
	 *_a05f7404 = 0x0cff0000;
	 *_a05f7408 = 0x00000020;
	 *_a05f740c = 0x00000000;
	 *_a05f7414 = 0x00000000;
	 *_a05f7418 = 0x00000000;
	 *_a05f7484 = 0x00000400;
	 *_a05f7488 = 0x00000200;
	 *_a05f748c = 0x00000200;
	 *_a05f7490 = 0x00000222;
	 *_a05f7494 = 0x00000222;
	 *_a05f74a0 = 0x00002001;
	 *_a05f74a4 = 0x00002001;
	 *_a05f74b4 = 0x00000001;
	 *_a05f74b8 = 0x88437f00;
	 *_a05f7800 = 0x009f0000;
	 *_a05f7804 = 0x0cff0000;
	 *_a05f7808 = 0x00000020;
	 *_a05f780c = 0x00000000;
	 *_a05f7810 = 0x00000005;
	 *_a05f7814 = 0x00000000;
	 *_a05f7818 = 0x00000000;
	 *_a05f781c = 0x00000000;
	 *_a05f7820 = 0x009f0000;
	 *_a05f7824 = 0x0cff0000;
	 *_a05f7828 = 0x00000020;
	 *_a05f782c = 0x00000000;
	 *_a05f7830 = 0x00000005;
	 *_a05f7834 = 0x00000000;
	 *_a05f7838 = 0x00000000;
	 *_a05f783c = 0x00000000;
	 *_a05f7840 = 0x009f0000;
	 *_a05f7844 = 0x0cff0000;
	 *_a05f7848 = 0x00000020;
	 *_a05f784c = 0x00000000;
	 *_a05f7850 = 0x00000005;
	 *_a05f7854 = 0x00000000;
	 *_a05f7858 = 0x00000000;
	 *_a05f785c = 0x00000000;
	 *_a05f7860 = 0x009f0000;
	 *_a05f7864 = 0x0cff0000;
	 *_a05f7868 = 0x00000020;
	 *_a05f786c = 0x00000000;
	 *_a05f7870 = 0x00000005;
	 *_a05f7874 = 0x00000000;
	 *_a05f7878 = 0x00000000;
	 *_a05f787c = 0x00000000;
	 *_a05f7890 = 0x00000fff;
	 *_a05f7894 = 0x00000fff;
	 *_a05f7898 = 0x00000000;
	 *_a05f789c = 0x00000001;
	 *_a05f78a0 = 0x00000000;
	 *_a05f78a4 = 0x00000000;
	 *_a05f78a8 = 0x00000000;
	 *_a05f78ac = 0x00000000;
	 *_a05f78b0 = 0x00000000;
	 *_a05f78b4 = 0x00000000;
	 *_a05f78b8 = 0x00000000;
	 *_a05f78bc = 0x46597f00;
	 *_a05f7c00 = 0x04ff0000;
	 *_a05f7c04 = 0x0cff0000;
	 *_a05f7c08 = 0x00000020;
	 *_a05f7c0c = 0x00000000;
	 *_a05f7c10 = 0x00000000;
	 *_a05f7c14 = 0x00000000;
	 *_a05f7c18 = 0x00000000;
	 *_a05f7c80 = 0x67027f00;
	 *_a05f6900 = 0xffffffff;
	 *_a05f6908 = 0xffffffff;

	 _8c00b8fa();
	 
	 break;

      case 2:
	 reg[PTEH] = 0x00000000;
	 reg[PTEL] = 0x00000000;
	 reg[TTB]  = 0x00000000;
	 reg[TEA]  = 0x00000000;
	 reg[TRA]  = 0x00000000;
	 reg[EXPEVT] = 0x00000000;
	 reg[INTEVT] = 0x00000000;
	 reg[PTEA] = 0x00000000;
	 reg[QACR0] = 0x00000000;
	 reg[QACR1] = 0x00000000;
	 reg[RMONAR] = 0x00;
	 reg[RCR1] = 0x00;
	 reg[STBCR] = 0x03;
	 reg[WTCNT] = 0x5a00;
	 reg[WTCSR] = 0x5a00;
	 reg[STBCR2] = 0x00;
	 reg[TOCR] = 0x00;
	 reg[TSTR] = 0x00;
	 reg[TCOR0] = 0xffffffff;
	 reg[TCNT0] = 0xffffffff;
	 reg[TCR0] = 0x0002;
	 reg[TCOR1] = 0xffffffff;
	 reg[TCNT1] = 0xffffffff;
	 reg[TCR1] = 0x0000;
	 reg[TCRO2] = 0xffffffff;
	 reg[TCNT2] = 0xffffffff;
	 reg[TCR2] = 0x0000;
	 reg[TSTR] = 0x0001;
	 reg[SAR1] = 0x00000000;
	 reg[DAR1] = 0x00000000;
	 reg[DMATCR1] = 0x00000000;
	 reg[CHCR1] = 0x00005440;
	 reg[SAR2] = 0x00000000;
	 reg[DAR2] = 0x00000000;
	 reg[DMATCR2] = 0x00000000;
	 reg[CHCR2] = 0x000052c0;
	 reg[SAR3] = 0x00000000;
	 reg[DAR3] = 0x00000000;
	 reg[DMATCR3] = 0x00000000;
	 reg[CHCR3] = 0x00005440;
	 reg[DMAOR] = 0x00008201;
	 reg[SCSMR2] = 0x0000;
	 reg[SCBRR2] = 0xff;
	 reg[SCSCR2] = 0x0000;
	 reg[SCFCR2] = 0x0000;
	 reg[SCSPTR2] = 0x0000;
	 reg[ICR] = 0x0000;
	 reg[IPRA] = 0x0000;
	 reg[IPRB] = 0x0000;
	 reg[IPRC] = 0x0000;
	 reg[BBRA] = 0x0000;
	 reg[BBRB] = 0x0000;
	 reg[BRCR] = 0x0000;
	 *_a05f6800 = 0x11ff0000;
	 *_a05f6804 = 0x00000020;
	 *_a05f6808 = 0x00000000;
	 *_a05f6810 = 0x0cff0000;
	 *_a05f6814 = 0x0cff0000;
	 *_a05f6818 = 0x00000000;
	 *_a05f681c = 0x00000000;
	 *_a05f6820 = 0x00000000;
	 *_a05f6840 = 0x00000000;
	 *_a05f6844 = 0x00000000;
	 *_a05f6848 = 0x00000000;
	 *_a05f684c = 0x00000000;
	 *_a05f6884 = 0x00000000;
	 *_a05f6888 = 0x00000000;
	 *_a05f68a0 = 0x80000000;
	 *_a05f68a4 = 0x00000000;
	 *_a05f68ac = 0x00000000;
	 *_a05f6910 = 0x00000000;
	 *_a05f6914 = 0x00000000;
	 *_a05f6918 = 0x00000000;
	 *_a05f6920 = 0x00000000;
	 *_a05f6924 = 0x00000000;
	 *_a05f6928 = 0x00000000;
	 *_a05f6930 = 0x00000000;
	 *_a05f6934 = 0x00000000;
	 *_a05f6938 = 0x00000000;
	 *_a05f6940 = 0x00000000;
	 *_a05f6944 = 0x00000000;
	 *_a05f6950 = 0x00000000;
	 *_a05f6954 = 0x00000000;
	 *_a05f6c04 = 0x0cff0000;
	 *_a05f6c10 = 0x00000000;
	 *_a05f6c14 = 0x00000000;
	 *_a05f6c18 = 0x00000000;
	 *_a05f6c80 = 0xc3500000;
	 *_a05f6c8c = 0x61557f00;
	 *_a05f6ce8 = 0x00000001;
	 *_a05f7404 = 0x0cff0000;
	 *_a05f7408 = 0x00000020;
	 *_a05f740c = 0x00000000;
	 *_a05f7414 = 0x00000000;
	 *_a05f7418 = 0x00000000;
	 *_a05f7484 = 0x00000400;
	 *_a05f7488 = 0x00000200;
	 *_a05f748c = 0x00000200;
	 *_a05f7490 = 0x00000222;
	 *_a05f7494 = 0x00000222;
	 *_a05f74a0 = 0x00002001;
	 *_a05f74a4 = 0x00002001;
	 *_a05f74b4 = 0x00000001;
	 *_a05f74b8 = 0x88437f00;
	 *_a05f7800 = 0x009f0000;
	 *_a05f7804 = 0x0cff0000;
	 *_a05f7808 = 0x00000020;
	 *_a05f780c = 0x00000000;
	 *_a05f7810 = 0x00000005;
	 *_a05f7814 = 0x00000000;
	 *_a05f7818 = 0x00000000;
	 *_a05f781c = 0x00000000;
	 *_a05f7820 = 0x009f0000;
	 *_a05f7824 = 0x0cff0000;
	 *_a05f7828 = 0x00000020;
	 *_a05f782c = 0x00000000;
	 *_a05f7830 = 0x00000005;
	 *_a05f7834 = 0x00000000;
	 *_a05f7838 = 0x00000000;
	 *_a05f783c = 0x00000000;
	 *_a05f7840 = 0x009f0000;
	 *_a05f7844 = 0x0cff0000;
	 *_a05f7848 = 0x00000020;
	 *_a05f784c = 0x00000000;
	 *_a05f7850 = 0x00000005;
	 *_a05f7854 = 0x00000000;
	 *_a05f7858 = 0x00000000;
	 *_a05f785c = 0x00000000;
	 *_a05f7860 = 0x009f0000;
	 *_a05f7864 = 0x0cff0000;
	 *_a05f7868 = 0x00000020;
	 *_a05f786c = 0x00000000;
	 *_a05f7870 = 0x00000005;
	 *_a05f7874 = 0x00000000;
	 *_a05f7878 = 0x00000000;
	 *_a05f787c = 0x00000000;
	 *_a05f7890 = 0x00000fff;
	 *_a05f7894 = 0x00000fff;
	 *_a05f7898 = 0x00000000;
	 *_a05f789c = 0x00000001;
	 *_a05f78a0 = 0x00000000;
	 *_a05f78a4 = 0x00000000;
	 *_a05f78a8 = 0x00000000;
	 *_a05f78ac = 0x00000000;
	 *_a05f78b0 = 0x00000000;
	 *_a05f78b4 = 0x00000000;
	 *_a05f78b8 = 0x00000000;
	 *_a05f78bc = 0x46597f00;
	 *_a05f7c00 = 0x04ff0000;
	 *_a05f7c04 = 0x0cff0000;
	 *_a05f7c08 = 0x00000020;
	 *_a05f7c0c = 0x00000000;
	 *_a05f7c10 = 0x00000000;
	 *_a05f7c14 = 0x00000000;
	 *_a05f7c18 = 0x00000000;
	 *_a05f7c80 = 0x67027f00;
	 *_a05f6900 = 0xffffffff;
	 *_a05f6908 = 0xffffffff;

	 _8c00b8fa();

	 break;

      case 4:
	 *_a05f80a8 = 0x15d1c951;
	 *_a05f80a0 = 0x00000020;
	 sysvars->error_code[0] = 0x00090009;	/* void _8c000000() {       */
	 sysvars->error_code[1] = 0x001b0009;	/* while (1)                */
	 sysvars->error_code[2] = 0x0009affd;	/*    __asm__("sleep\n"); } */
	 sysvars->var1 = 0x0000;
	 sysvars->var2 = 0x0000;
	 sysvars->rte_code[0] = 0x00090009;	/* void _8c000010() {       */
	 sysvars->rte_code[1] = 0x0009002b;	/*    __asm__("rte\n"); }   */ 
	 sysvars->rts_code[0] = 0x00090009;	/* void _8c000018() {       */
	 sysvars->rts_code[1] = 0x0009000b;	/*    __asm__("rts\n"); }   */
	 sysvars->unknown0 = 0x16;
	 sysvars->disc_type = 0;
	 sysvars->old_disc_type = -128;
	 sysvars->IP_vector = (uint32_t *)0x8c008100;
	 break;

      case 6:
	 *_a05f8008 = 0x00000000;
	 
	 _8c00b948(mode);

	 break;

      case 8:
	 _8c00b9d6();

	 break;
	 
      case 10:
	 /* _8c00bee0 */

	 *_a05f80e8 = 0x00160018;
	 *_a05f80ec = 0x000000a8;	/* H position: 0xa8 */
	 *_a05f80f0 = 0x00280028;	/* V position: 0x28 */
	 *_a05f8044 = 0x00800000;

	 *_a05f80c8 = 0x03450000;
	 *_a05f80cc = 0x00150208;
	 *_a05f80d0 = 0x00000100;	/* enable video */
	 *_a05f80d4 = 0x007e0345;	/* H border: 0x7e to 0x345 */
	 *_a05f80d8 = 0x020c0359;	/* scanlines: 0x20c pixels: 0x359 */
	 *_a05f80dc = 0x00280208;	/* V border: 0x280 to 0x208 */
	 *_a05f80e0 = 0x03f1933f;
	 /* jump to _8c00bfac */
	 *_a0702800 = 0x00000000;
	 *_a070289c = 0x00000000;
	 *_a07028a4 = 0x000007ff;
	 *_a07028b4 = 0x00000000;
	 *_a07028bc = 0x000007ff;

	 dst = _a0703000;	/* zero a0703000 to a0703300 */
	 for (i = 0; i < 24; i++) {
	    for (j = 0; i < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0703400;	/* zero a0703400 to a0703600 */
	 for (i = 0; i < 64; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0704000;	/* zero a0704000 to a0704170 */
	 for (i = 0; i < 46; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 _8c00ba42();

	 break;

      case 12:
	 /* _8c00bf28 */

	 *_a05f80e8 = 0x00160008;
	 *_a05f80ec = 0x000000a4;	/* H position: 0xa4 */
	 *_a05f80f0 = 0x00120012;	/* V position: 0x12 */
	 *_a05f8044 = 0x00000000;
	 *_a05f80c8 = 0x03450000;
	 *_a05f80cc = 0x00150104;
	 *_a05f80d0 = 0x00000150;	/* enable video */
	 *_a05f80d4 = 0x007e0345;	/* H border: 0x7e to 0x345 */
	 *_a05f80d8 = 0x020c0359;	/* scanlines: 0x20c pixels: 0x359 */
	 *_a05f80dc = 0x00240204;	/* V border: 0x240 to 0x204 */
	 *_a05f80e0 = 0x07d6c63f;
	 /* jump to _8c00bfac */
	 *_a0702800 = 0x00000000;
	 *_a070289c = 0x00000000;
	 *_a07028a4 = 0x000007ff;
	 *_a07028b4 = 0x00000000;
	 *_a07028bc = 0x000007ff;

	 dst = _a0703000;	/* zero a0703000 to a0703300 */
	 for (i = 0; i < 24; i++) {
	    for (j = 0; i < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0703400;	/* zero a0703400 to a0703600 */
	 for (i = 0; i < 64; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0704000;	/* zero a0704000 to a0704170 */
	 for (i = 0; i < 46; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 _8c00ba42();

	 break;

      case 14:
	 /* _8c00bf6c */
	 
	 *_a05f80e8 = 0x00160008;
	 *_a05f80ec = 0x000000ae;	/* H position: 0xae */
	 *_a05f80f0 = 0x002e002d;	/* V position: 0x2d */
	 *_a05f8044 = 0x00000000;
	 *_a05f80c8 = 0x034b0000;
	 *_a05f80cc = 0x00150136;
	 *_a05f80d0 = 0x00000190;	/* enable video */
	 *_a05f80d4 = 0x008d034b;	/* H border: 0x8d to 0x34b */
	 *_a05f80d8 = 0x0270035f;	/* scanlines: 0x270 pixels: 0x24b */
	 *_a05f80dc = 0x002c026c;	/* V border: 0x2c0 to 0x26c */
	 *_a05f80e0 = 0x07d6a53f;

	 *_a0702800 = 0x00000000;
	 *_a070289c = 0x00000000;
	 *_a07028a4 = 0x000007ff;
	 *_a07028b4 = 0x00000000;
	 *_a07028bc = 0x000007ff;

	 dst = _a0703000;	/* zero a0703000 to a0703300 */
	 for (i = 0; i < 24; i++) {
	    for (j = 0; i < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0703400;	/* zero a0703400 to a0703600 */
	 for (i = 0; i < 64; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 dst = _a0704000;	/* zero a0704000 to a0704170 */
	 for (i = 0; i < 46; i++) {
	    for (j = 0; j < 8; j++)
	       *(dst++) = 0x00000000;
	    wait_aica_fifo();
	 }

	 _8c00ba42();

	 break;
   }

}

void
_8c00b8fa()
{

   unsigned int time;

   if (*_a0600004 == 0x10) {
      *_a0600480 = 0x00;
      dummy = *_a0600480;
      time = reg[TCNT0];
      while (reg[TCNT0] >= (time - 3));
      *_a0600480 = 0x01;
      dummy = *_a0600480;
   }
   sysvars->timer_count = 0xfffb3b4b + reg[TCNT0];

}

void
_8c00b92c(arg1)
   int arg1;
{

   sysvars->var1 = reg[PDTRA];

   for (i = 0; i < 4; i++) {
	   sysvars->var2 = reg[PDTRA];
      if (arg1 == sysvars->var2 & 0x03) {
	 return;
      }
   }
   reg[PR] = (uint32_t *)0x8c000000;		/* loop forever */

}

void
_8c00b948(mode)
   int mode;
{

   int old_PCTRA, i;

   if (mode == 0) {
      old_PCTRA = reg[PCTRA];
      i = old_PCTRA | 0x08;
      reg[PCTRA] = i;
      reg[PDTRA] = reg[PDTRA] | 0x03;
      _8c00b92c(3);
      reg[PCTRA] = i | 0x03;
      _8c00b92c(3);
      reg[PDTRA] = reg[PDTRA] & 0xfffe;
      _8c00b92c(0);
      reg[PCTRA] = i;
      _8c00b92c(3);
      reg[PCTRA] = i | 0x04;
      _8c00b92c(3);
      reg[PDTRA] = reg[PDTRA] & 0xfffd;
      _8c00b92c(0);
      reg[PCTRA] = old_PCTRA;
   }

   i = reg[PDTRA] & 0x0300;
   *_a0702c00 = i | (*_a0702c00 & 0x0000fcff);

   *DisplayCable = i>>2;

   *_8c000070 =   (int)*_a021a000;		/* get stuff from flash */
   *_8c000074 =  (char)*_a021a004;
   *_8c000068 = (short)*_a021a056;
   *_8c00006a = (short)*_a021a058;
   *_8c00006c = (short)*_a021a05a;
   *_8c00006e = (short)*_a021a05c;

}

void
_8c00b9d6()
{

   int i;

   switch (*_8c000074) {
      case 0x33:
	 i = 0x0014;
	 break;
      case 0x32:
	 i = 0x000c;
	 break;
      case 0x31:
	 i = 0x0004;
	 break;
      default:
	 i = 0x0000;
	 break;
   }

   reg[PDTRA] = i & 0x001c;

   if ((*DisplayCable>>6) == 0) {
      i = 0x0a;
   }
   else switch (*_8c000074) {
      case 0x33:
      case 0x31:
	 i = 0x0e;
	 break;
      default:
	 i = 0x0c;
	 break;
   }

   _8c00b800(i);

   *BorderCol = *_a05f8040;

}

void
wait_aica_fifo()	/* _8c00ba26 */
{

   for (i = 0; i < 0x1800; i++) {
      if (*_a05f688c & 0x01 == 0)
	 break;
   }

}

void
_8c00ba42()
{

   unsigned int *dst;

   *_a07045c0 = 0x00000000;
   *_a07045c4 = 0x00000000;

   dst = _a0700000;
   for (i = 0; i < 65; i++) {
      for (j = 0; j < 3; j++) {
	 *(dst) = 0x00000000;
	 *(dst + 0x04) = 0x00000000;
         *(dst + 0x08) = 0x00000000;
         *(dst + 0x0c) = 0x00000000;
         *(dst + 0x10) = 0x00000000;
         *(dst + 0x14) = 0x00000000;

         wait_aica_fifo();
	 dst += 0x80;
      }
   }

   _8c00bab8(1);

   dst = _a0800000;
   for (i = 0; i < 65536; i++) {
      for (j = 0; j < 8; j++)
         *(dst++) = 0x00000000;

      wait_aica_fifo();
   }

   *_a0800000 = 0xea000007;
   *_a0800004 = 0xea000010;
   *_a0800008 = 0xea00000f;
   *_a080000c = 0xea00000e;
   *_a0800010 = 0xea00000d;
   *_a0800014 = 0xea00000c;
   *_a0800018 = 0xea00000b;
   *_a080001c = 0xea00000a;
   wait_aica_fifo();
   *_a0800020 = 0xe1a00000;
   *_a0800024 = 0xe59fd040;
   *_a0800028 = 0xe10fa000;
   *_a080002c = 0xe38aa040;
   *_a0800030 = 0xe129f00a;
   *_a0800034 = 0xe59f000c;
   *_a0800038 = 0xe2800001;
   *_a080003c = 0xe58f0004;
   wait_aica_fifo();
   *_a0800040 = 0xeafffffb;
   *_a0800044 = 0xe1a00000;
   *_a0800048 = 0x00000000;
   *_a080004c = 0xe24ee004;
   *_a0800050 = 0xe28fd008;
   *_a0800054 = 0xe58de000;
   *_a0800058 = 0xe8dd8000;
   *_a080005c = 0xe1a00000;
   wait_aica_fifo();
   *_a0800060 = 0x00000000;
   *_a0800064 = 0x00000000;
   *_a0800068 = 0x00000000;
   *_a080006c = 0x00000068;
   *_a0800070 = 0x00000000;
   *_a0800074 = 0x00000000;
   *_a0800078 = 0x00000000;
   *_a080007c = 0x00000000;
   wait_aica_fifo();

   *_a0702c00 &= 0xfffffffe;

}





struct _time_str {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
};

uint32_t
8c00c000(cmd)
	uint32_t cmd;
{

	uint32_t *start, *end;
	uint8_t buf1[12],	/* R15 + 0x40 */
		buf2[12],	/* R15 + 0x34 */
		buf3[12],	/* R15 + 0x28 */
		buf4[12];	/* R15 + 0x1c */
	struct _time_str str;	/* R15 + 0x10 */
	uint32_t i,		/* R15 + 3 */
		 j;		/* R15 + 2 */
	uint32_t var1;		/* R15 */
	uint32_t var2;		/* R15 + 0x04 */
	register uint32_t retval;	/* R14 */

	/* bounds checking */
	if (cmd >= 5) {
		return (-1);
	}

	switch (cmd) {
		case INIT:
			/* 8c00c020 - clear area at 8c00ee00-8c00ee50 */
			start = (uint32_t *)0x8c00ee00;
			end = (uint32_t *)0x8c00ee50;
			while (start != end) {
				*start++ = 0;
			}
			return;			/* no return value? */
			break;

		case UNKNOWN1:
			/* 8c00cbb2 */
			if (sysvars->gd_drv.stat == STAT_OPEN
					|| sysvars->gd_drv.stat == STAT_NODISK
					|| sysvars->gd_drv.stat == RETRY
					|| sysvars->gd_drv.stat == STAT_ERROR) {
				return (_8c00c040(1, 1, 0));
			} else {
				return (_8c00c040(0, 1, 0));
			}
			break;

		case UNKNOWN2:
			/* 8c00cbc6 */
			if (sysvars->gd_drv.stat == STAT_OPEN
					|| sysvars->gd_drv.stat == STAT_NODISK
					|| sysvars->gd_drv.stat == RETRY
					|| sysvars->gd_drv.stat == STAT_ERROR) {
				for (i = 0; i < 100; i++) {
					wait_for_new_frame();
				}
			}
			return (_8c00c040(2, 2, 0));
			break;

		case SETDATE:
			/* 8c00cbec */
			if (*(uint8_t *)0x8c000027 != 0) {
				/* 8c00ccf2 */
				return (0);
			}
			R13 = 0;
			retval = _8c00cb80();
			i = 0x00278d00 + retval;	/* i = R15 + 0x0c */
			j = 0x001e5280 + retval;	/* j = R15 + 0x08 */
			_8c00d340();
			_8c00d4f4(retval, buf1);	/* buf1 = R15 + 0x40 */
			_8c00d4f4(j, buf2);		/* buf2 = R15 + 0x34 */
			_8c00d4f4(i, buf3);		/* buf3 = R15 + 0x28 */
			_8c00d38c(buf4);		/* buf4 = R15 + 0x1c */
			j = (uint32_t)sysvars->IP_vector;
			_8c00d474(buf4, &var1);		/* var1 = R15 */
			if (retval == -1) {
				R13 = 0x20;
				_8c00cd64(&str);	/* str = R15 + 0x10 */
				_8c00d474(&str, &var2); /* var2 = R15 + 0x04 */
				/* 8c00cc62 */
				if ((uint32_t)(var1 + 0xff000000) < 0x00ed4e00) {
					var1 = var1 + var2 + 0xff000000;
				}
				/* 8c00cc7a */
			}
			/* 8c00cc7a */
			sysvars->IP_vector = var1;
			if (_8c00d438(buf1, buf4) != 1 && _8c00d438(buf3, buf4) == -1) {
				/* 8c00cca6 */
				if (_8c00d438(buf2, buf4) == -1) {
					_8c00d474(buf4, &var1);
					/* check up on this later! */
					*(uint32_t *)0x8c000078 = var1;
					R13 = R13 | 0x10;
					R0 = 3;
				} else {
					/* 8c00ccc8 */
					R0 = 1;
				}
				/* 8c00ccca */
				sysvars->date_set = R0;
			} else {
				/* 8c00cca0 */
				sysvars->date_sate = 0;
			}
			/* 8c00cccc */
			if (sysvars->date_set == 0) {
				sysvars->current_color = *HW32(0xa05f8040);
				/* this should pop up menu and ask for date */
				_8c00c040(2, 3, 1);
			}
			/* 8c00cce4 */

			if (R13 != 0) {
				_8c00cdf0(R13, 0x8cfe0000);
			}
			/* 8c00ccee */
			sysvars->IP_vectpr = (uint32_t *)j;
			return (0);
			break;

		case OPENMENU:
			/* 8c00cd00 */
			sysvars->current_color = *HW32(0xa05f8040);
			if (sysvars->display_cable == 0) {
				_8c00c040(3, 3, 1);
			}
			/* 8c00cd1a */
			return (_8c00c040(sysvars->menu_param, 3, 1));
			break;
	}

}

uint32_t
_8c00c040(a, b, c)
	uint32_t a, b, c;
{
	return (_8c00c880(a, b, c));
}

uint32_t
_8c00c880(a, b, c)
	uint32_t a, b, c;
{
	reg[SR] |= 0x00f0;

	if (b & 0x00000001 != 0) {
		_8c00c9e2();
	}
	
	for (i = 0; i < 8; i++) {
		(uint32_t *)0x8c00ee04[i] = *(uint32_t *)0x8c00d30c[i];
	}

	*(uint32_t *)0x8c00ee00 = reg[VBR];
	reg[VBR] = 0x8c00c000;

	if (b & 0x00000001 != 0) {
		_8c00c9b8();
		_8c00dd60(0, 0);
		for (i = 0; i < 8; i++) {
			(uint32_t *)0x8c00d30c[i] = 0;
		}
	} else {
		/* 8c00c8fa */
		for (i = 0; i < 8; i++) {
			(uint32_t *)0x8c00d30c[i] = (uint32_t *)0x8c00ee28[i];
		}
	}
	/* 8c00c90c */
	*(uint32_t *)0xa05f6920 = *(uint32_t *)0xa05f6920 & 0xbfff;
	reg[SR] = reg[SR] & 0xff0f;

	_8c00ca78(a, b, c);

	reg[SR] = reg[SR] | 0x00f0;

	if (b & 0x02 != 0) {
		_8c00c9da();
		for (i = 0; i < 8; i++) {
			(uint32_t *)0x8c00ee28[i] = 0;
		}
	} else {
		/* 8c00c94c */
		for (i = 0; i < 8; i++) {
			(uint32_t *)0x8c00ee28[i] = *(uint32_t *)0x8c00d30c[i];
		}
	}
	/* 8c00c95e */
	reg[VBR] = *(uint32_t *)0x8c00ee00;
	for (i = 0; i < 8; i++) {
		(uint32_t *)8c00d30c[i] = (uint32_t *)0x8c00ee04;
	}
	reg[SR] = reg[SR] & 0xff0f;
}

void
_8c00ca78(a, b, c)
	uint32_t a;	/* R11 */
	uint32_t b;
	uint32_t c;
{

	uint32_t i = 0;		/* R4 */

	wait_for_new_frame();

	if (*(uint16_t *)0xff800030 & 0x0300) {
		/* 8c00ca9c */
		switch (sysvars->debug_switches.u0.switches.levelLo) {
			case '1':
			case '3':
				/* 8c00cab6 */
				i = 0x0200;
				break;
			case '0':
			case '2':
			default:
				/* 8c00cab0 */
				i = 0x0100;
				break;
		}
	}
	/* 8c00cab8 */

	if (c == 1) {
		/* 8c00cad4 */
		clear_8c200000_to_8c300000()
		check_lib_handles(8c090000);
		check_lib_handles(8c010000);
		_8c010000(a | i);
		_8c00dcb6(8c010000);
		_8c00dcb6(8c090000);
	} else {
		/* 8c00cae6 */
		if (a < 2) {
			clear_8c200000_to_8c300000()
			check_lib_handles(8c090000);
			check_lib_handles(8c184000);
			_8c184000(a | i);	/* this plays animation */
		} else {
			/* 8c00cb02 */
			reg[SR] = reg[SR] & 0xff0f;
			/* 8c00cb0c */
			_8c184000(a | i);
			_8c00dcb6(8c184000);
			_8c00dcb6(8c090000);
		}
	}
	/* 8c00cb0c */
}

void
wait_for_new_frame()		/* 0x8c00cb2a */
{
	while (*HW32(0xa05f810c) & 0x2000 == 0);
	while (*HW32(0xa05f810c) & 0x2000 != 0);
}

void
clear_8c200000_to_8c300000()	/* 0x8c00cb3e */
{
	uint32_t i;
	uint32_t *p = 0x8c200000;

	for (i = 0; i < 0x40000; i++) {
		*p++ = 0;
	}
}

uint32_t
_8c00cb80()
{
	_8c00cdf0(0, 0x8cfe0000);
}

/* Really! This function is BUGGED! Compared with a Euro bootrom and
 * that one makes sense! Check if it can be exploited somehow!
 */
uint32_t
_8c00cd28(addr, size)
	sint8_t *addr;
	sint32_t size;
{
	uint32_t bug;	 /* bug = R6, used uninitialized! */
	sint32_t i;

	for (i = 0; i < size; i++) {
		if (*addr > 57) {
			bug = (bug<<4) | (*addr - 55) & 0x0f;
		} else {
			bug = (bug<<4) | (*addr - 48) & 0x0f;
		}
	}
	return (bug);
}

/* Corrected version from euro bootrom */
/* uint32_t
_8c00cd2a(addr, size)
	sint8_t *addr;
	sint32_t size;
{

	int val = 0;	// val = R6
	int i;		// i = R14

	for (i = 0; i < size; i++) {
		if (*addr > 57) {
			val = (val<<4) | (*addr++ - 55) & 0x0f;
		} else {
			val = (val<<4) | (*addr++ - 48) & 0x0f;
		}
	}
	return (val);
	
}

*/

void
_8c00cd64(str)
	struct _time_str *str;
{
	str->year = (uint16_t)_8c00cd28((uint8_t *)0xa021a02d, 4);
	str->month = (uint8_t)_8c00cd28((uint8_t *)0xa021a031, 2);
	str->day = (uint8_t)_8c00cd28((uint8_t *)0xa021a033, 2);
	str->hour = (uint8_t)_8c00cd28((uint8_t *)0xa021a035, 2);
	str->min = (uint8_t)_8c00cd28((uint8_t *)0xa021a037, 2);
	str->sec = 0;
}

void
_8c00cdf0(arg, addr)
	uint32_t arg;
	uint32_t addr;
{

	uint32_t retval;
	char buf[60];

	retval = _8c00e39e(2);
	if (retval == -8 && _8c00e4a4(0)) {
		return (-1);
	} else {
		/* 8c00ce1a */
		if (retval) {
			return (-1);
		}
	}
	/* 8c00ce1e */
	if (_8c00e534(5, buf)) {
		return (-1);
	}

	retval = _8c00e5b2(0);
	while (retval == 2) {
		retval = _8c00e5b2(2);
	}
	/* 8c00ce3e */
	if (retval != 1) {
		return (-1);
	}

	if (arg == 0) {
		p = (uint8_t *)0x8c000078;

		for (i = 0; i < 8; i++) {
			*p++ = buf[i];
		}
		return (0);
	}
	/* 8c00ce60 */
	p = (uint8_t *)0x8c00007f;	/* R5 */
	p2 = buf + 7;			/* p2 = R4 */
	for (i = 0; i < 5; i++) {
		if (arg & 0x01) {
			*p2-- = *p--;
			if (i == 4) {
				*p2-- = *p--;
				*p2-- = *p--;
				*p2-- = *p--;
			}
			/* 8c00ce90 */
		}
		/* 8c00c090 */
		arg = arg>>1;
		if (i == 4) {
			p -= 3;
			p2 -= 3;
		}
	}
	/* 8c00cea6 */

	if (arg & 0x01) {
		p2 += 12;
		*--p2 = *--p;
		*--p2 = *--p;
		*--p2 = *--p;
		*--p2 = *--p;
	}
	/* 8c00cec4 */
	if (_8c00e570(5, buf) != 0) {
		return (-1);
	}

	retval = _8c00e5b2(0);
	while (retval == 2) {
		retval = _8c00e5b2(retval);
	}
	if (retval != 1) {
		return (-1);
	}

	return (0);

}

void
_8c00d240(size, buf, data)
	uint32_t size;
	uint32_t *buf;
	uint32_t *data;
{

	uint32_t val = *(uint32_t *)(data + size - 4);

	switch (size) {
		case 64:
			buf[15] = val;
			buf[14] = data[14];
			val = data[13];
		case 56:
			buf[13] = val;
			buf[12] = data[12];
			val = data[11];
		case 48:
			buf[11] = val;
			buf[10] = data[10];
			val = data[9];
		case 40:
			buf[9] = val;
			buf[8] = data[8];
			val = data[7];
		case 32:
			buf[7] = val;
			buf[6] = data[6];
			val = data[5];
		case 24:
			buf[5] = val;
			buf[4] = data[4];
			val = data[3];
		case 16:
			buf[3] = val;
			buf[2] = data[2];
			val = data[1];
		case 8:
			buf[1] = val;
			buf[0] = data[0];
		case 0:
			break;
	}
}

void
_8c00d474(str, var)
	struct _time_str *str;
	uint32_t *var;
{
	uint32_t buf[12];
	uint32_t a, b;
	uint32_t time;		/* time = R6 */

	_8c00d240(sizeof(buf), buf, _8c00eac0);	/* passing args in r0, r1, r2 for some reason... */

	a = str->year - 1950;
	b = a + 2;

	time = (b / 4) + (a * 365);

	if (b & 0x03 && str->month <= 2) {
		time--;
	}
	/* 8c00d4ae */
	time = time + buf[str->month - 1];
	time = time + str->day - 1;
	time = time * 24;
	time = time + str->hour;
	time = time * 60;
	time = time + str->min;
	time = time * 60;
	time = time + str->sec;

	*var = time;

}

sint32_t
memcmp(ptr1, ptr2, n)	/* 8c00d2bc */
	void *ptr1;
	void *ptr2;
	uint32_t n;
{
	if (n == 0) {
		return (0);
	}

	for (i = 0; i < n; i++) {
		if (*ptr1++ != *ptr2++) {
			break;
		}
	}
	return (*--ptr1 - *--ptr2);
}

sint32_t
check_lib_handles(arg)		/* 8c00dc38 */
	void *arg;		/* R10 */
{
	uint32_t i = 16;	/* R6 */
	void *addr;

	if (memcmp((void *)(arg + 0x30), (void *)0x8c00eb58, 16) != 0) {
		return (-1);
	}
	/* 8c00dc80 */
	for (i = 1;; i++) {
		addr = (void *)((i * 32) + (arg + 32));
		if (memcmp((void *)(addr + 16), (void *)0x8c00eb68, 16) == 0) {
			break;
		}
		_8c00dae0();
	}
	return (0);

}

/* days in the months */
uint32_t _8c00eac0[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

uint8_t _8c00eb58[] = { "Lib Handle Start" };	/* 8c00eb58 */
uint8_t _8c00eb68[] = { "Lib Handle End  " };	/* 8c00eb68 */














/* ----------------------------------------------------------------- */
/* Really, really old version here */

void
_8c00c000(func)		/* this _might_ play animation && open shell aswell */
   unsigned int func;
{

   if (func >= 5) {
      return;		/* actually seems to want to return -1 but this 
			   functions returns void...prolly an asm-programmer
			   messed it up...doesnt make any difference though */

   }

   switch (func) {
      case 0:		/* clear _8c00ee00 to _8c00ee50 */
	 /* _8c00c020 */
	 ptr1 = _8c00ee00;
	 ptr2 = _8c00ee50;
	 size = ptr2 - ptr1;
	 for (i = 0; i < size/4; i++) {
	    *(ptr1++) = 0x00000000;
	 }
	 return;
	 break;

      case 1:
	 /* _8c00cbb2 */
	 if (*GdDrvStatus >= STAT_OPEN)
	    _8c00c040(1, 1, 0);
	 else
	    _8c00c040(0, 1, 0);
	 break;

      case 2:
	 /* _8c00cbc6 */
	 if (*GdDrvStatus >= STAT_OPEN) {
	    for (i = 0; i < 100; i++)
	       wait_vsync();
	 }
	 _8c00c040(2, 2, 0);
	 break;

      case 3:
	 /* _8c00cbec */
	 if (*_8c000027 != 0) {
	    return (0);
	 }
	 i = _8c00cb80();
	 saved1 = i + 0x00278d00;
	 saved2 = i + 0x001e5280;
	 _8c00d340();
	 _8c00d4f4(i, buf1);
	 _8c00d4f4(saved2, buf2);
	 _8c00d4f4(saved1, buf3);
	 _8c00d38c(buf4);

	 saved2 = *IPVector;

	 if (_8c00d474(buf4, /* R15 */) == -1) {
	    _8c00cd64(R15 + 4);	/* bah..need to work out what these really are */
	 }
	 /* _8c00cc7a */

	 break;
      case 4:
	 /* _8c00cd00 */

	 *BorderCol = *_a05f8040;

	 if (*DisplayCable == 0) {
	    _8c00c040(3, 3, 1);
	 }
	 /* _8c00cd1a */
	 _8c00c040(*MenuParam, 3, 1);

	 break;
   }

}

void
_8c00c040(arg1, arg2, arg3)
   unsigned int arg1, arg2, arg3;
{
   _8c00c880(arg1, arg2, arg3);
}

void
_8c00c880(arg1, arg2, arg3)
   unsigned int arg1, arg2, arg3;
{

   reg[SR] |= 0x00f0;

   if (arg2 & 0x01 != 0) {
      _8c00c9e2();
   }

   for (i = 0; i < 9; i++) {
      _8c00ee04[i] = *_8c00d30c[i];
   }

   *_8c00ee00 = reg[VBR];
   reg[VBR] = _8c00c000;

   if (arg2 & 0x01 != 0) {
      _8c00c9b8();
      _8c00dd60(0, 0);
      for (i = 0; i < 9; i++) {
	 *_8c00d30c[i] = 0x00000000;
      }
   }
   else {
      for (i = 0; i < 9; i++) {
	 *_8c00d30c[i] = _8c00ee28[i];
      }
   }
   /* _8c00c90c */

   *_a05f6920 = *_a05f6920 & 0x0000bfff;

   reg[SR] = reg[SR] & 0xff0f;
   _8c00ca78(arg1, arg2, arg3);
   reg[SR] = reg[SR] | 0x00f0;

   if (arg2 != 2) {
      _8c00c9da();
      for (i = 0; i < 9; i++) {
	 _8c00ee28[i] = 0x00000000;
      }
   }
   else {
      for (i = 0; i < 9; i++) {
         _8c00ee28[i] = *_8c00d30c[i];
      }
   }
   /* _8c00c95e */

   reg[VBR] = *_8c00ee00;

   for (i = 0; i < 9; i++) {
      *_8c00d30c[i] = _8c00ee04[i];
   }

   reg[SR] = reg[SR] & 0xff0f;

}

int
_8c00c9da()
{
   return (_8c00dcb6(*_8c00ea9c));
}

void
wait_vsync()	/* _8c00cb2a */	/* this is _not_ vsync...change later */
{

   while (*_a05f810c & 0x2000 == 0);
   while (*_a05f810c & 0x2000 != 0);

}

int
_8c00d2bc(arg1, addr, arg3)
   int arg1, *addr, arg3;
{

   if (arg3 == 0) {
      return (0);
   }

   if (arg3 < 0) {
      /* bah...fucked up function */
   }

}

int
_8c00dcb6(arg1)
   int arg1;
{

   if (_8c00d2bc(arg + 0x30, _8c00eb58, 0x10) != 0) {
      return (-1);
   }
   
   for (i = 1;; i++) {
      if (_8c00d2bc((i<<5) + 0x10, _8c00eb68, 0x10) == 0) {
	 break;
      }
      _8c00db52(i<<5);
   }

   return (0);

}
