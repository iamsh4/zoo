// Register Name, Address, Channel #, Description

// G2 DMA Start Address
G2_REG(SB_ADSTAG, 0x005f7800, 0, "G2 ch0 Internal DMA start address")
G2_REG(SB_E1STAG, 0x005f7820, 1, "G2 ch1 Internal DMA start address")
G2_REG(SB_E2STAG, 0x005f7840, 2, "G2 ch2 Internal DMA start address")
G2_REG(SB_DDSTAG, 0x005f7860, 3, "G2 ch3 Internal DMA start address")

// G2 System Mem. or Texture Mem. start address
G2_REG(SB_ADSTAR, 0x005f7804, 0, "G2 ch0 SysMem DMA start address")
G2_REG(SB_E1STAR, 0x005f7824, 1, "G2 ch1 SysMem DMA start address")
G2_REG(SB_E2STAR, 0x005f7844, 2, "G2 ch2 SysMem DMA start address")
G2_REG(SB_DDSTAR, 0x005f7864, 3, "G2 ch3 SysMem DMA start address")

// G2-DMA Transfer Length
G2_REG(SB_ADLEN, 0x005f7808, 0, "G2 ch0 DMA Transfer Length")
G2_REG(SB_E1LEN, 0x005f7828, 1, "G2 ch1 DMA Transfer Length")
G2_REG(SB_E2LEN, 0x005f7848, 2, "G2 ch2 DMA Transfer Length")
G2_REG(SB_DDLEN, 0x005f7868, 3, "G2 ch3 DMA Transfer Length")

// G2-DMA Transfer Direction
G2_REG(SB_ADDIR, 0x005f780C, 0, "G2 ch0 DMA Transfer Direction")
G2_REG(SB_E1DIR, 0x005f782C, 1, "G2 ch1 DMA Transfer Direction")
G2_REG(SB_E2DIR, 0x005f784C, 2, "G2 ch2 DMA Transfer Direction")
G2_REG(SB_DDDIR, 0x005f786C, 3, "G2 ch3 DMA Transfer Direction")

// G2-DMA Trigger Selection. Controls enable/disable and how DMAs can be triggered
G2_REG(SB_ADTSEL, 0x005f7810, 0, "G2 ch0 DMA Trigger Selection")
G2_REG(SB_E1TSEL, 0x005f7830, 1, "G2 ch1 DMA Trigger Selection")
G2_REG(SB_E2TSEL, 0x005f7850, 2, "G2 ch2 DMA Trigger Selection")
G2_REG(SB_DDTSEL, 0x005f7870, 3, "G2 ch3 DMA Trigger Selection")

// G2-DMA Enable/Disable. Also, forcible terminate in-progress DMA by writing a 0 here
G2_REG(SB_ADEN, 0x005f7814, 0, "G2 ch0 DMA Enable/Disable")
G2_REG(SB_E1EN, 0x005f7834, 1, "G2 ch1 DMA Enable/Disable")
G2_REG(SB_E2EN, 0x005f7854, 2, "G2 ch2 DMA Enable/Disable")
G2_REG(SB_DDEN, 0x005f7874, 3, "G2 ch3 DMA Enable/Disable")

// G2-DMA Start / Status
G2_REG(SB_ADST, 0x005f7818, 0, "G2 ch0 DMA Start/Status")
G2_REG(SB_E1ST, 0x005f7838, 1, "G2 ch1 DMA Start/Status")
G2_REG(SB_E2ST, 0x005f7858, 2, "G2 ch2 DMA Start/Status")
G2_REG(SB_DDST, 0x005f7878, 3, "G2 ch3 DMA Start/Status")

// G2-DMA Suspend
G2_REG(SB_ADSUSP, 0x005f781C, 0, "G2 ch0 DMA Suspend")
G2_REG(SB_E1SUSP, 0x005f783C, 1, "G2 ch1 DMA Suspend")
G2_REG(SB_E2SUSP, 0x005f785C, 2, "G2 ch2 DMA Suspend")
G2_REG(SB_DDSUSP, 0x005f787C, 3, "G2 ch3 DMA Suspend")