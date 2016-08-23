#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <signal.h>

#define BUFSIZE 1024

int main( int argc, char** argv )
{
	int read_bytes;
	int written_bytes;

	int                socket_fd;
	struct sockaddr_in serveraddr;
	struct hostent*    server;

	char* hostname;
	int   port;
	char* client_id;
	int   reconnect_interval;
	char  buffer[ BUFSIZE ];

	struct timeval timeout;
	fd_set read_fd_set;

	if( argc != 5 )
	{
		fprintf( stderr, "Usage: %s <hostname> <port> <client-id> <reconnect-interval>\n", argv[0] );
		exit( 1 );
	}

	hostname           = argv[ 1 ];
	port               = atoi( argv[ 2 ] );
	client_id          = argv[ 3 ];
	reconnect_interval = atoi( argv[ 4 ] );

	if( reconnect_interval < 0 )
		reconnect_interval = 60;

	signal( SIGPIPE, SIG_IGN );

	for(;;)
	{
	
		socket_fd = socket( AF_INET, SOCK_STREAM, 0 );
		if( socket_fd < 0 ) 
		{
			printf( "Cannot open socket, retrying at %i seconds", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}

		server = gethostbyname( hostname );
		if( server == NULL )
		{
			close( socket_fd );
	        	printf( "Cannot find host %s, retrying at %i second(s)\n", hostname, reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}
		memset( &serveraddr, 0x00, sizeof( serveraddr ) );
		serveraddr.sin_family = AF_INET;
		memcpy( &serveraddr.sin_addr.s_addr, server->h_addr, server->h_length );
		serveraddr.sin_port = htons( port );

		if( connect( socket_fd, (struct sockaddr*) &serveraddr, sizeof(serveraddr) ) < 0 )
		{
			close( socket_fd );
	        	fprintf( stderr, "Cannot connect to %s at port %i\n, reconnecting at %i second(s)\n", hostname, port, reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}
	
		printf( "Connected to host %s at port %i, sending HELLO\n", hostname, port );
		memset( buffer, 0, BUFSIZE );
		sprintf( buffer, "HELLO\n%s", client_id );

		written_bytes = write( socket_fd, buffer, strlen( buffer ) + 1 );
		if( written_bytes < 0 )
		{
			close( socket_fd );
			fprintf( stderr, "Cannot write to socket. Aborting connection. Reconnecting at %i second(s)\n", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}
		else
		{
			for( int n = 0;; ++n )
			{
				FD_ZERO( &read_fd_set );
				FD_SET( socket_fd, &read_fd_set );

				printf( "Waiting (%i)...\n", n );
				timeout.tv_sec  = reconnect_interval;
				timeout.tv_usec = 0;

				if( select( FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout ) == -1 )
				{
					fprintf( stderr, "select() failed\n" );
					break;
				}

				if( FD_ISSET( socket_fd, &read_fd_set ) )
				{
					memset( buffer, 0, BUFSIZE );
					read_bytes = read( socket_fd, buffer, BUFSIZE );
					if( read_bytes == 0 )
					{
						fprintf( stderr, "Connection closed\n" );
						break;
					}
					if( read_bytes < 0 )
					{
						fprintf( stderr, "Cannot read from socket" );
						break;
					}
					printf( "Message from the server: %s\n", buffer );
				}
				else
				{
					sprintf( buffer, "HEARTBEAT\n" );
					written_bytes = write( socket_fd, buffer, strlen( buffer ) + 1 );
					if( written_bytes < 0 )
					{
						fprintf( stderr, "Cannot write to socket.\n" );
						break;
					}
				}
			}
		}
		close( socket_fd );
		printf( "Reconnecting at %i second(s)\n", reconnect_interval );
		sleep( reconnect_interval );

	} // main loop

	return 0;
}
