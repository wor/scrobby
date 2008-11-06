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
#include "song.h"
#include "mpdpp.h"

using std::string;

ScrobbyConfig config;

Handshake handshake;
MPD::Song s;

pthread_t mpdconnection_th;
pthread_t handshake_th;

pthread_mutex_t curl_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t handshake_lock = PTHREAD_MUTEX_INITIALIZER;

std::vector<string> SongsQueue;

bool scrobby_exit = 0;
bool notify_about_now_playing = 0;

namespace
{
	void signal_handler(int);
	bool send_handshake();
	
	void *mpdconnection_handler(void *data);
	void *handshake_handler(void *);
}

int main(int argc, char **argv)
{
	const string config_file = argc > 1 ? argv[1] : "/etc/scrobby.conf";
	
	DefaultConfiguration(config);
	
	if (!ReadConfiguration(config, config_file))
	{
		std::cerr << "cannot read configuration file: " << config_file << std::endl;
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
	
	GetCachedSongs(SongsQueue);
	
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

	while (!scrobby_exit && !usleep(500000))
	{
		if (Mpd->Connected())
			Mpd->UpdateStatus();
	}
	
	s.Submit();
	Log("Shutting down...", llInfo);
	if (remove(config.file_pid.c_str()) != 0)
		Log("Couldn't remove pid file!", llInfo);
	delete Mpd;
	
	return 0;
}

namespace
{
	void signal_handler(int)
	{
		scrobby_exit = 1;
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
		
		pthread_mutex_lock(&curl_lock);
		CURL *hs = curl_easy_init();
		curl_easy_setopt(hs, CURLOPT_URL, handshake_url.c_str());
		curl_easy_setopt(hs, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(hs, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(hs, CURLOPT_CONNECTTIMEOUT, curl_timeout);
		code = curl_easy_perform(hs);
		curl_easy_cleanup(hs);
		pthread_mutex_unlock(&curl_lock);
		
		if (code != CURLE_OK)
		{
			Log("Error while sending handshake: " + string(curl_easy_strerror(code)), llInfo);
			return false;
		}
		
		size_t i = result.find("\n");
		handshake.status = result.substr(0, i);
		if (handshake.status != "OK")
			return false;
		result = result.substr(i+1);
		i = result.find("\n");
		handshake.session_id = result.substr(0, i);
		result = result.substr(i+1);
		i = result.find("\n");
		handshake.nowplaying_url = result.substr(0, i);
		result = result.substr(i+1);
		ignore_newlines(result);
		handshake.submission_url = result;
		return true;
	}
	
	void *mpdconnection_handler(void *data)
	{
		MPD::Connection *Mpd = static_cast<MPD::Connection *>(data);
		while (!scrobby_exit)
		{
			int x = 0;
			while (!Mpd->Connected())
			{
				s.Submit();
				Log("Connecting to MPD...", llVerbose);
				Mpd->Disconnect();
				if (Mpd->Connect())
				{
					Log("Connected to " + config.mpd_host + "!", llInfo);
					x = 0;
				}
				else
				{
					x++;
					Log("Cannot connect, retrieving in " + IntoStr(10*x) + " seconds...", llInfo);
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
		while (!scrobby_exit)
		{
			if (handshake.status != "OK")
			{
				pthread_mutex_lock(&handshake_lock);
				handshake.Clear();
				if (send_handshake() && !handshake.status.empty())
				{
					Log("Handshake returned " + handshake.status, llInfo);
				}
				if (handshake.status == "OK")
				{
					Log("Connected to Audioscrobbler!", llInfo);
					if (!SongsQueue.empty())
					{
						Log("Queue's not empty, submitting songs...", llInfo);
						
						string result, postdata;
						CURLcode code;
						
						pthread_mutex_lock(&curl_lock);
						CURL *submission = curl_easy_init();
						
						postdata = "s=";
						postdata += handshake.session_id;
						
						for (std::vector<string>::const_iterator it = SongsQueue.begin(); it != SongsQueue.end(); it++)
							postdata += *it;
						
						Log("URL: " + handshake.submission_url, llVerbose);
						Log("Post data: " + postdata, llVerbose);
						
						curl_easy_setopt(submission, CURLOPT_URL, handshake.submission_url.c_str());
						curl_easy_setopt(submission, CURLOPT_POST, 1);
						curl_easy_setopt(submission, CURLOPT_POSTFIELDS, postdata.c_str());
						curl_easy_setopt(submission, CURLOPT_WRITEFUNCTION, write_data);
						curl_easy_setopt(submission, CURLOPT_WRITEDATA, &result);
						curl_easy_setopt(submission, CURLOPT_CONNECTTIMEOUT, curl_timeout);
						code = curl_easy_perform(submission);
						curl_easy_cleanup(submission);
						pthread_mutex_unlock(&curl_lock);
						
						ignore_newlines(result);
						
						if (result == "OK")
						{
							Log("Number of submitted songs: " + IntoStr(SongsQueue.size()), llInfo);
							SongsQueue.clear();
							ClearCache();
							x = 0;
						}
						else
						{
							if (result.empty())
							{
								Log("Error while submitting songs: " + string(curl_easy_strerror(code)), llInfo);
							}
							else
							{
								Log("Audioscrobbler returned status " + result, llInfo);
							}
						}
					}
					notify_about_now_playing = !s.isStream();
				}
				else
				{
					x++;
					Log("Connection refused, retrieving in " + IntoStr(10*x) + " seconds...", llInfo);
					sleep(10*x);
				}
				pthread_mutex_unlock(&handshake_lock);
			}
			sleep(1);
		}
		pthread_exit(NULL);
	}
}

