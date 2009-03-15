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

#include <csignal>
#include <cstdlib>
#include <curl/curl.h>
#include <iostream>

#include "callback.h"
#include "configuration.h"
#include "misc.h"
#include "scrobby.h"
#include "song.h"
#include "mpdpp.h"

using std::string;

Handshake myHandshake;
MPD::Song s;

pthread_mutex_t Handshake::itsLock = PTHREAD_MUTEX_INITIALIZER;

namespace
{
	time_t now = 0;
	
	void do_at_exit();
	void signal_handler(int);
	
	void *queue_handler(void *);
	
	bool handshake_sent_properly();
}

int main(int argc, char **argv)
{
	DefaultConfiguration(Config);
	
	if (argc > 1)
	{
		ParseArgv(Config, argc, argv);
	}
	if (!Config.file_config.empty())
	{
		if (!ReadConfiguration(Config, Config.file_config))
		{
			std::cerr << "cannot read configuration file: " << Config.file_config << std::endl;
			return 1;
		}
	}
	else if (!ReadConfiguration(Config, Config.user_home_folder + "/.scrobbyconf"))
	{
		if (!ReadConfiguration(Config, "/etc/scrobby.conf"))
		{
			std::cerr << "default configuration files not found!\n";
			return 1;
		}
	}
	if (Config.log_level == llUndefined)
	{
		Config.log_level = llInfo;
	}
	if (Config.lastfm_user.empty() || (Config.lastfm_md5_password.empty() && Config.lastfm_password.empty()))
	{
		std::cerr << "last.fm user/password is not set.\n";
		return 1;
	}
	ChangeToUser();
	if (!CheckFiles(Config))
	{
		return 1;
	}
	if (Config.daemonize)
	{
		if (!Daemonize())
			std::cerr << "couldn't daemonize!\n";
	}
	
	MPD::Song::GetCached();
	
	MPD::Connection *Mpd = new MPD::Connection;
	
	if (Config.mpd_host != "localhost")
		Mpd->SetHostname(Config.mpd_host);
	if (Config.mpd_port != 6600)
		Mpd->SetPort(Config.mpd_port);
	
	Mpd->SetTimeout(Config.mpd_timeout);
	Mpd->SetStatusUpdater(ScrobbyStatusChanged, NULL);
	Mpd->SetErrorHandler(ScrobbyErrorCallback, NULL);
	
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);
	
	atexit(do_at_exit);
	
	pthread_t queue_thread;
	pthread_create(&queue_thread, 0, queue_handler, 0);
	pthread_detach(queue_thread);
	
	int handshake_delay = 0;
	int mpd_delay = 0;
	
	time_t handshake_ts = 0;
	time_t mpd_ts = 0;
	
	while (!sleep(1))
	{
		time(&now);
		myHandshake.Lock();
		if (now > handshake_ts && !myHandshake.OK())
		{
			myHandshake.Clear();
			if (handshake_sent_properly() && !myHandshake.Status.empty())
			{
				Log(llInfo, "Handshake returned %s", myHandshake.Status.c_str());
			}
			if (!myHandshake.OK())
			{
				Log(llInfo, "Connected to Audioscrobbler!");
				handshake_delay = 0;
			}
			else
			{
				handshake_delay += 20;
				Log(llInfo, "Connection to Audioscrobbler refused, retrieving in %d seconds...", handshake_delay);
				handshake_ts = time(0)+handshake_delay;
			}
		}
		myHandshake.Unlock();
		if (Mpd->Connected())
		{
			Mpd->UpdateStatus();
		}
		else if (now > mpd_ts)
		{
			s.Submit();
			Log(llVerbose, "Connecting to MPD...");
			if (Mpd->Connect())
			{
				Log(llInfo, "Connected to MPD at %s !", Config.mpd_host.c_str());
				mpd_delay = 0;
			}
			else
			{
				mpd_delay += 10;
				Log(llInfo, "Cannot connect to MPD, retrieving in %d seconds...", mpd_delay);
				mpd_ts = time(0)+mpd_delay;
			}
		}
	}
	return 0;
}

namespace
{
	void do_at_exit()
	{
		s.Submit();
		s.ExtractQueue();
		Log(llInfo, "Shutting down...");
		if (remove(Config.file_pid.c_str()) != 0)
			Log(llInfo, "Couldn't remove pid file!");
	}
	
	void signal_handler(int)
	{
		exit(0);
	}
	
	void *queue_handler(void *)
	{
		int queue_delay = 0;
		time_t queue_ts = 0;
		while (!sleep(1))
		{
			if (now > queue_ts && (!MPD::Song::SubmitQueue.empty() || !MPD::Song::Queue.empty()))
			{
				if (!MPD::Song::SendQueue())
				{
					queue_delay += 30;
					Log(llInfo, "Submission failed, retrieving in %d seconds...", queue_delay);
					queue_ts = time(0)+queue_delay;
				}
				else
					queue_delay = 0;
			}
		}
		pthread_exit(0);
	}
	
	bool handshake_sent_properly()
	{
		CURLcode code;
		string handshake_url;
		string result;
		string timestamp = IntoStr(time(NULL));
		
		handshake_url = "http://post.audioscrobbler.com/?hs=true&p=1.2.1&c=mpc&v="VERSION"&u=";
		handshake_url += Config.lastfm_user;
		handshake_url += "&t=";
		handshake_url += timestamp;
		handshake_url += "&a=";
		handshake_url += md5sum((Config.lastfm_md5_password.empty() ? md5sum(Config.lastfm_password) : Config.lastfm_md5_password) + timestamp);
		
		CURL *hs = curl_easy_init();
		curl_easy_setopt(hs, CURLOPT_URL, handshake_url.c_str());
		curl_easy_setopt(hs, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(hs, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(hs, CURLOPT_CONNECTTIMEOUT, curl_connecttimeout);
		curl_easy_setopt(hs, CURLOPT_TIMEOUT, curl_timeout);
		curl_easy_setopt(hs, CURLOPT_DNS_CACHE_TIMEOUT, 0);
		curl_easy_setopt(hs, CURLOPT_NOPROGRESS, 1);
		curl_easy_setopt(hs, CURLOPT_NOSIGNAL, 1);
		code = curl_easy_perform(hs);
		curl_easy_cleanup(hs);
		
		if (code != CURLE_OK)
		{
			Log(llInfo, "Error while sending handshake: %s", curl_easy_strerror(code));
			return false;
		}
		
		size_t i = result.find("\n");
		myHandshake.Status = result.substr(0, i);
		if (myHandshake.Status != "OK")
			return false;
		result = result.substr(i+1);
		i = result.find("\n");
		myHandshake.SessionID = result.substr(0, i);
		result = result.substr(i+1);
		i = result.find("\n");
		myHandshake.NowPlayingURL = result.substr(0, i);
		result = result.substr(i+1);
		IgnoreNewlines(result);
		myHandshake.SubmissionURL = result;
		return true;
	}
}

