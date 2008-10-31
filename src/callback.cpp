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

#include "callback.h"
#include "misc.h"
#include "scrobby.h"

using std::string;

MPD::State old_state = MPD::psUnknown;
MPD::State current_state = MPD::psUnknown;

extern HandshakeResult hr;
extern SubmissionCandidate sc;

extern pthread_mutex_t curl;
extern pthread_mutex_t hr_lock;

extern bool notify_about_now_playing;

void ScrobbyErrorCallback(MPD::Connection *, int, string errormessage, void *)
{
	ignore_newlines(errormessage);
	Log("MPD: " + errormessage);
}

void ScrobbyStatusChanged(MPD::Connection *Mpd, MPD::StatusChanges changed, void *)
{
	if (changed.State)
	{
		old_state = current_state;
		current_state = Mpd->GetState();
		if (old_state == MPD::psStop && current_state == MPD::psPlay)
			changed.SongID = 1;
	}
	if (changed.ElapsedTime)
	{
		if (!Mpd->GetElapsedTime())
			changed.SongID = 1;
		sc.noticed_playback++;
	}
	if (changed.SongID || (old_state == MPD::psPlay && current_state == MPD::psStop))
	{
		SubmitSong(sc);
		
		// in this case allow entering only once
		if (old_state == MPD::psPlay && current_state == MPD::psStop)
			old_state = MPD::psUnknown;
		
		if (Mpd->GetElapsedTime() < 5) // 5 seconds of playing for song to define this, should be enough
			sc.started_time = time(NULL);
		
		if (current_state == MPD::psPlay || current_state == MPD::psPause)
		{
			sc.song = Mpd->CurrentSong();
			notify_about_now_playing = 1;
		}
	}
	if (notify_about_now_playing)
	{
		if (sc.song && (!sc.song->artist || !sc.song->title))
		{
			Log("Playing song with missing tags detected.");
		}
		else if (sc.song && sc.song->artist && sc.song->title)
		{
			if (hr.status == "OK" && !hr.nowplaying_url.empty())
			{
				Log("Playing song detected, sending notification...");
			}
			else
			{
				Log("Playing song detected, notification not sent due to problem with connection.");
				goto NOTIFICATION_FAILED;
			}
			
			string result, postdata;
			CURLcode code;
			
			pthread_mutex_lock(&curl);
			CURL *np_notification = curl_easy_init();
			
			char *c_artist = curl_easy_escape(np_notification, sc.song->artist, 0);
			char *c_title = curl_easy_escape(np_notification, sc.song->title, 0);
			char *c_album = sc.song->album ? curl_easy_escape(np_notification, sc.song->album, 0) : NULL;
			char *c_track = sc.song->track ? curl_easy_escape(np_notification, sc.song->track, 0) : NULL;
			
			postdata = "s=";
			postdata += hr.session_id;
			postdata += "&a=";
			postdata += c_artist;
			postdata += "&t=";
			postdata += c_title;
			postdata += "&b=";
			if (c_album)
				postdata += c_album;
			postdata += "&l=";
			postdata += IntoStr(sc.song->time);
			postdata += "&n=";
			if (c_track)
				postdata += c_track;
			postdata += "&m=";
			
			curl_free(c_artist);
			curl_free(c_title);
			curl_free(c_album);
			curl_free(c_track);
			
			Log("URL: " + hr.nowplaying_url);
			Log("Post data: " + postdata);
			
			curl_easy_setopt(np_notification, CURLOPT_URL, hr.nowplaying_url.c_str());
			curl_easy_setopt(np_notification, CURLOPT_POST, 1);
			curl_easy_setopt(np_notification, CURLOPT_POSTFIELDS, postdata.c_str());
			curl_easy_setopt(np_notification, CURLOPT_WRITEFUNCTION, write_data);
			curl_easy_setopt(np_notification, CURLOPT_WRITEDATA, &result);
			curl_easy_setopt(np_notification, CURLOPT_CONNECTTIMEOUT, 5);
			code = curl_easy_perform(np_notification);
			curl_easy_cleanup(np_notification);
			pthread_mutex_unlock(&curl);
			
			ignore_newlines(result);
			
			if (code != CURLE_OK)
			{
				Log("Error while sending notification: " + string(curl_easy_strerror(code)));
			}
			else if (result == "OK")
			{
				Log("Notification about currently playing song sent.");
			}
			else
			{
				Log("Audioscrobbler returned status " + result);
			}
		}
		if (0)
		{
			NOTIFICATION_FAILED:
			
			pthread_mutex_lock(&hr_lock);
			hr.Clear(); // handshake probably failed if we are here, so reset it
			Log("Handshake status reset");
			pthread_mutex_unlock(&hr_lock);
		}
		notify_about_now_playing = 0;
	}
}

