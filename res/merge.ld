SECTIONS
{
  /* Align to the section boundary defined by the compiler/linker */
  . = ALIGN(__section_alignment__);

  /* 1. Merge read-only data (.rdata) directly into the code section (.text).
        This keeps the code segment strictly Read+Execute (no Write permission),
        satisfying modern OS security policies. */
  .text : {
    *(.text)
    *(.text.*)
    *(.rdata)
    *(SORT(.rdata$*))
  }

  /* 2. Keep the Import Address Table (.idata) in its own distinct section.
        The Windows loader must write resolved addresses here at startup,
        so this section requires Read+Write permissions. */
  .idata BLOCK(__section_alignment__) : {
    *(.idata)
    *(SORT(.idata$*))
  }

  /* 3. Keep the Exception Directory (.pdata) distinct.
        Windows x64 loaders require structured exception handling (SEH) data
        to be aligned and distinct to load the binary successfully. */
  .pdata BLOCK(__section_alignment__) : {
    *(.pdata)
  }
}