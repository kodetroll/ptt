/* ptt.h

*/

#ifndef __PTT_H__
#define __PTT_H__

/* Make this header file easier to include in C++ code */
#ifdef __cplusplus
extern "C" {
#endif

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

#define PASS 1
#define FAIL 0
#define ERROR -1

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

#define DEF_DEVICENAME 	"/dev/ttyS0"
#define DEF_LINENAME 	"BOTH"
#define DEF_CFGFILE 	"ptt.conf"
#define DEF_PORTNUM 	0
#define DEF_VALUE 		OFF


#define MAJOR_VER 		1
#define MINOR_VER 		3
#define COPY_YEARS 		"2009-2018"

typedef struct
{
	int verbose;					// Verbose Reporting {0|1} {OFF|ON}
	int quiet;						// Silent Output {0|1} {OFF|ON}
	int debug;						// Debug reporting {0|1} {OFF|ON}
	int level;						// Debug level {0|5}
	int port_number;				// The specified serial port number 0-3
	unsigned char ctrl_line;		// The specified line to ctrl (DTR or RTS)
	unsigned char value;			// The specified state for the line (0/1)
	int numlines;					// Number of lines to control
    const char* devicename;			// serial device name
    const char* linename;			// serial line name

} configuration;


// Global Prototypes
int load_defaults(void);
static int handler(void* user, const char* section, const char* name, const char* value);
int load_config(char * cfile);
void prt_hdr(char * name);
void copyright(void);
void version(char * name);
void usage(char * name);
void print_line_state(int bit_mask, int value);
void parse_args(int argc, char *argv[]);
char * getCtrlLineName(int cline);
int getCtrlLine(char * line);
int getPortNumber(char * portname);


#ifdef __cplusplus
}
#endif

#endif /* __PTT_H__ */
