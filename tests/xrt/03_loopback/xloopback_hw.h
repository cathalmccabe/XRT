// ==============================================================
// File generated by Vivado(TM) HLS - High-Level Synthesis from C, C++ and SystemC
// Version: 2015.1
// Copyright (C) 2015 Xilinx Inc. All rights reserved.
// 
// ==============================================================

// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read)
//        bit 7  - auto_restart (Read/Write)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0  - Channel 0 (ap_done)
//        bit 1  - Channel 1 (ap_ready)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0  - Channel 0 (ap_done)
//        bit 1  - Channel 1 (ap_ready)
//        others - reserved
// 0x10 : Data signal of s1
//        bit 31~0 - s1[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of s2
//        bit 31~0 - s2[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of length_r
//        bit 31~0 - length_r[31:0] (Read/Write)
// 0x24 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XLOOPBACK_CONTROL_ADDR_AP_CTRL       0x00
#define XLOOPBACK_CONTROL_ADDR_GIE           0x04
#define XLOOPBACK_CONTROL_ADDR_IER           0x08
#define XLOOPBACK_CONTROL_ADDR_ISR           0x0c
#define XLOOPBACK_CONTROL_ADDR_S1_DATA       0x10
#define XLOOPBACK_CONTROL_BITS_S1_DATA       32
#define XLOOPBACK_CONTROL_ADDR_S2_DATA       0x18
#define XLOOPBACK_CONTROL_BITS_S2_DATA       32
#define XLOOPBACK_CONTROL_ADDR_LENGTH_R_DATA 0x20
#define XLOOPBACK_CONTROL_BITS_LENGTH_R_DATA 32
