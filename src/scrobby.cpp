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
#include <cstdlib>
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
	DefaultConfiguration(config);
	
	if (argc > 1)
	{
		ParseArgv(config, argc, argv);
	}
	if (!config.file_config.empty())
	{
		if (!ReadConfiguration(config, config.file_config))
		{
			std::cerr << "cannot read configuration file: " << config.file_config << std::endl;
			return 1;
		}
	}
	else if (!ReadConfiguration(config, string(getenv("HOME") ? getenv("HOME") : "") + "/.scrobbyconf"))
	{
		if (!ReadConfiguration(config, "/etc/scrobby.conf"))
		{
			std::cerr << "default configuration files not found!\n";
			return 1;
		}
	}
	if (config.log_level == llUndefined)
	{
		config.log_level = llInfo;
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
	if (config.daemonize)
	{
		if (!Daemonize())
			std::cerr << "couldn't daemonize!\n";
	}
	
	GetCachedSongs(SongsQueue);
	
	MPD::Connection *Mpd = new MPD::Connection;
	
	if (config.mpd_host != "localhost")
		Mpd->SetHostname(config.mpd_host);
	if (config.mpd_port != 6600)
		Mpd->SetPort(config.mpd_port);
	
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
	Log(llInfo, "Shutting down...");
	if (remove(config.file_pid.c_str()) != 0)
		Log(llInfo, "Couldn't remove pid file!");
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
			Log(llInfo, "Error while sending handshake: %s", curl_easy_strerror(code));
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
				Log(llVerbose, "Connecting to MPD...");
				Mpd->Disconnect();
				if (Mpd->Connect())
				{
					Log(llInfo, "Connected to %s !", config.mpd_host.c_str());
					x = 0;
				}
				else
				{
					x++;
					Log(llInfo, "Cannot connect, retrieving in %d seconds...", 10*x);
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
					Log(llInfo, "Handshake returned %s", handshake.status.c_str());
				}
				if (handshake.status == "OK")
				{
					Log(llInfo, "Connected to Audioscrobbler!");
					if (!SongsQueue.empty())
					{
						Log(llInfo, "Queue's not empty, submitting songs...");
						
						string result, postdata;
						CURLcode code;
						
						pthread_mutex_lock(&curl_lock);
						CURL *submission = curl_easy_init();
						
						postdata = "s=";
						postdata += handshake.session_id;
						
						for (std::vector<string>::const_iterator it = SongsQueue.begin(); it != SongsQueue.end(); it++)
							postdata += *it;
						
						Log(llVerbose, "URL: %s", handshake.submission_url.c_str());
						Log(llVerbose, "Post data: %s", postdata.c_str());
						
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
							Log(llInfo, "Number of submitted songs: %d", SongsQueue.size());
							SongsQueue.clear();
							ClearCache();
							x = 0;
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
					notify_about_now_playing = !s.isStream();
				}
				else
				{
					x++;
					Log(llInfo, "Connection refused, retrieving in %d seconds...", 10*x);
					sleep(10*x);
				}
				pthread_mutex_unlock(&handshake_lock);
			}
			sleep(1);
		}
		pthread_exit(NULL);
	}
}

