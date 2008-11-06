/***************************************************************************
 *   Copyright (C) 2008 by Andrzej Rybczak   *
 *   electricityispower@gmail.com   *
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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "mpdpp.h"

using std::string;

MPD::Connection::Connection() : isConnected(0),
				 itsErrorCode(0),
				 itsHost("localhost"),
				 itsPort(6600),
				 itsTimeout(15),
				 itsUpdater(0),
				 itsErrorHandler(0)
{
	itsConnection = 0;
	itsCurrentStatus = 0;
	itsOldStatus = 0;
}

MPD::Connection::~Connection()
{
	if (itsConnection)
		mpd_closeConnection(itsConnection);
	if (itsOldStatus)
		mpd_freeStatus(itsOldStatus);
	if (itsCurrentStatus)
		mpd_freeStatus(itsCurrentStatus);
}

bool MPD::Connection::Connect()
{
	if (!isConnected && !itsConnection)
	{
		itsConnection = mpd_newConnection(itsHost.c_str(), itsPort, itsTimeout);
		isConnected = 1;
		if (CheckForErrors())
			return false;
		if (!itsPassword.empty())
			SendPassword();
		return !CheckForErrors();
	}
	else
		return true;
}

bool MPD::Connection::Connected() const
{
	return isConnected;
}

void MPD::Connection::Disconnect()
{
	if (itsConnection)
		mpd_closeConnection(itsConnection);
	if (itsOldStatus)
		mpd_freeStatus(itsOldStatus);
	if (itsCurrentStatus)
		mpd_freeStatus(itsCurrentStatus);
	itsConnection = 0;
	itsCurrentStatus = 0;
	itsOldStatus = 0;
	isConnected = 0;
}

void MPD::Connection::SetHostname(const string &host)
{
	size_t at = host.find("@");
	if (at != string::npos)
	{
		itsPassword = host.substr(0, at);
		itsHost = host.substr(at+1);
	}
	else
		itsHost = host;
}

void MPD::Connection::SendPassword() const
{
	mpd_sendPasswordCommand(itsConnection, itsPassword.c_str());
	mpd_finishCommand(itsConnection);
}

void MPD::Connection::SetStatusUpdater(StatusUpdater updater, void *data)
{
	itsUpdater = updater;
	itsStatusUpdaterUserdata = data;
}

void MPD::Connection::SetErrorHandler(ErrorHandler handler, void *data)
{
	itsErrorHandler = handler;
	itsErrorHandlerUserdata = data;
}

void MPD::Connection::UpdateStatus()
{
	CheckForErrors();
	
	if (itsOldStatus)
		mpd_freeStatus(itsOldStatus);
	itsOldStatus = itsCurrentStatus;
	mpd_sendStatusCommand(itsConnection);
	itsCurrentStatus = mpd_getStatus(itsConnection);
	
	if (CheckForErrors())
		return;
	
	if (itsCurrentStatus && itsUpdater)
	{
		if (itsOldStatus == NULL)
		{
			itsChanges.SongID = 1;
			itsChanges.ElapsedTime = 1;
			itsChanges.State = 1;
		}
		else
		{
			itsChanges.SongID = itsOldStatus->songid != itsCurrentStatus->songid;
			itsChanges.ElapsedTime = itsOldStatus->elapsedTime != itsCurrentStatus->elapsedTime;
			itsChanges.State = itsOldStatus->state != itsCurrentStatus->state;
		}
		itsUpdater(this, itsChanges, itsErrorHandlerUserdata);
	}
}

mpd_Song * MPD::Connection::CurrentSong() const
{
	if (isConnected && (GetState() == psPlay || GetState() == psPause))
	{
		mpd_sendCurrentSongCommand(itsConnection);
		mpd_InfoEntity *item = NULL;
		item = mpd_getNextInfoEntity(itsConnection);
		if (item)
		{
			mpd_Song *result = item->info.song;
			item->info.song = 0;
			mpd_freeInfoEntity(item);
			return result;
		}
		mpd_finishCommand(itsConnection);
	}
	return NULL;
}

int MPD::Connection::CheckForErrors()
{
	itsErrorCode = 0;
	if (itsConnection->error)
	{
		itsErrorMessage = itsConnection->errorStr;
		if (itsConnection->error == MPD_ERROR_ACK)
		{
			if (itsErrorHandler)
				itsErrorHandler(this, itsConnection->errorCode, itsErrorMessage, itsErrorHandlerUserdata);
			itsErrorCode = itsConnection->errorCode;
		}
		else
		{
			isConnected = 0; // the rest of errors are fatal to connection
			if (itsErrorHandler)
				itsErrorHandler(this, itsConnection->error, itsErrorMessage, itsErrorHandlerUserdata);
			itsErrorCode = itsConnection->error;
		}
		mpd_clearError(itsConnection);
	}
	return itsErrorCode;
}
