OUTPUT_FORMAT("elf64-alpha")
ENTRY(__start)
SECTIONS
{
  . = 0xfffffc0000310000;
  _text = .;
  .text : { *(.text) }
  .text2 : { *(.text2) }
  _etext = .;

  /* Exception table */
  . = ALIGN(16);
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  /* Kernel symbol table */
  . = ALIGN(8);
  __start___ksymtab = .;
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;
  .kstrtab : { *(.kstrtab) }

  /* Startup code */
  . = ALIGN(8192);
  __init_begin = .;
  .text.init : { *(.text.init) }
  .data.init : { *(.data.init) }

  . = ALIGN(16);
  __setup_start = .;
  .setup.init : { *(.setup.init) }
  __setup_end = .;

  . = ALIGN(8);
  __initcall_start = .;
  .initcall.init : { *(.initcall.init) }
  __initcall_end = .;

  . = ALIGN(2*8192);	/* Align double page for init_task_union */
  __init_end = .;

  /* The initial task and kernel stack */
  init_task : { *(init_task) }

  /* Global data */
  _data = .;
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }
  .rodata : { *(.rodata) }
  .data : { *(.data) CONSTRUCTORS }
  .got : { *(.got) }
  .sdata : { *(.sdata) }
  _edata = .;
  _bss = .;
  .sbss : { *(.sbss) *(.scommon) }
  .bss : { *(.bss) *(COMMON) }
  _end = .;

  .mdebug 0 : { *(.mdebug) }
  .note 0 : { *(.note) }
  .comment 0 : { *(.comment) }

  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
}
