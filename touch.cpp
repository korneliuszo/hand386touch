/*
 * hello.cpp
 *
 *  Created on: Jun 24, 2023
 *      Author: kosa
 */

#include <vmm.hpp>
#include <gcc.h>
#include <stdbool.h>
#include <dev_vxd_dev_vmm.h>
#include <vxdcall.hpp>
#include <stdio.h>

#include <mouse.hpp>

extern "C"
[[gnu::section(".ddb"), gnu::used, gnu::visibility ("default")]]
const DDB DDB = Init_DDB(Device_ID::Undefined_Device_ID,
		1, 0, "TOUCH", Init_Order::Undefined_Init_Order);

Mouse mouse;
//mouse.Set_Mouse_Position(positions[seq].x, positions[seq].y);



class GPIO_Manager
{
	static inline uint8_t readchip(uint8_t addr)
	{
		uint8_t ret;
		asm volatile(
				"outb %%al,$0x22\n\t"
				"nop\n\tnop\n\t"
				"inb $0x23,%%al\n\t"
				"nop\n\tnop\n\t"
			:"=ax"(ret)
			:"ax"(addr));
		return ret;
	}

	static inline void writechip(uint8_t addr, uint8_t val)
	{
		asm volatile(
				"outb %%al,$0x22\n\t"
				"nop\n\tnop\n\t"
				"xchg %%ah,%%al\n\t"
				"outb %%al,$0x23\n\t"
				"nop\n\tnop\n\t"
			:
			:"ax"((val<<8) | addr));
	}
public:
	GPIO_Manager()
	{
		writechip(0x13,0xc5);
	}
	~GPIO_Manager()
	{
		writechip(0x00,0x4c);
	}
	void set_dir(uint8_t oe)
	{
		writechip(0x4e,oe);
	}
	void set_output(uint8_t out)
	{
		writechip(0x47,out);
	}
	void set_dirh(uint8_t oe)
	{
		writechip(0x4f,oe);
	}
	void set_outputh(uint8_t out)
	{
		writechip(0x4d,out);
	}
	uint8_t get_input()
	{
		return readchip(0x46);
	}
	uint8_t get_inputh()
	{
		return readchip(0x4c);
	}
};



void spi_init()
{
	GPIO_Manager manager;
	manager.set_output((1<<5));
	manager.set_dir((1<<7)|(1<<5)|(1<<6));
	manager.set_dirh((0<<0));
}

void delayticks(uint64_t ticks)
{
	uint64_t end = VTD_Get_Real_Time() + ticks;
	while(VTD_Get_Real_Time() < end);
}

void spi_transfer(uint8_t *buff, int len)
{
	GPIO_Manager manager;
	manager.set_output(0);

	for(int i=0;i<len;i++)
	{
		delayticks(60/0.8);
		uint8_t val = buff[i];
		for(int j=0;j<8;j++)
		{
			manager.set_output(((!!(val&0x80))<<7)|(1<<6));
			manager.set_output(((!!(val&0x80))<<7)|(0<<6));
			val = (val<<1) | ((manager.get_inputh()&(1<<0))?1:0);
		}
		buff[i] = val;
	}
	//manager.set_output((1<<5));

}

volatile uint16_t mx,my;
volatile bool last_clicked,clicked;
void timeout(uint32_t crs);


void mouse_complete(void* obj)
{
	Set_Global_Time_Out(1, 0,
			(const void *)saved_flags<
			timeout,'B'>);
}

void mouse_secondclick(void* obj)
{
	(void)obj;
	mouse.Set_Mouse_Position({mouse_complete,nullptr}, mx, my, clicked);
}


void timeout(uint32_t crs)
{
	uint8_t ver[5]={};
	spi_transfer(ver,5);
	if((ver[0]&0x80) == 0x80)
	{
#ifndef NDEBUG
		char str[64];
		snprintf(str,64,"VER: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\r\n",
			ver[0],ver[1],ver[2],ver[3],ver[4]);
		Out_Debug_String(str);
#endif
		clicked = !!(ver[0]&0x01);
		uint16_t x = ver[1] | (ver[2]<<7);
		uint16_t y = ver[3] | (ver[4]<<7);
		mx=(((uint32_t)(x-0x40))*(1084313))>>16;
		my=-(((uint32_t)(y-0xC0))*(1170609))>>16;
		if(!last_clicked)
			mouse.Set_Mouse_Position({mouse_secondclick,nullptr}, mx, my, false);
		else
			mouse.Set_Mouse_Position({mouse_complete,nullptr}, mx, my, clicked);
		last_clicked=clicked;
	}
	else
	{
		if(last_clicked)
		{
#ifndef NDEBUG
			Out_Debug_String("Buuu\r\n");
#endif
			last_clicked = false;
			mouse.Set_Mouse_Position({mouse_complete,nullptr}, mx, my, false);
		}
		else
		{
			Set_Global_Time_Out(1, 0,
					(const void *)saved_flags<
					timeout,'B'>);
		}
	}
	return;
}

bool Device_Init(uint32_t cmdtail, uint32_t sysVM, uint32_t crs)
{
	bool exists = mouse.Init(sysVM, crs);
	if(exists)
	{
		spi_init();
		uint8_t raddr[3] = {0x55,0x01,0x22};
		spi_transfer(raddr,3);
		delayticks(200);

		uint8_t resp[5]={};
		spi_transfer(resp,5);
		if(resp[0]!=0x55)
		{
			Out_Debug_String("No touchpad?\r\n");
			return 1;
		}

		uint8_t buff[8] = {0x55,0x06,0x21,0x00,(uint8_t)(0x0B+resp[4]),0x02,0x01,0x51};
		spi_transfer(buff,8);
		delayticks(200);

		uint8_t sync[1];
		do{
			sync[0]=0x00;
			spi_transfer(sync,1);
#ifndef NDEBUG
			char b[20];
			snprintf(b,20,"U: 0x%02x\r\n",sync[0]);
			Out_Debug_String(b);
#endif
		}while((sync[0])!=0x4d);
#if 0
		while(true)
		{
			uint8_t ver[5]={};
			spi_transfer(ver,5);
			char str[64];
			snprintf(str,64,"VER: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\r\n",
				ver[0],ver[1],ver[2],ver[3],ver[4]);
			Out_Debug_String(str);
		}
#endif
		Set_Global_Time_Out(1, 0,
				(const void *)saved_flags<
				timeout,'B'>);

	}
	return exists;
}

bool Focus(uint32_t VID, uint32_t flags, uint32_t VM, uint32_t crs)
{
	mouse.Focus(VID, flags, VM);
	return 1;
}

void Crit_Init(){
	Out_Debug_String("Hello from touchscreen\r\n");
}

[[gnu::section(".vxd_control"), gnu::used]]
static const Control_callback ahndlr =
		Init_Control_callback
		<System_Control::Device_Init,Device_Init,'S','b','B'>();


[[gnu::section(".vxd_control"), gnu::used]]
static const Control_callback bhndlr =
		Init_Control_callback
		<System_Control::Set_Device_Focus,Focus,'d','S','b','B'>();
