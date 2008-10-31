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

#ifndef _SCROBBY_H
#define _SCROBBY_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctime>

#include "libmpdclient.h"

struct HandshakeResult
{
	void Clear()
	{
		status.clear();
		session_id.clear();
		nowplaying_url.clear();
		submission_url.clear();
	}
	
	std::string status;
	std::string session_id;
	std::string nowplaying_url;
	std::string submission_url;
};

struct SubmissionCandidate
{
	SubmissionCandidate() : song(0), started_time(0), noticed_playback(0) { }
	~SubmissionCandidate() { if (song) mpd_freeSong(song); }
	
	void Clear();
	bool canBeSubmitted();
	
	mpd_Song *song;
	time_t started_time;
	int noticed_playback;
};

void SubmitSong(SubmissionCandidate &);

#endif

