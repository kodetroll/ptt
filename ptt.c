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
 * Last Revised: March 2015
 * Version: 1.4
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include "ini.h"

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

#define DEF_DEVICENAME "/dev/ttyS0"
#define DEF_LINENAME "BOTH"
#define DEF_CFGFILE "ptt.conf"
#define DEF_PORTNUM 0

#define MINOR_VER 3
#define MAJOR_VER 1
#define COPY_YEARS "2009-2015"

static int verbose;			// Verbose Reporting {0|1} {OFF|ON}
static int quiet;			// Silent Output {0|1} {OFF|ON}
static int debug;			// Debug reporting {0|1} {OFF|ON}
static int level;			// Debug level {0|5}
int port_number;            // The specified serial port number 0-3
unsigned char ctrl_line;	// The specified line to ctrl (DTR or RTS)
int numlines;				// Number of lines to control
char * devicename;			// serial device name
char * linename;			// serial line name
char * cfgfile;				// Config file name

// Global Prototypes
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

typedef struct
{
	int verbose;					// Verbose Reporting {0|1} {OFF|ON}
	int quiet;						// Silent Output {0|1} {OFF|ON}
	int debug;						// Debug reporting {0|1} {OFF|ON}
	int level;						// Debug level {0|5}
	int port_number;				// The specified serial port number 0-3
	unsigned char ctrl_line;		// The specified line to ctrl (DTR or RTS)
	int numlines;					// Number of lines to control
    const char* devicename;			// serial device name
    const char* linename;			// serial line name

} configuration;


/* This function will match section and name to sets specified below to parse
 * an ini file line into it's value. This value is stored in the configuration*
 * structure. From the 'ini' file lib.
 */
static int handler(void* user, const char* section, const char* name, const char* value)
{
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("DEBUG", "Debug")) {
        pconfig->debug = atoi(value);
    } else if (MATCH("DEBUG", "Verbose")) {
        pconfig->verbose = atoi(value);
    } else if (MATCH("DEBUG", "Quiet")) {
        pconfig->quiet = atoi(value);
    } else if (MATCH("DEBUG", "Level")) {
        pconfig->level = atoi(value);
    } else if (MATCH("DEVICES", "DeviceName")) {
        pconfig->devicename = strdup(value);
    } else if (MATCH("DEVICES", "LineName")) {
        pconfig->linename = strdup(value);
    } else if (MATCH("DEVICES", "ControlLine")) {
        pconfig->ctrl_line = atoi(value);
    } else if (MATCH("DEVICES", "PortNumber")) {
        pconfig->port_number = atoi(value);
    } else if (MATCH("LINES", "Lines")) {
        pconfig->numlines = atoi(value);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

/* This function accomplishes the loading of the ini file into the configuration */
int load_config(char * cfile)
{
	configuration config;
	char * p;

	if (ini_parse(cfile, handler, &config) < 0) {
		printf("Can't load '%s'\n",cfile);
		return(ERROR);
	}
	printf("Config loaded from '%s':\n", cfile);

	//printf("name=%s\n", config.name);

	debug = config.debug;
	verbose = config.verbose;
	level = config.level;
	quiet = config.quiet;

	if (config.devicename != "")
		strcpy(devicename,config.devicename);

	if (debug)
		printf("devicename: '%s'\n",devicename);

	port_number = getPortNumber(devicename);

	if (config.linename != "")
		strcpy(linename,config.linename);

	if (debug)
		printf("linename: '%s'\n",linename);

	ctrl_line = getCtrlLine(linename);

	if (config.port_number != ERROR)
		port_number = config.port_number;

	if (debug)
		printf("port_number: '%d'\n",port_number);

	if (config.numlines != ERROR)
		numlines = config.numlines;

	if (debug)
		printf("numlines: '%d'\n",numlines);

	if (config.ctrl_line != ERROR)
		ctrl_line = config.ctrl_line;

	if (debug)
		printf("ctrl_line: '%d'\n",ctrl_line);

	return(PASS);
}

void prt_hdr(char * name)
{
    printf("%s V%d.%d\n",name,MAJOR_VER,MINOR_VER);
}

void copyright(void)
{
    printf("Copyright (C) %s KB4OID Labs, a division of Kodetroll Heavy Industries\n",COPY_YEARS);
}

void version(char * name)
{
    printf("This %s Version %d.%d (C) %s\n",name,MAJOR_VER,MINOR_VER,COPY_YEARS);
}

void usage(char * name)
{
	printf("\n");
	printf("Usage is %s [options] <value>\n",name);
	printf("\n");
	printf("Where:\n");
	printf("  --port, -p <port>           Serial port number [0-7]\n");
	printf("  --device, -d  <devicename>  Serial device name, e.g '/dev/ttyS0'\n");
	printf("  --line, -l <ctrl_line>      Line to control [NONE, DTR, RTS, BOTH] \n");
	printf("  --file, -f <config file>    Use alternate config file\n");
    printf("  <value> is '0' or '1' for ON or OFF\n") ;
}

void print_line_state(int bit_mask, int value)
{
    if ((bit_mask & value) == bit_mask)
        printf("ON, ");
    else
        printf("OFF, ");
}

void parse_args(int argc, char *argv[])
{
    int c;

	while (1)
	{
		static struct option long_options[] =
		{
			/* These options set a flag. */
			{"verbose", no_argument,   &verbose, 1},
			{"brief",   no_argument,   &verbose, 0},	// default
			{"debug",   no_argument,     &debug, 1},
			{"nodebug",   no_argument,   &debug, 0},	// default
			{"quiet",   no_argument,     &quiet, 1},
			{"unquiet", no_argument,     &quiet, 0},	// default
			/* These options don’t set a flag.
			   We distinguish them by their indices. */
			{"help",     no_argument,       0, 'h'},
			{"version",  no_argument,       0, 'v'},
			{"device",   required_argument, 0, 'd'},
			{"port",     required_argument, 0, 'p'},
			{"line",     required_argument, 0, 'l'},
			{"file",     required_argument, 0, 'f'},
			{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long (argc, argv, "hvd:p:l:f:",
				long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				/* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				printf ("option %s", long_options[option_index].name);
				if (optarg)
					printf (" with arg %s", optarg);
				printf ("\n");
				break;

			case 'v':
				version(argv[0]);
				exit(1);
				break;

			case 'h':
				usage(argv[0]);
				exit(1);
				break;

			case 'd':
				printf ("option -d with value `%s'\n", optarg);
				devicename = strdup(optarg);
				break;

			case 'p':
				printf ("option -p with value `%s'\n", optarg);
				port_number = atoi(optarg);
				break;

			case 'l':
				printf ("option -l with value `%s'\n", optarg);
				linename = strdup(optarg);
				break;

			case 'f':
				printf ("option -f with value `%s'\n", optarg);
				cfgfile = strdup(optarg);
				break;

			case '?':
				/* getopt_long already printed an error message. */
				break;

			default:
				abort ();
		}
	}

	/* Instead of reporting ‘--verbose’
	and ‘--brief’ as they are encountered,
	we report the final status resulting from them. */
	if (verbose)
		puts ("verbose flag is set");
	if (quiet)
		puts ("quiet flag is set");
	if (debug)
		puts ("debug flag is set");

	/* Print any remaining command line arguments (not options). */
	if (optind < argc)
	{
		printf ("non-option ARGV-elements: ");
		while (optind < argc)
			printf ("%s ", argv[optind++]);
		putchar ('\n');
	}

}

char * getCtrlLineName(int cline)
{

	switch(cline)
	{
		case CTRL_NONE:
			return("NONE");
		case CTRL_DTR:
			return("DTR");
		case CTRL_RTS:
			return("RTS");
		case CTRL_BOTH:
			return("BOTH");
		case ERROR:
		default:
			return("ERROR");
	}

}

int getCtrlLine(char * line)
{

	if (strcmp(line,"NONE") ==0)
		return(CTRL_NONE);
	else if (strcmp(line,"DTR") ==0)
		return(CTRL_DTR);
	else if (strcmp(line,"RTS") ==0)
		return(CTRL_RTS);
	else if (strcmp(line,"BOTH") ==0)
		return(CTRL_BOTH);
	else
		return(ERROR);

//    CTRL_NONE,	// Use none to control PTT
//    CTRL_DTR,	// Use only DTR to control PTT
//    CTRL_RTS,	// Use only RTS to control PTT
//    CTRL_BOTH	// Use both RTS & DTR to control PTT

}

int getPortNumber(char * portname)
{

	if (strcmp(portname,"/dev/ttyS0") ==0)
		return(0);
	else if (strcmp(portname,"/dev/ttyS1") ==0)
		return(1);
	else if (strcmp(portname,"/dev/ttyS2") ==0)
		return(2);
	else if (strcmp(portname,"/dev/ttyS3") ==0)
		return(3);
	else if (strcmp(portname,"/dev/ttyS4") ==0)
		return(4);
	else if (strcmp(portname,"/dev/ttyS5") ==0)
		return(5);
	else if (strcmp(portname,"/dev/ttyS6") ==0)
		return(6);
	else if (strcmp(portname,"/dev/ttyS7") ==0)
		return(7);
	else
		return(-1);

}

int getPortAddress(int portnum)
{
	int port_address;
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
    return(port_address);
}

int main(int argc, char *argv[])
{
    int port_address;		    // The serial port base I/O port address
    unsigned char old_value;	// The original value of the MCR register
    unsigned char new_value;	// The new value of the MCR register
    unsigned char value;		// The specified state ON or OFF
//    unsigned char ctrl_line;	// The specified line to ctrl (DTR or RTS)

    debug = ON;
    verbose = OFF;
    quiet = OFF;
	level = 0;
	numlines = ERROR;

    devicename = strdup(DEF_DEVICENAME);
    linename = strdup(DEF_LINENAME);
    cfgfile = strdup(DEF_CFGFILE);
    port_number = DEF_PORTNUM;

	/* start with 'DTR only' control assigned */
    ctrl_line = CTRL_DTR;

	if (debug)
	{
		printf("Port Number: %d\n",port_number);
		printf("Ctrl Line: '%s' (%d)\n",getCtrlLineName(ctrl_line),ctrl_line);
		printf("devicename: '%s'\n",devicename);
		printf("linename: '%s'\n",linename);
		printf("cfgfile: '%s'\n",cfgfile);
	}

	if (!quiet)
	{
    	prt_hdr(argv[0]);
    	copyright();
	}

	/* Load defaults from ini config file */
	load_config(cfgfile);

	/* Parse command line arguments */
	parse_args(argc,argv);

	if (debug)
	{
		printf("Port Number: %d\n",port_number);
		printf("Ctrl Line: '%s' (%d)\n",getCtrlLineName(ctrl_line),ctrl_line);
		printf("devicename: '%s'\n",devicename);
		printf("linename: '%s'\n",linename);
		printf("cfgfile: '%s'\n",cfgfile);
	}

	//exit(0);
//    /* Ensure that the user has supplied exactly three parameters
//     * (argc = 4) supplied to the program on the command line. If not,
//     * then print usage and exit.
//     */
//    if (argc != 4)
//        usage(argv[0]);
//
//    /* Grab the tty port number from the command line args. Convert the
//     * ASCII char from the command line to it's integer form.
//     */
//    port_number = atoi( argv[1]);
//
//    /* Determine which control line to activate, by generating a
//     * boolean value from the input value supplied by the operator.
//     */
//    ctrl_line = atoi( argv[2]) & 0x03;

    /* Based on the provided port number, select the IO base address
     * of the serial port to be controlled. Any thing other that
     * 0-3 may not work and is dependant on specific hardware.
     */
	port_address = getPortAddress(port_number);

	/* Setup the default control pin BIT map */
    switch(ctrl_line)
    {
        case CTRL_NONE: printf("ptt mode is CTRL_NONE\n"); break;	// DTR: 0, RTS: 0 -> 0x00
        case CTRL_DTR: printf("ptt mode is CTRL_DTR\n"); break;		// DTR: 0, RTS: 1 -> 0x01
        case CTRL_RTS: printf("ptt mode is CTRL_RTS\n"); break;		// DTR: 1, RTS: 0 -> 0x02
        case CTRL_BOTH: printf("ptt mode is CTRL_BOTH\n"); break;	// DTR: 1, RTS: 0 -> 0x03
    }

    /* Show the BASE COM Port address based on the port number */
    if (verbose)
        printf("COMM Port base address: 0x%024X\n",port_address);

    /* Add the MCR OFFSET to BASE address to find the MCR address */
    port_address += MCR_ADDR_OFFSET;

    /* Show the MCR register Address */
    if (verbose)
        printf("COMM Port MCR Register address: 0x%02X\n",port_address);

    /* Apply the IO MASK to generate the final IO address */
    port_address &= IO_MASK;

    /* Show the final MCR Register address */
    if (verbose)
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
    if (verbose)
        printf("Initial Value: 0x%02X\n",old_value);

    if (verbose)
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
    if (verbose)
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
    if (verbose)
        printf("New Value: 0x%02X\n",new_value);

    /* Send the new value to the MCR */
    outb( new_value, port_address);

    /* Read it back in for verification */
    new_value = inb( port_address );

    /* Show the read back value to the operator */
    if (verbose)
        printf("New Value: 0x%02X\n",new_value);

    /* Show the operator the end result! */
    if (!quiet)
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

