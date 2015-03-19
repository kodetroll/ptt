/* ptt.c - This is a program that is intended to set a control line
 * (DTR or RTS) on a serial comm port to a specified state (ON or OFF),
 * for the purpose of controlling a radio (or other device) attached to
 * the serial port through a keying interface. It will take parameters
 * from the command line and parse them to determine what serial port,
 * control lines, and what state is desired. it will then set the
 * specified serial port control pins to the desired state and
 * exit, leaving the control pins in that state.
 *
 * Note: This program is only intended to work with classic legacy
 * 8250 based serial port hardware. It may work with other serial port
 * interfaces, perhaps MOS Chips or similar style hardware. However,
 * the serial port device must support 8250 style IO register access
 * and have the control pins mapped to the same locations as in an 8250.
 *
 * The program works by calculating the IO base address of the specified
 * serial port, then adding the MCR register offset to this value. It
 * will then do an ioperms call to allow the user space program to have
 * access to the IO port. The program will then read the MCR register
 * of the desired serial comm port, mask off and set the MCR register
 * bit corresponding to the configured port control line as specified
 * on the command line. It then writes the modified value back to the
 * MCR register, thus affecting the output control line state. It then
 * exits. This program is really more of a proof-of-concept program to
 * demonstrate low level access to the i/o ports.
 *
 * (C) KB4OID Labs, a division of Kodetroll Heavy Industries
 * Author: Kodetroll (Steve McCarter, KB4OID)
 * Project: None
 * Date Created: September 2009
 * Revised: September 2013
 * Last Revised: October 2014
 * Version: 1.3
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

/* This define will select whether we wish to compile using sys/io.h or
 * sys/asm.h. Which you use will likely depend on system architecture.
 */
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

// Define some boolean states.
#define TRUE    1
#define FALSE   0

#define ON 		1
#define OFF 	0

/* define the number of consecutive registers to apply the ioperm command to.
 * MCR_REG_ONLY ioperms the MCR only, while WHOLE_UART would apply to the
 * whole 3 bit io address space (base reg addr must be set appropriately)
 */
#define MCR_REG_ONLY 1
#define WHOLE_UART 8

/* If VERBOSE_PRINT is set to TRUE, then various debug statements will
 * be enabled and much info (hence the verbose tag) will be printed to
 * the screen during execution. If this app is being used in a script
 * of some kind that is running un-attended, then this verbose level of
 * detail may be interesting. otherwise, not so much, so turn it off.
 * This is currently not selectable at run time, sorry!
 */

//#define VERBOSE_PRINT FALSE
#define VERBOSE_PRINT TRUE

#define SILENT_MODE FALSE

#define DTR_MASK 	1		// Bit 0: 2^0
#define RTS_MASK 	2		// Bit 1: 2^1

#define MCR_MASK 	0x03	// Mask off all but lower 2 bits of MCR

#define UPPER_MCR_MASK 	0xC0	// Mask off all but upper 2 bits of MCR

/* define which pins will be used to control PTT, choices are
 * NONE, DTR only, RTS only or BOTH.
 */
enum {
    CTRL_NONE,	// Use none to control PTT
    CTRL_DTR,	// Use only DTR to control PTT
    CTRL_RTS,	// Use only RTS to control PTT
    CTRL_BOTH	// Use both RTS & DTR to control PTT
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

#define MINOR_VER 3
#define MAJOR_VER 1
#define COPY_YEARS "2009-2013"

void prt_hdr()
{
    printf("PTT V%d.%d (C) %s Steve McCarter, KB4OID\n",MAJOR_VER,MINOR_VER,COPY_YEARS);
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

void print_line_state(int bit_mask, int value)
{
    if ((bit_mask & value) == bit_mask)
        printf("ON, ");
    else
        printf("OFF, ");
}

int main(int argc, char *argv[])
{
    int port_number;            // The specified serial port number 0-3
    int port_address;		    // The serial port base I/O port address
    unsigned char old_value;	// The original value of the MCR register
    unsigned char new_value;	// The new value of the MCR register
    unsigned char value;		// The specified state ON or OFF
    unsigned char ctrl_line;	// The specified line to ctrl (DTR or RTS)

	/* start with 'DTR only' control assigned */
    ctrl_line = CTRL_DTR;

    prt_hdr();

    /* Ensure that the user has supplied exactly three parameters 
     * (argc = 4) supplied to the program on the command line. If not, 
     * then print usage and exit.
     */
    if (argc != 4)
        usage();

    /* Grab the tty port number from the command line args. Convert the 
     * ASCII char from the command line to it's integer form.
     */
    port_number = atoi( argv[1]);

    /* Determine which control line to activate, by generating a 
     * boolean value from the input value supplied by the operator.
     */
    ctrl_line = atoi( argv[2]) & 0x03;

    /* Based on the provided port number, select the IO base address
     * of the serial port to be controlled. Any thing other that
     * 0-3 may not work and is dependant on specific hardware.
     */
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

	/* Setup the default control pin BIT map */
    switch(ctrl_line)
    {
        case CTRL_NONE: printf("ptt mode is CTRL_NONE\n"); break;	// DTR: 0, RTS: 0 -> 0x00
        case CTRL_DTR: printf("ptt mode is CTRL_DTR\n"); break;		// DTR: 0, RTS: 1 -> 0x01
        case CTRL_RTS: printf("ptt mode is CTRL_RTS\n"); break;		// DTR: 1, RTS: 0 -> 0x02
        case CTRL_BOTH: printf("ptt mode is CTRL_BOTH\n"); break;	// DTR: 1, RTS: 0 -> 0x03
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
    if (ioperm(port_address, MCR_REG_ONLY, ON)!=0) {
        error ("ptt: ioperm(0x%x) failed: %s", port_address, strerror(errno));
        return -1;
    }

    /* Get the initial value of the MCR */
    old_value = inb( port_address );

    /* Show this value to the operator */
    if (VERBOSE_PRINT == TRUE)
        printf("Initial Value: 0x%02X\n",old_value);

    if (VERBOSE_PRINT == TRUE)
		if ((old_value & UPPER_MCR_MASK) > 0)
			printf("Warning, MCR Initial Value indicates no UART present\n");

    switch(ctrl_line)
    {
        case CTRL_NONE:
			break;
        case CTRL_DTR:
			printf("PTT (DTR) was: ");
			print_line_state(DTR_MASK,old_value);
			break;
        case CTRL_RTS:
			printf("PTT (RTS) was: ");
			print_line_state(RTS_MASK,old_value);
			break;
        case CTRL_BOTH:
			printf("PTT (DTR) was: ");
			print_line_state(DTR_MASK,old_value);
			printf("PTT (RTS) was: ");
			print_line_state(RTS_MASK,old_value);
			break;
    }
    printf("\n");

    /* Generate a boolean value from the operator
     * supplied input value
     */
    value = atoi( argv[3]) & 0x01;

    /* Show this to the operator */
    if (VERBOSE_PRINT == TRUE)
    {
		switch(ctrl_line)
		{
			case CTRL_NONE:
				printf("Desired Value: DTR NOT CHANGED\n");
				printf("Desired Value: RTS NOT CHANGED\n");
				break;
			case CTRL_DTR:
				if (value == ON)
					printf("Desired Value: DTR ON\n");
				else
					printf("Desired Value: DTR OFF\n");
				printf("Desired Value: RTS NOT CHANGED\n");
				break;
			case CTRL_RTS:
				printf("Desired Value: DTR NOT CHANGED\n");
				if (value == ON)
					printf("Desired Value: RTS ON\n");
				else
					printf("Desired Value: RTS OFF\n");
				break;
			case CTRL_BOTH:
				if (value == ON)
				{
					printf("Desired Value: DTR ON\n");
					printf("Desired Value: RTS ON\n");
				}
				else
				{
					printf("Desired Value: RTS OFF\n");
					printf("Desired Value: DTR OFF\n");
				}
				break;
		}
	}

    /* Modify the initial value of the MCR
     * based on the desired control configuration
     */
    switch(ctrl_line)
    {
        case CTRL_NONE:
			break;
        case CTRL_DTR:
			if (value == ON)
				new_value = DTR_MASK | old_value;
			else
				new_value = (~DTR_MASK) & old_value;
			break;
        case CTRL_RTS:
			if (value == ON)
				new_value = RTS_MASK | old_value;
			else
				new_value = (~RTS_MASK) & old_value;
			break;
        case CTRL_BOTH:
			if (value == ON)
				new_value = DTR_MASK | old_value;
			else
				new_value = (~DTR_MASK) & old_value;
			if (value == ON)
				new_value = RTS_MASK | old_value;
			else
				new_value = (~RTS_MASK) & old_value;
			break;
    }

    new_value = MCR_MASK & new_value;

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

    /* Show the operator the end result! */
    if (SILENT_MODE == FALSE)
    {
		switch(ctrl_line)
		{
			case CTRL_NONE:
				break;
			case CTRL_DTR:
				if (new_value & DTR_MASK == DTR_MASK)
					printf("PTT now: DTR ON!\n");
				else
					printf("PTT now: DTR OFF!\n");
				break;
			case CTRL_RTS:
				if (new_value & RTS_MASK == RTS_MASK)
					printf("PTT now: RTS ON!\n");
				else
					printf("PTT now: RTS OFF!\n");
				break;
			case CTRL_BOTH:
				if (new_value & DTR_MASK == DTR_MASK)
					printf("PTT now: DTR ON!\n");
				else
					printf("PTT now: DTR OFF!\n");
				if (new_value & RTS_MASK == RTS_MASK)
					printf("PTT now: RTS ON!\n");
				else
					printf("PTT now: RTS OFF!\n");
				break;
		}

    }

    /* Peace, out! */
    exit(0);
}




