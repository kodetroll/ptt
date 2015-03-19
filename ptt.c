/* ptt.c - This program will set a control line on a serial comm
 * to s specified state (ON or OFF) for the purpose of controlling
 * a radio attached to the serial port through a keying interface.
 * This program will take parameters from the command line and parse
 * these to determine what serial port and what state is desired. The
 * program works by calculating the IO base address of the specified
 * serial port, then adding the MCR register offset to this value. It
 * will then do an ioperms call to allow the user space program to
 * have access to the IO port. The program will then read the MCR
 * register of the desired serial comm port, mask off and set the
 * MCR register bit corresponding to the configured port control line
 * as specified on the command line. It then writes the modified value
 * back to the MCR register, thus affecting the output control line
 * state. It then exits. This program is really more of a proof-of-concept
 * program to demonstrate low level access to the i/o ports.
 *
 * Author: Steve McCarter, KB4OID
 * Date: September 2009
 * Revised: September 2013
 * Version: 0.99.2
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

#define HAVE_SYS_IO_H

#ifdef HAVE_SYS_IO_H
    #include <sys/io.h>
    #define WITH_OUTB
#else
    #ifdef HAVE_ASM_IO_H
        #include <asm/io.h>
        #define WITH_OUTB
    #endif
#endif

#define TRUE    1
#define FALSE   0

//#define VERBOSE_PRINT FALSE
#define VERBOSE_PRINT TRUE
#define SILENT_MODE FALSE

enum {
    CTRL_DTR,
    CTRL_RTS,
    CTRL_BOTH
};

/* MCR_OFFSET is the register address offset of the MCR
 * register from the COM port base register IO address.
 * This value is usually 0x04 for the MCR of a serial port.
 */
int MCR_ADDR_OFFSET = 0x04;

/* IO_MASK is used to mask off the upper portion of the
 * IO address when creating the port address. This is
 * required to keep from causing a segfault by accidently
 * addressing an IO port address greater than 0x3FF without
 * using iopl().
 */
//int IO_MASK = 0x3FF;
int IO_MASK = 0xFFFF;

/* CTRL_BIT is the bit number of the desired port line
 * to be controlled. This is expressed as the shift amount
 * required to create CTRL_MASK. See the following:
 *     DTR: 0x00 (BIT 0),
 *     RTS: 0x01 (BIT 1)
 *     BOTH: 0x02 (BIT 0 & 1)
 */
int CTRL_BIT = 0x00;

/* This value starts as 00000001 and is then LEFT shifted
 * by CTRL_BIT bits. The resulting value is used as the
 * mask to affect changes only on the desired bit in the
 * MCR.
 */
unsigned char CTRL_MASK = 0x01;

void prt_hdr()
{
    printf("PTT V1.1 (C) 2009 Steve McCarter, KB4OID\n");
}

void usage()
{

    printf("   Usage is \"./ptt <port_number> <ctrl_line> <value>\"\n") ;
    printf("   \n") ;
    printf("   Where:\n") ;
    printf("   <port_number> is 0-3 for ttyS0-ttyS3\n") ;
    printf("   <ctrl_line> is '0' for DTR, '1' for RTS, '2' for both\n") ;
    printf("   <value> is '0' or '1' for ON or OFF\n") ;
    exit( -1) ;

}

int main(int argc, char *argv[])
{
    int port_number;            // The specified serial port number 0-3
    int port_address;		    // The serial port base I/O port address
    unsigned char old_value;	// The original value of the MCR register
    unsigned char new_value;	// The new value of the MCR register
    unsigned char value;		// The specified state ON or OFF
    unsigned char ctrl_line;	// The specified line to ctrl (DTR or RTS)

    ctrl_line = CTRL_DTR;

    prt_hdr();

    /* Ensure that the user has supplied exactly three
     * parameters supplied to the program on the command
     * line. If not, then print usage and exit.
     */
    if (argc != 4)
        usage();

    /* Convert the ASCII char from the command line to it's integer form */
    port_number = atoi( argv[1]);

    /* Generate a boolean value from the operator
     * supplied input value
     */
    ctrl_line = atoi( argv[2]) & 0x01;

    /* Based on the port number (0-3), select the IO base address */
    switch (port_number)
    {
        case 0: port_address = 0x3F8; break;
        case 1: port_address = 0x2F8; break;
        case 2: port_address = 0x3E8; break;
        case 3: port_address = 0x2E8; break;
	case 4: port_address = 0xec98; break;
	case 5: port_address = 0xdcc0; break;
	case 6: port_address = 0xdcc8; break;
	case 7: port_address = 0xdcd0; break;
	case 8: port_address = 0xdcd8; break;
	default: port_address = 0x3F8; break;
    }

    switch(ctrl_line)
    {
        case CTRL_DTR: CTRL_BIT = 0x00; break;      // DTR: 0x00 (BIT 0),
        case CTRL_RTS: CTRL_BIT = 0x01; break;      // RTS: 0x01 (BIT 1)
    }

    /* Show the BASE COM Port address based on the port number */
    if (VERBOSE_PRINT == TRUE)
        printf("COMM Port base address: 0x%024X\n",port_address);

    /* Add the MCR OFFSET to BASE address to find the MCR address */
    port_address += MCR_ADDR_OFFSET;

    /* Show the MCR register Address */
    if (VERBOSE_PRINT == TRUE)
        printf("COMM Port MCR Register address: 0x%02X\n",port_address);

    /* Apply the IO MASK to generate the final IO address */
    port_address &= IO_MASK;

    /* Show the final MCR Register address */
    if (VERBOSE_PRINT == TRUE)
        printf("COMM Port MCR Register address: 0x%02X\n",port_address);

    /* If the port_address is less than 0x3FF, then we do a simple ioperm()
     * action to set the perms on the ioport so that the user can change
     * the value in the MCR register.
     */
    if (ioperm(port_address, 3, 1)!=0) {
        error ("ptt: ioperm(0x%x) failed: %s", port_address, strerror(errno));
        return -1;
    }

    /* Get the initial value of the MCR */
    old_value = inb( port_address );

    /* Show this value to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("Initial Value: 0x%02X\n",old_value);

    /* Show the initial value of CTRL_MASK */
    if (VERBOSE_PRINT == TRUE)
        printf("CTRL_MASK: 0x%02X\n",CTRL_MASK);

    /* Create the correct CTRL Bit MASK value */
    CTRL_MASK <<= CTRL_BIT;

    /* Show this to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("CTRL_MASK: 0x%02X\n",CTRL_MASK);

    switch(ctrl_line)
    {
        case CTRL_DTR: printf("PTT (DTR) was: "); break;
        case CTRL_RTS: printf("PTT (RTS) was: "); break;
    }

    if (CTRL_MASK && old_value == CTRL_MASK)
        printf("ON, ");
    else
        printf("OFF, ");

    /* Generate a boolean value from the operator
     * supplied input value
     */
    value = atoi( argv[3]) & 0x01;

    /* Show this to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("Desired Value: 0x%02X\n",value);

    /* Modify the initial value of the MCR
    * based on the CTRL_MASK
    */
    if (value == 1)
        new_value = CTRL_MASK | old_value;
    else
        new_value = (~CTRL_MASK) & old_value;

    /* Show this to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("New Value: 0x%02X\n",new_value);

    /* Send the new value to the MCR */
    outb( new_value, port_address);

    /* Read it back in for verification */
    new_value = inb( port_address );

    /* Show the read back value to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("New Value: 0x%02X\n",new_value);

    /* Mask off the CTRL'd BIT */
    new_value &= CTRL_MASK;

    /* Convert it to a integer 0 or 1 */
    new_value >>= CTRL_BIT;

    /* Show the operator the end result! */
    if (SILENT_MODE == FALSE)
    {
        if (new_value == 1)
            printf("PTT now: ON!\n");
        else
            printf("PTT now: OFF!\n");
    }

    /* Peace, out! */
    exit(0);
}



