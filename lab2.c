/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Jeremy Adkins ja3072
 * James Kolsby jrk2181
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "fbputchar.h"
#include "usbkeyboard.h"

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* micro36.ee.columbia.edu */
#define SERVER_HOST "128.59.148.182"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128

/*
 * References:
 *
 * http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);
void send_to_server(char *buf);


int x,y;
int outrow = 19;
int outcol = 0;
char display[20][64];
char printBuf[2][64];

void fbprint(char msg[2][64]) {

    int r,c;

    for (r = 0; r < 18; r++) {
      for (c = 0; c < 64; c++) {
	display[r][c] = display[r+2][c];
      }
    }

    for (r = 18; r < 20; r++) {
      for (c = 0; c < 64; c++) {
	display[r][c] = msg[r-18][c];
      }
    }

    for (r = 0; r < 20; r++) {
      for (c = 0; c < 64; c++){
	fbputchar(display[r][c], r+1, c);
      }
    }

  }


int main()
{
  int err, col;

  char message[2][64];

/*
  char foo[2][64];
  for (int j = 0; j < 2; j++) {
	for (int k = 0; k < 64; k++) {
		foo[j][k] = ' ';
	}
  }

  foo[0][0] = 'F';
*/

  for (x = 0; x < 20; x++) {
    for (y = 0; y < 64; y++) {
      display[x][y] = ' ';
    }
  }

  for (x = 0; x < 2; x++) {
    for (y = 0; y < 64; y++) {
      message[x][y] = ' ';
      printBuf[x][y] = ' ';
    }
  }


  int inrow = 22;
  int incol = 0;
  int startfix = 0;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  //fbprint(foo);

//--------------------------------------------------------------------

  fbclear();
  for (col = 0; col < 64; col++) {
    fbputchar('-', 21, col);
  }
  for (col = 0; col < 53; col++) {
    fbputchar('-', 0, col);
  }
  fbputchar('_', 22, 0);

//--------------------------------------------------------------------

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
   
  // Create a TCP communications socket
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  // Get the server address 
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  // Connect the socket to the server 
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  // Start the network thread 
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  /* Look for and handle keypresses */
  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
      printf("%s\n", keystate);
      fbputs(keystate, 0, 56);
      if (packet.keycode[0] == 0x29) { /* ESC pressed? */
	//fbclear();
	break;
      }

      if (packet.keycode[0] == 0x28) { //Enter
        int r, c;

	char message_out[BUFFER_SIZE];

	fbprint(message);

	strncpy(message_out, message[0], BUFFER_SIZE/2);
	strncpy(message_out+BUFFER_SIZE/2, message[1], BUFFER_SIZE/2);

	send_to_server(message_out);

	for (c = 0; c < 64; c++) {
	  for (r = 0; r < 2; r++) {
	    message[r][c] = ' ';
	  }
	}

        for (r = 22; r < 24; r++) {
	  for (c = 0; c < 64; c++) {
	    fbputchar(' ', r, c);
	  }
        }

        fbputchar('_', 22, 0);
	incol = 0;
	inrow = 22;
	startfix = 0;


      }      

      if (startfix == 0) {
	incol = 0;
	startfix = 1;
      }

      char in = getkey(keystate);

      if (in != '\0') {
        if (incol < 63) {
	  
	  message[inrow-22][incol] = in;

	  fbputchar(in, inrow, incol);
          fbputchar('_', inrow, incol + 1);
	  incol++;
        }
        else if ((incol == 63) & (inrow == 22)){
	  
	  message[0][63] = in;

	  fbputchar(in, 22, 63);
	  fbputchar('_', 23, 0);
	  incol = 0;
	  inrow = 23;
        }
      }

      fbputchar(getkey(keystate), 0, 54);
    }
  }

  // Terminate the network thread
  pthread_cancel(network_thread);

  // Wait for the network thread to finish
  pthread_join(network_thread, NULL);

  return 0;
}

void send_to_server(char *buf) {
    int n;
    if ((n = write(sockfd, buf, BUFFER_SIZE - 1)) > 0) {
	printf("sent: %s", buf);
    }
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n, x, y, i;

  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("recvBuf %s", recvBuf);

  }
  
  i = 0;
  for (x = 0; x < 2; x++) {
    for (y = 0; y < 64; y++) {
      printBuf[x][y] = recvBuf[i];
      fbputchar(recvBuf[i], x + 3, y);
      i++;
    }
  }
  /*
  strncpy(printBuf[0], recvBuf, BUFFER_SIZE/2);
  strncpy(printBuf[1], recvBuf, BUFFER_SIZE/2);
  */

  fbprint(printBuf);

  return NULL;
}

