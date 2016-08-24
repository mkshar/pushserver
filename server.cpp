#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <sys/select.h>

#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include "shedule.hpp"

#define BUFSIZE 1024
#define MAX_CLIENTS 5

struct client_info_t
{
	int fd;
	std::string id;
	time_t last_update;

	client_info_t( int socket_fd ):
		fd( socket_fd ),
		last_update( 0 )
	{
	}
};


typedef std::map< int, client_info_t > clients_t;

int main( int argc, char** argv )
{
	alarms_t alarms;
	shedule_t shedule;
	signaled_alarms_t signaled;	

	clients_t clients;

	int internal_error;

	int i;
	char buffer[ BUFSIZE ];

	if( argc != 3 )
	{
		fprintf( stderr, "Usage: %s <port> <reconnect-interval>\n", argv[0] );
		exit( 1 );
	}
	
	load_alarms( alarms, "alarms.conf" );
	make_shedule( shedule, alarms );

	std::cout << "SHEDULE:" << std::endl;
	for( auto i: shedule )
	{
		std::cout << utime_t( i.first ) << ": " << ( *i.second ) << std::endl;
	}


	int port               = atoi( argv[ 1 ] );
	int reconnect_interval = atoi( argv[ 2 ] );
	int backlog_size       = MAX_CLIENTS;

	if( reconnect_interval < 0 )
		reconnect_interval = 60;

	struct timeval timeout;
	
	signal( SIGPIPE, SIG_IGN );

	for(;;)
	{

		int socket_fd = socket( AF_INET, SOCK_STREAM, 0 );
		if( socket_fd < 0 )
		{
			printf( "Cannot open socket, retrying at %i seconds", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}

		int enable = 1;
		if( setsockopt( socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof( int ) ) < 0 )
		{
			close( socket_fd );
			printf( "setsockopt(SO_REUSEADDR) failed, retrying at %i seconds", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}

		struct sockaddr_in serveraddr;
		memset( &serveraddr, 0x00, sizeof( serveraddr ) );
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_addr.s_addr = htons( INADDR_ANY );
		serveraddr.sin_port = htons( port );
 
		if( bind( socket_fd, (struct sockaddr *) &serveraddr, sizeof( serveraddr ) ) < 0 )
		{
			close( socket_fd );
			printf( "Cannot bind socket, retrying at %i seconds", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}
 
	 	if( listen( socket_fd, backlog_size ) < 0 )
		{
			close( socket_fd );
			printf( "Cannot listen, retrying at %i seconds", reconnect_interval );
			sleep( reconnect_interval );
			continue;
		}

		fd_set active_fd_set;
		
		FD_ZERO( &active_fd_set );
		FD_SET( socket_fd, &active_fd_set );

		for( int n = 0;; ++n )
		{
			printf( "Waiting (%i)...\n", n );

			internal_error = 0;

			fd_set read_fd_set = active_fd_set;
			timeout.tv_sec  = reconnect_interval;
			timeout.tv_usec = 0;

			if( select( FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout ) == -1 )
			{
				fprintf( stderr, "select() failed\n" );
				internal_error = 1;
				break;
			}

			for( i = 0; i < FD_SETSIZE; ++i )
			{
				if( FD_ISSET( i, &read_fd_set ) )
				{
					if( i == socket_fd ) /* incoming connection */
					{
						int new_socket_fd;
						struct sockaddr_in clientaddr;
						socklen_t size = sizeof( clientaddr );						
						new_socket_fd = accept( socket_fd, (struct sockaddr*) &clientaddr, &size );
						if( new_socket_fd < 0 )
                  				{
							fprintf( stderr, "accept() failed\n" );
							internal_error = 1;
							break;
						}
						printf( "Request from host %s, port %i.\n", inet_ntoa( clientaddr.sin_addr ), ntohs( clientaddr.sin_port ) );
						if( clients.size() >= MAX_CLIENTS )
							close( new_socket_fd ); // too many connections
						else
						{
							clients.insert( std::make_pair( new_socket_fd, client_info_t( new_socket_fd ) ) );
							FD_SET( new_socket_fd, &active_fd_set );
						}
					}
					else
					{
						memset( buffer, 0x00, sizeof( buffer ) );
						int bytes_read = read( i, buffer, sizeof( buffer ) );
						if( bytes_read > 0 )
						{
							if( ( strncmp( buffer, "HELLO\n", 6 ) == 0 ) && ( strlen( buffer ) > 6 ) )
							{
								char* id = buffer + 6;
								printf( "Client idenified: \"%s\"\n", id );
								auto client = clients.find( i );
								if( client != clients.end() )
								{
									client->second.id = id;
								}
								write( i, buffer, strlen( buffer ) );
							}
							else if( strncmp( buffer, "HEARTBEAT\n", 10 ) == 0 )
							{
								auto client = clients.find( i );
								if( client != clients.end() )
									printf( "Heartbeat message from client %s (%i)\n", client->second.id.c_str(), i );
							}
						}
						else
						{
							printf( "Closing client connection.\n" );
							close( i );
							auto client = clients.find( i );
							if( client != clients.end() )
								clients.erase( client );
							FD_CLR( i, &active_fd_set );
						}
					}
				}
			}
			if( internal_error != 0 )
				break;
				
			time_t now = time( nullptr );
			queue_alarms( now, shedule, signaled );
			
			std::vector< int > clients_del_queue;
			for( auto i: clients )
			{
				client_info_t& client = i.second;
				
//				if( client.fd == -1 )
//					continue;
				if( client.id.empty() ) // client not identified
					continue;
				auto alarms = signaled.find( client.id );
				if( alarms != signaled.end() )
				{
					if( alarms->second.empty() )
						continue;

					printf( "Alerting client %s (%i)\n", client.id.c_str(), client.fd );
					
					for( auto i: alarms->second )
					{
						if( write( client.fd, i->message.c_str(), i->message.size() + 1 ) <= 0 )
						{
							printf( "Cannot write to socket.\n" );
							FD_CLR( client.fd, &active_fd_set );
							close( client.fd );
							clients_del_queue.push_back( client.fd );
							break;
						}
						else
							client.last_update = now;
					}
					alarms->second.clear();
				}
			}
			for( auto i: clients_del_queue )
			{
				clients.erase( i );
			}
		}
		
		for( i = 0; i < FD_SETSIZE; ++i )
			if( FD_ISSET( i, &active_fd_set ) )
				close( i );

		printf( "Reconnecting at %i second(s)\n", reconnect_interval );
		sleep( reconnect_interval );

	} // main loop
 
	return 0;
}