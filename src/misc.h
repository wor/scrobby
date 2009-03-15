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

#ifndef _MISC_H
#define _MISC_H

#include <sstream>
#include <string>

#include "configuration.h"

size_t write_data(char *, size_t, size_t, void *);
size_t queue_write_data(char *, size_t, size_t, void *);
void ChangeToUser();

bool Daemonize();

void WriteCache(const std::string &);
void Log(LogLevel ll, const char *, ...);

void IgnoreNewlines(std::string &);

std::string md5sum(const std::string &);

std::string DateTime();

int StrToInt(const std::string &);

template <class T>
std::string IntoStr(T t)
{
	std::ostringstream ss;
	ss << t;
	return ss.str();
}

#endif

