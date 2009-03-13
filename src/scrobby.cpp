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

std::vector<string> SongsQueue;

bool notify_about_now_playing = 0;

namespace {
	void do_at_exit();
	
	void signal_handler(int);
	bool handshake_sent_properly();
	
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
	else if (!ReadConfiguration(config, config.user_home_folder + "/.scrobbyconf"))
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
	ChangeToUser();
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
	
	atexit(do_at_exit);
	
	int handshake_delay = 0;
	int mpd_delay = 0;
	
	time_t now = 0;
	time_t handshake_ts = 0;
	time_t mpd_ts = 0;
	
	while (!usleep(500000))
	{
		time(&now);
		if (now > handshake_delay && handshake.status != "OK")
		{
			handshake.Clear();
			if (handshake_sent_properly() && !handshake.status.empty())
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
					curl_easy_setopt(submission, CURLOPT_NOSIGNAL, 1);
					code = curl_easy_perform(submission);
					curl_easy_cleanup(submission);
					
					ignore_newlines(result);
					
					if (result == "OK")
					{
						Log(llInfo, "Number of submitted songs: %d", SongsQueue.size());
						SongsQueue.clear();
						ClearCache();
						handshake_delay = 0;
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
				handshake_delay += 10;
				Log(llInfo, "Connection to Audioscrobbler refused, retrieving in %d seconds...", handshake_delay);
				handshake_ts = time(0)+handshake_delay;
			}
		}
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
				Log(llInfo, "Connected to MPD at %s !", config.mpd_host.c_str());
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

namespace {
	
	void do_at_exit()
	{
		s.Submit();
		Log(llInfo, "Shutting down...");
		if (remove(config.file_pid.c_str()) != 0)
			Log(llInfo, "Couldn't remove pid file!");
	}
	
	void signal_handler(int)
	{
		exit(0);
	}
	
	bool handshake_sent_properly()
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
		
		CURL *hs = curl_easy_init();
		curl_easy_setopt(hs, CURLOPT_URL, handshake_url.c_str());
		curl_easy_setopt(hs, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(hs, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(hs, CURLOPT_CONNECTTIMEOUT, curl_timeout);
		curl_easy_setopt(hs, CURLOPT_NOSIGNAL, 1);
		code = curl_easy_perform(hs);
		curl_easy_cleanup(hs);
		
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
}

