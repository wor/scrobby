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

#include <csignal>
#include <curl/curl.h>
#include <pthread.h>
#include <iostream>
#include <vector>

#include "callback.h"
#include "configuration.h"
#include "misc.h"
#include "scrobby.h"
#include "mpdpp.h"

using std::string;

ScrobbyConfig config;

HandshakeResult hr;
SubmissionCandidate sc;

pthread_t mpdconnection_th;
pthread_t handshake_th;

pthread_mutex_t curl = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t hr_lock = PTHREAD_MUTEX_INITIALIZER;

std::vector<string> queue;

bool exit = 0;
bool notify_about_now_playing = 0;

namespace
{
	void signal_handler(int);
	bool send_handshake();
	
	void *mpdconnection_handler(void *data);
	void *handshake_handler(void *);
}

int main(/*int argc, char **argv*/)
{
	DefaultConfiguration(config);
	
	if (!ReadConfiguration(config, "/etc/scrobby.conf"))
	{
		std::cerr << "cannot read configuration file!";
		return 1;
	}
	if (config.lastfm_user.empty() || (config.lastfm_md5_password.empty() && config.lastfm_password.empty()))
	{
		std::cerr << "last.fm user/password is not set.\n";
		return 1;
	}
	if (!CheckFiles(config))
	{
		return 1;
	}
	if (!Daemonize())
		std::cerr << "couldn't daemonize!\n";
	
	GetCachedSongs(queue);
	
	MPD::Connection *Mpd = new MPD::Connection;
	
	if (config.mpd_host != "localhost")
		Mpd->SetHostname(config.mpd_host);
	if (config.mpd_port != 6600)
		Mpd->SetPort(config.mpd_port);
	if (!config.mpd_password.empty())
		Mpd->SetPassword(config.mpd_password);
	
	Mpd->SetTimeout(config.mpd_timeout);
	Mpd->SetStatusUpdater(ScrobbyStatusChanged, NULL);
	Mpd->SetErrorHandler(ScrobbyErrorCallback, NULL);
	
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);
	
	pthread_create(&mpdconnection_th, NULL, mpdconnection_handler, Mpd);
	pthread_create(&handshake_th, NULL, handshake_handler, NULL);
	pthread_detach(mpdconnection_th);
	pthread_detach(handshake_th);
	
	sleep(1);
	
	while (!exit && !usleep(500000))
	{
		if (Mpd->Connected())
			Mpd->UpdateStatus();
	}
	
	SubmitSong(sc);
	Log("Shutting down...");
	if (remove(config.file_pid.c_str()) != 0)
		Log("Couldn't remove pid file!");
	delete Mpd;
	
	return 0;
}

void SubmissionCandidate::Clear()
{
	if (song)
		mpd_freeSong(song);
	song = 0;
	started_time = 0;
	noticed_playback = 0;
}

bool SubmissionCandidate::canBeSubmitted()
{
	if (!started_time || song->time < 30 || !song->artist || !song->title)
	{
		if (!started_time)
		{
			Log("Song's start time isn't known, not submitting.");
		}
		else if (song->time < 30)
		{
			Log("Song's length is too short, not submitting.");
		}
		else if (!song->artist || !song->title)
		{
			Log("Song has missing tags, not submitting.");
		}
		return false;
	}
	else if (noticed_playback < 4*60 && noticed_playback < song->time/2)
	{
		Log("Noticed playback was too short, not submitting.");
		return false;
	}
	return true;
}

void SubmitSong(SubmissionCandidate &sc)
{
	if (!sc.song)
		return;
	
	if (sc.canBeSubmitted())
	{
		if (hr.status != "OK" || hr.submission_url.empty())
		{
			Log("Problems with handshake status, queue song at position " + IntoStr(queue.size()) + "...");
			goto SUBMISSION_FAILED;
		}
		
		Log("Submitting song...");
		
		string result, postdata;
		CURLcode code;
		
		pthread_mutex_lock(&curl);
		CURL *submission = curl_easy_init();
		
		char *c_artist = curl_easy_escape(submission, sc.song->artist, 0);
		char *c_title = curl_easy_escape(submission, sc.song->title, 0);
		char *c_album = sc.song->album ? curl_easy_escape(submission, sc.song->album, 0) : NULL;
		char *c_track = sc.song->track ? curl_easy_escape(submission, sc.song->track, 0) : NULL;
		
		postdata = "s=";
		postdata += hr.session_id;
		postdata += "&a[0]=";
		postdata += c_artist;
		postdata += "&t[0]=";
		postdata += c_title;
		postdata += "&i[0]=";
		postdata += IntoStr(sc.started_time);
		postdata += "&o[0]=P";
		postdata += "&r[0]=";
		postdata += "&l[0]=";
		postdata += IntoStr(sc.song->time);
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
		
		Log("URL: " + hr.submission_url);
		Log("Post data: " + postdata);
		
		curl_easy_setopt(submission, CURLOPT_URL, hr.submission_url.c_str());
		curl_easy_setopt(submission, CURLOPT_POST, 1);
		curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
		curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, 5);
		code = curl_easy_perform(submission);
		curl_easy_cleanup(submission);
		pthread_mutex_unlock(&curl);
		
		ignore_newlines(result);
		
		if (result == "OK")
		{
			Log("Song submitted.");
		}
		else
		{
			if (result.empty())
			{
				Log("Error while submitting song: " + string(curl_easy_strerror(code)));
			}
			else
			{
				Log("Audioscrobbler returned status " + result);
			}
			goto SUBMISSION_FAILED;
		}
	}
	if (0)
	{
		SUBMISSION_FAILED: // so we cache not submitted song
		
		pthread_mutex_lock(&hr_lock);
		hr.Clear(); // handshake probably failed if we are here, so reset it
		Log("Handshake status reset");
		
		string cache;
		string offset = IntoStr(queue.size());
		
		char *c_artist = curl_easy_escape(0, sc.song->artist, 0);
		char *c_title = curl_easy_escape(0, sc.song->title, 0);
		char *c_album = sc.song->album ? curl_easy_escape(0, sc.song->album, 0) : NULL;
		char *c_track = sc.song->track ? curl_easy_escape(0, sc.song->track, 0) : NULL;
		
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
		cache += IntoStr(sc.started_time);
		cache += "&o[";
		cache += offset;
		cache += "]=P";
		cache += "&r[";
		cache += offset;
		cache += "]=";
		cache += "&l[";
		cache += offset;
		cache += "]=";
		cache += IntoStr(sc.song->time);
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
		
		Log("Metadata: " + cache);
		
		curl_free(c_artist);
		curl_free(c_title);
		curl_free(c_album);
		curl_free(c_track);
		
		Cache(cache);
		queue.push_back(cache);
		Log("Song cached.");
		pthread_mutex_unlock(&hr_lock);
	}
	sc.Clear();
}

namespace
{
	void signal_handler(int)
	{
		exit = 1;
	}
	
	bool send_handshake()
	{
		CURLcode code;
		string handshake_url;
		string result;
		string timestamp = IntoStr(time(NULL));
		
		handshake_url = "http://post.audioscrobbler.com/?hs=true&p=1.2.1&c=mpc&v="VERSION"&u=";
		handshake_url += config.lastfm_user;
		handshake_url += "&t=";
		handshake_url += timestamp;
		handshake_url += "&a=";
		handshake_url += md5sum((config.lastfm_md5_password.empty() ? md5sum(config.lastfm_password) : config.lastfm_md5_password) + timestamp);
		
		pthread_mutex_lock(&curl);
		CURL *handshake = curl_easy_init();
		curl_easy_setopt(handshake, CURLOPT_URL, handshake_url.c_str());
		curl_easy_setopt(handshake, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(handshake, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(handshake, CURLOPT_CONNECTTIMEOUT, 5);
		code = curl_easy_perform(handshake);
		curl_easy_cleanup(handshake);
		pthread_mutex_unlock(&curl);
		
		if (code != CURLE_OK)
		{
			Log("Error while sending handshake: " + string(curl_easy_strerror(code)));
			return false;
		}
		
		int i = result.find("\n");
		hr.status = result.substr(0, i);
		if (hr.status != "OK")
			return false;
		result = result.substr(i+1);
		i = result.find("\n");
		hr.session_id = result.substr(0, i);
		result = result.substr(i+1);
		i = result.find("\n");
		hr.nowplaying_url = result.substr(0, i);
		result = result.substr(i+1);
		ignore_newlines(result);
		hr.submission_url = result;
		return true;
	}
	
	void *mpdconnection_handler(void *data)
	{
		MPD::Connection *Mpd = static_cast<MPD::Connection *>(data);
		while (!exit)
		{
			int x = 0;
			while (!Mpd->Connected())
			{
				SubmitSong(sc);
				Log("Connecting to MPD...");
				Mpd->Disconnect();
				if (Mpd->Connect())
				{
					Log("Connected to " + config.mpd_host + "!");
					x = 0;
				}
				else
				{
					x++;
					Log("Cannot connect, retrieving in " + IntoStr(10*x) + " seconds...");
					sleep(10*x);
				}
			}
			sleep(1);
		}
		pthread_exit(NULL);
	}
	
	void *handshake_handler(void *)
	{
		int x = 0;
		while (!exit)
		{
			if (hr.status != "OK")
			{
				pthread_mutex_lock(&hr_lock);
				hr.Clear();
				if (send_handshake() && !hr.status.empty())
				{
					Log("Handshake returned " + hr.status);
				}
				if (hr.status == "OK")
				{
					Log("Connected to Audioscrobbler!");
					if (!queue.empty())
					{
						Log("Queue's not empty, submitting songs...");
						
						string result, postdata;
						CURLcode code;
						
						pthread_mutex_lock(&curl);
						CURL *submission = curl_easy_init();
						
						postdata = "s=";
						postdata += hr.session_id;
						
						for (std::vector<string>::const_iterator it = queue.begin(); it != queue.end(); it++)
							postdata += *it;
						
						Log("URL: " + hr.submission_url);
						Log("Post data: " + postdata);
						
						curl_easy_setopt(submission, CURLOPT_URL, hr.submission_url.c_str());
						curl_easy_setopt(submission, CURLOPT_POST, 1);
						curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
						curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
						curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
						curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, 5);
						code = curl_easy_perform(submission);
						curl_easy_cleanup(submission);
						pthread_mutex_unlock(&curl);
						
						ignore_newlines(result);
						
						if (result == "OK")
						{
							Log("Number of submitted songs: " + IntoStr(queue.size()));
							queue.clear();
							ClearCache();
							x = 0;
						}
						else
						{
							if (result.empty())
							{
								Log("Error while submitting songs: " + string(curl_easy_strerror(code)));
							}
							else
							{
								Log("Audioscrobbler returned status " + result);
							}
						}
					}
					notify_about_now_playing = 1;
				}
				else
				{
					x++;
					Log("Connection refused, retrieving in " + IntoStr(10*x) + " seconds...");
					sleep(10*x);
				}
				pthread_mutex_unlock(&hr_lock);
			}
			sleep(1);
		}
		pthread_exit(NULL);
	}
}

