/* The following definitions are in place to keep the determination of
   link speed and duplex orderly.  It is assumed that a Broadcom 548x
   is used unless defined otherwise in the board specific header file.*/

#ifdef CONFIG_MV88E1116R
/* Copper Specific register 1*/
#define MII_ANEG_LINK_MASK 0xEC00
#define MII_ANEG_1000BTF   0xAC00
#define MII_ANEG_1000BTH   0x8C00
#define MII_ANEG_100BTF    0x6C00
#define MII_ANEG_100BT4    0xFFFF // unused 
#define MII_ANEG_100BTH    0x4C00
#define MII_ANEG_10BTF     0x2C00
#define MII_ANEG_10BTH     0x0C00
#define MII_ANEG_NO_LINK   0x0000
#else
/* Auxilary Status Summary register */
#define MII_ANEG_LINK_MASK 0x0700
#define MII_ANEG_1000BTF   0x0700
#define MII_ANEG_1000BTH   0x0600
#define MII_ANEG_100BTF    0x0500
#define MII_ANEG_100BT4    0x0400
#define MII_ANEG_100BTH    0x0300
#define MII_ANEG_10BTF     0x0200
#define MII_ANEG_10BTH     0x0100
#define MII_ANEG_NO_LINK   0x0000
#endif
