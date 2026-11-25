/* MovieTexture_FFMpeg - FFMpeg movie renderer. */

#ifndef RAGE_MOVIE_TEXTURE_FFMPEG_H
#define RAGE_MOVIE_TEXTURE_FFMPEG_H

#include "MovieTexture_Generic.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

struct RageSurface;

namespace avcodec
{
	extern "C"
	{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
	}
};

#define STEPMANIA_FFMPEG_BUFFER_SIZE 4096
static const int kSwsFlags = SWS_FAST_BILINEAR; // XXX: Reasonable default?

struct FrameHolder {
	avcodec::AVFrame* frame = avcodec::av_frame_alloc();
	avcodec::AVPacket* packet = avcodec::av_packet_alloc();
	float frame_timestamp = 0;
	float frame_delay = 0;
	// Written by the decode thread, read by the display thread.
	std::atomic<bool> decoded{false};
	std::atomic<bool> skip{false};

	FrameHolder() = default;
	FrameHolder(const FrameHolder&) = delete;
	FrameHolder& operator=(const FrameHolder&) = delete;

	~FrameHolder() {
		if (packet != nullptr) {
			avcodec::av_packet_free(&packet);
		}
		if (frame != nullptr) {
			avcodec::av_frame_free(&frame);
		}
	}
};

class MovieTexture_FFMpeg : public MovieTexture_Generic
{
public:
	MovieTexture_FFMpeg(RageTextureID ID);

	static RageSurface* AVCodecCreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class RageMovieTextureDriver_FFMpeg : public RageMovieTextureDriver
{
public:
	virtual RageMovieTexture* Create(RageTextureID ID, RString& sError);
	static RageSurface* AVCodecCreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class MovieDecoder_FFMpeg : public MovieDecoder
{
public:
	MovieDecoder_FFMpeg();
	~MovieDecoder_FFMpeg();

	RString Open(RString sFile);
	void Close();
	void Rewind();

	// This draws a frame from the buffer onto the provided RageSurface.
	// Returns true if returning the last frame in the movie.
	bool GetFrame(RageSurface* pOut);
	int DecodeFrame(float fTargetTime);

	// Decode a single frame.  Return -2 on cancel, -1 on error, 0 on EOF, 1 if we have a frame.
	int DecodeNextFrame();

	// Decode-thread main loop. Keeps the buffer filled up to kMaxFramesAhead
	// frames past the one on screen, sleeping when full; handles Rewind() by
	// seeking back to the start. Runs until Cancel().
	// Returns -1 on fatal error, -2 on cancel.
	int DecodeMovie();
	bool IsCurrentFrameReady();

	int GetWidth() const { return av_stream_codec_->width; }
	int GetHeight() const { return av_stream_codec_->height; }

	RageSurface* CreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, MovieDecoderPixelFormatYCbCr& fmtout);

	float GetTimestamp() const;

	void Cancel();

	// If the next frame to display had an issue decoding, skip it.
	bool SkipNextFrame();

private:
	void Init();
	RString OpenCodec();

	// Read a packet and send it to our frame data buffer.
	// Returns -2 on cancel, -1 on error, 0 on EOF, 1 on OK.
	int SendPacketToBuffer();

	// Decode frame data from the packet in the buffer.
	// Returns -2 on cancel, -1 on error, 0 if the packet is finished.
	int DecodePacketInBuffer();

	// Decode thread: seek back to the start of the file after a Rewind().
	void SeekToStart();
	// Frame number n if it's in the buffer, else nullptr. Caller holds buffer_mutex_.
	FrameHolder* FrameAtLocked(int n) const;
	// Free frames before the one on screen. Caller holds buffer_mutex_.
	void FreeShownFramesLocked();

	avcodec::AVStream* av_stream_;
	avcodec::AVPixelFormat av_pixel_format_;	/* pixel format of output surface */
	avcodec::SwsContext* av_sws_context_;
	avcodec::AVCodecContext* av_stream_codec_;

	avcodec::AVFormatContext* av_format_context_;
	int display_frame_num_;
	int total_frames_; // Total number of frames in the movie.

	unsigned char* av_buffer_;
	avcodec::AVIOContext* av_io_context_;

	// The movie buffer: a sliding window from the frame on screen up to
	// kMaxFramesAhead frames past it; frame_buffer_[0] is frame number
	// buffer_base_. Frames are freed once shown, so memory stays bounded no
	// matter how long the movie is. Guarded by buffer_mutex_ (as are
	// display_frame_num_ and total_frames_ once decoding has started).
	std::deque<std::shared_ptr<FrameHolder>> frame_buffer_;
	int buffer_base_ = 0;
	mutable std::mutex buffer_mutex_;
	std::condition_variable buffer_cv_; // wakes the decode thread: space freed, rewind, cancel
	int generation_ = 0;                // bumped by Rewind(); stale decodes are discarded
	bool rewind_requested_ = false;

	// Decode-thread state for the frame being decoded.
	std::shared_ptr<FrameHolder> decoding_frame_;
	float last_timestamp_ = 0;
	float last_delay_ = 0;

	int current_packet_offset_;

	// 0 = no EOF
	// 1 = EOF while decoding
	std::atomic<int> end_of_file_{0};

	// If true, received a cancel signal from the MovieTexture.
	std::atomic<bool> cancel_{false};
	bool first_frame_ = true;
};

static struct AVPixelFormat_t
{
	int bpp;
	std::uint32_t masks[4];
	avcodec::AVPixelFormat pf;
	bool bHighColor;
	bool bByteSwapOnLittleEndian;
	MovieDecoderPixelFormatYCbCr YUV;
} AVPixelFormats[] = {
	{
		32,
		{ 0xFF000000,
		  0x00FF0000,
		  0x0000FF00,
		  0x000000FF },
		avcodec::AV_PIX_FMT_YUYV422,
		false, /* N/A */
		true,
		PixelFormatYCbCr_YUYV422,
	},
	{
		32,
		{ 0x0000FF00,
		  0x00FF0000,
		  0xFF000000,
		  0x000000FF },
		avcodec::AV_PIX_FMT_BGRA,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		32,
		{ 0x00FF0000,
		  0x0000FF00,
		  0x000000FF,
		  0xFF000000 },
		avcodec::AV_PIX_FMT_ARGB,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		24,
		{ 0xFF0000,
		  0x00FF00,
		  0x0000FF,
		  0x000000 },
		avcodec::AV_PIX_FMT_RGB24,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		24,
		{ 0x0000FF,
		  0x00FF00,
		  0xFF0000,
		  0x000000 },
		avcodec::AV_PIX_FMT_BGR24,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x0000 },
		avcodec::AV_PIX_FMT_RGB555,
		false,
		false,
		PixelFormatYCbCr_Invalid,
	},
	{ 0, { 0,0,0,0 }, avcodec::AV_PIX_FMT_NB, true, false, PixelFormatYCbCr_Invalid }
};

#endif

/*
 * (c) 2003-2005 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
