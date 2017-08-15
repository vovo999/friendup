/*©mit**************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
* Copyright 2014-2017 Friend Software Labs AS                                  *
*                                                                              *
* Permission is hereby granted, free of charge, to any person obtaining a copy *
* of this software and associated documentation files (the "Software"), to     *
* deal in the Software without restriction, including without limitation the   *
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or  *
* sell copies of the Software, and to permit persons to whom the Software is   *
* furnished to do so, subject to the following conditions:                     *
*                                                                              *
* The above copyright notice and this permission notice shall be included in   *
* all copies or substantial portions of the Software.                          *
*                                                                              *
* This program is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
* MIT License for more details.                                                *
*                                                                              *
*****************************************************************************©*/

#include <core/library.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "systembase.h"
#include <util/log/log.h>
#include <sys/stat.h>
#include <util/buffered_string.h>
#include <dirent.h>
#include <util/string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <communication/comm_service.h>
#include <communication/comm_service_remote.h>
#include <communication/comm_msg.h>
#include <system/json/jsmn.h>
#include <network/socket.h>

#define SUFFIX "fsys"
#define PREFIX "Remote"

/** @file
 * 
 *  Remote file system
 *
 *  @author PS (Pawel Stefanski)
 *  @date created 01/06/2016
 */


//
// special structure
//

typedef struct SpecialData
{
	CommServiceRemote	*csr;
	SystemBase 					*sb;
	
	char								*host;
	char 							*id;
	char 							*login;
	char								*passwd;
	char								*devid;
	char 							*privkey;
	char 							fileptr[ 64 ];
	
	char								*remoteUserName;
	char								*localDevName;
	char								*remoteDevName;
	
	int								idi, 
										logini, 
										passwdi, 
										devidi,
										hosti, 
										remotepathi, 
										fileptri;

	int								mode;			// read or write
	char 							*tmppath;			// path to temporary file
	char 							*remotepath;		// path to remote file
	int								fileSize;		// temporary file size
	
	CommFCConnection 	*con;			// remote fs connection
	
	char								*address;	// hold destination server address
	int 								port;			// port
	int								secured;		// is connection secured
}SpecialData;

//
//
//

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) 
{
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
			strncmp(json + tok->start, s, tok->end - tok->start) == 0) 
	{
		return 0;
	}
	return -1;
}

const char *GetSuffix()
{
	return SUFFIX;
}

const char *GetPrefix()
{
	return PREFIX;
}

//
//
//

void init( struct FHandler *s )
{
	//s->Info = dlsym( s->handle, "Info" );
}

//
//
//

void deinit( struct FHandler *s )
{
	
}

//
// connect macro
//

DataForm *SendMessageRFS( SpecialData *sd, DataForm *df )
{
	MsgItem tags[] = {
		{ ID_FCRE, (FULONG)0,  (FULONG)NULL },
		{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
		{ TAG_DONE, TAG_DONE, TAG_DONE }
	};

	DEBUG("Send message to targetDirect\n");
		
	DataForm *ldf = DataFormNew( tags );
		
	BufString *bs = NULL;
	FBYTE *lsdata = NULL;
	FULONG sockReadSize = 0;

	Socket *newsock = sd->sb->sl_SocketInterface.SocketConnectHost( sd->sb, sd->secured, sd->address, sd->port );
	if( newsock != NULL )
	{
		//bs = SendMessageAndWait( con, df );
		int size = sd->sb->sl_SocketInterface.SocketWrite( newsock, (char *)df, (FQUAD)df->df_Size );
		bs = sd->sb->sl_SocketInterface.SocketReadTillEnd( newsock, 0, 15 );
		
		if( bs != NULL )
		{
			DEBUG2("Received from socket %d\n", bs->bs_Size );
			lsdata = (FBYTE *)bs->bs_Buffer;
			sockReadSize = bs->bs_Size;
		}
		
		if( lsdata != NULL )
		{
			//DEBUG2("[CommServClient]:Received bytes %ld CommunicationServiceClient Sending message size: %d server: %128s\n", sockReadSize, df->df_Size, con->cfcc_Name );
			
			DataFormAdd( &ldf, lsdata, sockReadSize );
			
			ldf->df_Size = sockReadSize;
			DEBUG2("[CommServClient]: ---------------------Added new server to answer serverdfsize %ld sockreadsize %lu\n", ldf->df_Size, sockReadSize );
			
			DEBUG2("Message received '%.*s\n", (int)sockReadSize, lsdata );
			
			char *d = (char *)lsdata + (3*COMM_MSG_HEADER_SIZE);
			if( d[ 0 ] == 'f' && d[ 1 ] == 'a' && d[ 2 ] == 'i' && d[ 3 ] ==  'l' )
			{
				//char *tmp = "user session not found"; //22
				char *tmp = "device not found      ";
				
				if( strcmp( d, "fail<!--separate-->{\"response\":\"user session not found\"}" ) == 0 )
				{
					d += 33;
					memcpy( d, tmp, 22 );
				}
			}
		}
		else
		{
			DataFormAdd( &ldf, (FBYTE *)"{\"rb\":\"-1\"}", 11 );
		}
		
		if( bs != NULL )
		{
			BufStringDelete( bs );
		}
		
		sd->sb->sl_SocketInterface.SocketClose( newsock );
		
		return ldf;
	}
	
	DataFormDelete( ldf );
	
	return NULL;
}

//
//
//

CommFCConnection *ConnectToServerRFS( SpecialData *sd, char *conname )
{
	CommFCConnection *con = NULL;
	CommFCConnection *retcon = NULL;
	FBOOL coreConnection = FALSE;
	
	FriendCoreManager *fcm = sd->sb->fcm;
	
	DEBUG("Checking internal connections\n");
	con = fcm->fcm_CommService->s_Connections;
	while( con != NULL )
	{
		DEBUG("Going through connections %128s   vs  %128s\n", conname, con->cffc_ID );
		if( memcmp( conname, con->cfcc_Name, FRIEND_CORE_MANAGER_ID_SIZE ) == 0 )
		{
			coreConnection = TRUE;
			break;
		}
		con = (CommFCConnection *) con->node.mln_Succ;
	}
	
	Socket *newsock = NULL;
	char address[ 512 ];
	
	// connection was found, we must make copy of it
	
	if( con != NULL )
	{
		DEBUG("Create new socket based on existing connection\n");
		
		int port = fcm->fcm_CommService->s_port;// FRIEND_COMMUNICATION_PORT;
		//con = CommServiceAddConnection( fcm->fcm_CommServiceClient, con->cfcc_Address, address, id, SERVICE_TYPE_CLIENT );
		newsock = sd->sb->sl_SocketInterface.SocketConnectHost( sd->sb, fcm->fcm_CommService->s_secured, con->cfcc_Address, port );
		
		if( newsock != NULL )
		{
			sd->address = StringDuplicate( con->cfcc_Address );
			sd->port = port;
		
			retcon = sd->sb->sl_CommServiceInterface.CommFCConnectionNew( con->cfcc_Address, con->cfcc_Address, SERVICE_CONNECTION_OUTGOING, NULL );
			if( retcon != NULL )
			{
				retcon->cfcc_Socket = newsock;
			}
		}
	}
	
	// connection was not found, its probably ip/internet address
	
	else
	{
		DEBUG("Trying to setup connection with %s\n", conname );
		
		memset( address, 0, sizeof(address) );
		unsigned int i, i1 = 0;
		
		strcpy( address, conname );
		int port = fcm->fcm_CommService->s_port;
		
		for( i = 0 ; i < strlen( address ) ; i++ )
		{
			i1 = i + 1;
			
			if( address[ i ] == ':' )
			{
				address[ i ] = 0;
				if( address[ i1 ] != 0 )
				{
					port = atoi( &address[ i1 ] );
				}
			}
		}
		
		newsock = sd->sb->sl_SocketInterface.SocketConnectHost( sd->sb, fcm->fcm_CommService->s_secured, address, port );
		
		sd->address = StringDuplicate( address );
		sd->port = port;
		
		retcon = sd->sb->sl_CommServiceInterface.CommFCConnectionNew( address, conname, SERVICE_CONNECTION_OUTGOING, NULL );
		if( retcon != NULL )
		{
			retcon->cfcc_Socket = newsock;
		}
	}

	if( newsock != NULL )
	{
		int err = 0;
		DEBUG("Outgoing connection created\n");
		{
			DEBUG("Generate Data Form\n");
			DataForm * df = DataFormNew( NULL );
			DEBUG("DataForm Created\n");
			DataFormAdd( &df, (FBYTE *)fcm->fcm_ID, FRIEND_CORE_MANAGER_ID_SIZE );
			//INFO("Message created name byte %c%c%c%c\n", fcm->fcm_ID[32], fcm->fcm_ID[33], fcm->fcm_ID[34], fcm->fcm_ID[35]	);
		
			int sbytes = sd->sb->sl_SocketInterface.SocketWrite( newsock, (char *)df, (FQUAD)df->df_Size );
		
			DEBUG("Message sent %d\n", sbytes );
			DataFormDelete( df );
			
			char id[ FRIEND_CORE_MANAGER_ID_SIZE ];
			unsigned int i;
			
			// if thats our core connection we just copy ID
			
			if( coreConnection == TRUE )
			{
				memcpy( id, con->cffc_ID, FRIEND_CORE_MANAGER_ID_SIZE );
			}
			else
			{
				strcpy( id, address );
			
				for( i = 0; i< FRIEND_CORE_MANAGER_ID_SIZE; i++ )
				{
					if( fcm->fcm_ID [i ] == 0 )
					{
						fcm->fcm_ID[ i ] = '0';
					}
				}
			}
		}
	}
	
	if( retcon == NULL )
	{
		if( newsock != NULL )
		{
			sd->sb->sl_SocketInterface.SocketClose( newsock );
		}
	}
	else
	{
		retcon->cfcc_Service = fcm->fcm_CommService;
	}
	
	return retcon;
}

#define ANSWER_POSITION 3
#define HEADER_POSITION (ANSWER_POSITION*COMM_MSG_HEADER_SIZE)

//
// Mount device
//

void *Mount( struct FHandler *s, struct TagItem *ti, User *usr )
{
	File *dev = NULL;
	char *path = NULL;
	char *name = NULL;
	FULONG id = 0;
	SystemBase *sb = NULL;
	char *conname = NULL;
	char *config = NULL;
	char *loginuser = NULL;
	char *loginpasswd = NULL;
	char *mountingUserName = NULL;
	
	if( s == NULL )
	{
		FERROR("Rootfile == NULL\n");
		return NULL;
	}
	
	DEBUG("REMOTEFS Mounting filesystem!\n");
	
	if( ( dev = FCalloc( 1, sizeof( File ) ) ) != NULL )
	{
		struct TagItem *lptr = ti;
		
		//
		// checking passed arguments
		
		while( lptr->ti_Tag != TAG_DONE )
		{
			switch( lptr->ti_Tag )
			{
				case FSys_Mount_ID:
					id = (FULONG)lptr->ti_Data;
					break;
				case FSys_Mount_Path:
					path = (char *)lptr->ti_Data;
					DEBUG("Mount FS path set '%s'\n", path );
					break;
				case FSys_Mount_Server:
					conname = (char *)lptr->ti_Data;
					break;
				case FSys_Mount_Port:
					break;
				case FSys_Mount_Name:
					name = (char *)lptr->ti_Data;
					break;
				case FSys_Mount_SysBase:
					sb = (SystemBase *)lptr->ti_Data;
					break;
				case FSys_Mount_LoginUser:
					loginuser = (char *)lptr->ti_Data;
				break;
				case FSys_Mount_LoginPass:
					loginpasswd = (char *)lptr->ti_Data;
					break;
				case FSys_Mount_Config:
					config = (char *)lptr->ti_Data;
					break;
				case FSys_Mount_UserName:
					mountingUserName = (char *)lptr->ti_Data;
					break;
			}
		
			lptr++;
		}

		// Check if we're trying to log in using the sentinel user!
		// Sentinel's drives can always be mounted!
		/*
		DEBUG( "[MountFS] Testing if sentinel can mount %s ( %s==%s )\n", name, conname, fc_sentinel->Hostname );
		
		// Connection name is not always hostname!!
		
		if( fc_sentinel && fc_sentinel && strcmp( conname, fc_sentinel->Hostname ) == 0 && strcmp( fc_sentinel->Username, loginuser ) == 0 )
		{
			loginpasswd = fc_sentinel->UserObject->u_Password;
			DEBUG( "[MountFS] Using sentinel username and password.\n" );
		}
		*/
		//
		
		if( path == NULL )
		{
			FERROR("[ERROR]: Path option not found!\n");
			FFree( dev );
			return NULL;
		}
		
		if( conname == NULL )
		{
			FERROR("[ERROR]: Host option not found!\n");
			FFree( dev );
			return NULL;
		}
		
		init( s );
		
		// we are trying to open folder/connection
		
		unsigned int pathlen = strlen( path );
		dev->f_Path = FCalloc( pathlen + 10, sizeof(char) );
		strcpy( dev->f_Path, path );
		if( pathlen <=  0 )
		{
			strcat( dev->f_Path, ":" );
		}
		else
		{
			if( path[ pathlen-1 ] != ':' )
			{
				strcat( dev->f_Path, ":" );
			}
		}
		
		DEBUG("REMOTEFS:  path is ok '%s'\n", dev->f_Path );
		dev->f_FSys = s;
		dev->f_Position = 0;
		dev->f_User = usr;
		dev->f_Name = StringDuplicate( name );
		dev->f_Type = FType_Directory;
		dev->f_Size = 0;
		
		SpecialData *sd = FCalloc( 1, sizeof( SpecialData ) );
		if( sd != NULL )
		{
			dev->f_SpecialData = sd;
			sd->sb = sb;

			sd->address = StringDuplicate( conname );
			
			FriendCoreManager *fcm = sb->fcm;
			sd->csr = fcm->fcm_CommServiceRemote;
			sd->port = sd->csr->csr_port;
			//sd->port = 6503;
			
			if( config != NULL )
			{
				unsigned int i = 0, i1 = 0;
				unsigned int r;
				jsmn_parser p;
				jsmntok_t t[128]; // We expect no more than 128 tokens 

				jsmn_init(&p);
				r = jsmn_parse(&p, config, strlen(config), t, sizeof(t)/sizeof(t[0]));
				if( r > 0 )
				{
					for( i = 0 ;  i < r ; i++ )
					{
						i1 = i + 1;
						if (jsoneq( config, &t[i], "PublicKey") == 0) 
						{
							int len = t[ i1 ].end-t[ i1 ].start;
						
							sd->privkey = StringDuplicateN( config + t[ i1 ].start, len );
						}
					}
				}
			}
			
			sd->host = StringDuplicate( conname );
			
			sd->hosti = strlen( sd->host )+1;
			char usernamec[ 512 ];
			memset( usernamec, 0, 512 );
			sprintf( usernamec, "username=%s", loginuser );
			char passwordc[ 512 ];
			memset( passwordc, 0, 512 );
			sprintf( passwordc, "password=%s", loginpasswd );
			char sessionidc[ 512 ];
			memset( sessionidc, 0, 512 );
			sprintf( sessionidc, "sessionid=%s", "remote" );
			
			int enci;
			char enc[ 10 ];
			strcpy( enc, "enc=" );
			if( sd->privkey != NULL )
			{
				strcat( enc, "yes" );
				enci = 8;
			}
			else
			{
				strcat( enc, "no" );
				enci = 7;
			}
			
			sd->logini = strlen( usernamec );
			sd->passwdi = strlen( passwordc );
			sd->idi = strlen( sessionidc );
			
			sd->login = StringDuplicate( usernamec );
			sd->passwd = StringDuplicate( passwordc );

			sd->devid = StringDuplicate( "deviceid=remote" );

			sd->devidi = strlen( sd->devid );
		
			MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
			//{ MSG_GROUP_START, 0,  0 },
				{ ID_QUER, (FULONG)sd->hosti, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)30, (FULONG)"system.library/login" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)usernamec },
						{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)passwordc },
						//{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sessionidc },
						{ ID_PRMT, (FULONG) sd->devidi, (FULONG)sd->devid },
						{ ID_PRMT, (FULONG) enci, (FULONG) enc },
						{ ID_PRMT, (FULONG) 18, (FULONG)"appname=Mountlist" },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
		
			//DEBUG("\n\n\nBefore creating DataForm\n");
		
			DataForm *df = DataFormNew( tags );
		
			DEBUG("Message will be send\n");
			
			DataForm *recvdf = NULL;
			
			recvdf = SendMessageRFS( sd, df );
			DataFormDelete( df );
			
			//DataForm *recvdf = CommServiceSendMsg( sd->cs, df );
			DEBUG("Response received\n");
			int error = 1;

			if( recvdf != NULL && recvdf->df_Size > 0 && recvdf->df_ID == ID_FCRE )
			{
				DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
				unsigned int i=0;
				char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
				unsigned int r;
				jsmn_parser p;
				jsmntok_t t[128]; // We expect no more than 128 tokens 

				jsmn_init(&p);
				r = jsmn_parse(&p, d, (recvdf->df_Size - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE)), t, sizeof(t)/sizeof(t[0]));

				DEBUG1("commR %d\n", r );
				// Assume the top-level element is an object 
				if (r > 1 && t[0].type == JSMN_OBJECT) 
				{
					DEBUG1("Found json object : %s\n", d );
					unsigned int i = 0, i1 = 0;
					
					for( i = 0; i < r;  i++ )
					{
						int len = t[ i ].end-t[ i ].start;
						i1 = i + 1;
						
						//FERROR("Cannot mount device, error returned %.*s\n", len, d + t[i].start );
						
						if ( jsoneq( d, &t[i], "response" ) == 0) 
						{
							int len = t[ i1 ].end-t[ i1 ].start;
						
							if( strncmp( "0", d + t[i1].start, 1 ) != 0 )
							{
								//FERROR("Cannot mount device, error returned %.*s\n", len, d + t[i+1].start );
							
								if( sd != NULL )
								{
									if( sd->login ) FFree( sd->login );
									if( sd->passwd ) FFree( sd->passwd );
									if( sd->privkey ){ FFree( sd->privkey );}
									FFree( sd );
								}
								
								if( dev->f_Name ) FFree( dev->f_Name );
								if( dev->f_Path  ) FFree( dev->f_Path );
								FFree( dev );
								
								DataFormDelete( df );
								
								if( recvdf != NULL )
								{
									DataFormDelete( recvdf );
								}
								
								return NULL;
							}
						}
						//sessionid
						//if (jsoneq( d, &t[i], "authid") == 0) 
						if (jsoneq( d, &t[i], "sessionid") == 0) 
						{
							int len = t[ i1 ].end-t[ i1 ].start;
							char authidc[ 512 ];
							memset( authidc, 0, 512 );
							
							//int locs = sprintf( authidc, "sessionid=%.*s", len, d + t[ i1 ].start );
							//int locs = sprintf( authidc, "authid=%.*s", len, d + t[ i1 ].start );
							int locs = sprintf( authidc, "sessionid=%s", "remote" );
							sd->id = StringDuplicate( authidc );
							sd->idi = locs;
							if( len < 1 )
							{
								error = 2;
							}
							else
							{
								error = 0;
							}
							
							// mount function finished work with success, Im adding user to global remote users list
							//sd->sb->sl_UserManagerInterface.UMAddRemoteUser( sd->sb->sl_UM, sd->login, authidc, sd->host );
							sd->remoteUserName = StringDuplicate( loginuser );
							sd->localDevName = StringDuplicate( name );
							sd->remoteDevName = StringDuplicate( path );
							
							sd->sb->sl_UserManagerInterface.UMAddGlobalRemoteDrive( sd->sb->sl_UM, mountingUserName, sd->remoteUserName, authidc, sd->host, sd->localDevName, sd->remoteDevName, id );
						}
					}
				}
				else
				{
					error = 5;
				}
			}
			
			if( recvdf != NULL )
			{
				DataFormDelete( recvdf );
			}
			
			if( error > 0 )
			{
				FERROR("Message not received or another error appear: %d\n", error );
				
				if( dev->f_SpecialData )
				{
					SpecialData *sdat = (SpecialData *) dev->f_SpecialData;
					if( sdat != NULL )
					{
						if( sdat->host ){ FFree( sdat->host ); }
						if( sdat->login ){ FFree( sdat->login ); }
						if( sdat->passwd ){ FFree( sdat->passwd ); }
						if( sdat->id ){ FFree( sdat->id ); }
						if( sdat->address ){ FFree( sdat->address ); }
						if( sdat->privkey ){ FFree( sdat->privkey );}
						if( sdat->devid ){ FFree( sdat->devid ); }
						if( sdat->tmppath ){ FFree( sdat->tmppath ); }
						if( sdat->remotepath ){ FFree( sdat->remotepath ); }
						if( sdat->remoteUserName ){ FFree( sdat->remoteUserName ); }
						if( sdat->localDevName ){ FFree( sdat->localDevName ); }
						if( sdat->remoteDevName ){ FFree( sdat->remoteDevName ); }
						
						FFree( dev->f_SpecialData );
					}
				}
			
				if( dev->f_Name ){ FFree( dev->f_Name ); }
				if( dev->f_Path ){ FFree( dev->f_Path ); }
				
				FFree( dev );
				dev = NULL;
				FERROR("Cannot mount device, error %d\n", error );
				return NULL;
			}
			else
			{

			}
		}	// sd != NULL
		else
		{
			FERROR("Cannot allocate memory for special data\n");
			FFree( dev );
			return NULL;
		}
	}
	else
	{
		FERROR("Cannot allocate memory for device\n");
		return NULL;
	}
	
	DEBUG("REMOTEFS mount ok\n");
	
	return dev;
}

//
// Only free device
//

int Release( struct FHandler *s, void *f )
{
	if( f != NULL )
	{
		DEBUG("REMOTEFS Release filesystem\n");
		File *lf = (File*)f;
		
		if( lf->f_SpecialData )
		{
			SpecialData *sdat = (SpecialData *) lf->f_SpecialData;
			if( sdat != NULL )
			{
				//( UserManager *um, const char *uname, const char *hostname, char *localDevName, char *remoteDevName );
				sdat->sb->sl_UserManagerInterface.UMRemoveGlobalRemoteDrive( sdat->sb->sl_UM, sdat->remoteUserName, sdat->host, sdat->localDevName, sdat->remoteDevName );
				//sdat->sb->sl_UserManagerInterface.UMRemoveRemoteUser( sdat->sb->sl_UM, sdat->login, sdat->host );
				
				if( sdat->con != NULL )
				{
					sdat->sb->sl_CommServiceInterface.CommFCConnectionDelete( sdat->con );
				}
				
				if( sdat->host ){ FFree( sdat->host ); }
				if( sdat->login ){ FFree( sdat->login ); }
				if( sdat->passwd ){ FFree( sdat->passwd ); }
				if( sdat->id ){ FFree( sdat->id ); }
				if( sdat->address ){ FFree( sdat->address ); }
				if( sdat->privkey ){ FFree( sdat->privkey );}
				if( sdat->devid ){ FFree( sdat->devid ); }
				if( sdat->tmppath ){ FFree( sdat->tmppath ); }
				if( sdat->remotepath ){ FFree( sdat->remotepath ); }
				if( sdat->remoteUserName ){ FFree( sdat->remoteUserName ); }
				if( sdat->localDevName ){ FFree( sdat->localDevName ); }
				if( sdat->remoteDevName ){ FFree( sdat->remoteDevName ); }
				
				FFree( lf->f_SpecialData );
			}
		}
		
		if( lf->f_Name ){ FFree( lf->f_Name ); }
		if( lf->f_Path ){ FFree( lf->f_Path ); }

		return 0;
	}
	return -1;
}

//
// Unmount device
//

int UnMount( struct FHandler *s, void *f )
{
	if( f != NULL )
	{
		DEBUG("REMOTEFS Release filesystem\n");
		File *lf = (File*)f;
		
		if( lf->f_SpecialData )
		{
			SpecialData *sdat = (SpecialData *) lf->f_SpecialData;
			if( sdat != NULL )
			{
				sdat->sb->sl_UserManagerInterface.UMRemoveGlobalRemoteDrive( sdat->sb->sl_UM, sdat->remoteUserName, sdat->host, sdat->localDevName, sdat->remoteDevName );
				//sdat->sb->sl_UserManagerInterface.UMRemoveRemoteUser( sdat->sb->sl_UM, sdat->login, sdat->host );
				
				MsgItem tags[] = {
					{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
					{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
					{ ID_QUER, (FULONG)sdat->hosti, (FULONG)sdat->host  },
					{ ID_SLIB, (FULONG)0, (FULONG)NULL },
					{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)23, (FULONG)"system.library/unmount" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
					{ ID_PRMT, (FULONG) sdat->idi,  (FULONG)sdat->id },
					{ ID_PRMT, (FULONG) sdat->devidi, (FULONG)sdat->devid },
					{ ID_PRMT, (FULONG) 18, (FULONG)"appname=Mountlist" },
					{ MSG_GROUP_END, 0,  0 },
					{ MSG_GROUP_END, 0,  0 },
					{ MSG_END, MSG_END, MSG_END }
				};
				
				DataForm *df = DataFormNew( tags );
				
				DEBUG("Message will be send\n");
				
				DataForm *recvdf = NULL;
				
				recvdf = SendMessageRFS( sdat, df );
				DataFormDelete( df );

				DEBUG("Response received\n");
				int error = 1;
				
				if( recvdf != NULL && recvdf->df_Size > 0 && recvdf->df_ID == ID_FCRE )
				{

				}
				
				if( recvdf != NULL )
				{
					DataFormDelete( recvdf );
				}
				
				if( sdat->con != NULL )
				{
					sdat->sb->sl_CommServiceInterface.CommFCConnectionDelete( sdat->con );
				}
				
				if( sdat->host ){ FFree( sdat->host ); }
				if( sdat->login ){ FFree( sdat->login ); }
				if( sdat->passwd ){ FFree( sdat->passwd ); }
				if( sdat->id ){ FFree( sdat->id ); }
				if( sdat->address ){ FFree( sdat->address ); }
				if( sdat->privkey ){ FFree( sdat->privkey );}
				if( sdat->devid ){ FFree( sdat->devid ); }
				if( sdat->tmppath ){ FFree( sdat->tmppath ); }
				if( sdat->remotepath ){ FFree( sdat->remotepath ); }
				if( sdat->remoteUserName ){ FFree( sdat->remoteUserName ); }
				if( sdat->localDevName ){ FFree( sdat->localDevName ); }
				if( sdat->remoteDevName ){ FFree( sdat->remoteDevName ); }
				
				FFree( lf->f_SpecialData );
			}
		}
		
		if( lf->f_Name ){ FFree( lf->f_Name ); }
		if( lf->f_Path ){ FFree( lf->f_Path ); }

		return 0;
	}
	return -1;
}

//
// connect macro
//

DataForm *SendMessageWithReconnection( SpecialData *sd, DataForm *df )
{
	DataForm *recvdf = NULL;
	
	recvdf = sd->sb->sl_CommServiceInterface.CommServiceSendMsgDirect(  sd->con, df );
	
	if( recvdf == NULL )
	{
		DEBUG("RECVDNULL\n");
	}
	if( recvdf->df_Size == 0 )
	{
		DEBUG("SIZE 0\n");
	}
	if( recvdf->df_ID != ID_FCRE )
	{
		char *e = (char *)recvdf;
		DEBUG("NOT FCRE %c %c %c %c %c\n", e[0],e[1],e[2],e[3],e[4]);
	}
	
	if( recvdf == NULL || recvdf->df_Size == 0 || recvdf->df_ID != ID_FCRE )
	{
		DEBUG("Create new socket\n");
		Socket *newsock = sd->sb->sl_SocketInterface.SocketConnectHost( sd->sb, sd->secured, sd->address, sd->port );
		if( newsock != NULL )
		{
			sd->con->cfcc_Socket = newsock;
			
			{
				int err = 0;
				DEBUG("Outgoing connection created\n");
				{
					DEBUG("Generate Data Form\n");
					DataForm * df = DataFormNew( NULL );
					DEBUG("DataForm Created\n");
					SystemBase *sb = (SystemBase *)sd->sb;
					FriendCoreManager *fcm = sb->fcm;
					DataFormAdd( &df, (FBYTE *)fcm->fcm_ID, FRIEND_CORE_MANAGER_ID_SIZE );

					int sbytes = sd->sb->sl_SocketInterface.SocketWrite( newsock, (char *)df, (FQUAD)df->df_Size );
		
					DEBUG("Message sent %d\n", sbytes );
					DataFormDelete( df );
				}
			}
			
			recvdf = sd->sb->sl_CommServiceInterface.CommServiceSendMsgDirect(  sd->con, df );
		}
		else
		{
			FERROR("Cannot setup new connection\n");
		}
	}
	
	return recvdf;
}

//
// Open file
//

// compilation warning
int lstat(const char *path, struct stat *buf);

void *FileOpen( struct File *s, const char *path, char *mode )
{
	SpecialData *sd = (SpecialData *)s->f_SpecialData;
	char smode[ 8 ];
	char *tmpremotepath = NULL;
	
	strcpy( smode, "mode=" );
	
	if( mode[0] == 'r' )
	{
		strcat( smode, "rb" );			// we cannot have streaming currently
													// becaouse we dont know how big file it will be on the end
													// we must be sure also that file exist on server
	}
	else
	{
		strcat( smode, mode );
	}
	
	if( sd != NULL )
	{
		int hostsize = strlen( sd->host ) + 1;
		
		if( ( tmpremotepath = FCalloc( strlen( path ) + 512, sizeof( char ) ) ) != NULL )
		{
			strcpy( tmpremotepath, "path=" );
		
			if( s->f_Path != NULL )
			{
				strcat( tmpremotepath, s->f_Path );
			}

			if( path != NULL )
			{
				strcat( tmpremotepath, path ); //&(path[ doub+1 ]) );
			}
			
			sd->remotepathi = strlen( tmpremotepath ) + 1;
	
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
					{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
					{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
					{ ID_SLIB, (FULONG)0, (FULONG)NULL },
					{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
						{ ID_PATH, (FULONG)26, (FULONG)"system.library/ufile/open" },
						{ ID_PARM, (FULONG)0, MSG_GROUP_START },
							{ ID_PRMT, (FULONG) sd->remotepathi, (FULONG)tmpremotepath },
							{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
							{ ID_PRMT, (FULONG) sd->passwdi, (FULONG)sd->passwd },
							{ ID_PRMT, (FULONG) sd->idi, (FULONG)sd->id },
							{ ID_PRMT, (FULONG) 8,  (FULONG)smode },
						{ MSG_GROUP_END, 0,  0 },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
			
			DataForm *df = DataFormNew( tags );

			DataForm *recvdf = NULL;
			
			recvdf = SendMessageRFS( sd, df );
//			recvdf = SendMessageWithReconnection( sd, df );
		
			DEBUG("Response received %p\n", recvdf );
		
			if( recvdf != NULL && recvdf->df_Size > 0 && recvdf->df_ID == ID_FCRE )
			{
				// Write the buffer to the file
				// 48 header
				// 64 ID
				// 1 - 0 on the end
				
				//if( ok<!--separate-->{ "Fileptr" : "0x103d04d0" }
				
				//char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
				int r;
				jsmn_parser p;
				jsmntok_t t[128]; // We expect no more than 128 tokens 
				
				// we must skip part  ok<!--separate-->
				//d += 17;
				char *d = strstr( (((char *)recvdf) + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE)), "-->" )+3;

				jsmn_init(&p);
				//r = jsmn_parse(&p, d, (recvdf->df_Size - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE)), t, sizeof(t)/sizeof(t[0]));
				r = jsmn_parse(&p, d, (recvdf->df_Size - ((ANSWER_POSITION*COMM_MSG_HEADER_SIZE)+17)), t, sizeof(t)/sizeof(t[0]));

				DEBUG1("RemoteFS parse result %d parse string %s\n", r, d );
				// Assume the top-level element is an object 
				if (r > 1 && t[0].type == JSMN_OBJECT) 
				{
					DEBUG1("Found json object\n");
					int i = 0, i1 = 0;
				
					for( i = 0; i < r ; i++ )
					{
						i1 = i + 1;
						if (jsoneq( d, &t[i], "fileptr") == 0) 
						{
							int len = t[ i1 ].end-t[ i1 ].start;
							char pointer[ 255 ];
							memset( pointer, 0, sizeof(pointer ) );
							
							int pointeri = sprintf( pointer, "fptr=%.*s", len, d + t[i1].start );
							DEBUG("POINTER %s\n", pointer );
							
							// Ready the file structure
							File *locfil = NULL;
							if( ( locfil = FCalloc( 1, sizeof( File ) ) ) != NULL )
							{
								locfil->f_Path = StringDuplicate( path );
								locfil->f_RootDevice = s;
								locfil->f_Socket = s->f_Socket;
						
								if( ( locfil->f_SpecialData = FCalloc( 1, sizeof( SpecialData ) ) ) != NULL )
								{
									SpecialData *localsd = (SpecialData *)locfil->f_SpecialData;
									
									if( mode[0] == 'r' )
									{
										localsd->mode = MODE_READ;
										locfil->f_OperationMode = MODE_READ;
									}
									else if( mode[0] == 'w' )
									{
										localsd->mode = MODE_WRITE;
										locfil->f_OperationMode = MODE_WRITE;
									}

									localsd->sb = sd->sb;
									localsd->remotepath = StringDuplicate( path );
									locfil->f_SessionID = StringDuplicate( s->f_SessionID );
									strcpy( localsd->fileptr, pointer );
									localsd->fileptri = pointeri+1;
					
									DEBUG("[fsysremote] FileOpened, memory allocated for reading.\n" );
									
									DataFormDelete( recvdf );
									DataFormDelete( df );
									FFree( tmpremotepath );
								
									return locfil;
								}
				
								// Free this one
								FFree( locfil->f_Path );
								FFree( locfil );
							}
						}
					}
				}
			}
			
			if( recvdf != NULL ) DataFormDelete( recvdf );
			DataFormDelete( df );
			FFree( tmpremotepath );
		}
		
	} // sd != NULL

	return NULL;
}

//
// Close File
//

int FileClose( struct File *root, void *fp )
{	
	int result = -2;
	File *f = (File *)fp;
	
	SpecialData *sd = (SpecialData *)f->f_SpecialData;
	
	if( sd != NULL )
	{
		SpecialData *rsd = (SpecialData *)root->f_SpecialData;
		int hostsize = strlen( rsd->host )+1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)rsd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)27, (FULONG)"system.library/ufile/close" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) sd->fileptri, (FULONG)sd->fileptr },
						{ ID_PRMT, (FULONG) rsd->logini, (FULONG)rsd->login },
						{ ID_PRMT, (FULONG) rsd->passwdi,  (FULONG)rsd->passwd },
						{ ID_PRMT, (FULONG) rsd->idi,  (FULONG)rsd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
			
		DataForm *df = DataFormNew( tags );

		DataForm *recvdf = NULL; 
			
		recvdf = SendMessageRFS( rsd, df );
		//recvdf = SendMessageWithReconnection( rsd, df );
		
		DEBUG("Response received %p\n", recvdf );
		
		if( recvdf != NULL && recvdf->df_Size > 0 )
		{
			char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			
			DEBUG("RECEIVED  %10s\n", d );
			result =  recvdf->df_Size  - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			
			//int i = 0;
			/*
			for( i  =  0; i < (recvdf->df_Size  - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE)) ; i++  )
			{
				printf	("%c\n", d[ i  ] );
			}*/
			
			if( strcmp( d, "{\"rb\":\"-1\"}" ) == 0 )
			{
				DataFormDelete( recvdf );
				DataFormDelete( df );
				return -1;
			}
		}
		
		if( recvdf != NULL ) DataFormDelete( recvdf );
		DataFormDelete( df );
		
		if( sd->host != NULL ) FFree( sd->host );
		if( sd->id != NULL ) FFree( sd->id );
		if( sd->login != NULL ) FFree( sd->login );
		if( sd->passwd != NULL ) FFree( sd->passwd );
		if( sd->tmppath != NULL ) FFree( sd->tmppath );
		if( sd->remotepath != NULL ) FFree( sd->remotepath );
			
		FFree( sd );
		
		if( f != NULL )
		{
			if( f->f_SessionID != NULL ) FFree( f->f_SessionID );
			if( f->f_Path != NULL )
			{
				FFree( f->f_Path );
			}
			FFree( f );
		}
	} // sd != NULL
	
	return result;
}

//
// Read data from file
//

int FileRead( struct File *f, char *buffer, int rsize )
{
	int result = -2;
	
	SpecialData *sd = (SpecialData *)f->f_SpecialData;
	
	if( sd != NULL )
	{
		char sizec[ 256 ];
		int sizei = snprintf( sizec, 256, "size=%d", rsize )+1;
		
		File *root = f->f_RootDevice;
		SpecialData *rsd = (SpecialData *)root->f_SpecialData;
		int hostsize = strlen( rsd->host )+1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)rsd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)26, (FULONG)"system.library/ufile/read" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) sd->fileptri, (FULONG)sd->fileptr },
						{ ID_PRMT, (FULONG) sizei, (FULONG) sizec },
						{ ID_PRMT, (FULONG) rsd->logini, (FULONG)rsd->login },
						{ ID_PRMT, (FULONG) rsd->passwdi,  (FULONG)rsd->passwd },
						{ ID_PRMT, (FULONG) rsd->idi,  (FULONG)rsd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
			
		DataForm *df = DataFormNew( tags );

		DataForm *recvdf = NULL;
		
		recvdf = SendMessageRFS( rsd, df );
//		recvdf = SendMessageWithReconnection( rsd, df );
		
		DEBUG2("Response received %p\n", recvdf );
		
		if( recvdf != NULL && recvdf->df_ID == ID_FCRE &&  recvdf->df_Size > 0 )
		{
			// int i = 0;

			char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			
			result =  recvdf->df_Size  - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			//DEBUG2("RECEIVED  %.10s  size %llu only message size %d\n", d,  recvdf->df_Size, result );
			
			// 1531 - 1483
			//int i = 0;
			/*
			for( i  =  0; i < result ; i++  )
			{
				printf("%c\n", d[ i  ] );
			}*/
			
			if( strncmp( d, "{\"rb\":\"-1\"}", 11 ) == 0 )
			{
				DataFormDelete( recvdf );
				DataFormDelete( df );
				return -1;
			}
			
			result = result-1;
			
			if( f->f_Stream == TRUE )
			{
				sd->sb->sl_SocketInterface.SocketWrite( f->f_Socket, d, (FQUAD)result );
			}
			else
			{
				DEBUG2("Want to copy %d last char %d\n", result, buffer[ result-1 ] );
				memcpy( buffer, d, result );
				
				DEBUG2("Done %d MAX %d\n", result, rsize );
			}
		}
		
		if( recvdf != NULL ) DataFormDelete( recvdf );
		DataFormDelete( df );
	} // sd != NULL
	
	return result;
}

//
// write data to file
//

int FileWrite( struct File *f, char *buffer, int wsize )
{
	int result = -2;
	
	SpecialData *sd = (SpecialData *)f->f_SpecialData;
	
	if( sd != NULL )
	{
		char *data;
		
		if( ( data = FCalloc( wsize+10, sizeof(char) ) ) != NULL  )
		{
			strcpy( data, "data=" );
			memcpy( &data[ 5 ], buffer, wsize ); 
			
			char sizec[ 256 ];
			int sizei = snprintf( sizec, 256, "size=%d", wsize )+1;
		
			File *root = f->f_RootDevice;
			SpecialData *rsd = (SpecialData *)root->f_SpecialData;
			int hostsize = strlen( rsd->host )+1;
		
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
					{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
					{ ID_QUER, (FULONG)hostsize, (FULONG)rsd->host  },
					{ ID_SLIB, (FULONG)0, (FULONG)NULL },
					{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
						{ ID_PATH, (FULONG)27, (FULONG)"system.library/ufile/write" },
						{ ID_PARM, (FULONG)0, MSG_GROUP_START },
							{ ID_PRMT, (FULONG) wsize+6, (FULONG)data },
							{ ID_PRMT, (FULONG) sd->fileptri, (FULONG)sd->fileptr },
							{ ID_PRMT, (FULONG) sizei, (FULONG) sizec },
							{ ID_PRMT, (FULONG) rsd->logini, (FULONG)rsd->login },
							{ ID_PRMT, (FULONG) rsd->passwdi,  (FULONG)rsd->passwd },
							{ ID_PRMT, (FULONG) rsd->idi,  (FULONG)rsd->id },
						{ MSG_GROUP_END, 0,  0 },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
			
			DEBUG("---->write bytes %d and message %.10s\n", wsize, data );
			
			DataForm *df = DataFormNew( tags );

			DataForm *recvdf = NULL;
			
			recvdf = SendMessageRFS( rsd, df );
			//recvdf = SendMessageWithReconnection( rsd, df );
		
			DEBUG("Response received %p\n", recvdf );
		
			if( recvdf != NULL && recvdf->df_Size > 0 )
			{
				//int i=0;

				char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			
				DEBUG("RECEIVED  %.10s\n", d );
				result =  recvdf->df_Size  - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
				// 1531 - 1483
				//int i = 0;
				/*
				for( i  =  0; i < result ; i++  )
				{
					printf("%c\n", d[ i  ] );
				}
				*/
			
				if( strcmp( d, "{\"rb\":\"-1\"}" ) == 0 )
				{
					DataFormDelete( recvdf );
					DataFormDelete( df );
					return -1;
				}
				else
				{
					unsigned int i=0;
					char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
					int r;
					jsmn_parser p;
					jsmntok_t t[128]; // We expect no more than 128 tokens 

					jsmn_init(&p);
					r = jsmn_parse(&p, d, result, t, sizeof(t)/sizeof(t[0]));

					DEBUG1("remotefs write commR %d\n", r );
					// Assume the top-level element is an object 
					if (r > 1 && t[0].type == JSMN_OBJECT) 
					{
						DEBUG1("Found json object\n");
						int i = 0, i1 = 0;
				
						for( i = 0; i < r ;  i++ )
						{
							i1 = i + 1;
							if (jsoneq( d, &t[i], "filestored") == 0) 
							{
								int len = t[ i1 ].end-t[ i1 ].start;
								char sizec[ 256 ];
								
								strncpy( sizec, d + t[ i1 ].start, len );
								
								result = atoi( sizec );
							}
						}
					}
				}
			
				DataFormDelete( recvdf );
			}
			
			DataFormDelete( df );
			FFree( data );
		}
		else
		{
			FERROR("[RemoteFS] write Cannot allocate memory for buffer\n");
			return -2;
		}
		
	} // sd != NULL
	
	return result;
}

//
// seek
//

int FileSeek( struct File *s, int pos )
{
	SpecialData *sd = (SpecialData *)s->f_SpecialData;
	if( sd )
	{
		//return fseek( (FILE *)sd->fp, pos, SEEK_SET );
	}
	return -1;
}

//
// make directory in local file system
//

int MakeDir( struct File *s, const char *path )
{
	INFO("MakeDir!\n");

	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath + 512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}

		if( path != NULL )
		{
			strcat( comm, path ); //&(path[ doub+1 ]) );
		}

		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)28, (FULONG)"system.library/file/makedir" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) commi, (FULONG)comm },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
						{ ID_PRMT, (FULONG) sd->passwdi, (FULONG) sd->passwd },
						{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
		
		DataForm *df = DataFormNew( tags );
		
		DEBUG("Message will be send\n");

		DataForm *recvdf = NULL;

		recvdf = SendMessageRFS( sd, df );
		//recvdf = SendMessageWithReconnection( sd, df );
		
		DEBUG("Response received\n");
		
		if( recvdf != NULL)
		{
			//DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
			unsigned int i=0;
			char *d = (char *)recvdf;

		}
		
		DataFormDelete( df );
		DataFormDelete( recvdf );
		
		FFree( comm );
	}
	DEBUG("Delete END\n");
	
	return -1;
}

//
// Delete
//

int Delete( struct File *s, const char *path )
{
	DEBUG("Delete!\n");

	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath +512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}

		if( path != NULL )
		{
			strcat( comm, path ); //&(path[ doub+1 ]) );
		}

		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)27, (FULONG)"system.library/file/delete" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) commi, (FULONG)comm },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
						{ ID_PRMT, (FULONG) sd->passwdi, (FULONG) sd->passwd },
						{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
		
		DataForm *df = DataFormNew( tags );
		
		DEBUG("Message will be send\n");

		DataForm *recvdf = NULL;
			
		recvdf = SendMessageRFS( sd, df );
		//recvdf = SendMessageWithReconnection( sd, df );
		
		DEBUG("Response received\n");
		
		if( recvdf != NULL)
		{
			//DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
			unsigned int i=0;
			char *d = (char *)recvdf;

		}
		
		DataFormDelete( df );
		DataFormDelete( recvdf );
		
		FFree( comm );
	}
	DEBUG("Delete END\n");
	
	/*
	//BufString *bs = BufStringNew();
	int spath = strlen( path );
	int rspath = strlen( s->f_Path );
	//int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	DEBUG("Delete new path size %d\n", rspath + spath );
	
	if( ( comm = calloc( rspath + spath + 10, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, s->f_Path );
		
		if( comm[ strlen( comm ) -1] != '/' )
		{
			strcat( comm, "/" );
		}
		strcat( comm, path );
	
		DEBUG("Delete file or directory %s!\n", comm );
	
		int ret = RemoveDirectory( comm );

		free( comm );
		return ret;
	}
	*/
	DEBUG("Delete END\n");
	
	return 0;
}

//
// Rename
//

int Rename( struct File *s, const char *path, const char *nname )
{
	DEBUG("REMOTEFS Rename!\n");
	char *newname = NULL;
	int spath = strlen( path );
	int rspath = strlen( s->f_Path );
	
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	int error = 0;
	
	if( ( comm = FCalloc( rspath +512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}

		if( path != NULL )
		{
			strcat( comm, path );
		}

		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		char newname[ 512 ];
		int newnamesize = sprintf(  newname, "newname=%s",  nname ) + 1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)27, (FULONG)"system.library/file/rename" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) commi, (FULONG)comm },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
						{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)sd->passwd },
						{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
						{ ID_PRMT, (FULONG) newnamesize, (FULONG)newname },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
		
		
		DEBUG("\n\n\nBefore creating DataForm  comm %s login %s  pass %s session %s passsize %d\n", comm, sd->login, sd->passwd, sd->id, sd->passwdi );

		DataForm *df = DataFormNew( tags );
		
		DEBUG("Message will be send\n");

		DataForm *recvdf = NULL;
			
		recvdf = SendMessageRFS( sd, df );
		//recvdf = SendMessageWithReconnection( sd, df );
		
		DEBUG("Response received\n");
		
		if( recvdf != NULL)
		{
			DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
			unsigned int i=0;
			char *d = (char *)recvdf;

			//BufStringAddSize( bs, &d[ HEADER_POSITION ], recvdf->df_Size - (HEADER_POSITION) );
		}
		
		DataFormDelete( recvdf );
		DataFormDelete( df );
		
		FFree( comm );
	}
	DEBUG("REMOTEFS rename END\n");
	
	return error;
}


//
// Copy file from source to destination
//

int Copy( struct File *s, const char *dst, const char *src )
{
	return  0;
}

//
// Execute file
//

#define BUFFER_SIZE 1024

FILE *popen( const char *c, const char *r );

char *Execute( struct File *s, const char *path, const char *args )
{
	DEBUG("SYS mod run\n");
/*
	FULONG res = 0;
	char command[ BUFFER_SIZE ];
	char *temp = NULL;
	char *result = NULL;
    unsigned long size = 0;

	//
	//
	// we are calling native application and read output from it
	//
	//
	
	
	//void pclose( FILE *f );
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = calloc( strlen( s->f_Path ) + strlen(path), sizeof(char) ) ) != NULL )
	{
		strcpy( comm, s->f_Path );
		if( comm[ strlen( comm ) -1 ] != '/' )
		{
			strcat( comm, "/" );
		}
		strcat( comm, &(path[ doub+2 ]) );

		sprintf( command, "%s %s", comm, args );

		FILE* pipe = popen( command, "r");
		if( !pipe )
		{
			return 0;
		}

		char buffer[ BUFFER_SIZE ];
    
		while( !feof( pipe ) ) 
		{
			char *gptr;

			if( ( gptr = fgets( buffer, BUFFER_SIZE, pipe ) ) != NULL )
			{
				size = strlen( buffer );
				//DEBUG("inside buffer '%s' size %d\n", buffer, size );

				if( result == NULL )
				{
					if( ( result = calloc( size+1, sizeof(char) ) ) != NULL ){
						memcpy( result, buffer, size );
						result[ size ] = 0;

						res += size;
                    //DEBUG("SYS: copy %d  res %d\n", size, res );
					}
					else
					{
						printf("Cannot alloc mem result.\n");
					}
				}
				else
				{
					//DEBUG("TEMP res size %d %s\n", res, temp );
					if( ( temp = calloc( res+1, sizeof(char) ) ) != NULL )
					{
						memcpy( temp, result, res );
						//DEBUG("Data copy %s\n", temp );
						if( result != NULL ){ free( result ); result = NULL; }
						//DEBUG("before result calloc\n");
						if( ( result = calloc( res+size+1, sizeof(char) ) ) != NULL )
						{
							memcpy( result, temp, res );
							memcpy( &(result[ res ]), buffer, size );

							//DEBUG("res %d size %d result %s\n", res, size, result );
							res += size;
						}

						free( temp );
						temp = NULL;
					}
				}
				//res += (FULONG)size;
			}
		}
		pclose( pipe );
	}
	else
	{
		return NULL;
	}

	return result;
	*/
return NULL;
}

//
// Get info about file/folder and return as "string"
//

BufString *Info( File *s, const char *path )
{
	DEBUG("Info!\n");
	
	BufString *bs = BufStringNew();
	
	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	DEBUG("INFO!\n");
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath +512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}

		if( path != NULL )
		{
			strcat( comm, path ); //&(path[ doub+1 ]) );
		}

		DEBUG2("REMOTE PATH: %s\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		
		DEBUG2("Send message to  %s\n", sd->host );
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)sizeof(FULONG) , 0 },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)25, (FULONG)"system.library/file/info" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) commi, (FULONG)comm },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
						{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)sd->passwd },
						{ ID_PRMT, (FULONG)  sd->idi,  (FULONG)sd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
		
		DataForm *recvdf = NULL;
		
		DataForm *df = DataFormNew( tags );
		
		DEBUG("Message will be send\n");
			
		recvdf = SendMessageRFS( sd, df );
		//recvdf = SendMessageWithReconnection( sd, df );
		
		DEBUG("Response received\n");
		
		if( recvdf != NULL && recvdf->df_Size > 0 && recvdf->df_ID == ID_FCRE )
		{
			//DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			unsigned int i=0;
			char *d = (char *)recvdf;

			BufStringAddSize( bs, &d[ HEADER_POSITION ], recvdf->df_Size - (HEADER_POSITION) );
		}
		else
		{
			BufStringAdd( bs, "fail<!--separate-->");	
		}
		
		DataFormDelete( recvdf );
		DataFormDelete( df );
		
		FFree( comm );
	}
	DEBUG("Info END\n");
	
	return bs;
}

//
// Call a library
//
// TODO: Finish it!

BufString *Call( File *f, const char *path, char *args )
{
	DEBUG("Info!\n");
	BufString *bs = BufStringNew();
	
	SpecialData *sd = (SpecialData *)f->f_SpecialData;
	
	if( sd != NULL )
	{
		File *root = f->f_RootDevice;
		SpecialData *rsd = (SpecialData *)root->f_SpecialData;
		int hostsize = strlen( rsd->host ) + 1;
		
		char *pathp = NULL;
		char *argsp = NULL;
		
		int pathi = strlen( path ) + 1;
		int argsi = strlen( args ) + 1;
		
		pathp = FCalloc( pathi + 30, sizeof(char) );
		argsp = FCalloc( argsi + 30, sizeof(char) );
		
		DataForm *df = NULL;
		
		if( pathp != NULL && argsp != NULL )
		{
			strcpy( pathp, "path=" );
			strcpy( argsp, "args=" );
			strcat( pathp, path );
			strcat( argsp, args );
			
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
					{ ID_QUER, (FULONG)hostsize, (FULONG)rsd->host  },
					{ ID_SLIB, (FULONG)0, (FULONG)NULL },
					{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
						{ ID_PATH, (FULONG)26, (FULONG)"system.library/ufile/call" },
						{ ID_PARM, (FULONG)0, MSG_GROUP_START },
							{ ID_PRMT, (FULONG) pathi, (FULONG)pathp },
							{ ID_PRMT, (FULONG) argsi, (FULONG)argsp },
							{ ID_PRMT, (FULONG) rsd->logini, (FULONG)rsd->login },
							{ ID_PRMT, (FULONG) rsd->passwdi,  (FULONG)rsd->passwd },
							{ ID_PRMT, (FULONG) rsd->idi,  (FULONG)rsd->id },
						{ MSG_GROUP_END, 0,  0 },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
			df = DataFormNew( tags );
		}
		else
		{
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
					{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
					{ ID_QUER, (FULONG)hostsize, (FULONG)rsd->host  },
					{ ID_SLIB, (FULONG)0, (FULONG)NULL },
					{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
						{ ID_PATH, (FULONG)26, (FULONG)"system.library/ufile/call" },
						{ ID_PARM, (FULONG)0, MSG_GROUP_START },
							{ ID_PRMT, (FULONG) rsd->logini, (FULONG)rsd->login },
							{ ID_PRMT, (FULONG) rsd->passwdi,  (FULONG)rsd->passwd },
							{ ID_PRMT, (FULONG) rsd->idi,  (FULONG)rsd->id },
						{ MSG_GROUP_END, 0,  0 },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
			df = DataFormNew( tags );
		}

		DataForm *recvdf = NULL;

		recvdf = SendMessageRFS( sd, df );

		DEBUG("Response received %p\n", recvdf );
		
		if( recvdf != NULL && recvdf->df_Size > 0 )
		{
			char *d = (char *)recvdf + (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);
			
			DEBUG("RECEIVED  %10s\n", d );
			int result =  recvdf->df_Size  - (ANSWER_POSITION*COMM_MSG_HEADER_SIZE);

			BufStringAddSize( bs, d, result );
			
			DataFormDelete( recvdf );
		}
		else
		{
			BufStringAdd( bs, "fail<!--separate-->{\"result\":\"reponse message is not FC message\"}" );
		}
			
		DataFormDelete( df );
		
		if( pathp != NULL ) FFree( pathp );
		if( argsp != NULL ) FFree( argsp );
	} // sd != NULL
	return bs;
}

//
// return content of directory
//
	
BufString *Dir( File *s, const char *path )
{
	BufString *bs = BufStringNew();
	
	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	DEBUG("Dir2 rspath %d!\n", rspath );
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath +512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}

		if( path != NULL )
		{
			strcat( comm, path ); 
		}

		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		
		MsgItem tags[] = {
			{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
					{ ID_PATH, (FULONG)24, (FULONG)"system.library/file/dir" },
					{ ID_PARM, (FULONG)0, MSG_GROUP_START },
						{ ID_PRMT, (FULONG) commi, (FULONG)comm },
						{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
						{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)sd->passwd },
						{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
					{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
			{ MSG_GROUP_END, 0,  0 },
			{ MSG_END, MSG_END, MSG_END }
		};
		
		DEBUG("\n\n\nBefore creating DataForm  comm %s login %s  pass %s session %s passsize %d\n", comm, sd->login, sd->passwd, sd->id, sd->passwdi );

		DataForm *df = DataFormNew( tags );
		
		DEBUG("Message will be send\n");

		DataForm *recvdf = NULL;
			
		recvdf = SendMessageRFS( sd, df );

		DEBUG("Response received\n");
		
		if( recvdf != NULL && recvdf->df_ID == ID_FCRE )
		{
			char *d = (char *)recvdf;
			
			DEBUG("Bytes received %ld\n", recvdf->df_Size );

			if( recvdf->df_Size > 0 )
			{
				BufStringAddSize( bs, &d[ HEADER_POSITION ], recvdf->df_Size - (HEADER_POSITION) );
			}
			else
			{
				BufStringAdd( bs, "fail<!--separate-->{\"result\":\"received buffer is empty\"}" );
			}
		}
		else
		{
			BufStringAdd( bs, "fail<!--separate-->{\"result\":\"reponse message is not FC message\"}" );
		}
		
		DataFormDelete( recvdf );
		DataFormDelete( df );
		
		FFree( comm );
	}

	return bs;
}

//
// Get metadata
//

char *InfoGet( struct File *s, const char *path, const char *key )
{
	BufString *bs = BufStringNew();
	
	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	DEBUG("InfoGet rspath %d!\n", rspath );
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath +512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}
		
		if( path != NULL )
		{
			strcat( comm, path ); //&(path[ doub+1 ]) );
		}
		
		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		int keyparami = strlen( key );
		char *keyparam = FCalloc( keyparami+5 , sizeof(char) );
		if( keyparam != NULL )
		{
			keyparami = sprintf(  keyparam, "key=%s",  key ) + 1;
		
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
				{ ID_PATH, (FULONG)24, (FULONG)"system.library/file/infoget" },
				{ ID_PARM, (FULONG)0, MSG_GROUP_START },
				{ ID_PRMT, (FULONG) commi, (FULONG)comm },
				{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
				{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)sd->passwd },
				{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
				{ ID_PRMT, (FULONG) keyparami,  (FULONG)keyparam },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};
			
			DataForm *df = DataFormNew( tags );
		
			DEBUG("Message will be send\n");
		
			DataForm *recvdf = NULL;
		
			recvdf = SendMessageRFS( sd, df );

			DEBUG("Response received\n");
		
			if( recvdf != NULL && recvdf->df_ID == ID_FCRE )
			{
				//DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
				//unsigned int i=0;
				char *d = (char *)recvdf;
			
				DEBUG("Bytes received %ld\n", recvdf->df_Size );
			
				if( recvdf->df_Size > 0 )
				{
					BufStringAddSize( bs, &d[ HEADER_POSITION ], recvdf->df_Size - (HEADER_POSITION) );
				}
			}
			
			DataFormDelete( recvdf );
			DataFormDelete( df );
			FFree( keyparam );
		}
		
		FFree( comm );
	}
	DEBUG("Dir END\n");
	
	char *retstring = bs->bs_Buffer;
	bs->bs_Buffer = NULL;
	BufStringDelete( bs );
	
	return retstring;
}

//
// set metadata
//

int InfoSet( File *s, const char *path, const char *key, const char *value )
{
	BufString *bs = BufStringNew();
	
	int rspath = 0;
	if( s->f_Path != NULL )
	{
		rspath = strlen( s->f_Path );
	}
	
	DEBUG("InfoGet rspath %d!\n", rspath );
	
	int doub = strlen( s->f_Name );
	
	char *comm = NULL;
	
	if( ( comm = FCalloc( rspath + 512, sizeof(char) ) ) != NULL )
	{
		strcpy( comm, "path=" );
		if( s->f_Path != NULL )
		{
			strcat( comm, s->f_Path );
		}
		
		if( path != NULL )
		{
			strcat( comm, path ); //&(path[ doub+1 ]) );
		}
		
		DEBUG2("REMOTE PATH\n\n %s\n\n", comm );
		
		SpecialData *sd = (SpecialData *) s->f_SpecialData;
		DEBUG("sb %p fcm \n", sd->sb );
		FriendCoreManager *fcm = sd->sb->fcm;
		sd->csr = fcm->fcm_CommServiceRemote;
		
		int hostsize = strlen( sd->host )+1;
		int commi = strlen( comm )+1;
		
		int keyparami = strlen( key );
		int valparami = strlen( value );
		char *keyparam = FCalloc( keyparami+5 , sizeof(char) );
		char *valparam = FCalloc( valparami+7 , sizeof(char) );
		
		if( keyparam != NULL && valparam != NULL )
		{
			keyparami = sprintf(  keyparam, "key=%s",  key ) + 1;
			valparami = sprintf(  valparam, "value=%s",  key ) + 1;
		
			MsgItem tags[] = {
				{ ID_FCRE, (FULONG)0, MSG_GROUP_START },
				{ ID_FRID, (FULONG)0 , MSG_INTEGER_VALUE },
				{ ID_QUER, (FULONG)hostsize, (FULONG)sd->host  },
				{ ID_SLIB, (FULONG)0, (FULONG)NULL },
				{ ID_HTTP, (FULONG)0, MSG_GROUP_START },
				{ ID_PATH, (FULONG)24, (FULONG)"system.library/file/infoset" },
				{ ID_PARM, (FULONG)0, MSG_GROUP_START },
				{ ID_PRMT, (FULONG) commi, (FULONG)comm },
				{ ID_PRMT, (FULONG) sd->logini, (FULONG)sd->login },
				{ ID_PRMT, (FULONG) sd->passwdi,  (FULONG)sd->passwd },
				{ ID_PRMT, (FULONG) sd->idi,  (FULONG)sd->id },
				{ ID_PRMT, (FULONG) keyparami,  (FULONG)keyparam },
				{ ID_PRMT, (FULONG) valparami,  (FULONG)valparam },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_GROUP_END, 0,  0 },
				{ MSG_END, MSG_END, MSG_END }
			};

			DataForm *df = DataFormNew( tags );
		
			DEBUG("Message will be send\n");
		
			DataForm *recvdf = NULL;
		
			recvdf = SendMessageRFS( sd, df );

			DEBUG("Response received\n");
		
			if( recvdf != NULL && recvdf->df_ID == ID_FCRE )
			{
				//DEBUG2("\n\n\n\nDATAFORM Received %ld\n", recvdf->df_Size );
			
				unsigned int i=0;
				char *d = (char *)recvdf;
			
				DEBUG("Bytes received %ld\n", recvdf->df_Size );
			
				if( recvdf->df_Size > 0 )
				{
					BufStringAddSize( bs, &d[ HEADER_POSITION ], recvdf->df_Size - (HEADER_POSITION) );
				}
			}
		
			DataFormDelete( recvdf );
			DataFormDelete( df );
		}
		
		if( keyparam != NULL )
		{
			FFree( keyparam );
		}
		
		if( valparam != NULL )
		{
			FFree( valparam );
		}
		
		FFree( comm );
	}
	DEBUG("Dir END\n");
	
	BufStringDelete( bs );
	
	return 0;
}

