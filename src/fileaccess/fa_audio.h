/*
 *  Audio playback from file access
 *  Copyright (C) 2009 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FA_AUDIO_H
#define FA_AUDIO_H

#include "media.h"
struct backend;
struct fa_handle;

event_t *be_file_playaudio(const char *url, media_pipe_t *mp,
			   char *errbuf, size_t errlen, int hold,
			   const char *mimetype);

#if ENABLE_LIBGME
event_t *fa_gme_playfile(media_pipe_t *mp, AVIOContext *avio,
			 char *errbuf, size_t errlen, int hold);
#endif

#endif /* FA_AUDIO_H */
