/*
 * DU Firmware_1.c
 *
 * Created: 4/19/2023 9:17:08 PM
 * Author : Israel
 */ 

#include <avr/io.h>
#include <string.h>
#include <stdio.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include "w5100.h"



unsigned char			OpenSocket(unsigned char  sock, unsigned char  eth_protocol, unsigned int  tcp_port);
void					CloseSocket(unsigned char  sock);
void					DisconnectSocket(unsigned char  sock);
unsigned char			Listen(unsigned char  sock);
unsigned char			Send(unsigned char  sock, const unsigned char  *buf, unsigned int  buflen);
unsigned int			Receive(unsigned char  sock, unsigned char  *buf, unsigned int  buflen);
unsigned int			ReceivedSize(unsigned char  sock);

void					my_select(void);
void					my_deselect(void);
unsigned char			my_xchg(unsigned char  val);
void					my_reset(void);




#define  MAX_BUF					512			/* largest buffer we can read from chip */

#define  HTTP_PORT					3333			/* recieve port */

#define LCD_Command_Dir DDRC		/* Define LCD command port direction register */
#define LCD_Command_Port PORTC		/* Define LCD command port */
#define LCD_Data_Dir DDRE			/* Define LCD data port direction register */
#define LCD_Data_Port PORTE			/* Define LCD data port */
#define RS PD4							/* Define Register Select (data reg./command reg.) signal pin */
#define RW PD5							/* Define Read/Write signal pin */
#define EN PD3


/*
 *  Ethernet setup
 *
 *  Define the MAC address, IP address, subnet mask, and gateway
 *  address for the target device.
 */
W5100_CFG					my_cfg =
{
	{0x00,0x16,0x36,0xDE,0x58,0xF6},			// mac_addr
	{192,168,200,60},							// ip_addr
	{255,255,255,0},							// sub_mask
	{192,168,1,1}								// gtw_addr
};



/*
 *  Callback function block
 *
 *  Define callback functions for target-independent support of the
 *  W5100 chip.  Here is where you store pointers to the various
 *  functions needed by the W5100 library code.  These functions all
 *  handle tasks that are target-dependent, which means the library
 *  code can be target-INdependent.
 */
W5100_CALLBACKS				my_callbacks;




unsigned char						buf[MAX_BUF];




unsigned char  OpenSocket(unsigned char  sock, unsigned char  eth_protocol, unsigned int  tcp_port)
{
    unsigned char			retval;
	unsigned int			sockaddr;
	
	retval = W5100_FAIL;							// assume this doesn't work
    if (sock >= W5100_NUM_SOCKETS)  return retval;	// illegal socket value is bad!

	sockaddr =  W5100_SKT_BASE(sock);				// calc base addr for this socket

    if (W51_read(sockaddr+W5100_SR_OFFSET) == W5100_SKT_SR_CLOSED)    // Make sure we close the socket first
	{
		CloseSocket(sock);
    }

    W51_write(sockaddr+W5100_MR_OFFSET ,eth_protocol);		// set protocol for this socket
    W51_write(sockaddr+W5100_PORT_OFFSET, ((tcp_port & 0xFF00) >> 8 ));		// set port for this socket (MSB)
    W51_write(sockaddr+W5100_PORT_OFFSET + 1, (tcp_port & 0x00FF));			// set port for this socket (LSB)
    W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_OPEN);	               	// open the socket

    while (W51_read(sockaddr+W5100_CR_OFFSET))  ;			// loop until device reports socket is open (blocks!!)

    if (W51_read(sockaddr+W5100_SR_OFFSET) == W5100_SKT_SR_INIT)  retval = sock;	// if success, return socket number
    else  CloseSocket(sock);								// if failed, close socket immediately

    return  retval;
}





void  CloseSocket(unsigned char  sock)
{
	unsigned int			sockaddr;
	
	if (sock > W5100_NUM_SOCKETS)  return;			// if illegal socket number, ignore request
	sockaddr = W5100_SKT_BASE(sock);				// calc base addr for this socket

	W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_CLOSE);	// tell chip to close the socket
	while (W51_read(sockaddr+W5100_CR_OFFSET))  ;	// loop until socket is closed (blocks!!)
}




void  DisconnectSocket(unsigned char  sock)
{
	unsigned int			sockaddr;
	
	if (sock > W5100_NUM_SOCKETS)  return;			// if illegal socket number, ignore request
	sockaddr = W5100_SKT_BASE(sock);				// calc base addr for this socket

	W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_DISCON);		// disconnect the socket
	while (W51_read(sockaddr+W5100_CR_OFFSET))  ;	// loop until socket is closed (blocks!!)
}




unsigned char  Listen(unsigned char  sock)
{
	unsigned char			retval;
	unsigned int			sockaddr;

	retval = W5100_FAIL;							// assume this fails	
	if (sock > W5100_NUM_SOCKETS)  return  retval;	// if illegal socket number, ignore request

	sockaddr = W5100_SKT_BASE(sock);				// calc base addr for this socket
	if (W51_read(sockaddr+W5100_SR_OFFSET) == W5100_SKT_SR_INIT)	// if socket is in initialized state...
	{
		W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_LISTEN);		// put socket in listen state
		while (W51_read(sockaddr+W5100_CR_OFFSET))  ;		// block until command is accepted

		if (W51_read(sockaddr+W5100_SR_OFFSET) == W5100_SKT_SR_LISTEN)  retval = W5100_OK;	// if socket state changed, show success
		else  CloseSocket(sock);					// not in listen mode, close and show an error occurred
    }
    return  retval;
}




unsigned char  Send(unsigned char  sock, const unsigned char  *buf, unsigned int  buflen)
{
	unsigned int					ptr;
	unsigned int					offaddr;
	unsigned int					realaddr;
	unsigned int					txsize;
	unsigned int					timeout;   
	unsigned int					sockaddr;

    if (buflen == 0 || sock >= W5100_NUM_SOCKETS)  return  W5100_FAIL;		// ignore illegal requests
	sockaddr = W5100_SKT_BASE(sock);			// calc base addr for this socket
    // Make sure the TX Free Size Register is available
	txsize = W51_read(sockaddr+W5100_TX_FSR_OFFSET);		// make sure the TX free-size reg is available
    txsize = (((txsize & 0x00FF) << 8 ) + W51_read(sockaddr+W5100_TX_FSR_OFFSET + 1));

    timeout = 0;
    while (txsize < buflen)
	{
		_delay_ms(1);

		txsize = W51_read(sockaddr+W5100_TX_FSR_OFFSET);		// make sure the TX free-size reg is available
    	txsize = (((txsize & 0x00FF) << 8 ) + W51_read(sockaddr+W5100_TX_FSR_OFFSET + 1));

		if (timeout++ > 1000) 						// if max delay has passed...
		{
			DisconnectSocket(sock);					// can't connect, close it down
			return  W5100_FAIL;						// show failure
		}
	}	

   // Read the Tx Write Pointer
	ptr = W51_read(sockaddr+W5100_TX_WR_OFFSET);
	offaddr = (((ptr & 0x00FF) << 8 ) + W51_read(sockaddr+W5100_TX_WR_OFFSET + 1));

    while (buflen)
	{
		buflen--;
		realaddr = W5100_TXBUFADDR + (offaddr & W5100_TX_BUF_MASK);		// calc W5100 physical buffer addr for this socket

		W51_write(realaddr, *buf);					// send a byte of application data to TX buffer
		offaddr++;									// next TX buffer addr
		buf++;										// next input buffer addr
    }

    W51_write(sockaddr+W5100_TX_WR_OFFSET, (offaddr & 0xFF00) >> 8);	// send MSB of new write-pointer addr
    W51_write(sockaddr+W5100_TX_WR_OFFSET + 1, (offaddr & 0x00FF));		// send LSB	

    W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_SEND);	// start the send on its way
	while (W51_read(sockaddr+W5100_CR_OFFSET))  ;	// loop until socket starts the send (blocks!!)

    return  W5100_OK;
}



/*
 *  Define the SPI port, used to exchange data with a W5100 chip.
 */
#define SPI_PORT 	PORTB				/* target-specific port containing the SPI lines */
#define SPI_DDR  	DDRB				/* target-specific DDR for the SPI port lines */

#define CS_DDR		DDRB				/* target-specific DDR for chip-select */
#define CS_PORT 	PORTB				/* target-specific port used as chip-select */
#define CS_BIT		2					/* target-specific port line used as chip-select */

#define RESET_DDR   DDRD				/* target-specific DDR for reset */
#define RESET_PORT	PORTD				/* target-specific port used for reset */
#define RESET_BIT	3					/* target-specific port line used as reset */



/*
 *  Define macros for selecting and deselecting the W5100 device.
 */
#define  W51_ENABLE		CS_PORT&=~(1<<CS_BIT)
#define  W51_DISABLE	CS_PORT|=(1<<CS_BIT)




unsigned int  Receive(unsigned char  sock, unsigned char  *buf, unsigned int  buflen)
{
    unsigned int					ptr;
	unsigned int					offaddr;
	unsigned int					realaddr;   	
	unsigned int					sockaddr;

    if (buflen == 0 || sock >= W5100_NUM_SOCKETS)  return  W5100_FAIL;		// ignore illegal conditions

	if (buflen > (MAX_BUF-2))  buflen = MAX_BUF - 2;		// requests that exceed the max are truncated

	sockaddr = W5100_SKT_BASE(sock);						// calc base addr for this socket
	ptr = W51_read(sockaddr+W5100_RX_RD_OFFSET);			// get the RX read pointer (MSB)
    offaddr = (((ptr & 0x00FF) << 8 ) + W51_read(sockaddr+W5100_RX_RD_OFFSET + 1));		// get LSB and calc offset addr

	while (buflen)
	{
		buflen--;
		realaddr = W5100_RXBUFADDR + (offaddr & W5100_RX_BUF_MASK);
		*buf = W51_read(realaddr);
		offaddr++;
		buf++;
	}
	*buf='\0'; 												// buffer read is complete, terminate the string

    // Increase the S0_RX_RD value, so it point to the next receive
	W51_write(sockaddr+W5100_RX_RD_OFFSET, (offaddr & 0xFF00) >> 8);	// update RX read offset (MSB)
    W51_write(sockaddr+W5100_RX_RD_OFFSET + 1,(offaddr & 0x00FF));		// update LSB

    // Now Send the RECV command
    W51_write(sockaddr+W5100_CR_OFFSET, W5100_SKT_CR_RECV);			// issue the receive command
    _delay_us(5);											// wait for receive to start

    return  W5100_OK;
}




unsigned int  ReceivedSize(unsigned char  sock)
{
	unsigned int					val;
	unsigned int					sockaddr;

	if (sock >= W5100_NUM_SOCKETS)  return  0;
	sockaddr = W5100_SKT_BASE(sock);						// calc base addr for this socket
	val = W51_read(sockaddr+W5100_RX_RSR_OFFSET) & 0xff;
	val = (val << 8) + W51_read(sockaddr+W5100_RX_RSR_OFFSET + 1);
	return  val;
}




/*
 *  Simple wrapper function for selecting the W5100 device.  This function
 *  allows the library code to invoke a target-specific function for enabling
 *  the W5100 chip.
 */
void  my_select(void)
{
	W51_ENABLE;
}



/*
 *  Simple wrapper function for deselecting the W5100 device.  This function
 *  allows the library code to invoke a target-specific function for disabling
 *  the W5100 chip.
 */
void  my_deselect(void)
{
	W51_DISABLE;
}



/*
 *  my_xchg      callback function; exchanges a byte with W5100 chip
 */
unsigned char  my_xchg(unsigned char  val)
{
	SPDR = val;
	while  (!(SPSR & (1<<SPIF)))  ;
	return  SPDR;
}



/*
 *  my_reset      callback function; force a hardware reset of the W5100 device
 */
void  my_reset(void)
{
	RESET_PORT |= (1<<RESET_BIT);							// pull reset line high
	RESET_DDR |= (1<<RESET_BIT);							// now make it an output
	RESET_PORT &= ~(1<<RESET_BIT);							// pull the line low
	_delay_ms(5);											// let the device reset
	RESET_PORT |= (1<<RESET_BIT);							// done with reset, pull the line high
	_delay_ms(10);											// let the chip wake up
}

// LCD SETUP/////////////////////////////////////////////////////////////////////////////
void LCD_Command(unsigned char cmnd)
{
	LCD_Data_Port= cmnd;
	LCD_Command_Port &= ~(1<<RS);		/* RS=0 command reg. */
	LCD_Command_Port &= ~(1<<RW);		/* RW=0 Write operation */
	LCD_Command_Port |= (1<<EN);		/* Enable pulse */
	_delay_us(1);
	LCD_Command_Port &= ~(1<<EN);
	_delay_ms(3);
}

void LCD_Char (unsigned char char_data)	/* LCD data write function */
{
	LCD_Data_Port= char_data;
	LCD_Command_Port |= (1<<RS);		/* RS=1 Data reg. */
	LCD_Command_Port &= ~(1<<RW);		/* RW=0 write operation */
	LCD_Command_Port |= (1<<EN);		/* Enable Pulse */
	_delay_us(1);
	LCD_Command_Port &= ~(1<<EN);
	_delay_ms(1);
}

void LCD_Init (void)					/* LCD Initialize function */
{
	LCD_Command_Dir = 0xFF;				/* Make LCD command port direction as o/p */
	LCD_Data_Dir = 0xFF;				/* Make LCD data port direction as o/p */
	_delay_ms(20);						/* LCD Power ON delay always >15ms */
	
	LCD_Command (0x38);					/* Initialization of 16X2 LCD in 8bit mode */
	LCD_Command (0x0C);					/* Display ON Cursor OFF */
	LCD_Command (0x06);					/* Auto Increment cursor */
	LCD_Command (0x01);					/* clear display */
	LCD_Command (0x80);					/* cursor at home position */
}

void LCD_String (char *str)				/* Send string to LCD function */
{
	int i;
	for(i=0;str[i]!=0;i++)				/* Send each char of string till the NULL */
	{
		LCD_Char (str[i]);
	}
}

void LCD_String_xy (char row, char pos, char *str)	/* Send string to LCD with xy position */
{
	if (row == 0 && pos<16)
	LCD_Command((pos & 0x0F)|0x80);		/* Command of first row and required position<16 */
	else if (row == 1 && pos<16)
	LCD_Command((pos & 0x0F)|0xC0);		/* Command of first row and required position<16 */
	LCD_String(str);					/* Call LCD string function */
}

void LCD_Clear()
{
	LCD_Command (0x01);					/* clear display */
	LCD_Command (0x80);					/* cursor at home position */
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////




int main(void)
{
	
	LCD_Init();							/* Initialize LCD */
    unsigned int sockaddr;
    unsigned char mysocket;
    unsigned int rsize;

    mysocket = 0;                                            // magic number! declare the socket number we will use (0-3)
    sockaddr = W5100_SKT_BASE(mysocket);                    // calc address of W5100 register set for this socket

    /*
     *  Initialize the ATmega644p SPI subsystem
     */
    CS_PORT |= (1 << CS_BIT);                                    // pull CS pin high
    CS_DDR |= (1 << CS_BIT);                                    // now make it an output

    SPI_DDR |= (1 << DDB5) | (1 << DDB7) | (1 << DDB4);  // set MOSI, SCK, and SS as outputs
    SPI_DDR &= ~(1 << DDB6);                             // set MISO as input
    SPI_PORT |= (1 << DDB4);
	SPCR = (1 << SPE) | (1 << MSTR);                             // enable SPI, master mode 0
    SPSR |= (1 << SPI2X);                                        // set the clock rate fck/2

    /*
     *  Load up the callback block, then initialize the Wiznet W5100
     */
    my_callbacks._select = &my_select;                        // callback for selecting the W5100
    my_callbacks._xchg = &my_xchg;                            // callback for exchanging data
    my_callbacks._deselect = &my_deselect;                    // callback for deselecting the W5100
    my_callbacks._reset = &my_reset;                        // callback for hardware-reset of the W5100

    W51_register(&my_callbacks);                            // register our target-specific W5100 routines with the W5100 library
    W51_init();                                                // now initialize the W5100

    /*
     *  Configure the W5100 device to handle PING requests.
     *  This requires configuring the chip, not a specific socket.
     */
    W51_config(&my_cfg);                                    // config the W5100 (MAC, TCP address, subnet, etc)

    /*
     *  The main loop.  Control stays in this loop forever, processing any received packets
     *  and sending any requested data.
     */
    while (1)
    {
        unsigned char buf[256];  // buffer to store incoming data
        rsize = ReceivedSize(mysocket);  // find out how many bytes received

        if (rsize > 0)
        {
            if (Receive(mysocket, buf, rsize) != W5100_OK) break;  // if we had problems, all done

            // Convert the buffer to a string and process it here
			
            buf[rsize] = '\0';
            //process_data(buf);
			LCD_Clear();
			LCD_String(buf);
        }
    }

    return 0;
}




		

				

