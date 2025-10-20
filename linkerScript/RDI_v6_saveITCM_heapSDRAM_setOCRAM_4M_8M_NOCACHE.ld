_ocram_block_count = 0;

MEMORY
{
    ITCM (rwx):             ORIGIN = 0x00000000, LENGTH = (512 - (_ocram_block_count * 32)) * 1024
    DTCM (rwx):             ORIGIN = 0x20000000, LENGTH = (512 - (_ocram_block_count * 32)) * 1024
    RAM (rwx):              ORIGIN = 0x20200000, LENGTH = (512 + (_ocram_block_count * 32)) * 1024
    OCRAM (rwx):            ORIGIN = 0x20280000, LENGTH = (_ocram_block_count * 32) * 1024
	FLASH (rwx):            ORIGIN = 0x60000000, LENGTH = 16128K
	ERAM_NOCACHE (rwx):     ORIGIN = 0x80000000, LENGTH = 4096K
    ERAM_NOCACHE_PCM (rwx): ORIGIN = 0x80400000, LENGTH = 8192K 
    ERAM (rwx):             ORIGIN = 0x80C00000, LENGTH = 20480K 
}

ENTRY(ImageVectorTable)

SECTIONS
{
	/DISCARD/ :	{
		/* Unused exception related info that only wastes space */
		*(.ARM.exidx);
		*(.ARM.exidx.*);
		*(.ARM.extab.*);
		*(.ARM.extab.text*);
		*(.gnu.linkonce.armexidx.*);
	}

	.text.headers : {
		KEEP(*(.flashconfig))
		FILL(0xFF)
		. = ORIGIN(FLASH) + 0x1000;
		KEEP(*(.ivt))
		KEEP(*(.bootdata))
		. = ALIGN(1024);
	} > FLASH

	.text.code : {
		KEEP(*(.startup))
		*(.flashmem*)
		*(EXCLUDE_FILE (
			/* Have to add these to exclude in .text.code, put in text.itcm */
			eeprom.c.o
			interrupt.c.o
			memcpy-armv7m.S.o
			/* Optional */
			/*
			usb_serial.c.o
			usb_serial2.c.o
			usb_serial3.c.o
			serialEventUSB1.cpp.o
			serialEventUSB2.cpp.o
			*/
			ADC.cpp.o
			ADC_Module.cpp.o
			/* Limited benefit for SdFat */
			FatFile.cpp.o
			ExFatFile.cpp.o
			ExFatFileWrite.cpp.o

			SD.cpp.o
			SdioTeensy.cpp.o
			SdioTeensy4.cpp.o
			FsFile.cpp.o
			FatFile.cpp.o

		).text*)
		. = ALIGN(4);
		KEEP(*(.init))
		__preinit_array_start = .;
		KEEP (*(.preinit_array))
		__preinit_array_end = .;
		__init_array_start = .;
		KEEP (*(.init_array))
		__init_array_end = .;
		. = ALIGN(4);
	} > FLASH

	.text.progmem : {
		*(.progmem*)
		. = ALIGN(4);
	} > FLASH

	.text.itcm : {
		. = . + 32; /* MPU to trap NULL pointer deref */
		*(.fastrun)
		/* Have to add these to keep in text.itcm (excluded above from text.code) */
		*(eeprom.c.o .text*)
		*(interrupt.c.o .text*)
		*(memcpy-armv7m.S.o .text*)
		/* Optional */
		/*
		*(usb_serial.c.o .text*)
		*(usb_serial2.c.o .text*)
		*(usb_serial3.c.o .text*)
		*(serialEventUSB1.cpp.o .text*)
		*(serialEventUSB2.cpp.o .text*)
		*/
		*(ADC.cpp.o .text*)
		*(ADC_Module.cpp.o .text*)
		/* Limited benefit for SdFat */
		*(FatFile.cpp.o .text*)
		*(ExFatFile.cpp.o .text*)
		*(ExFatFileWrite.cpp.o .text*)
		*(SD.cpp.o .text*)
		*(SdioTeensy.cpp.o .text*)
		*(SdioTeensy4.cpp.o .text*)
		*(FsFile.cpp.o .text*)
		*(FatFile.cpp.o .text*)
		. = ALIGN(16);
	} > ITCM  AT> FLASH

	.data : {
		*(.endpoint_queue)
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.data*)))
		KEEP(*(.vectorsram))
	} > DTCM  AT> FLASH

	.bss ALIGN(4) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(COMMON)))
		. = ALIGN(32);
		. = . + 32; /* MPU to trap stack overflow */
	} > DTCM

	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM

	.bss.extramnocache (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.externalram_nocache)))
		. = ALIGN(32);
	} > ERAM_NOCACHE

	.bss.extramnocache_pcm (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.externalram_nocache_pcm)))
		. = ALIGN(32);
	} > ERAM_NOCACHE_PCM

	.bss.extram (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.externalram)))
		. = ALIGN(32);
	} > ERAM

	.text.csf : {
		FILL(0xFF)
		. = ALIGN(1024);
		KEEP(*(.csf))
		__text_csf_end = .;
	} > FLASH

	__exidx_start = 0;
	__exidx_end = 0;

	_stext = ADDR(.text.itcm);
	_etext = ADDR(.text.itcm) + SIZEOF(.text.itcm);
	_stextload = LOADADDR(.text.itcm);

	_sdata = ADDR(.data);
	_edata = ADDR(.data) + SIZEOF(.data);
	_sdataload = LOADADDR(.data);

	_sbss = ADDR(.bss);
	_ebss = ADDR(.bss) + SIZEOF(.bss);

	_dma_start = ADDR(.bss.dma);
	_dma_end = ADDR(.bss.dma) + SIZEOF(.bss.dma);

	_extram_start = ADDR(.bss.extram);
	_extram_end = ADDR(.bss.extram) + SIZEOF(.bss.extram);

	_heap_start = _extram_end;
	_heap_end = ORIGIN(ERAM) + LENGTH(ERAM);

	_itcm_block_count = (SIZEOF(.text.itcm) + 0x7FFF) >> 15;

	_flexram_bank_config = (0x55555555 >> (32 - 2 * _ocram_block_count)) << (2 * (16 - _ocram_block_count)) | (0xAAAAAAAA >> (2 * (_ocram_block_count + _itcm_block_count))) << (2 * _itcm_block_count) | (0xFFFFFFFF >> (32 - 2 * _itcm_block_count));

	/* move the stack pointer back by _ocram_block_count banks so it starts in DTCM not OCRAM */
	_estack = ORIGIN(DTCM) + ((16 - _itcm_block_count - _ocram_block_count) << 15);

	_flashimagelen = __text_csf_end - ORIGIN(FLASH);
	_teensy_model_identifier = 0x26;

	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }

}
