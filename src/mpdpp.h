/***************************************************************************
 *   Copyright (C) 2008-2009 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#ifndef _MPDPP_H
#define _MPDPP_H

#include <string>
#include "libmpdclient.h"

namespace MPD
{
	enum State { psUnknown, psStop, psPlay, psPause };
	
	struct StatusChanges
	{
		bool Playlist;
		bool SongID;
		bool ElapsedTime;
		bool State;
	};
	
	class Connection
	{
		typedef void (*StatusUpdater) (Connection *, StatusChanges, void *);
		typedef void (*ErrorHandler) (Connection *, int, std::string, void *);
		
		public:
			Connection();
			~Connection();
			
			bool Connect();
			bool Connected() const;
			void Disconnect();
			
			const std::string & GetHostname() { return itsHost; }
			int GetPort() { return itsPort; }
			
			void SetHostname(const std::string &);
			void SetPort(int port) { itsPort = port; }
			void SetTimeout(int timeout) { itsTimeout = timeout; }
			void SetPassword(const std::string &password) { itsPassword = password; }
			void SendPassword() const;
			
			void SetStatusUpdater(StatusUpdater, void *);
			void SetErrorHandler(ErrorHandler, void *);
			void UpdateStatus();
			
			State GetState() const { return isConnected && itsCurrentStatus ? (State)itsCurrentStatus->state : psUnknown; }
			int GetElapsedTime() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->elapsedTime : -1; }
			int GetCrossfade() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->crossfade : -1; }
			int GetPlaylistLength() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->playlistLength : -1; }
			
			const std::string & GetErrorMessage() const { return itsErrorMessage; }
			int GetErrorCode() const { return itsErrorCode; }
			
			mpd_Song * CurrentSong() const;
			
		private:
			int CheckForErrors();
			
			StatusChanges itsChanges;
			
			mpd_Connection *itsConnection;
			bool isConnected;
			
			std::string itsErrorMessage;
			int itsErrorCode;
			
			std::string itsHost;
			std::string itsPassword;
			int itsPort;
			int itsTimeout;
			
			mpd_Status *itsCurrentStatus;
			mpd_Status *itsOldStatus;
			
			StatusUpdater itsUpdater;
			void *itsStatusUpdaterUserdata;
			ErrorHandler itsErrorHandler;
			void *itsErrorHandlerUserdata;
	};
}

#endif

