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
#include <string>
#include <vector>

#include "misc.h"
#include "scrobby.h"
#include "song.h"

using std::string;

extern Handshake handshake;

extern std::vector<string> SongsQueue;

MPD::Song::Song() : itsSong(0),
		    itsStartTime(0),
		    itsNoticedPlayback(0),
		    itsIsStream(0)
{
}

MPD::Song::~Song()
{
	if (itsSong)
		mpd_freeSong(itsSong);
}

void MPD::Song::Clear()
{
	if (itsSong)
		mpd_freeSong(itsSong);
	itsSong = 0;
	itsStartTime = 0;
	itsNoticedPlayback = 0;
	itsIsStream = 0;
}

void MPD::Song::SetData(mpd_Song *song)
{
	if (!song)
		return;
	if (itsSong)
		mpd_freeSong(itsSong);
	itsSong = song;
	itsIsStream = strncmp("http://", itsSong->file, 7) == 0;
}

void MPD::Song::SetStartTime()
{
	itsStartTime = time(NULL);
}

void MPD::Song::Submit()
{
	if (!itsSong)
		return;
	
	if (itsIsStream)
	{
		itsSong->time = itsNoticedPlayback;
	}
	
	if (canBeSubmitted())
	{
		if (handshake.status != "OK" || handshake.submission_url.empty())
		{
			Log(llInfo, "Problems with handshake status, queue song at position %d...", SongsQueue.size());
			goto SUBMISSION_FAILED;
		}
		
		Log(llInfo, "Submitting song...");
		
		string result, postdata;
		CURLcode code;
		
		CURL *submission = curl_easy_init();
		
		char *c_artist = curl_easy_escape(submission, itsSong->artist, 0);
		char *c_title = curl_easy_escape(submission, itsSong->title, 0);
		char *c_album = itsSong->album ? curl_easy_escape(submission, itsSong->album, 0) : NULL;
		char *c_track = itsSong->track ? curl_easy_escape(submission, itsSong->track, 0) : NULL;
		
		postdata = "s=";
		postdata += handshake.session_id;
		postdata += "&a[0]=";
		postdata += c_artist;
		postdata += "&t[0]=";
		postdata += c_title;
		postdata += "&i[0]=";
		postdata += IntoStr(itsStartTime);
		postdata += "&o[0]=P";
		postdata += "&r[0]=";
		postdata += "&l[0]=";
		postdata += IntoStr(itsSong->time);
		postdata += "&b[0]=";
		if (c_album)
			postdata += c_album;
		postdata += "&n[0]=";
		if (c_track)
			postdata += c_track;
		postdata += "&m[0]=";
		
		curl_free(c_artist);
		curl_free(c_title);
		curl_free(c_album);
		curl_free(c_track);
		
		Log(llVerbose, "URL: %s", handshake.submission_url.c_str());
		Log(llVerbose, "Post data: %s", postdata.c_str());
		
		curl_easy_setopt(submission, CURLOPT_URL, handshake.submission_url.c_str());
		curl_easy_setopt(submission, CURLOPT_POST, 1);
		curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
		curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, curl_timeout);
		curl_easy_setopt(submission, CURLOPT_NOSIGNAL, 1);
		code = curl_easy_perform(submission);
		curl_easy_cleanup(submission);
		
		ignore_newlines(result);
		
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
			}
			goto SUBMISSION_FAILED;
		}
	}
	if (0)
	{
		SUBMISSION_FAILED: // so we cache not submitted song
		
		handshake.Clear(); // handshake probably failed if we are here, so reset it
		Log(llVerbose, "Handshake status reset");
		
		string cache;
		string offset = IntoStr(SongsQueue.size());
		
		char *c_artist = curl_easy_escape(0, itsSong->artist, 0);
		char *c_title = curl_easy_escape(0, itsSong->title, 0);
		char *c_album = itsSong->album ? curl_easy_escape(0, itsSong->album, 0) : NULL;
		char *c_track = itsSong->track ? curl_easy_escape(0, itsSong->track, 0) : NULL;
		
		cache = "&a[";
		cache += offset;
		cache += "]=";
		cache += c_artist;
		cache += "&t[";
		cache += offset;
		cache += "]=";
		cache += c_title;
		cache += "&i[";
		cache += offset;
		cache += "]=";
		cache += IntoStr(itsStartTime);
		cache += "&o[";
		cache += offset;
		cache += "]=P";
		cache += "&r[";
		cache += offset;
		cache += "]=";
		cache += "&l[";
		cache += offset;
		cache += "]=";
		cache += IntoStr(itsSong->time);
		cache += "&b[";
		cache += offset;
		cache += "]=";
		if (c_album)
			cache += c_album;
		cache += "&n[";
		cache += offset;
		cache += "]=";
		if (c_track)
			cache += c_track;
		cache += "&m[";
		cache += offset;
		cache += "]=";
		
		Log(llVerbose, "Metadata: %s", cache.c_str());
		
		curl_free(c_artist);
		curl_free(c_title);
		curl_free(c_album);
		curl_free(c_track);
		
		Cache(cache);
		SongsQueue.push_back(cache);
		Log(llInfo, "Song cached.");
	}
	Clear();
}

bool MPD::Song::isStream() const
{
	return itsIsStream;
}

int & MPD::Song::Playback()
{
	return itsNoticedPlayback;
}

const mpd_Song *& MPD::Song::Data() const
{
	return (const mpd_Song *&)itsSong;
}

bool MPD::Song::canBeSubmitted()
{
	if (!itsStartTime || itsSong->time < 30 || !itsSong->artist || !itsSong->title)
	{
		if (!itsStartTime)
		{
			Log(llInfo, "Song's start time isn't known, not submitting.");
		}
		else if (itsSong->time < 30)
		{
			Log(llInfo, "Song's length is too short, not submitting.");
		}
		else if (!itsSong->artist || !itsSong->title)
		{
			Log(llInfo, "Song has missing tags, not submitting.");
		}
		return false;
	}
	else if (itsNoticedPlayback < 4*60 && itsNoticedPlayback < itsSong->time/2)
	{
		Log(llInfo, "Noticed playback was too short, not submitting.");
		return false;
	}
	return true;
}

