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

#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <string>

#include "callback.h"
#include "misc.h"
#include "scrobby.h"
#include "song.h"

using std::string;

extern Handshake myHandshake;
extern MPD::Song s;

bool MPD::Song::NowPlayingNotify = 0;

std::deque<std::string> MPD::Song::SubmitQueue;
std::queue<MPD::Song> MPD::Song::Queue;

MPD::Song::Song() : Data(0),
		    StartTime(0),
		    Playback(0),
		    itsIsStream(0),
		    onlySubmitMusicBrainsTagged(1)
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
		Queue.push(*this);
		Data = 0;
		Log(llInfo, "Song queued for submission.");
	}
	Clear();
}

bool MPD::Song::isStream() const
{
	return itsIsStream;
}

bool MPD::Song::canBeSubmitted()
{
	if (onlySubmitMusicBrainsTagged && !Data->musicbrainz_trackid) {
		Log(llInfo, "Song has missing musicbrainz track id, not submitting.");
		return false;
	}

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
				SubmitQueue.push_back(line);
		}
	}
}

void MPD::Song::ExtractQueue()
{
	for (; !Queue.empty(); Queue.pop())
	{
		const MPD::Song &s = Queue.front();
		
		string cache_str;
		std::ostringstream cache;
		
		char *c_artist = curl_easy_escape(0, s.Data->artist, 0);
		char *c_title = curl_easy_escape(0, s.Data->title, 0);
		char *c_album = s.Data->album ? curl_easy_escape(0, s.Data->album, 0) : 0;
		char *c_track = s.Data->track ? curl_easy_escape(0, s.Data->track, 0) : 0;
		char *c_mb_trackid = s.Data->musicbrainz_trackid ? curl_easy_escape(0, s.Data->musicbrainz_trackid, 0) : 0;
		
		cache
		<< "&a[" << Song::SubmitQueue.size() << "]=" << c_artist
		<< "&t[" << Song::SubmitQueue.size() << "]=" << c_title
		<< "&i[" << Song::SubmitQueue.size() << "]=" << s.StartTime
		<< "&o[" << Song::SubmitQueue.size() << "]=P"
		<< "&r[" << Song::SubmitQueue.size() << "]="
		<< "&l[" << Song::SubmitQueue.size() << "]=" << s.Data->time
		<< "&b[" << Song::SubmitQueue.size() << "]=";
		if (c_album)
			cache << c_album;
		cache << "&n[" << Song::SubmitQueue.size() << "]=";
		if (c_track)
			cache << c_track;
		cache << "&m[" << Song::SubmitQueue.size() << "]=";
		if (c_mb_trackid)
			cache << c_mb_trackid;
		
		cache_str = cache.str();
		
		SubmitQueue.push_back(cache_str);
		WriteCache(cache_str);
		
		curl_free(c_artist);
		curl_free(c_title);
		curl_free(c_album);
		curl_free(c_track);
		curl_free(c_mb_trackid);
	}
}

bool MPD::Song::SendQueue()
{
	ExtractQueue();
	
	if (!myHandshake.OK())
		return false;
	
	Log(llInfo, "Submitting songs...");
	
	string result, postdata;
	CURLcode code;
	
	postdata = "s=";
	postdata += myHandshake.SessionID;
	
	for (std::deque<string>::const_iterator it = Song::SubmitQueue.begin(); it != Song::SubmitQueue.end(); it++)
		postdata += *it;
	
	Log(llVerbose, "URL: %s", myHandshake.SubmissionURL.c_str());
	Log(llVerbose, "Post data: %s", postdata.c_str());
	
	CURL *submission = curl_easy_init();
	curl_easy_setopt(submission, CURLOPT_URL, myHandshake.SubmissionURL.c_str());
	curl_easy_setopt(submission, CURLOPT_POST, 1);
	curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
	curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
	curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, curl_queue_connecttimeout);
	curl_easy_setopt(submission, CURLOPT_TIMEOUT, curl_queue_timeout);
	curl_easy_setopt(submission, CURLOPT_DNS_CACHE_TIMEOUT, 0);
	curl_easy_setopt(submission, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt(submission, CURLOPT_NOSIGNAL, 1);
	code = curl_easy_perform(submission);
	curl_easy_cleanup(submission);
	
	IgnoreNewlines(result);
	
	if (result == "OK")
	{
		Log(llInfo, "Number of submitted songs: %d", Song::SubmitQueue.size());
		SubmitQueue.clear();
		std::ofstream f(Config.file_cache.c_str(), std::ios::trunc);
		f.close();
		NowPlayingNotify = s.Data && !s.isStream();
		return true;
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
			// BADSESSION or FAILED was returned, handshake needs resetting.
			myHandshake.Clear();
			Log(llVerbose, "Handshake reset");
		}
		return false;
	}
}

