/*
 * socketio.c socket layer API implementation 
 */

#include <socketio.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#endif /* linux header files */

/* helper function prototypes */
static inline int is_connected(struct socketio *);
static inline int sio_select(struct socketio *, int);

#define SELECT_READ 0x0
#define SELECT_WRITE 0x1 

/**
 * sio_set_option : set a socket option 
 */
extern void sio_set_option(struct socketio *sio,
			   int opt)
{
	sio->opts |= opt;
}


/**
 * sio_set_timeout : change timeout value for
 * non-blocking sockets. 
 */
extern void sio_set_timeout(struct socketio *sio,
			    unsigned int s)
{
	sio->timeout = s;
}


/**
 * sio_set_maxnfails : change maximum number of fails 
 * value
 */
extern void sio_set_maxnfails(struct socketio *sio,
			      int n)
{
	sio->maxnfails = n;
}


/**
 * sio_create_socket : initilizes a sio socket
 * On windows it may also initilize Winsock lib.
 */
extern int sio_socket(struct socketio *sio)
{

#ifdef _WIN32
	WSADATA data;
	if (WSAStartup(MAKEWORD(2,2), &data) != 0)
		return -1;
	
#endif /* win32 */
	
	sio->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sio->socket == INV_SOCK) {
#ifdef _WIN32
		WSACleanup();
#endif /* win32 */
		return -1;
	} else if (IS_NONBLOCK(sio)){
		/* make a non-blocking socket if it's required */
		int result;
#ifdef _WIN32
		
		unsigned long mode = 1;
	        result = ioctlsocket(sio->socket, FIONBIO, &mode);
		if (result != NO_ERROR)
			goto error;
		
#elif defined __linux__

		int flags;
		flags = fcntl(sio->socket, F_GETFL, 0);

		if (flags < 1 || (fcntl(sio->socket, F_SETFL,
				 flags | O_NONBLOCK) < 0))
			goto error;
#endif /* linux */
	}

	return 0;

error :
	sio_close(sio);
	return -1;
}


/**
 * sio_connect : try to connect to another device.
 * This function tries to connect to host:port.
 * It uses a documented algorithm when is used a 
 * a non-blocking socket
 */
extern int sio_connect (struct socketio *sio, char *host,
			port_t port)
{
	struct sockaddr_in s;
	memset((char *)&s, 0, sizeof(struct sockaddr_in));

	s.af_family = AF_INET;
	s.sin_port = htons(port);

#ifdef _WIN32

	inetPton(AF_INET, host, &s.sin_addr->s_addr);

#elif defined __linux__

	inet_pton (AF_INET, host, &sin_addr->s_addr);
	errno = 0;

#endif 
	int res = 0;
	res = connect(AF_INET,(struct sockaddr *)&s,
		      sizeof(struct sockaddr_in));

	if (res == SOCK_ERROR) {

/* error handling */
#ifdef _WIN32
		switch (WSAGetLastError()){
		case WSAEINPROGRESS :
		case WSAEWOULDBLOCK:
			return  is_connected(sio);
		default :
			return -1;
		}
		
#elif defined __linux__
		if (errno == EINPROGRESS)
			return  is_connected(sio);
		else
			return -1;
#endif
	}

	return 0;
}




/**
 * sio_send : try to send data over a socket 
 * If sio_send couldn't send at least 1 byte after
 * maxnfails * timeout seconds network connection is disconnected 
 */
int sio_send (struct socketio *sio, char *buf,
		     size_t len)
{
	size_t total_bytes = 0 ;
	ssize_t temp;
	int nfails = 0;

	while (total_bytes < len) {

		temp = send (sio->socket, &buf[total_bytes],
			     len - total_bytes , 0);

		if (temp == SOCK_ERR) {
			nfails++;
			
#ifdef _WIN32
			if (WSAGetLastError() ==
			    WSAEWOULDBLOCK)
				sio_select(sio);
			else
				return -1;
			
#elif defined __linux__
			
			if (errno == EAGAIN ||
			    errno == EWOULDBLOCK)
				sio_select(sio, SELECT_WRITE);
			else
				return -1;
			
#endif

			if (nfails >= sio->maxnfails)
				return -1;
		} else {
			total_bytes += temp;
			nfails = 0;
		}

	}

	return 0;
}


/**
 * sio_recv : try to receive len bytes
 * The algorithm used for this function is the
 * same as send's 
 */ 
int sio_recv (struct socketio *sio, char *buf,
	      size_t len)
{
	size_t total_bytes = 0;
	ssize_t temp;
	int nfails = 0;

	while (total_bytes < len) {
		temp = recv(sio->socket, &buf[total_bytes],
			    len - total_bytes, 0);

		if (temp == SOCK_ERR) {
			nfails++;
			
#ifdef _WIN32
			if (WSAGetLastError() ==
			    WSAEWOULDBLOCK) 
				 sio_select(sio);
			else
				return -1;

#elif defined __linux__

			if (errno == EAGAIN ||
			    errno == EWOULDBLOCK)
				sio_select(sio, SELECT_READ);
			else
				return -1;
#endif /* linux */

			if (nfails >= sio->maxnfails)
				return -1;

		} else if (temp > 0){
			
			total_bytes += (size_t)temp;
			nfails = 0;

		} else  /* Connection is disconnected */
			return -1;
	}

	return 0;
}


/**
 * sio_close : close a socket descriptor
 * It also calls WSACleanup() on windows platform.
 */
void sio_close(struct socketio *sio)
{
#ifdef _WIN32
	WSACleanup();
	closesocket(sio->socket);
#elif defined __linux__
	close(sio->socket);
#endif

	sio->socket = INV_SOCK;
}


/**
 * is_connected : check weather socket is 
 * connected or not.
 */
static inline int is_connected(struct socketio *sio)
{
	int res;

	res = sio_select(sio, SELECT_WRITE);

#ifdef _WIN32
	
	if (res == 1)
		return 0;
	
       	return -1;

#elif defined __linux__

	if (res == 1) {
		
		int err;
		err = 0;
		socklen_t o_size = sizeof(int); 

		getsockopt(sio->socket, SOL_SOCKET, SO_ERROR,
			   &err, &o_size);

		if (err)
			return -1;

		return 0;

	}

	return -1;
#endif
}


/**
 * sio_select : a wrapper for select() function.
 * Note : This is not a general function.
 * It only works in this project and SIO module.
 */
static inline int sio_select(struct socketio *sio, int mode)
{
	struct timeval t;
	fd_set set;
	int res;
	
	FD_ZERO(&set);
	FD_SET(sio->socket, &set);

	t->tv_sec = sio->timeout;
	t->tv_usec = 0;

	res = select (sio->socket +1,
		      mode == SELECT_READ ? &set : NULL,
		      mode == SELECT_WRITE ? &set, NULL,
		      NULL, &t);

	if (res == 1)
		return 1;

	return 0;
	
}
