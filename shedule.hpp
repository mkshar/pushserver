#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <ostream>

struct alarm_t
{
	std::string user_id;

	unsigned    hour;
	unsigned    minute;
	std::string message;

	alarm_t
	(
		const std::string& user_id,
		unsigned hour,
		unsigned minute,
		const std::string& message
	):
		user_id( user_id ),
		hour( hour ),
		minute( minute ),
		message( message )
	{
	}

	virtual ~alarm_t()
	{
	}
	
	friend std::ostream& operator << ( std::ostream& stream, const alarm_t& alarm );

	virtual const char* type() const = 0; //{ return "?"; }
	virtual time_t first( time_t rawtime ) const = 0; //{ return rawtime; }
	virtual time_t next ( time_t rawtime ) const = 0; //{ return rawtime; }
};

struct normal_alarm_t:
	public alarm_t
{
	using alarm_t::alarm_t;

	virtual const char* type() const
	{
		return "N";
	}

	virtual time_t first( time_t rawtime ) const
	{
		tm* timeinfo = localtime( &rawtime );
		timeinfo->tm_hour = hour;
		timeinfo->tm_min  = minute;
		timeinfo->tm_sec  = 0;
		return mktime( timeinfo );
	}

	virtual time_t next ( time_t rawtime ) const
	{
		tm* timeinfo = localtime( &rawtime );
		timeinfo->tm_mday += 1;
		timeinfo->tm_hour =  hour;
		timeinfo->tm_min  =  minute;
		timeinfo->tm_sec  = 0;
		return mktime( timeinfo );
	}
};

struct periodic_alarm_t:
	public alarm_t
{
	using alarm_t::alarm_t;

	virtual const char* type() const
	{
		return "P";
	}

	virtual time_t first( time_t rawtime ) const
	{
		return rawtime + 1;
	}

	virtual time_t next ( time_t rawtime ) const
	{
		tm* timeinfo = localtime( &rawtime );
		timeinfo->tm_hour += hour;
		timeinfo->tm_min  += minute;
		return mktime( timeinfo );
	}
};

typedef std::shared_ptr< alarm_t > alarm_ptr;
typedef std::vector< alarm_ptr > alarms_t;

struct utime_t
{
	const time_t data;
	utime_t( time_t data ):
		data( data )
	{
	}

	operator time_t() const
	{
		return data;
	}
};

typedef std::multimap< time_t, alarm_ptr > shedule_t;

inline std::ostream& operator << ( std::ostream& stream, const utime_t& utime )
{
	char buffer[ 80 ];
	if( strftime( buffer, sizeof( buffer ), "%Y.%m.%d %H:%M:%S", localtime( &utime.data ) ) != 0 )
		stream << buffer;
	return stream;
}

inline std::ostream& operator << ( std::ostream& stream, const alarm_t& alarm )
{
	return stream
		<< alarm.user_id
		<< ": "
		<< alarm.hour
		<< ":"
		<< alarm.minute
		<< " ("
		<< alarm.type()
		<< ") "
		<< alarm.message;
}

inline void make_shedule( shedule_t& shedule, const alarms_t& alarms )
{
	time_t now = time( nullptr );	

	for( const auto& i: alarms )
	{
		time_t start_at = i->first( now );
		shedule.insert( std::make_pair( start_at, i ) );
	}

	if( shedule.empty() )
		return;

	for(;;)
	{
		auto head = shedule.begin();
		if( head->first <= now )
		{
			alarm_ptr alarm = head->second;

			shedule.erase( head );
			time_t start_at = alarm->next( now );
			shedule.insert( std::make_pair( start_at, alarm ) );
		}
		else
			break;
	}
}

typedef std::map< std::string, std::set< alarm_ptr > > signaled_alarms_t;

inline void queue_alarms( time_t now, shedule_t& shedule, signaled_alarms_t& alarms )
{
	if( shedule.empty() )
		return;

	for(;;)
	{
		auto head = shedule.begin();
		time_t start_at = head->first;
		if( start_at <= now )
		{
       			alarm_ptr alarm = head->second;

			//alarms.push_back( alarm );
			alarms[ alarm->user_id ].insert( alarm );


			shedule.erase( head );

			do
			{
				start_at = alarm->next( start_at );
			}
			while( start_at <= now );

			shedule.insert( std::make_pair( start_at, alarm ) );
		}
		else
			break;
	}
}

inline alarm_t* read( std::istream& stream )
{
	if( stream.eof() )
		return nullptr;

	std::string temp;

	std::string type;
	std::string user_id;
        unsigned hour, minute;
	std::string message;

	stream >> type;
	getline( stream, temp );
	stream >> user_id;
	getline( stream, temp );
	stream >> hour >> minute;
	getline( stream, temp );
	getline( stream, message );
	getline( stream, temp );

	if( type == "normal" )
		return new normal_alarm_t( user_id, hour, minute, message );
	else if( type == "periodic" )
		return new periodic_alarm_t( user_id, hour, minute, message );
	else
		return nullptr;
}

inline void load_alarms( alarms_t& alarms, const char* filename )
{
	std::ifstream in( filename );
	for(;;)
	{
		alarm_t* alarm = read( in );
		if( alarm != nullptr )
		{
			alarms.push_back( alarm_ptr( alarm ) );
		}
		else
			break;
	}
}