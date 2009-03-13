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

#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "callback.h"
#include "misc.h"
#include "scrobby.h"
#include "song.h"

using std::string;

extern Handshake myHandshake;

std::deque<std::string> MPD::Song::Queue;

MPD::Song::Song() : Data(0),
		    StartTime(0),
		    Playback(0),
		    itsIsStream(0)
{
}

MPD::Song::~Song()
{
	if (Data)
		mpd_freeSong(Data);
}

void MPD::Song::Clear()
{
	if (Data)
		mpd_freeSong(Data);
	Data = 0;
	StartTime = 0;
	Playback = 0;
	itsIsStream = 0;
}

void MPD::Song::SetData(mpd_Song *song)
{
	if (!song)
		return;
	if (Data)
		mpd_freeSong(Data);
	Data = song;
	itsIsStream = strncmp("http://", Data->file, 7) == 0;
}

void MPD::Song::Submit()
{
	if (!Data)
		return;
	
	if (itsIsStream)
		Data->time = Playback;
	
	if (canBeSubmitted())
	{
		if (!Queue.empty())
		{
			Cache();
			Clear();
			SendQueue();
			return;
		}
		
		if (myHandshake.Status != "OK" || myHandshake.SubmissionURL.empty())
		{
			Log(llInfo, "Problem with handshake, queue song at position %d...", Song::Queue.size());
			Cache();
			Clear();
			return;
		}
		
		Log(llInfo, "Submitting song...");
		
		std::ostringstream postdata;
		string result, postdata_str;
		CURLcode code;
		
		char *c_artist = curl_easy_escape(0, Data->artist, 0);
		char *c_title = curl_easy_escape(0, Data->title, 0);
		char *c_album = Data->album ? curl_easy_escape(0, Data->album, 0) : NULL;
		char *c_track = Data->track ? curl_easy_escape(0, Data->track, 0) : NULL;
		
		postdata
		<< "s=" << myHandshake.SessionID
		<< "&a[0]=" << c_artist
		<< "&t[0]=" << c_title
		<< "&i[0]=" << StartTime
		<< "&o[0]=P"
		<< "&r[0]="
		<< "&l[0]=" << Data->time
		<< "&b[0]=";
		if (c_album)
			postdata << c_album;
		postdata << "&n[0]=";
		if (c_track)
			postdata << c_track;
		postdata << "&m[0]=";
		
		curl_free(c_artist);
		curl_free(c_title);
		curl_free(c_album);
		curl_free(c_track);
		
		postdata_str = postdata.str();
		
		Log(llVerbose, "URL: %s", myHandshake.SubmissionURL.c_str());
		Log(llVerbose, "Post data: %s", postdata_str.c_str());
		
		CURL *submission = curl_easy_init();
		curl_easy_setopt(submission, CURLOPT_URL, myHandshake.SubmissionURL.c_str());
		curl_easy_setopt(submission, CURLOPT_POST, 1);
		curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata_str.c_str());
		curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, curl_timeout);
		code = curl_easy_perform(submission);
		curl_easy_cleanup(submission);
		
		IgnoreNewlines(result);
		
		if (result == "OK")
		{
			Log(llInfo, "Song submitted.");
		}
		else
		{
			if (result.empty())
			{
				Log(llInfo, "Error while submitting song: %s", curl_easy_strerror(code));
			}
			else
			{
				Log(llInfo, "Audioscrobbler returned status %s", result.c_str());
				myHandshake.Clear(); // handshake probably failed if we are here, so reset it
				Log(llVerbose, "Handshake reset");
			}
			Cache();
		}
	}
	Clear();
}

void MPD::Song::Cache()
{
	std::ostringstream cache;
	string cache_str;
	
	char *c_artist = curl_easy_escape(0, Data->artist, 0);
	char *c_title = curl_easy_escape(0, Data->title, 0);
	char *c_album = Data->album ? curl_easy_escape(0, Data->album, 0) : NULL;
	char *c_track = Data->track ? curl_easy_escape(0, Data->track, 0) : NULL;
	
	cache
	<< "&a[" << Song::Queue.size() << "]=" << c_artist
	<< "&t[" << Song::Queue.size() << "]=" << c_title
	<< "&i[" << Song::Queue.size() << "]=" << StartTime
	<< "&o[" << Song::Queue.size() << "]=P"
	<< "&r[" << Song::Queue.size() << "]="
	<< "&l[" << Song::Queue.size() << "]=" << Data->time
	<< "&b[" << Song::Queue.size() << "]=";
	if (c_album)
		cache << c_album;
	cache << "&n[" << Song::Queue.size() << "]=";
	if (c_track)
		cache << c_track;
	cache << "&m[" << Song::Queue.size() << "]=";
	
	cache_str = cache.str();
	
	Log(llVerbose, "Metadata: %s", cache_str.c_str());
	
	curl_free(c_artist);
	curl_free(c_title);
	curl_free(c_album);
	curl_free(c_track);
	
	WriteCache(cache_str);
	Song::Queue.push_back(cache_str);
	Log(llInfo, "Song cached.");
}

bool MPD::Song::isStream() const
{
	return itsIsStream;
}

bool MPD::Song::canBeSubmitted()
{
	if (!StartTime || Data->time < 30 || !Data->artist || !Data->title)
	{
		if (!StartTime)
		{
			Log(llInfo, "Song's start time wasn't known, not submitting.");
		}
		else if (Data->time < 30)
		{
			Log(llInfo, "Song's length is too short, not submitting.");
		}
		else if (!Data->artist || !Data->title)
		{
			Log(llInfo, "Song has missing tags, not submitting.");
		}
		return false;
	}
	else if (Playback < 4*60 && Playback < Data->time/2)
	{
		Log(llInfo, "Noticed playback was too short, not submitting.");
		return false;
	}
	return true;
}

void MPD::Song::GetCached()
{
	std::ifstream f(Config.file_cache.c_str());
	if (f.is_open())
	{
		std::string line;
		while (!f.eof())
		{
			getline(f, line);
			if (!line.empty())
				Queue.push_back(line);
		}
	}
}

void MPD::Song::SendQueue()
{
	if (Song::Queue.empty())
		return;
	
	Log(llInfo, "Queue is not empty, submitting songs...");
	
	string result, postdata;
	CURLcode code;
	
	postdata = "s=";
	postdata += myHandshake.SessionID;
	
	for (std::deque<string>::const_iterator it = Song::Queue.begin(); it != Song::Queue.end(); it++)
		postdata += *it;
	
	Log(llVerbose, "URL: %s", myHandshake.SubmissionURL.c_str());
	Log(llVerbose, "Post data: %s", postdata.c_str());
	
	CURL *submission = curl_easy_init();
	curl_easy_setopt(submission, CURLOPT_URL, myHandshake.SubmissionURL.c_str());
	curl_easy_setopt(submission, CURLOPT_POST, 1);
	curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
	curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
	curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, curl_timeout);
	code = curl_easy_perform(submission);
	curl_easy_cleanup(submission);
	
	IgnoreNewlines(result);
	
	if (result == "OK")
	{
		Log(llInfo, "Number of submitted songs: %d", Song::Queue.size());
		Song::Queue.clear();
		ClearCache();
	}
	else
	{
		if (result.empty())
		{
			Log(llInfo, "Error while submitting songs: %s", curl_easy_strerror(code));
		}
		else
		{
			Log(llInfo, "Audioscrobbler returned status %s", result.c_str());
		}
	}
}

