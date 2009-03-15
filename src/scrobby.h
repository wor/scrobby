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

#ifndef _SCROBBY_H
#define _SCROBBY_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

const int curl_connecttimeout = 5;
const int curl_timeout = 10;

const int curl_queue_connecttimeout = 30;
const int curl_queue_timeout = 60;

struct Handshake
{
	void Clear()
	{
		Status.clear();
		SessionID.clear();
		NowPlayingURL.clear();
		SubmissionURL.clear();
	}
	
	bool OK() { return Status == "OK"; }
	
	void Lock() { pthread_mutex_lock(&itsLock); }
	void Unlock() { pthread_mutex_unlock(&itsLock); }
	
	std::string Status;
	std::string SessionID;
	std::string NowPlayingURL;
	std::string SubmissionURL;
	
	private:
		static pthread_mutex_t itsLock;
};

#endif

